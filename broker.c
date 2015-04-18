#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <unistd.h>
#include <memory.h>
#include <json/json.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sqlite3.h>

#include "md5.h"

static int PORT = 10001;
#define BUFFER_SIZE 2048

int sockFDClient;
struct sockaddr_in servAddr;
char buffer[BUFFER_SIZE];
sqlite3 *sqConn;
sqlite3_stmt *sqRes;
int sqError;

uint32_t key[4] = {
		31231234, 412334, 12341, 657657
};

void encipher(unsigned int num_rounds, uint32_t v[2], uint32_t const key[4]) {
    unsigned int i;
    uint32_t v0=v[0], v1=v[1], sum=0, delta=0x9E3779B9;
    for (i=0; i < num_rounds; i++) {
        v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
        sum += delta;
        v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum>>11) & 3]);
    }
    v[0]=v0; v[1]=v1;
}

void decipher(unsigned int num_rounds, uint32_t v[2], uint32_t const key[4]) {
    unsigned int i;
    uint32_t v0=v[0], v1=v[1], delta=0x9E3779B9, sum=delta*num_rounds;
    for (i=0; i < num_rounds; i++) {
        v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum>>11) & 3]);
        sum -= delta;
        v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
    }
    v[0]=v0; v[1]=v1;
}

int encipherEvent(char *eventBuffer, char *bufferOutput) {

	char buffer[8];
	int len, numOfBlocks, i, j;
	char *pt;

	len = strlen(eventBuffer);
	numOfBlocks = (len / 8) * 8 + ( (len % 8) == 0 ? 0 : 1);

	pt = bufferOutput;

	memcpy(pt, &len, sizeof(len));
	pt += sizeof(len);

	memcpy(pt, &numOfBlocks, sizeof(numOfBlocks));
	pt += sizeof(numOfBlocks);

	for (i = 0; i < len; i += 8) {

		bzero(buffer, sizeof buffer);
		for (j = 0; j < 8; j++) {
			if ((i + j) < len) {
				buffer[j] = eventBuffer[i + j];
			}
		}

		encipher(32, (uint32_t*) buffer, key);
		memcpy(pt, buffer, 8);
		pt += 8;

	}

	encipher(32, bufferOutput, key);

	return numOfBlocks;

}


int decipherEvent(char *bufferInput, char *bufferOutput, int lenBufferOutput) {

	char buffer[8];
	int len, numOfBlocks, i, j;
	char *pt, *ptOutput;

	bzero(bufferOutput, lenBufferOutput);

	pt = bufferInput;
	ptOutput = bufferOutput;

	decipher(32, pt, key);

	memcpy(&len, pt, sizeof(len));
	pt += sizeof(len);

	printf("len = %d\n", len);

	memcpy(&numOfBlocks, pt, sizeof(numOfBlocks));
	pt += sizeof(numOfBlocks);

	printf("num of blocks = %d\n", numOfBlocks);

	for (i = 0; i < numOfBlocks; i ++) {

		bzero(buffer, sizeof buffer);

		memcpy(buffer, pt, 8);
		decipher(32, (uint32_t*) buffer, key);

		memcpy(ptOutput, buffer, 8);

		pt += 8;
		ptOutput += 8;

	}

	return len;

}


void createDbConnection() {

	sqError = sqlite3_open("broker.db", &sqConn);

	if (sqError) {
		fprintf(stderr, "Failed to connect to DB\n");
		exit(EXIT_FAILURE);
	}

}

void deleteAll() {

	char sqlFormat[250];

	sprintf(sqlFormat, "DELETE FROM event");

	sqError = sqlite3_exec(sqConn, sqlFormat, 0, 0, 0);

	sprintf(sqlFormat, "DELETE FROM subscriber");

	sqError = sqlite3_exec(sqConn, sqlFormat, 0, 0, 0);

}

void insertEventAttribute(time_t time, char *hash, char *attr, char *value) {

	char sqlFormat[250];

	sprintf(sqlFormat, "INSERT INTO event VALUES ('%s','%ld','%s','%s')", hash,
			time, attr, value);

	sqError = sqlite3_exec(sqConn, sqlFormat, 0, 0, 0);

}

