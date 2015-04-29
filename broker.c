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
#include <assert.h>
#include <signal.h>

#include "interpreter.h"
#include "md5.h"
#include "queue.h"
#include "crypto.h"

#define DEBUG_ENABLE 1

static int PORT = 10001;

#define BUFFER_SIZE 2048
#define PERIOD_TO_DELETE_OLD_SUBSCRIBERS (3600 * 2)
#define DEBUG(MSG, ...) if (DEBUG_ENABLE) printf(MSG "\n", ## __VA_ARGS__)

static int sockFDClient;
static struct sockaddr_in servAddr;
static char buffer[BUFFER_SIZE];
static sqlite3 *sqConn;
static sqlite3_stmt *sqRes;
static int sqError;
static pthread_t threadOldestEntries, threadConsumer;

void* _threadSendOldEvents(void *arg);

void createDbConnection() {

	sqError = sqlite3_open("broker.db", &sqConn);

	if (sqError) {
		fprintf(stderr, "Failed to connect to DB\n");
		exit(EXIT_FAILURE);
	}

}

void deleteAll() {

	char sqlFormat[250];

	sprintf(sqlFormat, "DELETE FROM subscriber");
	sqError = sqlite3_exec(sqConn, sqlFormat, 0, 0, 0);

}

void insertEventAttribute(time_t time, char *hash, char *attr, char *value) {

	char sqlFormat[250];

	sprintf(sqlFormat, "INSERT INTO event VALUES ('%s','%ld','%s','%s')", hash,
			time, attr, value);

	sqError = sqlite3_exec(sqConn, sqlFormat, 0, 0, 0);

}

