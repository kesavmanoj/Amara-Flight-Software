/**
 * @file UART_Driver.h
 * @brief Non-blocking UART driver with ISR RX and DMA-backed TX channels.
 */

#ifndef INC_UART_DRIVER_H_
#define INC_UART_DRIVER_H_

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>
#include "ring_buffer.h"
#include <string.h>

typedef enum {
	UART_DRIVER_OK = 0,
	UART_DRIVER_INVALID_PARAM,
	UART_DRIVER_NOT_INITIALIZED,
	UART_DRIVER_BUFFER_FULL,
	UART_DRIVER_BUSY,
	UART_DRIVER_ERROR
} UART_Driver_Status_t;

typedef enum {
	UART_DRIVER_CHANNEL_CONSOLE = 0,
	UART_DRIVER_CHANNEL_TELEMETRY,
	UART_DRIVER_CHANNEL_COUNT
} UART_Driver_Channel_t;

/**
 * @brief Initialize the console UART channel.
 *
 * This is a convenience wrapper around @ref UART_Driver_InitChannel for the console
 * channel used by the command parser and logger.
 */
UART_Driver_Status_t UART_Driver_Init(UART_HandleTypeDef *huart);
/**
 * @brief Initialize one named UART driver channel.
 *
 * The console channel also initializes the RX ring buffer and starts interrupt-driven
 * byte reception.
 */
UART_Driver_Status_t UART_Driver_InitChannel(UART_Driver_Channel_t channel, UART_HandleTypeDef *huart);

// RX
/** @brief Pop one received console byte from the RX ring buffer. */
bool UART_ReadByte(uint8_t *data);
/** @brief Get the number of queued console RX bytes waiting to be parsed. */
uint16_t UART_Available(void);
/** @brief Get the cumulative console RX overflow count. */
uint32_t UART_GetRxOverflowCount(void);

// TX
/** @brief Queue one console-channel byte buffer for DMA-backed transmission. */
UART_Driver_Status_t UART_Write(uint8_t *data, uint16_t len);
/** @brief Queue one null-terminated console string for transmission. */
UART_Driver_Status_t UART_WriteString(const char *str);
/** @brief Queue one buffer for transmission on the selected UART driver channel. */
UART_Driver_Status_t UART_WriteChannel(UART_Driver_Channel_t channel, uint8_t *data, uint16_t len);
/** @brief Return true when the selected TX channel has no queued or active DMA transfer. */
bool UART_IsChannelIdle(UART_Driver_Channel_t channel);
/** @brief Convenience wrapper for checking whether the console TX channel is idle. */
bool UART_IsIdle(void);
/** @brief Convert UART driver status codes into printable strings. */
const char *UART_Driver_StatusToString(UART_Driver_Status_t status);

// IRQ Handlers
/** @brief HAL RX-complete callback hook for console byte ingestion. */
void UART_RxCpltCallback(UART_HandleTypeDef *huart);
/** @brief HAL TX-complete callback hook that advances the next queued DMA chunk. */
void UART_TxCpltCallback(UART_HandleTypeDef *huart);
/** @brief HAL UART error callback hook that releases and retries queued TX work. */
void UART_ErrorCallback(UART_HandleTypeDef *huart);

#endif /* INC_UART_DRIVER_H_ */