void insertSubscriber(char *ip, int port, char *hash) {

	char sqlFormat[250];

	time_t timeVal;

	timeVal = time(NULL);

	sprintf(sqlFormat, "DELETE FROM subscriber WHERE ip = '%s' AND port = '%d' AND hash = '%s'",
			ip, port, hash);

	sqError = sqlite3_exec(sqConn, sqlFormat, 0, 0, 0);

	sprintf(sqlFormat, "INSERT INTO subscriber VALUES ('%ld','%s','%d','%s')",
			timeVal, ip, port, hash);

	sqError = sqlite3_exec(sqConn, sqlFormat, 0, 0, 0);

}

void createUDPClientSocket() {

	sockFDClient = socket(AF_INET, SOCK_DGRAM, 0);

	bzero(&servAddr, sizeof(servAddr));

	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = htonl(INADDR_ANY);
	servAddr.sin_port = htons(PORT);

	bind(sockFDClient, (struct sockaddr*) &servAddr, sizeof(servAddr));
}

// http://beej.us/guide/bgnet/output/html/multipage/inet_ntopman.html
char *get_ip_str(const struct sockaddr *sa, char *s, size_t maxlen) {
	switch (sa->sa_family) {
	case AF_INET:
		inet_ntop(AF_INET, &(((struct sockaddr_in *) sa)->sin_addr), s, maxlen);
		break;

	case AF_INET6:
		inet_ntop(AF_INET6, &(((struct sockaddr_in6 *) sa)->sin6_addr), s,
				maxlen);
		break;

	default:
		strncpy(s, "Unknown AF", maxlen);
		return NULL;
	}

	return s;
}

void subscribe(struct sockaddr_in cliAddr, json_object *jsonObject) {

	char ipString[20];
	int port;
	char keyMd5Table[128];
	unsigned char md5Result[16];
	json_object *attributesObj;

	port = ntohs(cliAddr.sin_port);

	get_ip_str(((const struct sockaddr*) &cliAddr), ipString, sizeof ipString);

	json_object_object_foreach(jsonObject, key, val)
	{

		if (strcmp(key, "attributes") == 0) {
			int arrayLen, i;
			char *valArray;
			MD5_CTX mdContext;

			attributesObj = json_object_object_get(jsonObject, key);
			arrayLen = json_object_array_length(attributesObj);

			MD5_Init(&mdContext);

			for (i = 0; i < arrayLen; i++) {
				val = json_object_array_get_idx(attributesObj, i);
				valArray = json_object_get_string(val);

				MD5_Update(&mdContext, valArray, strlen(valArray));
				//fprintf(stderr, "value[%d]: %s \n", i, json_object_get_string(val));
			}

			MD5_Final(md5Result, &mdContext);

			sprintf(keyMd5Table,
					"%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x",
					md5Result[0], md5Result[1], md5Result[2], md5Result[3],
					md5Result[4], md5Result[5], md5Result[6], md5Result[7],
					md5Result[8], md5Result[9], md5Result[10], md5Result[11],
					md5Result[12], md5Result[13], md5Result[14], md5Result[15]);

			insertSubscriber(ipString, port, keyMd5Table);

		}

	}
}

void sendEvent(char *hash, json_object *jsonObject) {

	char *json;
	char sqFormat[200];
	const char *tail;
	sqlite3_stmt *res;
	struct sockaddr_in sa;
	char bufferXTEA[1024];
	int blocks;

	json = json_object_get_string(jsonObject);

	sprintf(sqFormat,
			"SELECT time, ip, port, hash FROM subscriber WHERE hash = '%s'",
			hash);

	//sendto(sockFDClient, json, strlen(json), 0, );

	sqError = sqlite3_prepare_v2(sqConn, sqFormat, 1000, &res, &tail);

	while (sqlite3_step(res) == SQLITE_ROW) {

		time_t timeVal = (time_t) sqlite3_column_int(res, 0);
		char *ip = sqlite3_column_text(res, 1);
		int port = sqlite3_column_int(res, 2);
		char *hash = sqlite3_column_text(res, 3);

		printf("sendto %s %d\n", ip, port);

		bzero(&sa, sizeof(sa));
		sa.sin_family = AF_INET;
		sa.sin_addr.s_addr = inet_addr(ip);
		sa.sin_port = htons(port);

		blocks = encipherEvent(json, bufferXTEA);
		sendto(sockFDClient, bufferXTEA, blocks * 8, 0, (struct sockaddr*) &sa, sizeof(sa));

	}

	sqlite3_finalize(res);

}

