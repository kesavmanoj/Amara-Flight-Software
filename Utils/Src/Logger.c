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

// Helper Functions
static void Get_Timestamp(char* buf, size_t buf_size){
	RTC_TimeTypeDef sTime;
	RTC_DateTypeDef sDate;

	if((HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) ||
	   (HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK)){
		snprintf(buf, buf_size, "[BOOT+%lums]", HAL_GetTick());
		return;
	}

	/* RTC is initialized to a default epoch on boot; fall back to uptime until it is set. */
	if((sDate.Year == 0U) &&
	   (sDate.Month == RTC_MONTH_JANUARY) &&
	   (sDate.Date == 1U) &&
	   (sTime.Hours == 0U) &&
	   (sTime.Minutes == 0U) &&
	   (sTime.Seconds == 0U)){
		snprintf(buf, buf_size, "[BOOT+%lums]", HAL_GetTick());
		return;
	}

	snprintf(buf, buf_size, "[%02u:%02u:%02u]", sTime.Hours, sTime.Minutes, sTime.Seconds);
}

static void Logger_Log(const char *prefix, const char *fmt, va_list args)
{
    char timestamp[24];
    char log_buffer[LOG_BUFFER_SIZE];

    Get_Timestamp(timestamp, sizeof(timestamp));

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



