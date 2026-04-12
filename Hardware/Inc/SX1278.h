/*
 * SX1278.h
 *
 *  Created on: 12-Apr-2026
 *      Author: Codex
 */

#ifndef INC_SX1278_H_
#define INC_SX1278_H_

#include "SPI_Bus.h"
#include <stdbool.h>
#include <stdint.h>

#define SX1278_EXPECTED_VERSION           0x12U
#define SX1278_MAX_PAYLOAD_LENGTH         255U
#define SX1278_DEFAULT_FREQUENCY_HZ       433000000UL
#define SX1278_DEFAULT_SYNC_WORD          0x12U
#define SX1278_DEFAULT_PREAMBLE_LENGTH    8U

typedef enum {
    SX1278_STATUS_OK = 0,
    SX1278_STATUS_IDLE,
    SX1278_STATUS_INVALID_PARAM,
    SX1278_STATUS_NOT_INITIALIZED,
    SX1278_STATUS_SPI_ERROR,
    SX1278_STATUS_TIMEOUT,
    SX1278_STATUS_VERSION_MISMATCH,
    SX1278_STATUS_NO_PACKET,
    SX1278_STATUS_BUFFER_TOO_SMALL
} SX1278_Status_t;

typedef enum {
    SX1278_MODE_SLEEP = 0,
    SX1278_MODE_STANDBY,
    SX1278_MODE_TX,
    SX1278_MODE_RX_CONTINUOUS,
    SX1278_MODE_RX_SINGLE
} SX1278_Mode_t;

typedef struct {
    uint32_t frequency_hz;
    uint8_t sync_word;
    uint16_t preamble_length;
    uint8_t payload_length;
    int16_t last_rssi_dbm;
    int8_t last_snr_db;
    uint8_t last_irq_flags;
    SX1278_Mode_t mode;
    bool initialized;
} SX1278_Runtime_t;

typedef struct {
    SPI_Device_t spi;
    GPIO_TypeDef *reset_port;
    uint16_t reset_pin;
    bool has_reset_pin;
    SX1278_Runtime_t state;
} SX1278_Handle_t;

SX1278_Status_t SX1278_Init(SX1278_Handle_t *radio,
                            SPI_HandleTypeDef *hspi,
                            GPIO_TypeDef *cs_port,
                            uint16_t cs_pin,
                            GPIO_TypeDef *reset_port,
                            uint16_t reset_pin,
                            bool has_reset_pin);

SX1278_Status_t SX1278_Reset(SX1278_Handle_t *radio);
SX1278_Status_t SX1278_ReadRegister(SX1278_Handle_t *radio, uint8_t reg, uint8_t *value);
SX1278_Status_t SX1278_WriteRegister(SX1278_Handle_t *radio, uint8_t reg, uint8_t value);
SX1278_Status_t SX1278_ReadBurst(SX1278_Handle_t *radio, uint8_t reg, uint8_t *buffer, uint8_t length);
SX1278_Status_t SX1278_WriteBurst(SX1278_Handle_t *radio, uint8_t reg, const uint8_t *buffer, uint8_t length);
SX1278_Status_t SX1278_SetMode(SX1278_Handle_t *radio, SX1278_Mode_t mode);
SX1278_Status_t SX1278_SetFrequencyHz(SX1278_Handle_t *radio, uint32_t frequency_hz);
SX1278_Status_t SX1278_SetSyncWord(SX1278_Handle_t *radio, uint8_t sync_word);
SX1278_Status_t SX1278_SetPreambleLength(SX1278_Handle_t *radio, uint16_t preamble_length);
SX1278_Status_t SX1278_GetIrqFlags(SX1278_Handle_t *radio, uint8_t *irq_flags);
SX1278_Status_t SX1278_ClearIrqFlags(SX1278_Handle_t *radio, uint8_t irq_flags);
SX1278_Status_t SX1278_StartReceiveContinuous(SX1278_Handle_t *radio);
SX1278_Status_t SX1278_Transmit(SX1278_Handle_t *radio, const uint8_t *payload, uint8_t length, uint32_t timeout_ms);
SX1278_Status_t SX1278_Receive(SX1278_Handle_t *radio, uint8_t *buffer, uint8_t *length);
SX1278_Status_t SX1278_ReadVersion(SX1278_Handle_t *radio, uint8_t *version);
const char *SX1278_StatusToString(SX1278_Status_t status);

#endif /* INC_SX1278_H_ */
