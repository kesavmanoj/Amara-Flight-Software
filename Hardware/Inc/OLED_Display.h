/*
 * OledDisplay.h
 *
 *  Created on: 11-Mar-2026
 *      Author: KESAV
 */

#ifndef INC_OLED_DISPLAY_H_
#define INC_OLED_DISPLAY_H_

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define OLED_I2C_ADDR	(0x3C << 1)

typedef struct {
	I2C_HandleTypeDef *hi2c;
} 	OLED_HandleTypeDef;

void OLED_Init(OLED_HandleTypeDef *oled);
void OLED_Clear(OLED_HandleTypeDef *oled);
void OLED_WriteString(OLED_HandleTypeDef *oled, const char *str);
void OLED_DisplayHI(OLED_HandleTypeDef *oled);


#endif /* INC_OLED_DISPLAY_H_ */
