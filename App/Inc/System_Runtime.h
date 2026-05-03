/*
 * System_Runtime.h
 *
 *  Created on: 14-Apr-2026
 *      Author: Codex
 */

#ifndef INC_SYSTEM_RUNTIME_H_
#define INC_SYSTEM_RUNTIME_H_

#include <stdbool.h>
#include "stm32f4xx_hal.h"
#include "Telemetry.h"
#include "I2C_Bus.h"
#include "OLED_Display.h"
#include "SX1278.h"
#include "IPMS.h"

typedef struct {
    RTC_HandleTypeDef *hrtc;
    IWDG_HandleTypeDef *hiwdg;
    ADC_HandleTypeDef *hadc;
    I2C_HandleTypeDef *hi2c;
    SPI_HandleTypeDef *hspi;
    UART_HandleTypeDef *console_uart;
    UART_HandleTypeDef *telemetry_uart;
    I2C_Bus_Handle_t *i2c_bus;
    OLED_HandleTypeDef *oled;
    SX1278_Handle_t *radio;
} SystemRuntimeContext_t;

typedef struct {
    void (*SystemClock_Config)(void);
    void (*MX_DMA_Init)(void);
    void (*MX_USART1_UART_Init)(void);
    void (*MX_USART2_UART_Init)(void);
    void (*MX_I2C1_Init)(void);
    void (*MX_SPI1_Init)(void);
    void (*MX_ADC1_Init)(void);
    void (*MX_CRC_Init)(void);
} SystemRuntimeHooks_t;

const char *SystemRuntime_TelemetryTimestampSourceToString(TelemetryTimestampSource_t source);
void SystemRuntime_ReportIpmsEvents(void);
void SystemRuntime_PrepareForLowPower(SystemRuntimeContext_t *context);
void SystemRuntime_RestoreAfterSleep(SystemRuntimeContext_t *context);
void SystemRuntime_RestoreAfterStop(SystemRuntimeContext_t *context, const SystemRuntimeHooks_t *hooks);
void SystemRuntime_ExecuteIpmsAction(SystemRuntimeContext_t *context,
                                     const SystemRuntimeHooks_t *hooks,
                                     const IPMS_ActionRequest_t *request);

#endif /* INC_SYSTEM_RUNTIME_H_ */
