/*
 * I2C_Bus.c
 *
 *  Created on: 12-Mar-2026
 *      Author: KESAV
 */


#include "I2C_Bus.h"
#include "Logger.h"

static const uint32_t I2C_TIMEOUT_VAL = 100;

static I2C_Status_t I2C_Bus_ValidateHandle(const I2C_Bus_Handle_t *bus)
{
	if((bus == NULL) || (bus->hi2c == NULL)){
		return I2C_ERROR;
	}

	return I2C_OK;
}

static I2C_Status_t I2C_Bus_ConvertHalStatus(HAL_StatusTypeDef status)
{
	switch(status){
	case HAL_OK:
		return I2C_OK;
	case HAL_BUSY:
		return I2C_BUSY;
	case HAL_TIMEOUT:
		return I2C_TIMEOUT;
	case HAL_ERROR:
	default:
		return I2C_ERROR;
	}
}

I2C_Status_t I2C_Bus_Init(I2C_Bus_Handle_t *bus, I2C_HandleTypeDef *hi2c){
	if((bus == NULL) || (hi2c == NULL)){
		return I2C_ERROR;
	}

	bus->hi2c = hi2c;
	bus->timeout_ms = I2C_TIMEOUT_VAL;

	return I2C_OK;
}

void I2C_Bus_Scan(I2C_Bus_Handle_t *bus){
	if(I2C_Bus_ValidateHandle(bus) != I2C_OK){
		Logger_Warn("I2C bus scan skipped: bus handle not initialized");
		return;
	}

	Logger_Info("Starting I2C Bus Scan: ");
	for(uint16_t i = 0; i < 128; i++){
		if(HAL_I2C_IsDeviceReady(bus->hi2c, (uint16_t)(i << 1), 3U, 5U) == HAL_OK){
			Logger_Info("Device found at Address: 0x%02X", i);
		}
	}
	Logger_Info("Scan Complete");
}

I2C_Status_t I2C_Bus_IsDeviceReady(I2C_Bus_Handle_t *bus, uint16_t devAddr, uint32_t trials){
	if(I2C_Bus_ValidateHandle(bus) != I2C_OK){
		return I2C_ERROR;
	}

	HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(bus->hi2c,
													 (uint16_t)(devAddr << 1),
													 trials,
													 bus->timeout_ms);

	return I2C_Bus_ConvertHalStatus(status);
}

I2C_Status_t I2C_Bus_Write(I2C_Bus_Handle_t *bus, uint16_t devAddr, const uint8_t *pData, uint16_t len){
	if((I2C_Bus_ValidateHandle(bus) != I2C_OK) || (pData == NULL) || (len == 0U)){
		return I2C_ERROR;
	}

	HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(bus->hi2c,
														(uint16_t)(devAddr << 1),
														(uint8_t *)pData,
														len,
														bus->timeout_ms);

	return I2C_Bus_ConvertHalStatus(status);
}

I2C_Status_t I2C_Bus_ReadRegister(I2C_Bus_Handle_t *bus, uint16_t devAddr, uint16_t regAddr, uint8_t *pData, uint16_t len){
	if((I2C_Bus_ValidateHandle(bus) != I2C_OK) || (pData == NULL) || (len == 0U)){
		return I2C_ERROR;
	}

	HAL_StatusTypeDef status = HAL_I2C_Mem_Read(bus->hi2c,
												(uint16_t)(devAddr << 1),
												regAddr,
												I2C_MEMADD_SIZE_8BIT,
												pData,
												len,
												bus->timeout_ms);

	return I2C_Bus_ConvertHalStatus(status);
}

I2C_Status_t I2C_Bus_WriteRegister(I2C_Bus_Handle_t *bus, uint16_t devAddr, uint16_t regAddr, const uint8_t *pData, uint16_t len){
	if((I2C_Bus_ValidateHandle(bus) != I2C_OK) || (pData == NULL) || (len == 0U)){
		return I2C_ERROR;
	}

	HAL_StatusTypeDef status = HAL_I2C_Mem_Write(bus->hi2c,
												 (uint16_t)(devAddr << 1),
												 regAddr,
												 I2C_MEMADD_SIZE_8BIT,
												 (uint8_t *)pData,
												 len,
												 bus->timeout_ms);

	return I2C_Bus_ConvertHalStatus(status);
}

const char *I2C_Bus_StatusToString(I2C_Status_t status)
{
	switch(status){
	case I2C_OK:
		return "OK";
	case I2C_BUSY:
		return "BUSY";
	case I2C_TIMEOUT:
		return "TIMEOUT";
	case I2C_ERROR:
	default:
		return "ERROR";
	}
}


