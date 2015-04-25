/*
 * crypto.c
 *
 *  Created on: Apr 25, 2015
 *      Author: max
 */

#include "crypto.h"

static uint32_t key[4] = { 31231234, 412334, 12341, 657657 };

void encipher(unsigned int num_rounds, uint32_t v[2], uint32_t const key[4]) {
	unsigned int i;
	uint32_t v0 = v[0], v1 = v[1], sum = 0, delta = 0x9E3779B9;
	for (i = 0; i < num_rounds; i++) {
		v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
		sum += delta;
		v1 += (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
	}
	v[0] = v0;
	v[1] = v1;
}

void decipher(unsigned int num_rounds, uint32_t v[2], uint32_t const key[4]) {
	unsigned int i;
	uint32_t v0 = v[0], v1 = v[1], delta = 0x9E3779B9, sum = delta * num_rounds;
	for (i = 0; i < num_rounds; i++) {
		v1 -= (((v0 << 4) ^ (v0 >> 5)) + v0) ^ (sum + key[(sum >> 11) & 3]);
		sum -= delta;
		v0 -= (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + key[sum & 3]);
	}
	v[0] = v0;
	v[1] = v1;
}

int encipherEvent(char *eventBuffer, char *bufferOutput) {

	char buffer[8];
	int len, numOfBlocks, i, j;
	char *pt;

	len = strlen(eventBuffer);
	numOfBlocks = (len / 8) * 8 + ((len % 8) == 0 ? 0 : 1);

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

	for (i = 0; i < numOfBlocks; i++) {

		bzero(buffer, sizeof buffer);

		memcpy(buffer, pt, 8);
		decipher(32, (uint32_t*) buffer, key);

		memcpy(ptOutput, buffer, 8);

		pt += 8;
		ptOutput += 8;

	}

	return len;

}

