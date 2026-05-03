/*
 * I2C_Bus.h
 *
 *  Created on: 12-Mar-2026
 *      Author: KESAV
 */

#ifndef INC_I2C_BUS_H_
#define INC_I2C_BUS_H_

#include "stm32f4xx_hal.h"
#include <stdint.h>

typedef enum {
	I2C_OK = 0,
	I2C_ERROR,
	I2C_BUSY,
	I2C_TIMEOUT
} I2C_Status_t;

typedef struct {
	I2C_HandleTypeDef *hi2c;
	uint32_t timeout_ms;
} I2C_Bus_Handle_t;

I2C_Status_t I2C_Bus_Init(I2C_Bus_Handle_t *bus, I2C_HandleTypeDef *hi2c);

void I2C_Bus_Scan(I2C_Bus_Handle_t *bus);

I2C_Status_t I2C_Bus_IsDeviceReady(I2C_Bus_Handle_t *bus, uint16_t devAddr, uint32_t trials);

I2C_Status_t I2C_Bus_Write(I2C_Bus_Handle_t *bus, uint16_t devAddr,
											 const uint8_t *pData, uint16_t len);

I2C_Status_t I2C_Bus_WriteRegister(I2C_Bus_Handle_t *bus, uint16_t devAddr, uint16_t regAddr, 
													const uint8_t *pData, uint16_t len);

I2C_Status_t I2C_Bus_ReadRegister (I2C_Bus_Handle_t *bus, uint16_t devAddr, uint16_t regAddr, 
													uint8_t *pData, uint16_t len);
const char *I2C_Bus_StatusToString(I2C_Status_t status);

#endif /* INC_I2C_BUS_H_ */
