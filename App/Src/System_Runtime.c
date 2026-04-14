/*
 * System_Runtime.c
 *
 *  Created on: 14-Apr-2026
 *      Author: Codex
 */

#include "System_Runtime.h"
#include "ADC_Monitor.h"
#include "Logger.h"
#include "Runtime_State.h"
#include "UART_Driver.h"

static uint32_t SystemRuntime_PackIpmsEventValue(const IPMS_Event_t *event)
{
    if (event == NULL)
    {
        return 0U;
    }

    switch (event->type)
    {
        case IPMS_EVENT_STATE_TRANSITION:
            return ((uint32_t)event->old_state << 24)
                | ((uint32_t)event->new_state << 16)
                | ((uint32_t)event->reason << 8)
                | (uint32_t)event->simulation_mode;

        case IPMS_EVENT_WAKEUP:
            return ((uint32_t)event->wake_source << 24)
                | ((uint32_t)event->new_state << 16)
                | ((uint32_t)event->policy_mode << 8)
                | (uint32_t)event->reason;

        case IPMS_EVENT_SIMULATION_MODE_CHANGE:
            return ((uint32_t)event->simulation_mode << 24)
                | ((uint32_t)event->old_state << 16)
                | ((uint32_t)event->policy_mode << 8)
                | (uint32_t)event->reason;

        case IPMS_EVENT_POLICY_MODE_CHANGE:
        default:
            return ((uint32_t)event->policy_mode << 24)
                | ((uint32_t)event->old_state << 16)
                | ((uint32_t)event->simulation_mode << 8)
                | (uint32_t)event->reason;
    }
}

const char *SystemRuntime_TelemetryTimestampSourceToString(TelemetryTimestampSource_t source)
{
    switch (source)
    {
        case TELEM_TIMESTAMP_SOURCE_RTC:
            return "RTC";
        case TELEM_TIMESTAMP_SOURCE_UPTIME_FALLBACK:
        default:
            return "UPTIME_FALLBACK";
    }
}

void SystemRuntime_ReportIpmsEvents(void)
{
    IPMS_Event_t event;
    Telemetry_Status_t telem_status;

    while (IPMS_PopEvent(&event))
    {
        switch (event.type)
        {
            case IPMS_EVENT_STATE_TRANSITION:
                if ((event.new_state == IPMS_POWER_STATE_LOW_POWER_WARNING)
                    || (event.new_state == IPMS_POWER_STATE_SLEEP_CANDIDATE)
                    || (event.new_state == IPMS_POWER_STATE_STOP_CANDIDATE))
                {
                    Logger_Warn("IPMS state: %s -> %s reason=%s measured=%.3fV effective=%.3fV",
                                IPMS_PowerStateToString(event.old_state),
                                IPMS_PowerStateToString(event.new_state),
                                IPMS_ReasonToString(event.reason),
                                event.measured_battery_voltage,
                                event.effective_battery_voltage);
                }
                else
                {
                    Logger_Info("IPMS state: %s -> %s reason=%s measured=%.3fV effective=%.3fV",
                                IPMS_PowerStateToString(event.old_state),
                                IPMS_PowerStateToString(event.new_state),
                                IPMS_ReasonToString(event.reason),
                                event.measured_battery_voltage,
                                event.effective_battery_voltage);
                }

                telem_status = Telemetry_SendEventEx(TELEM_EVENT_POWER_STATE, SystemRuntime_PackIpmsEventValue(&event));
                Logger_Info("IPMS telemetry event: type=POWER_STATE result=%s", Telemetry_StatusToString(telem_status));
                break;

            case IPMS_EVENT_WAKEUP:
                Logger_Info("IPMS wakeup: source=%s next_state=%s reason=%s",
                            IPMS_WakeSourceToString(event.wake_source),
                            IPMS_PowerStateToString(event.new_state),
                            IPMS_ReasonToString(event.reason));
                telem_status = Telemetry_SendEventEx(TELEM_EVENT_POWER_WAKEUP, SystemRuntime_PackIpmsEventValue(&event));
                Logger_Info("IPMS telemetry event: type=POWER_WAKEUP result=%s", Telemetry_StatusToString(telem_status));
                break;

            case IPMS_EVENT_SIMULATION_MODE_CHANGE:
            case IPMS_EVENT_POLICY_MODE_CHANGE:
            default:
                Logger_Info("IPMS mode event: type=%s sim=%s policy=%s reason=%s",
                            IPMS_EventTypeToString(event.type),
                            IPMS_SimulationModeToString(event.simulation_mode),
                            IPMS_PolicyModeToString(event.policy_mode),
                            IPMS_ReasonToString(event.reason));
                telem_status = Telemetry_SendEventEx(TELEM_EVENT_POWER_MODE, SystemRuntime_PackIpmsEventValue(&event));
                Logger_Info("IPMS telemetry event: type=POWER_MODE result=%s", Telemetry_StatusToString(telem_status));
                break;
        }
    }
}

void SystemRuntime_PrepareForLowPower(SystemRuntimeContext_t *context)
{
    if (context == NULL)
    {
        return;
    }

    (void)ADC_Monitor_Stop();
    if (context->radio != NULL)
    {
        (void)SX1278_SetMode(context->radio, SX1278_MODE_SLEEP);
    }
    RuntimeState_InvalidateLatestAdcSample();
}

