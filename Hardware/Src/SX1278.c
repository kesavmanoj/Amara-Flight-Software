/*
 * SX1278.c
 *
 *  Created on: 12-Apr-2026
 *      Author: Codex
 */

#include "SX1278.h"
#include <string.h>

#define SX1278_REG_FIFO                    0x00U
#define SX1278_REG_OP_MODE                 0x01U
#define SX1278_REG_FRF_MSB                 0x06U
#define SX1278_REG_FRF_MID                 0x07U
#define SX1278_REG_FRF_LSB                 0x08U
#define SX1278_REG_PA_CONFIG               0x09U
#define SX1278_REG_LNA                     0x0CU
#define SX1278_REG_FIFO_ADDR_PTR           0x0DU
#define SX1278_REG_FIFO_TX_BASE_ADDR       0x0EU
#define SX1278_REG_FIFO_RX_BASE_ADDR       0x0FU
#define SX1278_REG_FIFO_RX_CURRENT_ADDR    0x10U
#define SX1278_REG_IRQ_FLAGS               0x12U
#define SX1278_REG_RX_NB_BYTES             0x13U
#define SX1278_REG_PKT_SNR_VALUE           0x19U
#define SX1278_REG_PKT_RSSI_VALUE          0x1AU
#define SX1278_REG_MODEM_CONFIG_1          0x1DU
#define SX1278_REG_MODEM_CONFIG_2          0x1EU
#define SX1278_REG_PREAMBLE_MSB            0x20U
#define SX1278_REG_PREAMBLE_LSB            0x21U
#define SX1278_REG_PAYLOAD_LENGTH          0x22U
#define SX1278_REG_MODEM_CONFIG_3          0x26U
#define SX1278_REG_SYNC_WORD               0x39U
#define SX1278_REG_DIO_MAPPING_1           0x40U
#define SX1278_REG_VERSION                 0x42U

#define SX1278_MODE_LONG_RANGE             0x80U
#define SX1278_MODE_SLEEP_BITS             0x00U
#define SX1278_MODE_STANDBY_BITS           0x01U
#define SX1278_MODE_TX_BITS                0x03U
#define SX1278_MODE_RX_CONT_BITS           0x05U
#define SX1278_MODE_RX_SINGLE_BITS         0x06U

#define SX1278_IRQ_RX_DONE                 0x40U
#define SX1278_IRQ_PAYLOAD_CRC_ERROR       0x20U
#define SX1278_IRQ_TX_DONE                 0x08U
#define SX1278_IRQ_CLEAR_ALL               0xFFU

#define SX1278_FIFO_TX_BASE                0x00U
#define SX1278_FIFO_RX_BASE                0x00U
#define SX1278_TX_DONE_TIMEOUT_MS          1000U

static SX1278_Status_t SX1278_Validate(const SX1278_Handle_t *radio)
{
    if (radio == NULL)
    {
        return SX1278_STATUS_INVALID_PARAM;
    }

    if (radio->spi.hspi == NULL)
    {
        return SX1278_STATUS_NOT_INITIALIZED;
    }

    if (radio->state.initialized == false)
    {
        return SX1278_STATUS_NOT_INITIALIZED;
    }

    return SX1278_STATUS_OK;
}

static SX1278_Status_t SX1278_ConvertSpiStatus(SPI_Driver_Status_t status)
{
    switch (status)
    {
        case SPI_DRIVER_OK:
            return SX1278_STATUS_OK;
        case SPI_DRIVER_BUSY:
            return SX1278_STATUS_SPI_ERROR;
        case SPI_DRIVER_TIMEOUT:
            return SX1278_STATUS_TIMEOUT;
        case SPI_DRIVER_INVALID_PARAM:
            return SX1278_STATUS_INVALID_PARAM;
        case SPI_DRIVER_NOT_INITIALIZED:
            return SX1278_STATUS_NOT_INITIALIZED;
        case SPI_DRIVER_ERROR:
        default:
            return SX1278_STATUS_SPI_ERROR;
    }
}

