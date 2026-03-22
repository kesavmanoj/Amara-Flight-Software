/*
 * Telemetry.h
 *
 *  Created on: 11-Mar-2026
 *      Author: KESAV
 */

#ifndef INC_TELEMETRY_H_
#define INC_TELEMETRY_H_


#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define TELEM_SYNC_WORD    0x55AA
/* Keep the non-CRC portion of TelemetryFrame_t 4-byte aligned for HAL_CRC_Calculate(). */
#define TELEM_PAYLOAD_SIZE 61
/* Circular queue capacity; usable frame slots are TELEM_QUEUE_SIZE - 1. */
#define TELEM_QUEUE_SIZE   9

typedef enum {
    TELEM_ID_SYSTEM_STATUS = 0x01,
    TELEM_ID_ADC_HEALTH    = 0x02,
    TELEM_ID_EVENT         = 0x03
} TelemetryPacketID_t;

typedef struct __attribute__((packed)) {
    uint8_t status_code;
    uint32_t uptime_ms;
} TelemetrySystemStatusPayload_t;

typedef struct __attribute__((packed)) {
    float vdda_voltage;
    float battery_voltage;
    float mcu_temp_c;
} TelemetryADCHealthPayload_t;

typedef struct __attribute__((packed)) {
    uint8_t event_code;
    uint32_t event_value;
} TelemetryEventPayload_t;

typedef struct __attribute__((packed)) {
    uint16_t sync_word;
    uint32_t timestamp;
    uint8_t  packet_id;
    uint8_t  payload[TELEM_PAYLOAD_SIZE];
    uint32_t crc;
} TelemetryFrame_t;

// API Functions

void Telemetry_Init(CRC_HandleTypeDef *hcrc);

bool Telemetry_QueuePacket(TelemetryPacketID_t id, uint8_t *payload, uint16_t len);

void Telemetry_Process(void);

bool Telemetry_SendSystemStatus(uint8_t status);
bool Telemetry_SendADCHealth(float vdda_voltage, float battery_voltage, float mcu_temp_c);
bool Telemetry_SendEvent(uint8_t event_code, uint32_t event_value);


#endif /* INC_TELEMETRY_H_ */
