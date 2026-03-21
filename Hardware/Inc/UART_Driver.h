/*
 * UART_Driver.h
 *
 *  Created on: 20-Mar-2026
 *      Author: KESAV
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
	UART_DRIVER_ERROR
} UART_Driver_Status_t;

UART_Driver_Status_t UART_Driver_Init(UART_HandleTypeDef *huart);

// RX
bool UART_ReadByte(uint8_t *data);
uint16_t UART_Available(void);

// TX
UART_Driver_Status_t UART_Write(uint8_t *data, uint16_t len);
UART_Driver_Status_t UART_WriteString(const char *str);

// IRQ Handler
void UART_RxCpltCallback(UART_HandleTypeDef *huart);

#endif /* INC_UART_DRIVER_H_ */
