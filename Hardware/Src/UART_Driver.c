/*
 * UART_Driver.c
 *
 *  Created on: 20-Mar-2026
 *      Author: KESAV
 */
#include "UART_Driver.h"

#define UART_RX_BUFFER_SIZE 128

static UART_HandleTypeDef *pUart = NULL;
static RingBuffer_t rx_buffer;

static uint8_t rx_byte;

// API FUNCTIONS

UART_Driver_Status_t UART_Driver_Init(UART_HandleTypeDef *huart){
	if(huart == NULL) return UART_DRIVER_ERROR;

	RingBuffer_Init(&rx_buffer);
	pUart = huart;

	if(HAL_UART_Receive_IT(pUart, &rx_byte, 1) != HAL_OK) return UART_DRIVER_ERROR;

	return UART_DRIVER_OK;
}

// RX
bool UART_ReadByte(uint8_t *data){
	return RingBuffer_Pop(&rx_buffer, data);
}

uint16_t UART_Available(void){
	return RingBuffer_Available(&rx_buffer);
}

// TX
UART_Driver_Status_t UART_Write(uint8_t *data, uint16_t len){

	if(pUart == NULL || data == NULL) return UART_DRIVER_ERROR;
	if(HAL_UART_Transmit(pUart, data, len, 100) != HAL_OK) return UART_DRIVER_ERROR;

	return UART_DRIVER_OK;
}
UART_Driver_Status_t UART_WriteString(const char *str){
	if(str == NULL) return UART_DRIVER_ERROR;

	return UART_Write((uint8_t *)str, (uint16_t)strlen(str));
}

// IRQ Handler
void UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != pUart)
        return;

    /* Store byte ONLY */
    RingBuffer_Push(&rx_buffer, rx_byte);

    /* Restart RX */
    if (HAL_UART_Receive_IT(pUart, &rx_byte, 1) != HAL_OK)
    {
        HAL_UART_AbortReceive(pUart);
        HAL_UART_Receive_IT(pUart, &rx_byte, 1);
    }
}