static uint8_t SX1278_ModeToRegister(SX1278_Mode_t mode)
{
    switch (mode)
    {
        case SX1278_MODE_SLEEP:
            return SX1278_MODE_SLEEP_BITS;
        case SX1278_MODE_STANDBY:
            return SX1278_MODE_STANDBY_BITS;
        case SX1278_MODE_TX:
            return SX1278_MODE_TX_BITS;
        case SX1278_MODE_RX_CONTINUOUS:
            return SX1278_MODE_RX_CONT_BITS;
        case SX1278_MODE_RX_SINGLE:
        default:
            return SX1278_MODE_RX_SINGLE_BITS;
    }
}

static void SX1278_ApplyResetPulse(const SX1278_Handle_t *radio)
{
    if ((radio == NULL) || (radio->has_reset_pin == false) || (radio->reset_port == NULL))
    {
        return;
    }

    HAL_GPIO_WritePin(radio->reset_port, radio->reset_pin, GPIO_PIN_RESET);
    HAL_Delay(1U);
    HAL_GPIO_WritePin(radio->reset_port, radio->reset_pin, GPIO_PIN_SET);
    HAL_Delay(10U);
}

static SX1278_Status_t SX1278_WriteRegisterRaw(SX1278_Handle_t *radio, uint8_t reg, uint8_t value)
{
    uint8_t tx[2];

    tx[0] = (uint8_t)(reg | 0x80U);
    tx[1] = value;

    SPI_CS_Low(&radio->spi);
    SPI_Driver_Status_t spi_status = SPI_Transmit(&radio->spi, tx, sizeof(tx));
    SPI_CS_High(&radio->spi);

    return SX1278_ConvertSpiStatus(spi_status);
}

static SX1278_Status_t SX1278_ReadRegisterRaw(SX1278_Handle_t *radio, uint8_t reg, uint8_t *value)
{
    uint8_t tx[2] = {reg & 0x7FU, 0x00U};
    uint8_t rx[2] = {0U, 0U};
    SX1278_Status_t status;

    if (value == NULL)
    {
        return SX1278_STATUS_INVALID_PARAM;
    }

    SPI_CS_Low(&radio->spi);
    status = SX1278_ConvertSpiStatus(SPI_TransmitReceive(&radio->spi, tx, rx, sizeof(tx)));
    SPI_CS_High(&radio->spi);

    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    *value = rx[1];
    return SX1278_STATUS_OK;
}

SX1278_Status_t SX1278_Init(SX1278_Handle_t *radio,
                            SPI_HandleTypeDef *hspi,
                            GPIO_TypeDef *cs_port,
                            uint16_t cs_pin,
                            GPIO_TypeDef *reset_port,
                            uint16_t reset_pin,
                            bool has_reset_pin)
{
    uint8_t version = 0U;
    SX1278_Status_t status;

    if ((radio == NULL) || (hspi == NULL) || (cs_port == NULL))
    {
        return SX1278_STATUS_INVALID_PARAM;
    }

    if (SPI_Device_Init(&radio->spi, hspi, cs_port, cs_pin) != SPI_DRIVER_OK)
    {
        return SX1278_STATUS_SPI_ERROR;
    }

    radio->reset_port = reset_port;
    radio->reset_pin = reset_pin;
    radio->has_reset_pin = has_reset_pin;
    radio->state.initialized = false;
    radio->state.mode = SX1278_MODE_SLEEP;
    radio->state.frequency_hz = SX1278_DEFAULT_FREQUENCY_HZ;
    radio->state.sync_word = SX1278_DEFAULT_SYNC_WORD;
    radio->state.preamble_length = SX1278_DEFAULT_PREAMBLE_LENGTH;
    radio->state.payload_length = 0U;
    radio->state.last_irq_flags = 0U;
    radio->state.last_rssi_dbm = 0;
    radio->state.last_snr_db = 0;

    SX1278_ApplyResetPulse(radio);

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_OP_MODE, (uint8_t)(SX1278_MODE_LONG_RANGE | SX1278_MODE_SLEEP_BITS));
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    HAL_Delay(1U);

    status = SX1278_ReadRegisterRaw(radio, SX1278_REG_VERSION, &version);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    if (version != SX1278_EXPECTED_VERSION)
    {
        return SX1278_STATUS_VERSION_MISMATCH;
    }

    status = SX1278_SetMode(radio, SX1278_MODE_STANDBY);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_FIFO_TX_BASE_ADDR, SX1278_FIFO_TX_BASE);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_FIFO_RX_BASE_ADDR, SX1278_FIFO_RX_BASE);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_SetFrequencyHz(radio, SX1278_DEFAULT_FREQUENCY_HZ);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_SetSyncWord(radio, SX1278_DEFAULT_SYNC_WORD);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_SetPreambleLength(radio, SX1278_DEFAULT_PREAMBLE_LENGTH);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_MODEM_CONFIG_1, 0x72U);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_MODEM_CONFIG_2, 0x74U);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_MODEM_CONFIG_3, 0x04U);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_LNA, 0x23U);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_PA_CONFIG, 0x8FU);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_DIO_MAPPING_1, 0x00U);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_ClearIrqFlags(radio, SX1278_IRQ_CLEAR_ALL);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    radio->state.initialized = true;
    return SX1278_StartReceiveContinuous(radio);
}

