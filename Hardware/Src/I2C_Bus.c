/*
 * I2C_Bus.c
 *
 *  Created on: 12-Mar-2026
 *      Author: KESAV
 */


#include "I2C_Bus.h"
#include "Logger.h"

static I2C_HandleTypeDef *pI2cHandle = NULL;
static const uint32_t I2C_TIMEOUT_VAL = 100;

void I2C_Bus_Init(I2C_HandleTypeDef *hi2c){
	pI2cHandle = hi2c;
}

void I2C_Bus_Scan(void){
	Logger_Info("Starting I2C Bus Scan: ");
	for(uint16_t i = 0; i < 128; i++){
		if(HAL_I2C_IsDeviceReady(pI2cHandle, (i << 1), 3, 5) == HAL_OK){
			Logger_Info("Device found at Address: 0x%02X", i);
		}
	}
	Logger_Info("Scan Complete");
}

I2C_Status_t I2C_Bus_ReadRegister(uint16_t devAddr, uint16_t regAddr, uint8_t *pData, uint16_t len){
	HAL_StatusTypeDef status = HAL_I2C_Mem_Read(pI2cHandle, (devAddr << 1), regAddr, I2C_MEMADD_SIZE_8BIT, pData, len, I2C_TIMEOUT_VAL);
	if(status == HAL_OK){
		return I2C_OK;
	} else {
		return I2C_ERROR;
	}
}

I2C_Status_t I2C_Bus_WriteRegister(uint16_t devAddr, uint16_t regAddr, uint8_t *pData, uint16_t len){
	HAL_StatusTypeDef status = HAL_I2C_Mem_Write(pI2cHandle, (devAddr << 1), regAddr, I2C_MEMADD_SIZE_8BIT, pData, len, I2C_TIMEOUT_VAL);
		if(status == HAL_OK){
			return I2C_OK;
		} else {
			return I2C_ERROR;
		}
}