void processEvent(struct sockaddr_in cliAddr, json_object *jsonObject) {

	char keyMd5Table[128];
	unsigned char md5Result[16];
	MD5_CTX mdContext;
	time_t timeVal;

	MD5_Init(&mdContext);

	json_object_object_foreach(jsonObject, key1, val1) {

		if (strcmp(key1, "type") == 0) continue;

		MD5_Update(&mdContext, key1, strlen(key1));

	}

	MD5_Final(md5Result, &mdContext);

	sprintf(keyMd5Table,
			"%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x%.2x",
			md5Result[0], md5Result[1], md5Result[2], md5Result[3],
			md5Result[4], md5Result[5], md5Result[6], md5Result[7],
			md5Result[8], md5Result[9], md5Result[10], md5Result[11],
			md5Result[12], md5Result[13], md5Result[14], md5Result[15]);

	timeVal = time(NULL);

	json_object_object_foreach(jsonObject, key, val) {

		if (strcmp(key, "type") == 0) continue;

		switch (json_object_get_type(val)) {
		case json_type_string:
			insertEventAttribute(timeVal, keyMd5Table, key,
					json_object_get_string(val));
			break;
		}

	}

	sendEvent(keyMd5Table, jsonObject);

}

void processUDPClientMessages() {

	enum {
		__BEGIN_TYPE, EventT, SubscribeT, __END_TYPE
	};

	int len, n, typeVal, lenBuffer;
	struct sockaddr_in cliAddr;
	json_object *jsonObject;
	char bufferInput[1024];
	int swapped;

	len = sizeof(cliAddr);

	while (1) {

		memset(buffer, 0, sizeof buffer);
		memset(bufferInput, 0, sizeof bufferInput);

		n = recvfrom(sockFDClient, buffer, BUFFER_SIZE, 0,
				(struct sockaddr *) &cliAddr, &len);

		if (buffer[0] == '{') {
			strcpy(bufferInput, buffer);
		} else {
			lenBuffer = decipherEvent(buffer, bufferInput, sizeof(bufferInput));
			//buffer[n] = 0;
			bufferInput[lenBuffer] = 0;
		}

		fprintf(stderr, "Processing: %s\n", bufferInput);

		jsonObject = json_tokener_parse(bufferInput);

		typeVal = __BEGIN_TYPE;

		json_object_object_foreach(jsonObject, key, val) {

			if (strcmp(key, "type") == 0) {

				char *typeStr = json_object_get_string(val);

				if (strcmp(typeStr, "event") == 0) {
					typeVal = EventT;
				} else if (strcmp(typeStr, "subscribe") == 0) {
					typeVal = SubscribeT;
				}

			}

		}

		if (typeVal == EventT) {
			processEvent(cliAddr, jsonObject);
		} else if (typeVal == SubscribeT) {
			subscribe(cliAddr, jsonObject);
		} else {
			fprintf(stderr, "Invalid event\n");
		}

		json_object_put(jsonObject);

	}

}

void loadConfiguration(char *fileName) {
	char chr;
	json_object *jsonObject, *neighborsObj;
	FILE *file;
	int pt = 0, arrayLen, i;

	memset(buffer, 0, sizeof buffer);
	file = fopen(fileName, "r");
	while ((chr = fgetc(file)) != EOF) {
		buffer[pt++] = chr;
	}
	fclose(file);

	jsonObject = json_tokener_parse(buffer);

	PORT = json_object_get_int(json_object_object_get(jsonObject, "port"));
	neighborsObj = json_object_object_get(jsonObject, "neighbors");
	arrayLen = json_object_array_length(neighborsObj);

	for (i = 0; i < arrayLen; i++) {
		char *addressNeighbor;
		int portNeighbor;
		pthread_t thread;
		json_object *neighbor = json_object_array_get_idx(neighborsObj, i);

		//pthread_create(&thread, NULL, _threadNeighbors, (void*) neighbor);

	}

}

void *_threadDeleteOldEntries(void *arg) {

	char sqlFormat[250];
	time_t oldestEventTime;

	while (1) {
		oldestEventTime = time(NULL);
		sprintf(sqlFormat, "DELETE FROM subscriber WHERE time < '%ld'", (long int) oldestEventTime - 3600 * 2);
		sqError = sqlite3_exec(sqConn, sqlFormat, 0, 0, 0);

		sleep(1800);
	}

}

static pthread_t threadOldestEntries;

void startThreads() {

	pthread_create(&threadOldestEntries, NULL, _threadDeleteOldEntries, NULL);

}

int main(int argc, char **argv) {

	createDbConnection();
	//deleteAll();
	loadConfiguration(argv[1]);
	startThreads();
	createUDPClientSocket();
	processUDPClientMessages();

	return EXIT_SUCCESS;

}

