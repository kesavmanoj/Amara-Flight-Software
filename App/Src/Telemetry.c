/*
 * Telemetry.c
 *
 *  Created on: 11-Mar-2026
 *      Author: KESAV
 */

#include "Telemetry.h"
#include "Ring_Buffer.h"
#include <string.h>

static CRC_HandleTypeDef *pCrc = NULL;
static UART_HandleTypeDef *pUart = NULL;

static TelemetryFrame_t frame_buffer[TELEM_QUEUE_SIZE];
static TelemetryFrame_t dma_frame;
static FrameQueue_t telem_queue;

static volatile uint8_t dma_busy = 0;
static bool frame_pending = false;

static void Telemetry_BuildFrame(TelemetryFrame_t *frame, TelemetryPacketID_t id, uint8_t *payload, uint16_t len){

	frame -> sync_word = TELEM_SYNC_WORD;
	frame -> timestamp = HAL_GetTick(); 		// Replace with the RTC value
	frame -> packet_id = (uint8_t)id;

	memset(frame -> payload, 0, TELEM_PAYLOAD_SIZE);

	if(payload != NULL && len > 0){
		memcpy(frame -> payload, payload, len);
	}

	uint32_t word_count = (sizeof(TelemetryFrame_t) - sizeof(uint32_t)) / 4;

	frame -> crc = HAL_CRC_Calculate(pCrc, (uint32_t *)frame, word_count);

}

void Telemetry_Init(CRC_HandleTypeDef *hcrc, UART_HandleTypeDef *huart){

	if((hcrc == NULL) || (huart == NULL)){
		pCrc = NULL;
		pUart = NULL;
		dma_busy = 0;
		frame_pending = false;
		return;
	}

	pUart 	= huart;
	pCrc 	= hcrc;

	FrameQueue_Init(&telem_queue, (uint8_t *)frame_buffer, sizeof(TelemetryFrame_t), TELEM_QUEUE_SIZE);
	dma_busy = 0;
	frame_pending = false;
	memset(&dma_frame, 0, sizeof(dma_frame));

}


bool Telemetry_QueuePacket(TelemetryPacketID_t id, uint8_t* payload, uint16_t len){

	if((pCrc == NULL) || (pUart == NULL)) return false;
	if(len > TELEM_PAYLOAD_SIZE) return false;

	TelemetryFrame_t frame;
	Telemetry_BuildFrame(&frame, id, payload, len);

	return FrameQueue_Push(&telem_queue, &frame);

}

void Telemetry_Process(void){

	if((pCrc == NULL) || (pUart == NULL)) return;
	if(dma_busy) return;

	if(!frame_pending){
		if(!FrameQueue_Pop(&telem_queue, &dma_frame)){
			return;
		}

		frame_pending = true;
	}

	if(HAL_UART_Transmit_DMA(pUart, (uint8_t *)&dma_frame, sizeof(dma_frame)) == HAL_OK){
		dma_busy = 1;
		frame_pending = false;
	}
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

void Telemetry_OnTxComplete(UART_HandleTypeDef *huart)
{
    if (huart == pUart)
    {
        dma_busy = 0;
    }
}

void Telemetry_OnError(UART_HandleTypeDef *huart)
{
    if (huart == pUart)
    {
        dma_busy = 0;
    }
}