SX1278_Status_t SX1278_Reset(SX1278_Handle_t *radio)
{
    SX1278_Status_t status;

    if (radio == NULL)
    {
        return SX1278_STATUS_INVALID_PARAM;
    }

    if (radio->spi.hspi == NULL)
    {
        return SX1278_STATUS_NOT_INITIALIZED;
    }

    SX1278_ApplyResetPulse(radio);
    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_OP_MODE, (uint8_t)(SX1278_MODE_LONG_RANGE | SX1278_MODE_SLEEP_BITS));
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    return SX1278_SetMode(radio, SX1278_MODE_STANDBY);
}

SX1278_Status_t SX1278_ReadRegister(SX1278_Handle_t *radio, uint8_t reg, uint8_t *value)
{
    SX1278_Status_t status = SX1278_Validate(radio);

    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    return SX1278_ReadRegisterRaw(radio, reg, value);
}

SX1278_Status_t SX1278_WriteRegister(SX1278_Handle_t *radio, uint8_t reg, uint8_t value)
{
    SX1278_Status_t status = SX1278_Validate(radio);

    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    return SX1278_WriteRegisterRaw(radio, reg, value);
}

SX1278_Status_t SX1278_ReadBurst(SX1278_Handle_t *radio, uint8_t reg, uint8_t *buffer, uint8_t length)
{
    uint8_t tx[SX1278_MAX_PAYLOAD_LENGTH + 1U];
    uint8_t rx[SX1278_MAX_PAYLOAD_LENGTH + 1U];
    SX1278_Status_t status = SX1278_Validate(radio);

    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    if ((buffer == NULL) || (length == 0U) || (length > SX1278_MAX_PAYLOAD_LENGTH))
    {
        return SX1278_STATUS_INVALID_PARAM;
    }

    memset(tx, 0, (size_t)length + 1U);
    tx[0] = reg & 0x7FU;

    SPI_CS_Low(&radio->spi);
    status = SX1278_ConvertSpiStatus(SPI_TransmitReceive(&radio->spi, tx, rx, (uint16_t)length + 1U));
    SPI_CS_High(&radio->spi);

    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    memcpy(buffer, &rx[1], length);
    return SX1278_STATUS_OK;
}

SX1278_Status_t SX1278_WriteBurst(SX1278_Handle_t *radio, uint8_t reg, const uint8_t *buffer, uint8_t length)
{
    uint8_t tx[SX1278_MAX_PAYLOAD_LENGTH + 1U];
    SX1278_Status_t status = SX1278_Validate(radio);

    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    if ((buffer == NULL) || (length == 0U) || (length > SX1278_MAX_PAYLOAD_LENGTH))
    {
        return SX1278_STATUS_INVALID_PARAM;
    }

    tx[0] = (uint8_t)(reg | 0x80U);
    memcpy(&tx[1], buffer, length);

    SPI_CS_Low(&radio->spi);
    status = SX1278_ConvertSpiStatus(SPI_Transmit(&radio->spi, tx, (uint16_t)length + 1U));
    SPI_CS_High(&radio->spi);

    return status;
}

SX1278_Status_t SX1278_SetMode(SX1278_Handle_t *radio, SX1278_Mode_t mode)
{
    SX1278_Status_t status;

    if (radio == NULL)
    {
        return SX1278_STATUS_INVALID_PARAM;
    }

    if (radio->spi.hspi == NULL)
    {
        return SX1278_STATUS_NOT_INITIALIZED;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_OP_MODE, (uint8_t)(SX1278_MODE_LONG_RANGE | SX1278_ModeToRegister(mode)));
    if (status == SX1278_STATUS_OK)
    {
        radio->state.mode = mode;
    }

    return status;
}

