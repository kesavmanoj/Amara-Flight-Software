/*
 * Telemetry.c
 *
 *  Created on: 11-Mar-2026
 *      Author: KESAV
 */

#include "Telemetry.h"
#include <string.h>

static UART_HandleTypeDef	 *pTelemUart	 = NULL;
static CRC_HandleTypeDef	 *pTelemCrc		 = NULL;
static TelemetryFrame_t 	  current_frame;

void Telemetry_Init(UART_HandleTypeDef *huart, CRC_HandleTypeDef *hcrc){

	pTelemUart 	= huart;
	pTelemCrc 	= hcrc;

	current_frame.sync_word = TELEM_FRAME_START;
}

HAL_StatusTypeDef Telemetry_SendSystemStatus(uint8_t status_code){

	if(pTelemUart ==  NULL || pTelemCrc == NULL)
		return HAL_ERROR;
	if(HAL_UART_GetState(pTelemUart) != HAL_UART_STATE_READY)
		return HAL_BUSY;

	current_frame.timestamp = HAL_GetTick();
	current_frame.packet_id = 0x01;
	memset(current_frame.payload, 0, TELEM_PAYLOAD_SIZE);
	current_frame.payload[0] = status_code;

	uint32_t data_size_words = (sizeof(TelemetryFrame_t) - sizeof(uint32_t)) / 4;

	current_frame.crc = HAL_CRC_Calculate(pTelemCrc, (uint32_t *)&current_frame, data_size_words);

	return HAL_UART_Transmit_DMA(pTelemUart, (uint8_t *)&current_frame, sizeof(TelemetryFrame_t));

}











