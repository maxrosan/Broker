/*
 * queue.c
 *
 *  Created on: Apr 25, 2015
 *      Author: max
 */

#include "queue.h"

static pthread_mutex_t queueMutex;
static pthread_cond_t queueCond;
static int queueHead, queueTail, queueSize;
static queueEntry* queueEntries[QUEUE_SIZE];

void prepareQueue() {

	pthread_mutex_init(&queueMutex, NULL);
	pthread_cond_init(&queueCond, NULL);

	queueHead = queueTail = queueSize = 0;

}

void queuePush(char *hash, char *event) {

	queueEntry* entry;

	if (queueSize == QUEUE_SIZE) {
		return;
	}

	entry = (queueEntry*) malloc(sizeof(queueEntry));

	entry->hash = strdup(hash);
	entry->event = strdup(event);

	pthread_mutex_lock(&queueMutex);

	queueEntries[queueHead] = entry;

	queueHead = (queueHead + 1) % QUEUE_SIZE;
	queueSize++;

	if (queueSize == 1) {
		pthread_cond_broadcast(&queueCond);
	}

	pthread_mutex_unlock(&queueMutex);

	printf("pushed\n");

}

queueEntry* queuePop(int waitForNewEntry) {

	queueEntry* entry = NULL;

	pthread_mutex_lock(&queueMutex);

	do {

		if (queueSize > 0) {

			entry = queueEntries[queueTail];
			queueTail = (queueTail + 1) % QUEUE_SIZE;
			queueSize--;

			waitForNewEntry = 0;

		} else {

			if (waitForNewEntry) {
				pthread_cond_wait(&queueCond, &queueMutex);
			}

		}

	} while (waitForNewEntry);

	pthread_mutex_unlock(&queueMutex);

	return entry;

}

void queueFreeEntry(queueEntry *entry) {

	free(entry->hash);
	free(entry->event);
	free(entry);

}

