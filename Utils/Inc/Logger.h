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
#include "UART_Driver.h"
#include "Storage_Service.h"

// typedef enum {
// 	LOG_LEVEL_INFO,
// 	LOG_LEVEL_WARN,
// 	LOG_LEVEL_ERROR,
// 	LOG_LEVEL_NONE
// } LogLevel_t;

typedef struct {
    uint32_t messages_attempted;
    uint32_t messages_dropped;
    uint32_t messages_persist_dropped;
    UART_Driver_Status_t last_uart_status;
    StorageService_Status_t last_storage_status;
} Logger_Stats_t;

void Get_Timestamp(char* buf, size_t buf_size);

void Logger_Info(const char *fmt, ...);
void Logger_Warn(const char *fmt, ...);
void Logger_Error(const char *fmt, ...);
uint32_t Logger_GetDroppedCount(void);
void Logger_GetStats(Logger_Stats_t *stats);



#endif /* INC_LOGGER_H_ */
