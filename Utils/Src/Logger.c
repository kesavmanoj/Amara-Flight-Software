/*
 * Logger.c
 *
 *  Created on: 12-Mar-2026
 *      Author: KESAV
 */


#include "Logger.h"
#include "rtc.h"
#include "cmsis_os2.h"
#include "UART_Driver.h"

#define LOG_BUFFER_SIZE 256

static volatile uint32_t g_logger_dropped_count = 0U;
static volatile uint32_t g_logger_attempted_count = 0U;
static volatile uint32_t g_logger_persist_dropped_count = 0U;
static volatile UART_Driver_Status_t g_logger_last_uart_status = UART_DRIVER_NOT_INITIALIZED;
static volatile StorageService_Status_t g_logger_last_storage_status = STORAGE_SERVICE_STATUS_NOT_READY;
extern osMutexId_t ConsoleMutexHandle;

static bool Logger_LockConsole(void)
{
	if((osKernelGetState() == osKernelRunning) && (ConsoleMutexHandle != NULL)){
		return (osMutexAcquire(ConsoleMutexHandle, osWaitForever) == osOK);
	}

	return false;
}

static void Logger_UnlockConsole(bool locked)
{
	if(locked){
		(void)osMutexRelease(ConsoleMutexHandle);
	}
}

// Helper Functions
void Get_Timestamp(char* buf, size_t buf_size){
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
    UART_Driver_Status_t uart_status;
    StorageService_Status_t storage_status = STORAGE_SERVICE_STATUS_NOT_READY;
    bool console_locked;

    Get_Timestamp(timestamp, sizeof(timestamp));
    console_locked = Logger_LockConsole();

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
    g_logger_attempted_count++;
    uart_status = UART_Write((uint8_t*)log_buffer, (uint16_t)len);
    g_logger_last_uart_status = uart_status;

    if (uart_status != UART_DRIVER_OK)
    {
        g_logger_dropped_count++;
    }

    if ((osKernelGetState() == osKernelRunning) && (__get_IPSR() == 0U))
    {
        storage_status = StorageService_EnqueueLogLine(log_buffer, (uint16_t)len);
        g_logger_last_storage_status = storage_status;

        if ((storage_status != STORAGE_SERVICE_STATUS_OK) &&
            (storage_status != STORAGE_SERVICE_STATUS_NOT_READY))
        {
            g_logger_persist_dropped_count++;
        }
    }

    Logger_UnlockConsole(console_locked);
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

uint32_t Logger_GetDroppedCount(void)
{
    return g_logger_dropped_count;
}

void Logger_GetStats(Logger_Stats_t *stats)
{
    if (stats == NULL)
    {
        return;
    }

    stats->messages_attempted = g_logger_attempted_count;
    stats->messages_dropped = g_logger_dropped_count;
    stats->messages_persist_dropped = g_logger_persist_dropped_count;
    stats->last_uart_status = g_logger_last_uart_status;
    stats->last_storage_status = g_logger_last_storage_status;
}
