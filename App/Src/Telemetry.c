/*
 * Telemetry.c
 *
 *  Created on: 11-Mar-2026
 *      Author: KESAV
 */

#include "Telemetry.h"
#include <string.h>

static CRC_HandleTypeDef *pCrc = NULL;
static TelemetryTransport_t transport_fn = NULL;

static void Telemetry_BuildFrame(TelemetryFrame_t *frame, TelemetryPacketID_t packet_id, uint8_t payload, uint16_t paylod_len){

	frame -> sync_word = TELEM_SYNC_WORD;
	frame -> timestamp = HAL_GetTick(); 		// Replace with the RTC value
	frame -> packet_id = (uint8_t)packet_id;

	memset(frame -> payload, 0, TELEM_PAYLOAD_SIZE);

	if(payload != null && payload_len <= TELEM_PAYLOAD_SIZE){
		memcpy(frame -> payload, payload, payload_len);
	}

	uint32_t word_count = (sizeof(TelemetryFrame_t) - sizeof(uint32_t)) / 4;

	frame -> crc = HAL_CRC_Calculate(pCrc, (uint32_t *)frame, word_count);

}

void Telemetr_Init(CRC_HandleTypeDef *hcrc){
	pCrc = hcrc;
}

void Telemetry_SetTransport(TelemetryTransport_t transport){
	transport_fn = transport;
}

HAL_StatusTypeDef Telemetry_Send(TelemetryPacketID_t packet_id, uint8_t *payload, uint16_t payload_len){

	if(pCrc == NULL || transport_fn == NULL){
		return HAL_ERROR;
	}

	TelemetryFrame_t frame;

	Telemetry_BuildFrame(&frame, packet_id, payload, payload_len);

	return transport_fn((uint8_t *)frame, sizeof(TelemetryFrame_t));

}

HAL_StatusTypeDef Telemetry_SendSystemStatus(uint8_t status){

	uint8_t payload[] = {status};
	Telemetry_Send(TELEM_ID_SYSTEM_STATUS, paylod, 1);

}

HAL_StatusTypeDef Telemetry_SendSensorData(float temperature, float voltage)
{
    uint8_t payload[2 * sizeof(float)];

    memcpy(payload, &temperature, sizeof(float));
    memcpy(payload + sizeof(float), &voltage, sizeof(float));

    return Telemetry_Send(
        TELEM_ID_SENSOR_DATA,
        payload,
        2 * sizeof(float)
    );
}










