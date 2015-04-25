/*
 * queue.h
 *
 *  Created on: Apr 25, 2015
 *      Author: max
 */

#ifndef QUEUE_H
#define QUEUE_H

#include <stdlib.h>
#include <memory.h>
#include <pthread.h>

#define QUEUE_SIZE 1024

typedef struct _queueEntry {

	char *hash;
	char *event;

} queueEntry;

void prepareQueue();
void queuePush(char *hash, char *event);
queueEntry* queuePop(int waitForNewEntry);
void queueFreeEntry(queueEntry *entry);

#endif
