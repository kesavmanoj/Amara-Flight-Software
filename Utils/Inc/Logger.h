/*
 * Logger.h
 *
 *  Created on: 12-Mar-2026
 *      Author: KESAV
 */

#ifndef INC_LOGGER_H_
#define INC_LOGGER_H_

#include <stdio.h>
#include <stdarg.h>
#include "stm32f4xx_hal.h"

typedef enum {
	LOG_LEVEL_INFO,
	LOG_LEVEL_WARN,
	LOG_LEVEL_ERROR,
	LOG_LEVEL_NONE
} LogLevel_t;



void Logger_Info(const char *fmt, ...);
void Logger_Warn(const char *fmt, ...);
void Logger_Error(const char *fmt, ...);



#endif /* INC_LOGGER_H_ */
