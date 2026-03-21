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

/* 			BYTE BUFFER				 */

#define RING_BUFFER_SIZE 128

typedef struct {
	uint8_t buffer[RING_BUFFER_SIZE];
	volatile uint16_t head;
	volatile uint16_t tail;
} RingBuffer_t;

void RingBuffer_Init(RingBuffer_t *rb);

bool RingBuffer_Push(RingBuffer_t *rb, uint8_t data);
bool RingBuffer_Pop(RingBuffer_t *rb, uint8_t *data);

bool RingBuffer_PushArray(RingBuffer_t *rb, uint8_t *data, uint16_t len);
uint16_t RingBuffer_PopArray(RingBuffer_t *rb, uint8_t *data, uint16_t max_len);

bool RingBuffer_IsEmpty(RingBuffer_t *rb);
bool RingBuffer_IsFull(RingBuffer_t *rb);

uint16_t RingBuffer_Available(RingBuffer_t *rb);


/* 			FRAME BUFFER			 */

typedef struct {
	uint8_t *buffer;
	uint16_t element_size;
	uint16_t capacity;
	volatile uint16_t head;
	volatile uint16_t tail;
} FrameQueue_t;

void FrameQueue_Init(FrameQueue_t *fq, uint8_t *buffer, uint16_t element_size, uint16_t capacity);

bool FrameQueue_IsEmpty(FrameQueue_t *fq);
bool FrameQueue_IsFull(FrameQueue_t *fq);

bool FrameQueue_Push(FrameQueue_t *fq, void *item);
bool FrameQueue_Pop(FrameQueue_t *fq, void *item);

uint16_t FrameQueue_Count(FrameQueue_t *fq);

#endif /* INC_RING_BUFFER_H_ */
