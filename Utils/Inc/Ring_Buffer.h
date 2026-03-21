/*
 * Ring_Buffer.h
 *
 *  Created on: 20-Mar-2026
 *      Author: KESAV
 */

#ifndef INC_RING_BUFFER_H_
#define INC_RING_BUFFER_H_

#include <stdbool.h>
#include <stdint.h>

#define RING_BUFFER_SIZE 128

typedef struct {
	uint8_t buffer[RING_BUFFER_SIZE];
	volatile uint16_t head;
	volatile uint16_t tail;
} RingBuffer_t;

void RingBuffer_Init(RingBuffer_t *rb);

bool RingBuffer_Push(RingBuffer_t *rb, uint8_t data);
bool RingBuffer_Pop(RingBuffer_t *rb, uint8_t *data);

bool RingBuffer_IsEmpty(RingBuffer_t *rb);
bool RingBuffer_IsFull(RingBuffer_t *rb);

uint16_t RingBuffer_Available(RingBuffer_t *rb);

#endif /* INC_RING_BUFFER_H_ */
