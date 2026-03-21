/*
 * I2C_Bus.h
 *
 *  Created on: 12-Mar-2026
 *      Author: KESAV
 */

#ifndef INC_I2C_BUS_H_
#define INC_I2C_BUS_H_

#include "stm32f4xx_hal.h"

typedef enum {
	I2C_OK = 0,
	I2C_ERROR,
	I2C_BUSY,
	I2C_TIMEOUT
} I2C_Status_t;


void I2C_Bus_Init(I2C_HandleTypeDef *hi2c);

void I2C_Bus_Scan(void);

I2C_Status_t I2C_Bus_WriteRegister (uint16_t devAddr, uint16_t regAddr, uint8_t *pData, uint16_t len);

I2C_Status_t I2C_Bus_ReadRegister  (uint16_t devAddr, uint16_t regAddr, uint8_t *pData, uint16_t len);

#endif /* INC_I2C_BUS_H_ */