void insertSubscriber(char *ip, int port, char *hash, char *condition) {

	char sqlFormat[250];

	time_t timeVal;

	timeVal = time(NULL);

	sprintf(sqlFormat,
			"DELETE FROM subscriber WHERE ip = '%s' AND port = '%d' AND hash = '%s'",
			ip, port, hash);

	sqError = sqlite3_exec(sqConn, sqlFormat, 0, 0, 0);

	sprintf(sqlFormat, "INSERT INTO subscriber VALUES (\"%ld\", \"%s\", \"%d\", \"%s\", \"%s\")",
			timeVal, ip, port, hash, condition);

	printf(sqlFormat);

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

typedef struct _eventToSend {

	time_t oldestEventToSend;
	char *hash;
	char *address;
	int port;
	char *condition;

} eventToSend;

void subscribe(struct sockaddr_in cliAddr, json_object *jsonObject) {

	char ipString[20];
	int port;
	char keyMd5Table[128];
	unsigned char md5Result[16];
	json_object *attributesObj, *val;
	int arrayLen, i;
	char *valArray, *whichEntrisToSend, *condition;
	MD5_CTX mdContext;

	port = ntohs(cliAddr.sin_port);

	get_ip_str(((const struct sockaddr*) &cliAddr), ipString, sizeof ipString);

	whichEntrisToSend = json_object_get_string(json_object_object_get(jsonObject, "which"));

	condition = json_object_get_string(json_object_object_get(jsonObject, "condition"));

	attributesObj = json_object_object_get(jsonObject, "attributes");
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

	insertSubscriber(ipString, port, keyMd5Table, condition);

	if (whichEntrisToSend != NULL && strcmp(whichEntrisToSend, "all") == 0) {

		pthread_t dispatchRequestOldEvents;
		eventToSend *event = (eventToSend*) malloc(sizeof(eventToSend));

		event->address = strdup(ipString);
		event->port = port;
		event->hash = strdup(keyMd5Table);
		event->oldestEventToSend = 0;
		event->condition = strdup(condition);

		pthread_create(&dispatchRequestOldEvents, NULL, _threadSendOldEvents, (void*) event);
		pthread_detach(dispatchRequestOldEvents);

	}

}

void sendEvent(char *hash, json_object *jsonObject) {

	char *json;

	json = json_object_get_string(jsonObject);

	printf("Pushing event %s %s\n", hash, json);

	queuePush(hash, json);

}

void processEvent(struct sockaddr_in cliAddr, json_object *jsonObject) {

	char keyMd5Table[128];
	unsigned char md5Result[16];
	MD5_CTX mdContext;
	time_t timeVal;

	MD5_Init(&mdContext);

	json_object_object_foreach(jsonObject, key1, val1) {

		if (strcmp(key1, "type") == 0)
			continue;

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

		if (strcmp(key, "type") == 0)
			continue;

		switch (json_object_get_type(val)) {
		case json_type_string:

			DEBUG("[%s] = [%s]", key, json_object_get_string(val));

			insertEventAttribute(timeVal, keyMd5Table, key,
					json_object_get_string(val));
			break;
		}

	}

	DEBUG("Event interpreted");

	sendEvent(keyMd5Table, jsonObject);

	DEBUG("Event being processed");

}

void processUDPClientMessages() {

	enum {
		__BEGIN_TYPE, EventT, SubscribeT, __END_TYPE
	};

	int len, n, typeVal, lenBuffer,
	 keepReceiving = 1;
	struct sockaddr_in cliAddr;
	json_object *jsonObject;
	char bufferInput[1024];
	int swapped;

	len = sizeof(cliAddr);

	while (keepReceiving) {

		memset(buffer, 0, sizeof buffer);
		memset(bufferInput, 0, sizeof bufferInput);

		n = recvfrom(sockFDClient, buffer, BUFFER_SIZE, 0,
				(struct sockaddr *) &cliAddr, &len);

		if (n == 0) {
			continue;
		}

		if (n == -1) {
			keepReceiving = 0;
			continue;
		}

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

		sprintf(sqlFormat, "DELETE FROM subscriber WHERE time < '%ld'",
				oldestEventTime - PERIOD_TO_DELETE_OLD_SUBSCRIBERS);

		sqError = sqlite3_exec(sqConn, sqlFormat, 0, 0, 0);

		sleep(PERIOD_TO_DELETE_OLD_SUBSCRIBERS);
	}

	return NULL;

}

void sendMessageTo(char *ip, int port, char *entry) {

	struct sockaddr_in sa;
	char *bufferXTEA;
	int blocks;

	bzero(&sa, sizeof(sa));

	bufferXTEA = (char*) malloc(sizeof(char) * 1024);

	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = inet_addr(ip);
	sa.sin_port = htons(port);

	blocks = encipherEvent(entry, bufferXTEA);

	sendto(sockFDClient, bufferXTEA, blocks * 8, 0,
			(struct sockaddr*) &sa, sizeof(sa));

	free(bufferXTEA);

}

void* _threadSendOldEvents(void *arg) {

#define NUMBER_OF_ATTRIBUTES 30
#define SIZE_OF_ATTRIBUTE 256
#define SIZE_OF_BUFFER 1024

	time_t timeVal, timeEntry = 0, lastTime = 0;
	char *sqFormat;
	const char *tail;
	sqlite3_stmt *res;
	char *attrName, *attrVal;
	char **attributes, **attributesValues;
	int numberOfAttributes = 0, i;
	char *bufferEvent, *bufferAttr;
	int blocks;
	Interpreter interpreter;

	sqFormat = (char*) malloc(sizeof(char) * SIZE_OF_ATTRIBUTE);

	attributes = (char**) malloc(sizeof(char*) * NUMBER_OF_ATTRIBUTES);
	attributesValues = (char**) malloc(sizeof(char*) * NUMBER_OF_ATTRIBUTES);

	for (i = 0; i < NUMBER_OF_ATTRIBUTES; i++) {
		attributes[i] = (char*) malloc(sizeof(char) * SIZE_OF_ATTRIBUTE);
		attributesValues[i] = (char*) malloc(sizeof(char) * SIZE_OF_ATTRIBUTE);
	}

	bufferEvent = (char*) malloc(sizeof(char) * SIZE_OF_BUFFER);
	bufferAttr = (char*) malloc(sizeof(char) * SIZE_OF_BUFFER);

	eventToSend *event = (eventToSend*) arg;

	timeVal = event->oldestEventToSend;

	sprintf(sqFormat,
			"SELECT time, attr, val FROM event WHERE hash = '%s' AND time >= '%d' ORDER BY time",
			event->hash,
			timeVal
			);

	sqError = sqlite3_prepare_v2(sqConn, sqFormat, 1000, &res, &tail);

	printf("Sending old events [%s]\n", sqFormat);

    interpreterCreate(&interpreter);
	interpreterPrepare(&interpreter);

	while (sqlite3_step(res) == SQLITE_ROW) {

		timeEntry = sqlite3_column_int(res, 0);
		attrName = sqlite3_column_text(res, 1);
		attrVal = sqlite3_column_text(res, 2);

		printf("Processing event: %d\n", timeEntry);

		if (lastTime == 0) lastTime = timeEntry;

		if (lastTime != timeEntry) {

			sprintf(bufferEvent, "{\"type\": \"event\", \"timeEvent\": \"%d\", ", lastTime);

			for (i = 0; i < numberOfAttributes; i++) {

				interpreterAddVariable(&interpreter, attributes[i], attributesValues[i]);

				if (i < (numberOfAttributes - 1)) {
					sprintf(bufferAttr, "\"%s\": \"%s\", ", attributes[i], attributesValues[i]);
				} else {
					sprintf(bufferAttr, "\"%s\": \"%s\" ", attributes[i], attributesValues[i]);
				}

				strcat(bufferEvent, bufferAttr);

			}

			strcat(bufferEvent, "}");

			printf("Sending old event: %s [%s]\n", bufferEvent, event->condition);

			if (interpreterGetConditionValue(&interpreter, event->condition)) {

				sendMessageTo(event->address, event->port, bufferEvent);

			}

			numberOfAttributes = 0;

		}

		strcpy(attributes[numberOfAttributes], attrName);
		strcpy(attributesValues[numberOfAttributes], attrVal);

		numberOfAttributes++;

		lastTime = timeEntry;

	}

	interpreterFree(&interpreter);

	sqlite3_finalize(res);

	free(event->address);
	free(event->hash);
	free(event->condition);
	free(event);

	for (i = 0; i < NUMBER_OF_ATTRIBUTES; i++) {
		free(attributes[i]);
		free(attributesValues[i]);
	}

	free(attributes);
	free(attributesValues);

	free(bufferEvent);
	free(bufferAttr);

	free(sqFormat);

}

void* _threadConsumer(void *arg) {

	queueEntry *entry;
	char sqFormat[200];
	const char *tail;
	sqlite3_stmt *res;
	int blocks;
	json_object *jsonObject;
	time_t timeVal;
	char *ip, *hash, *condition;
	int port, sendToSubscriber;
	Interpreter interpreter;
	int running = 1;

	while (running) {

		DEBUG("Waiting event to the subscribers...");

		entry = queuePop(1);

		if (entry == NULL) {
			running = 0;
			continue;
		}

		interpreterCreate(&interpreter);

		DEBUG("Preparing interpreter...");

		interpreterPrepare(&interpreter);

		DEBUG("Getting subscribers...");

		sprintf(sqFormat,
				"SELECT time, ip, port, hash, condition FROM subscriber WHERE hash = '%s'",
				entry->hash);

		sqError = sqlite3_prepare_v2(sqConn, sqFormat, 1000, &res, &tail);

		jsonObject = json_tokener_parse(entry->event);

		json_object_object_foreach(jsonObject, key, val) {

			DEBUG("attribute %s = %s", key, json_object_get_string(val));

			interpreterAddVariable(&interpreter, key, json_object_get_string(val));

		}

		json_object_put(jsonObject);

		while (sqlite3_step(res) == SQLITE_ROW) {

			timeVal = (time_t) sqlite3_column_int(res, 0);
			ip = sqlite3_column_text(res, 1);
			port = sqlite3_column_int(res, 2);
			hash = sqlite3_column_text(res, 3);
			condition = sqlite3_column_text(res, 4);
			sendToSubscriber = 0;

			if (strcmp(condition, "True") != 0) {
				sendToSubscriber = interpreterGetConditionValue(&interpreter, condition);
			} else {
				sendToSubscriber = 1;
			}

			if (sendToSubscriber) {
				printf("sendto %s %d\n", ip, port);

				sendMessageTo(ip, port, entry->event);
			}


		}

		sqlite3_finalize(res);

		queueFreeEntry(entry);

		//free(interpreter);

		interpreterFree(&interpreter);

	}

	return NULL;
}

void startThreads() {

	pthread_create(&threadConsumer, NULL, _threadConsumer, NULL);
	pthread_create(&threadOldestEntries, NULL, _threadDeleteOldEntries, NULL);

}

void catchSignal(int signo) {

	DEBUG("Exit...");

	queueDontWaitMore();
	close(sockFDClient);

}

int main(int argc, char **argv) {

	signal(SIGINT, catchSignal);

	interpreterGlobalLoad();

	prepareQueue();
	createDbConnection();
	deleteAll();
	loadConfiguration(argv[1]);
	startThreads();
	createUDPClientSocket();
	processUDPClientMessages();

	interpreterGlobalUnload();

	return EXIT_SUCCESS;

}

