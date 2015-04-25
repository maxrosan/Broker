/*
 * crypto.h
 *
 *  Created on: Apr 25, 2015
 *      Author: max
 */

#ifndef CRYPTO_H
#define CRYPTO_H

#include <stdlib.h>
#include <memory.h>
#include <stdint.h>

void encipher(unsigned int num_rounds, uint32_t v[2], uint32_t const key[4]);
void decipher(unsigned int num_rounds, uint32_t v[2], uint32_t const key[4]);
int encipherEvent(char *eventBuffer, char *bufferOutput);
int decipherEvent(char *bufferInput, char *bufferOutput, int lenBufferOutput);

#endif
