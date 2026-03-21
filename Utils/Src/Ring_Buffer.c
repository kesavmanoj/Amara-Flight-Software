/*
 * Ring_Buffer.c
 *
 *  Created on: 20-Mar-2026
 *      Author: KESAV
 */

#include "Ring_Buffer.h"

static inline uint16_t next_index(uint16_t index){
	return (++index) % RING_BUFFER_SIZE;
}

// API Functions


void RingBuffer_Init(RingBuffer_t *rb){
	rb -> head = 0;
	rb -> tail = 0;
}

bool RingBuffer_IsEmpty(RingBuffer_t *rb)
{
    return (rb->head == rb->tail);
}

bool RingBuffer_IsFull(RingBuffer_t *rb)
{
    return (next_index(rb->head) == rb->tail);
}

bool RingBuffer_Push(RingBuffer_t *rb, uint8_t data){

	uint16_t next = next_index(rb -> head);
	if(next == rb -> tail){
		return false;
	}

	rb -> buffer[rb -> head] = data;
	rb -> head = next;
	return true;
}

bool RingBuffer_Pop(RingBuffer_t *rb, uint8_t *data){

	if(rb -> head == rb -> tail){
		return false; // Ring buffer is empty
	}

   *data = rb -> buffer[rb ->tail];
	rb -> tail = next_index(rb -> tail);

	return true;
}

uint16_t RingBuffer_Available(RingBuffer_t *rb)
{
    if (rb->head >= rb->tail)
    {
        return (rb->head - rb->tail);
    }
    else
    {
        return (RING_BUFFER_SIZE - rb->tail + rb->head);
    }
}











