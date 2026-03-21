/*
 * Telemetry.c
 *
 *  Created on: 11-Mar-2026
 *      Author: KESAV
 */

#include "Telemetry.h"
#include <string.h>

static CRC_HandleTypeDef *pCrc = NULL;

static TelemetryFrame_t frame_buffer[TELEM_QUEUE_SIZE];
static FrameQueue_t telem_queue;

static RingBuffer_t tx_buffer;
extern UART_HandleTypeDef huart2;

static volatile uint8_t dma_busy = 0;

#define TELEM_DMA_CHUNK_SIZE 128
static uint8_t dma_tx_buffer[TELEM_DMA_CHUNK_SIZE];

static void Telemetry_BuildFrame(TelemetryFrame_t *frame, TelemetryPacketID_t id, uint8_t *payload, uint16_t len){

	frame -> sync_word = TELEM_SYNC_WORD;
	frame -> timestamp = HAL_GetTick(); 		// Replace with the RTC value
	frame -> packet_id = (uint8_t)id;

	memset(frame -> payload, 0, TELEM_PAYLOAD_SIZE);

	if(payload && len <= TELEM_PAYLOAD_SIZE){
		memcpy(frame -> payload, payload, len);
	}

	uint32_t word_count = (sizeof(TelemetryFrame_t) - sizeof(uint32_t)) / 4;

	frame -> crc = HAL_CRC_Calculate(pCrc, (uint32_t *)frame, word_count);

}

void Telemetry_Init(CRC_HandleTypeDef *hcrc){

	pCrc = hcrc;

	FrameQueue_Init(&telem_queue, (uint8_t *)frame_buffer, sizeof(TelemetryFrame_t), TELEM_QUEUE_SIZE);
	RingBuffer_Init(&tx_buffer);

}


bool Telemetry_QueuePacket(TelemetryPacketID_t id, uint8_t* payload, uint16_t len){

	if(pCrc == NULL) return false;

	TelemetryFrame_t frame;
	Telemetry_BuildFrame(&frame, id, payload, len);

	return FrameQueue_Push(&telem_queue, &frame);

}

void Telemetry_Process(void){

	TelemetryFrame_t frame;

	if(!FrameQueue_IsEmpty(&telem_queue)){
		if(FrameQueue_Pop(&telem_queue, &frame)){
			RingBuffer_PushArray(&tx_buffer, (uint8_t *)&frame, sizeof(frame));
		}
	}

	if(!dma_busy && !RingBuffer_IsEmpty(&tx_buffer)){
		uint16_t len = RingBuffer_PopArray(&tx_buffer, dma_tx_buffer, TELEM_DMA_CHUNK_SIZE);

		if(len > 0){
			dma_busy = 1;

			HAL_UART_Transmit_DMA(&huart2, dma_tx_buffer, len);
		}
	}
//	uint8_t byte;
//
//	while(!RingBuffer_IsEmpty(&tx_buffer)){
//
//		if(HAL_UART_GetState(&huart2) != HAL_UART_STATE_READY) break;
//
//		if(RingBuffer_Pop(&tx_buffer, &byte)){
//			HAL_UART_Transmit(&huart2, &byte, 1, 10);
//		}
//	}
}

bool Telemetry_SendSystemStatus(uint8_t status)
{
    uint8_t payload[1] = {status};

    return Telemetry_QueuePacket(
        TELEM_ID_SYSTEM_STATUS,
        payload,
        1
    );
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2)
    {
        dma_busy = 0;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2)
    {
        dma_busy = 0; // recover
    }
}







