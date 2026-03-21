/*
 * Telemetry.c
 *
 *  Created on: 11-Mar-2026
 *      Author: KESAV
 */

#include "Telemetry.h"
#include "Ring_Buffer.h"
#include "UART_Driver.h"
#include <string.h>

static CRC_HandleTypeDef *pCrc = NULL;

static TelemetryFrame_t frame_buffer[TELEM_QUEUE_SIZE];
static TelemetryFrame_t tx_frame;
static FrameQueue_t telem_queue;

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
	(void)huart;

	if((hcrc == NULL) || (huart == NULL)){
		pCrc = NULL;
		frame_pending = false;
		return;
	}

	pCrc 	= hcrc;

	FrameQueue_Init(&telem_queue, (uint8_t *)frame_buffer, sizeof(TelemetryFrame_t), TELEM_QUEUE_SIZE);
	frame_pending = false;
	memset(&tx_frame, 0, sizeof(tx_frame));

}


bool Telemetry_QueuePacket(TelemetryPacketID_t id, uint8_t* payload, uint16_t len){

	if(pCrc == NULL) return false;
	if(len > TELEM_PAYLOAD_SIZE) return false;

	TelemetryFrame_t frame;
	Telemetry_BuildFrame(&frame, id, payload, len);

	return FrameQueue_Push(&telem_queue, &frame);

}

void Telemetry_Process(void){
	if(pCrc == NULL) return;

	if(!frame_pending){
		if(!FrameQueue_Pop(&telem_queue, &tx_frame)){
			return;
		}

		frame_pending = true;
	}

	if(UART_WriteChannel(UART_DRIVER_CHANNEL_TELEMETRY, (uint8_t *)&tx_frame, sizeof(tx_frame)) == UART_DRIVER_OK){
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
	(void)huart;
}

void Telemetry_OnError(UART_HandleTypeDef *huart)
{
	(void)huart;
}







