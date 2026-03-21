/*
 * Logger.c
 *
 *  Created on: 12-Mar-2026
 *      Author: KESAV
 */


#include "Logger.h"
#include "rtc.h"
#include "UART_Driver.h"

#define LOG_BUFFER_SIZE 256

static char log_buffer[LOG_BUFFER_SIZE];

// Helper Functions
static void Get_Timestamp(char* buf){
	RTC_TimeTypeDef sTime;
	RTC_DateTypeDef sDate;

	HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN);
	HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);

	snprintf(buf, 15, "[%02d:%02d:%02d]", sTime.Hours, sTime.Minutes, sTime.Seconds);
}

static void Logger_Log(const char *prefix, const char *fmt, va_list args)
{
    char timestamp[15];

    Get_Timestamp(timestamp);

    int len = 0;

    /* Write timestamp + prefix */
    len = snprintf(log_buffer, LOG_BUFFER_SIZE, "%s %s", timestamp, prefix);

    if (len < 0 || len >= LOG_BUFFER_SIZE)
        return;

    /* Append formatted message */
    int ret = vsnprintf(log_buffer + len,
                        LOG_BUFFER_SIZE - len,
                        fmt,
                        args);

    if (ret < 0)
        return;

    len += ret;

    if (len >= LOG_BUFFER_SIZE)
        len = LOG_BUFFER_SIZE - 1;

    /* Append newline */
    ret = snprintf(log_buffer + len,
                   LOG_BUFFER_SIZE - len,
                   "\r\n");

    if (ret > 0)
        len += ret;

    if (len > LOG_BUFFER_SIZE)
        len = LOG_BUFFER_SIZE;

    /* Transmit using UART driver */
    UART_Write((uint8_t*)log_buffer, (uint16_t)len);
}

void Logger_Info(const char *fmt, ...){
	va_list args;
	va_start(args, fmt);
	Logger_Log("INFO ", fmt, args);
	va_end(args);
}

void Logger_Warn(const char *fmt, ...){
	va_list args;
	va_start(args, fmt);
	Logger_Log("WARN ", fmt, args);
	va_end(args);
}

void Logger_Error(const char *fmt, ...){
	va_list args;
	va_start(args, fmt);
	Logger_Log("ERROR ", fmt, args);
	va_end(args);
}



