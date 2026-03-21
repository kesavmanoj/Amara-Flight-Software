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
#define TELEM_PAYLOAD_SIZE 64
#define TELEM_QUEUE_SIZE   8

#define TELEM_TX_BUFFER_SIZE 256
#define TELEM_DMA_CHUNK_SIZE 128

typedef enum {
    TELEM_ID_SYSTEM_STATUS = 0x01,
    TELEM_ID_SENSOR_DATA   = 0x02
} TelemetryPacketID_t;


typedef struct __attribute__((packed)) {
    uint16_t sync_word;
    uint32_t timestamp;
    uint8_t  packet_id;
    uint8_t  payload[TELEM_PAYLOAD_SIZE];
    uint32_t crc;
} TelemetryFrame_t;

// API Functions

void Telemetry_Init(CRC_HandleTypeDef *hcrc, UART_HandleTypeDef *huart);

bool Telemetry_QueuePacket(TelemetryPacketID_t id, uint8_t *payload, uint16_t len);

void Telemetry_Process(void);

bool Telemetry_SendSystemStatus(uint8_t status);

void Telemetry_OnTxComplete(UART_HandleTypeDef *huart);

void Telemetry_OnError(UART_HandleTypeDef *huart);


#endif /* INC_TELEMETRY_H_ */