void SystemRuntime_RestoreAfterSleep(SystemRuntimeContext_t *context)
{
    if (context == NULL)
    {
        return;
    }

    if (context->hadc != NULL)
    {
        (void)ADC_Monitor_Init(context->hadc);
        (void)ADC_Monitor_Start();
    }

    if (context->radio != NULL)
    {
        (void)SX1278_StartReceiveContinuous(context->radio);
    }
}

void SystemRuntime_RestoreAfterStop(SystemRuntimeContext_t *context, const SystemRuntimeHooks_t *hooks)
{
    UART_Driver_Status_t uart_status;
    UART_Driver_Status_t telem_uart_status;
    ADC_Monitor_Status_t adc_init_status;
    ADC_Monitor_Status_t adc_start_status;
    I2C_Status_t i2c_status;
    OLED_Status_t oled_status = OLED_STATUS_INVALID_PARAM;
    SX1278_Status_t radio_status = SX1278_STATUS_INVALID_PARAM;

    if ((context == NULL) || (hooks == NULL))
    {
        return;
    }

    if (hooks->SystemClock_Config != NULL)
    {
        hooks->SystemClock_Config();
    }
    HAL_ResumeTick();

    if (hooks->MX_DMA_Init != NULL)
    {
        hooks->MX_DMA_Init();
    }
    if (hooks->MX_USART1_UART_Init != NULL)
    {
        hooks->MX_USART1_UART_Init();
    }
    if (hooks->MX_USART2_UART_Init != NULL)
    {
        hooks->MX_USART2_UART_Init();
    }
    if (hooks->MX_I2C1_Init != NULL)
    {
        hooks->MX_I2C1_Init();
    }
    if (hooks->MX_SPI1_Init != NULL)
    {
        hooks->MX_SPI1_Init();
    }
    if (hooks->MX_ADC1_Init != NULL)
    {
        hooks->MX_ADC1_Init();
    }
    if (hooks->MX_CRC_Init != NULL)
    {
        hooks->MX_CRC_Init();
    }

    uart_status = UART_Driver_Init(context->console_uart);
    telem_uart_status = UART_Driver_InitChannel(UART_DRIVER_CHANNEL_TELEMETRY, context->telemetry_uart);

    adc_init_status = ADC_MONITOR_ERROR;
    adc_start_status = ADC_MONITOR_ERROR;
    if (context->hadc != NULL)
    {
        adc_init_status = ADC_Monitor_Init(context->hadc);
        adc_start_status = ADC_Monitor_Start();
    }

    i2c_status = I2C_ERROR;
    if ((context->i2c_bus != NULL) && (context->hi2c != NULL))
    {
        i2c_status = I2C_Bus_Init(context->i2c_bus, context->hi2c);
        if ((i2c_status == I2C_OK) && (context->oled != NULL))
        {
            oled_status = OLED_Init(context->oled, context->i2c_bus, OLED_I2C_ADDR_0x3C);
        }
    }

    if ((context->radio != NULL) && (context->hspi != NULL))
    {
        radio_status = SX1278_Init(context->radio,
                                   context->hspi,
                                   context->radio->spi.cs_port,
                                   context->radio->spi.cs_pin,
                                   context->radio->reset_port,
                                   context->radio->reset_pin,
                                   context->radio->has_reset_pin);
    }

    RuntimeState_InvalidateLatestAdcSample();

    Logger_Info("IPMS stop restore: UART=%s TELEM_UART=%s ADC_INIT=%s ADC_START=%s I2C=%s OLED=%d RADIO=%s",
                UART_Driver_StatusToString(uart_status),
                UART_Driver_StatusToString(telem_uart_status),
                ADC_Monitor_StatusToString(adc_init_status),
                ADC_Monitor_StatusToString(adc_start_status),
                I2C_Bus_StatusToString(i2c_status),
                oled_status,
                SX1278_StatusToString(radio_status));
}

void SystemRuntime_ExecuteIpmsAction(SystemRuntimeContext_t *context,
                                     const SystemRuntimeHooks_t *hooks,
                                     const IPMS_ActionRequest_t *request)
{
    IPMS_Status_t rtc_status;

    if ((context == NULL) || (request == NULL) || (request->type == IPMS_ACTION_NONE))
    {
        return;
    }

    Logger_Warn("IPMS action: %s duration=%lu ms",
                IPMS_ActionTypeToString(request->type),
                (unsigned long)request->duration_ms);

    rtc_status = IPMS_ArmRtcWakeup(context->hrtc, request->duration_ms);
    if (rtc_status != IPMS_STATUS_OK)
    {
        Logger_Warn("IPMS wakeup arm failed: %s", IPMS_StatusToString(rtc_status));
        return;
    }

    SystemRuntime_PrepareForLowPower(context);
    if (context->hiwdg != NULL)
    {
        HAL_IWDG_Refresh(context->hiwdg);
    }
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WU);
    HAL_SuspendTick();

    if (request->type == IPMS_ACTION_ENTER_SLEEP)
    {
        HAL_PWR_EnterSLEEPMode(PWR_LOWPOWERREGULATOR_ON, PWR_SLEEPENTRY_WFI);
        HAL_ResumeTick();
        SystemRuntime_RestoreAfterSleep(context);
    }
    else
    {
        HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
        SystemRuntime_RestoreAfterStop(context, hooks);
    }

    IPMS_DisarmRtcWakeup(context->hrtc);
    IPMS_RecordWakeup(IPMS_WAKE_SOURCE_UNKNOWN, HAL_GetTick());
}
