/*
 * SPI_Bus.c
 *
 *  Created on: 20-Mar-2026
 *      Author: KESAV
 */
#include "SPI_Bus.h"

#define SPI_TIMEOUT_MS 100

static SPI_Driver_Status_t SPI_ConvertHalStatus(HAL_StatusTypeDef status)
{
	switch(status){
	case HAL_OK:
		return SPI_DRIVER_OK;
	case HAL_BUSY:
		return SPI_DRIVER_BUSY;
	case HAL_TIMEOUT:
		return SPI_DRIVER_TIMEOUT;
	case HAL_ERROR:
	default:
		return SPI_DRIVER_ERROR;
	}
}

static inline void SPI_Delay(void)
{
    /* Small delay for CS setup time if needed */
    __NOP(); __NOP(); __NOP();
}

// SPI Init
SPI_Driver_Status_t SPI_Device_Init(SPI_Device_t *dev, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin){

	if((dev == NULL) || (hspi == NULL) || (cs_port == NULL)) return SPI_DRIVER_INVALID_PARAM;

	dev -> hspi 	= hspi;
	dev -> cs_port 	= cs_port;
	dev -> cs_pin 	= cs_pin;

	HAL_GPIO_WritePin(cs_port, cs_pin, GPIO_PIN_SET);

	return SPI_DRIVER_OK;
}

// CS Control
void SPI_CS_High(SPI_Device_t *dev){
	HAL_GPIO_WritePin(dev ->cs_port, dev -> cs_pin, GPIO_PIN_SET);
	SPI_Delay();
}

void SPI_CS_Low(SPI_Device_t *dev){
	HAL_GPIO_WritePin(dev ->cs_port, dev -> cs_pin, GPIO_PIN_RESET);
	SPI_Delay();
}


// SPI Transfers
SPI_Driver_Status_t SPI_Transmit(SPI_Device_t *dev, uint8_t *pData, uint16_t len){

	if((dev == NULL) || (pData == NULL) || (len == 0U)) return SPI_DRIVER_INVALID_PARAM;
	if(dev->hspi == NULL) return SPI_DRIVER_NOT_INITIALIZED;
	return SPI_ConvertHalStatus(HAL_SPI_Transmit(dev -> hspi, pData, len, SPI_TIMEOUT_MS));

}

SPI_Driver_Status_t SPI_Receive(SPI_Device_t *dev, uint8_t *pData, uint16_t len){

	if((dev == NULL) || (pData == NULL) || (len == 0U)) return SPI_DRIVER_INVALID_PARAM;
	if(dev->hspi == NULL) return SPI_DRIVER_NOT_INITIALIZED;
	return SPI_ConvertHalStatus(HAL_SPI_Receive(dev -> hspi, pData, len, SPI_TIMEOUT_MS));

}

SPI_Driver_Status_t SPI_TransmitReceive(SPI_Device_t *dev, uint8_t *tx, uint8_t *rx, uint16_t len)
{
    if ((dev == NULL) || (tx == NULL) || (rx == NULL) || (len == 0U))
        return SPI_DRIVER_INVALID_PARAM;
    if (dev->hspi == NULL)
        return SPI_DRIVER_NOT_INITIALIZED;

    return SPI_ConvertHalStatus(HAL_SPI_TransmitReceive(dev->hspi, tx, rx, len, SPI_TIMEOUT_MS));
}

uint8_t SPI_TransferByte(SPI_Device_t *dev, uint8_t data)
{
    uint8_t rx = 0;

    if (HAL_SPI_TransmitReceive(dev->hspi, &data, &rx, 1, SPI_TIMEOUT_MS) != HAL_OK)
        return 0;

    return rx;
}

const char *SPI_Driver_StatusToString(SPI_Driver_Status_t status)
{
	switch(status){
	case SPI_DRIVER_OK:
		return "OK";
	case SPI_DRIVER_INVALID_PARAM:
		return "INVALID_PARAM";
	case SPI_DRIVER_NOT_INITIALIZED:
		return "NOT_INITIALIZED";
	case SPI_DRIVER_BUSY:
		return "BUSY";
	case SPI_DRIVER_TIMEOUT:
		return "TIMEOUT";
	case SPI_DRIVER_ERROR:
	default:
		return "ERROR";
	}
}

