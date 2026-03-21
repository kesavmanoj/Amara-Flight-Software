/*
 * Telemetry.h
 *
 *  Created on: 11-Mar-2026
 *      Author: KESAV
 */

#ifndef INC_TELEMETRY_H_
#define INC_TELEMETRY_H_

#include "stm32f4xx_hal.h"

#define TELEM_FRAME_START	0x55AA
#define TELEM_PAYLOAD_SIZE	65

typedef struct __attribute__((packed)){
	uint16_t 	sync_word;
	uint32_t 	timestamp;
	uint8_t 	packet_id;
	uint8_t 	payload[TELEM_PAYLOAD_SIZE];
	uint32_t 	crc;
} TelemetryFrame_t;

void Telemetry_Init(UART_HandleTypeDef *huart, CRC_HandleTypeDef *hcrc);
HAL_StatusTypeDef Telemetry_SendSystemStatus(uint8_t status_code);

#endif /* INC_TELEMETRY_H_ */