SX1278_Status_t SX1278_SetFrequencyHz(SX1278_Handle_t *radio, uint32_t frequency_hz)
{
    uint64_t frf;
    SX1278_Status_t status;

    if (frequency_hz == 0U)
    {
        return SX1278_STATUS_INVALID_PARAM;
    }

    status = SX1278_Validate(radio);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    frf = (((uint64_t)frequency_hz) << 19) / 32000000ULL;

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_FRF_MSB, (uint8_t)(frf >> 16));
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_FRF_MID, (uint8_t)(frf >> 8));
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_FRF_LSB, (uint8_t)frf);
    if (status == SX1278_STATUS_OK)
    {
        radio->state.frequency_hz = frequency_hz;
    }

    return status;
}

SX1278_Status_t SX1278_SetSyncWord(SX1278_Handle_t *radio, uint8_t sync_word)
{
    SX1278_Status_t status = SX1278_Validate(radio);

    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_SYNC_WORD, sync_word);
    if (status == SX1278_STATUS_OK)
    {
        radio->state.sync_word = sync_word;
    }

    return status;
}

SX1278_Status_t SX1278_SetPreambleLength(SX1278_Handle_t *radio, uint16_t preamble_length)
{
    SX1278_Status_t status = SX1278_Validate(radio);

    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_PREAMBLE_MSB, (uint8_t)(preamble_length >> 8));
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_PREAMBLE_LSB, (uint8_t)preamble_length);
    if (status == SX1278_STATUS_OK)
    {
        radio->state.preamble_length = preamble_length;
    }

    return status;
}

SX1278_Status_t SX1278_GetIrqFlags(SX1278_Handle_t *radio, uint8_t *irq_flags)
{
    SX1278_Status_t status = SX1278_ReadRegister(radio, SX1278_REG_IRQ_FLAGS, irq_flags);

    if ((status == SX1278_STATUS_OK) && (irq_flags != NULL))
    {
        radio->state.last_irq_flags = *irq_flags;
    }

    return status;
}

SX1278_Status_t SX1278_ClearIrqFlags(SX1278_Handle_t *radio, uint8_t irq_flags)
{
    SX1278_Status_t status = SX1278_WriteRegister(radio, SX1278_REG_IRQ_FLAGS, irq_flags);

    if (status == SX1278_STATUS_OK)
    {
        radio->state.last_irq_flags = 0U;
    }

    return status;
}

SX1278_Status_t SX1278_StartReceiveContinuous(SX1278_Handle_t *radio)
{
    SX1278_Status_t status = SX1278_Validate(radio);

    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_FIFO_ADDR_PTR, SX1278_FIFO_RX_BASE);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_ClearIrqFlags(radio, SX1278_IRQ_CLEAR_ALL);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    return SX1278_SetMode(radio, SX1278_MODE_RX_CONTINUOUS);
}

SX1278_Status_t SX1278_Transmit(SX1278_Handle_t *radio, const uint8_t *payload, uint8_t length, uint32_t timeout_ms)
{
    uint8_t irq_flags = 0U;
    uint32_t start_ms;
    SX1278_Status_t status = SX1278_Validate(radio);

    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    if ((payload == NULL) || (length == 0U) || (length > SX1278_MAX_PAYLOAD_LENGTH))
    {
        return SX1278_STATUS_INVALID_PARAM;
    }

    status = SX1278_SetMode(radio, SX1278_MODE_STANDBY);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_FIFO_ADDR_PTR, SX1278_FIFO_TX_BASE);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteBurst(radio, SX1278_REG_FIFO, payload, length);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_PAYLOAD_LENGTH, length);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_ClearIrqFlags(radio, SX1278_IRQ_CLEAR_ALL);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_SetMode(radio, SX1278_MODE_TX);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    start_ms = HAL_GetTick();
    while ((HAL_GetTick() - start_ms) < ((timeout_ms == 0U) ? SX1278_TX_DONE_TIMEOUT_MS : timeout_ms))
    {
        status = SX1278_GetIrqFlags(radio, &irq_flags);
        if (status != SX1278_STATUS_OK)
        {
            return status;
        }

        if ((irq_flags & SX1278_IRQ_TX_DONE) != 0U)
        {
            (void)SX1278_ClearIrqFlags(radio, SX1278_IRQ_TX_DONE);
            radio->state.payload_length = length;
            return SX1278_StartReceiveContinuous(radio);
        }
    }

    (void)SX1278_StartReceiveContinuous(radio);
    return SX1278_STATUS_TIMEOUT;
}

