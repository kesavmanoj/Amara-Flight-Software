/*
 * Telemetry.h
 *
 *  Created on: 11-Mar-2026
 *      Author: KESAV
 */

#ifndef INC_TELEMETRY_H_
#define INC_TELEMETRY_H_

#include <stm32f4xx_hal.h>
#include <stdint.h>

#define TELEM_SYNC_WORD 0x55AA
#define TELEM_PAYLOAD_SIZE 64

typedef enum {
	TELEM_ID_SYSTEM_STATUS 	= 0x01,
	TELEM_ID_SENSOR_DATA 	= 0x02,
	TELEM_ID_HEALTH 		= 0x03,
	TELEM_ID_DEBUG			= 0x04
} TelemetryPacketID_t;

typedef struct __attribute__((packed)) {
	uint16_t sync_word;
	uint32_t timestamp;
	uint8_t  packet_id;
	uint8_t  payload[TELEM_PAYLOAD_SIZE];
	uint32_t crc;
} TelemetryFrame_t;

// For variable transport type (UART, LoRa, etc)
typedef HAL_StatusTypeDef (*TelemetryTransport_t)(
		uint8_t *data,
		uint16_t size
);

void Telemetry_Init(CRC_HandleTypeDef);

void Telemetry_SetTransport(TelemetryTransport_t transport);


HAL_StatusTypeDef Telemetry_Send(TelemetryPacketID_t packet_id, uint8_t *payload, uint16_t payload_len);


HAL_StatusTypeDef Telemetry_SendSystemStatus(uint8_t status);

HAL_StatusTypeDef Telemetry_SendSensorData(float temperature, float voltage);




#endif /* INC_TELEMETRY_H_ */
