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
    TELEM_ID_EVENT         = 0x03,
    TELEM_ID_HEARTBEAT     = 0x04,
    TELEM_ID_COMMAND_ACK   = 0x05
} TelemetryPacketID_t;

typedef enum {
    TELEM_EVENT_BOOT             = 0x01,
    TELEM_EVENT_ADC_READ_ERROR   = 0x02,
    TELEM_EVENT_QUEUE_OVERFLOW   = 0x03
} TelemetryEventCode_t;

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
    uint32_t uptime_ms;
    uint16_t queue_depth;
    uint8_t frame_pending;
    uint8_t reserved;
} TelemetryHeartbeatPayload_t;

typedef struct __attribute__((packed)) {
    uint8_t command_id;
    int8_t status_code;
    uint16_t reserved;
    uint32_t argument;
} TelemetryCommandAckPayload_t;

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
bool Telemetry_SendEvent(TelemetryEventCode_t event_code, uint32_t event_value);
bool Telemetry_SendHeartbeat(void);
bool Telemetry_SendCommandAck(uint8_t command_id, int8_t status_code, uint32_t argument);


#endif /* INC_TELEMETRY_H_ */
