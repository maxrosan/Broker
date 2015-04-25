/*
 * subscriber_websocket.c
 *
 *  Created on: Apr 25, 2015
 *      Author: max
 */

#include <stdio.h>
#include <stdlib.h>
#include <libwebsockets.h>
#include <time.h>
#include <unistd.h>
#include <memory.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>

#include "crypto.h"

int sock;
struct sockaddr_in servAddr;
int port;
char *ipAddr;
char parametersList[1024];

uint32_t key[4] = { 31231234, 412334, 12341, 657657 };

pthread_mutex_t lock;
char *event = NULL;
time_t lastUpdate = 0;

void createUDPSocket() {

	sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);

	bzero(&servAddr, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_addr.s_addr = inet_addr(ipAddr);
	servAddr.sin_port = htons(port);

}

void *_threadSubscribe(void *arg) {

	char eventMsg[512];
	char buffer[1024];
	int blocks;

	while (1) {

		sprintf(eventMsg,
				"{ \"type\": \"subscribe\", \"condition\": \"True\", \"which\": \"last\", \"attributes\": "
						"[ %s ]   }", parametersList);

		blocks = encipherEvent(eventMsg, buffer);
		sendto(sock, buffer, blocks * 8, 0, (const struct sockaddr*) &servAddr,
				sizeof(servAddr));

		sleep(1800 + 10);

	}

	return NULL;
}

void *_threadMessage(void *arg) {

	char buffer[1024], bufferInput[1024];
	int blocks;
	struct sockaddr_in cliAddr;
	int len = sizeof(cliAddr), lenString, lenBuffer;

	srand(time(NULL));

	while (1) {

		printf("Waiting event\n");

		lenString = recvfrom(sock, buffer, sizeof buffer, 0,
				(struct sockaddr*) &cliAddr, &len);

		if (lenString <= 0) {
			sleep(1);
			continue;
		}

		lenBuffer = decipherEvent(buffer, bufferInput, sizeof(bufferInput));

		bufferInput[lenBuffer] = 0;
		printf("Process event: %s\n", bufferInput);

		//queuePush("NULL", bufferInput);

		pthread_mutex_lock(&lock);

		if (event != NULL) {
			free(event);
		}

		event = strdup(bufferInput);

		pthread_mutex_unlock(&lock);

	}

	return NULL;
}

static int callback_http(struct libwebsocket_context * this,
		struct libwebsocket *wsi, enum libwebsocket_callback_reasons reason,
		void *user, void *in, size_t len) {

	return 0;

}

static int callback_event(struct libwebsocket_context * this,
		struct libwebsocket *wsi, enum libwebsocket_callback_reasons reason,
		void *user, void *in, size_t len) {

	unsigned char buf[LWS_SEND_BUFFER_PRE_PADDING + 512 +
	LWS_SEND_BUFFER_POST_PADDING];

	unsigned char *p = &buf[LWS_SEND_BUFFER_PRE_PADDING];

	switch (reason) {

	case LWS_CALLBACK_ESTABLISHED: // just log message that someone is connecting
		printf("connection established\n");
		libwebsocket_callback_on_writable(this, wsi);
		break;

	case LWS_CALLBACK_SERVER_WRITEABLE: {

		char buffer[1024];

		//queueEntry* entry = queuePop(0);

		pthread_mutex_lock(&lock);

		if (event != NULL) {

			int n = sprintf((char*) p, "%s", event);
			libwebsocket_write(wsi, p, n, LWS_WRITE_TEXT);
			libwebsocket_callback_on_writable(this, wsi);

			//queueFreeEntry(entry);
			free(event);

			event = NULL;

		}

		pthread_mutex_unlock(&lock);

		break;
	}

	}

	return 0;

}

static struct libwebsocket_protocols protocols[] = {
/* first protocol must always be HTTP handler */
{ "http-only",   // name
		callback_http, // callback
		0              // per_session_data_size
		}, { "event-protocol", // protocol name - very important!
				callback_event,   // callback
				0                          // we don't use any per session data

		}, {
		NULL, NULL, 0 /* End of list */
		} };

int main(int argc, char **argv) { // ./subscriber_websocket ip_server port_broker port_ws attr1 attr2 ...

	ipAddr = strdup(argv[1]);
	port = atoi(argv[2]);

	int portWS = atoi(argv[3]), i;
	struct libwebsocket_context *context;
	struct lws_context_creation_info info;
	pthread_t threadMessage;
	pthread_t threadSubscribe;
	char bufferParam[512];

	strcpy(parametersList, "");

	for (i = 4; i < argc; i++) {

		if (i == (argc - 1)) {
			sprintf(bufferParam, "\"%s\"", argv[i]);
		} else {
			sprintf(bufferParam, "\"%s\", ", argv[i]);
		}

		strcat(parametersList, bufferParam);

	}

	info.port = portWS;
	info.extensions = NULL;
	info.iface = NULL;
	info.protocols = protocols;
	info.ssl_ca_filepath = NULL;
	info.ssl_private_key_filepath = NULL;
	info.ssl_cert_filepath = NULL;
	info.gid = -1;
	info.uid = -1;
	info.options = 0;
	info.user = NULL;
	info.ka_interval = info.ka_probes = info.ka_time = 0;

	//prepareQueue();
	pthread_mutex_init(&lock, NULL);
	event = NULL;
	createUDPSocket();

	pthread_create(&threadMessage, NULL, _threadMessage, NULL);
	pthread_create(&threadSubscribe, NULL, _threadSubscribe, NULL);

	context = libwebsocket_create_context(&info);

	if (context == NULL) {
		fprintf(stderr, "libwebsocket init failed\n");
		return -1;
	}

	printf("starting server...\n");

	while (1) {

		libwebsocket_service(context, 50);

	}

	libwebsocket_context_destroy(context);

	return 0;
}
