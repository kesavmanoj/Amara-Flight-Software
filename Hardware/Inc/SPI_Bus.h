/*
 * SPI_Bus.h
 *
 *  Created on: 20-Mar-2026
 *      Author: KESAV
 */

#ifndef INC_SPI_BUS_H_
#define INC_SPI_BUS_H_

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef enum {
	SPI_DRIVER_OK = 0,
	SPI_DRIVER_ERROR ,
	SPI_DRIVER_BUSY
} SPI_Driver_Status_t;

typedef struct {
	SPI_HandleTypeDef 	*hspi	;
	GPIO_TypeDef 		*cs_port;
	uint16_t 			 cs_pin ;
} SPI_Device_t;

// Init
SPI_Driver_Status_t SPI_Device_Init(SPI_Device_t *dev, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin);

// Chip Select
void SPI_CS_High(SPI_Device_t *dev);
void SPI_CS_Low (SPI_Device_t *dev);

// Data Transfers
SPI_Driver_Status_t SPI_Transmit(SPI_Device_t *dev, uint8_t *pData, uint16_t len);
SPI_Driver_Status_t SPI_Recieve (SPI_Device_t *dev, uint8_t *pData, uint16_t len);
SPI_Driver_Status_t SPI_TransmitRecive(SPI_Device_t *dev, uint8_t *rx, uint8_t *tx, uint16_t len);

// Single Byte Helper
uint8_t SPI_TransferByte(SPI_Device_t *dev, uint8_t data);

#endif /* INC_SPI_BUS_H_ */