SX1278_Status_t SX1278_Receive(SX1278_Handle_t *radio, uint8_t *buffer, uint8_t *length)
{
    uint8_t irq_flags = 0U;
    uint8_t current_addr = 0U;
    uint8_t rx_length = 0U;
    uint8_t snr_raw = 0U;
    uint8_t rssi_raw = 0U;
    SX1278_Status_t status = SX1278_Validate(radio);

    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    if ((buffer == NULL) || (length == NULL))
    {
        return SX1278_STATUS_INVALID_PARAM;
    }

    status = SX1278_GetIrqFlags(radio, &irq_flags);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    if ((irq_flags & SX1278_IRQ_RX_DONE) == 0U)
    {
        return SX1278_STATUS_NO_PACKET;
    }

    if ((irq_flags & SX1278_IRQ_PAYLOAD_CRC_ERROR) != 0U)
    {
        (void)SX1278_ClearIrqFlags(radio, irq_flags);
        return SX1278_STATUS_SPI_ERROR;
    }

    status = SX1278_ReadRegisterRaw(radio, SX1278_REG_RX_NB_BYTES, &rx_length);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    if (*length < rx_length)
    {
        *length = rx_length;
        return SX1278_STATUS_BUFFER_TOO_SMALL;
    }

    status = SX1278_ReadRegisterRaw(radio, SX1278_REG_FIFO_RX_CURRENT_ADDR, &current_addr);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_WriteRegisterRaw(radio, SX1278_REG_FIFO_ADDR_PTR, current_addr);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    status = SX1278_ReadBurst(radio, SX1278_REG_FIFO, buffer, rx_length);
    if (status != SX1278_STATUS_OK)
    {
        return status;
    }

    (void)SX1278_ReadRegisterRaw(radio, SX1278_REG_PKT_SNR_VALUE, &snr_raw);
    (void)SX1278_ReadRegisterRaw(radio, SX1278_REG_PKT_RSSI_VALUE, &rssi_raw);

    radio->state.last_snr_db = (int8_t)((int8_t)snr_raw / 4);
    radio->state.last_rssi_dbm = (int16_t)rssi_raw - 157;
    radio->state.payload_length = rx_length;
    *length = rx_length;

    (void)SX1278_ClearIrqFlags(radio, irq_flags);
    return SX1278_STATUS_OK;
}

SX1278_Status_t SX1278_ReadVersion(SX1278_Handle_t *radio, uint8_t *version)
{
    if (radio == NULL)
    {
        return SX1278_STATUS_INVALID_PARAM;
    }

    if (radio->spi.hspi == NULL)
    {
        return SX1278_STATUS_NOT_INITIALIZED;
    }

    return SX1278_ReadRegisterRaw(radio, SX1278_REG_VERSION, version);
}

const char *SX1278_StatusToString(SX1278_Status_t status)
{
    switch (status)
    {
        case SX1278_STATUS_OK:
            return "OK";
        case SX1278_STATUS_IDLE:
            return "IDLE";
        case SX1278_STATUS_INVALID_PARAM:
            return "INVALID_PARAM";
        case SX1278_STATUS_NOT_INITIALIZED:
            return "NOT_INITIALIZED";
        case SX1278_STATUS_SPI_ERROR:
            return "SPI_ERROR";
        case SX1278_STATUS_TIMEOUT:
            return "TIMEOUT";
        case SX1278_STATUS_VERSION_MISMATCH:
            return "VERSION_MISMATCH";
        case SX1278_STATUS_NO_PACKET:
            return "NO_PACKET";
        case SX1278_STATUS_BUFFER_TOO_SMALL:
            return "BUFFER_TOO_SMALL";
        default:
            return "ERROR";
    }
}
