/*
 * Ring_Buffer.c
 *
 *  Created on: 20-Mar-2026
 *      Author: KESAV
 */

#include "Ring_Buffer.h"
#include <string.h>

// BYTE BUFFER

static inline uint16_t next_index(uint16_t index){
	return (++index) % RING_BUFFER_SIZE;
}

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

bool RingBuffer_PushArray(RingBuffer_t *rb, uint8_t *data, uint16_t len){

	for(uint16_t i = 0; i < len; i++){
		if(!RingBuffer_Push(rb, data[i])) return false;
	}

	return true;

}

uint16_t RingBuffer_PopArray(RingBuffer_t *rb, uint8_t *data, uint16_t max_len){

	uint16_t count = 0;

	while(count < max_len && RingBuffer_IsEmpty(rb)){
		data[count++] = rb -> buffer[rb -> tail];
		rb -> tail = next_index(rb -> tail);
	}

	return count;
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


// FRAME QUEUE

static inline uint16_t fq_next(FrameQueue_t *fq, uint16_t index){
	return (index + 1) % fq -> capacity;
}

void FrameQueue_Init(FrameQueue_t *fq, uint8_t *buffer, uint16_t element_size, uint16_t capacity){

	fq -> buffer = buffer;
	fq -> element_size = element_size;
	fq -> capacity = capacity;
	fq -> head = 0;
	fq -> tail = 0;

}

bool FrameQueue_IsEmpty(FrameQueue_t *fq){

	return (fq -> head == fq -> tail);

}

bool FrameQueue_IsFull(FrameQueue_t *fq){

	return (fq_next(fq, fq -> head) == fq -> tail);

}

bool FrameQueue_Push(FrameQueue_t *fq, void *item){

	uint16_t next = fq_next(fq, fq -> head);

	if(next == fq -> tail) return false;

	uint8_t *dest = fq -> buffer + (fq -> head * fq -> element_size);
	memcpy(dest, item, fq -> element_size);

	fq -> head = next;
	return true;
}

bool FrameQueue_Pop(FrameQueue_t *fq, void *item){

	if(fq -> head == fq -> tail) return false; // Queue is empty

	uint8_t *src = fq -> buffer + (fq -> tail * fq -> element_size);
	memcpy(item, src, fq -> element_size);

	fq -> tail = fq_next(fq, fq -> tail);
	return true;

}

uint16_t FrameQueue_Count(FrameQueue_t *fq)
{
    if (fq->head >= fq->tail)
        return (fq->head - fq->tail);
    else
        return (fq->capacity - fq->tail + fq->head);
}






