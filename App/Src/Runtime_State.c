/*
 * Runtime_State.c
 *
 *  Created on: 14-Apr-2026
 *      Author: Codex
 */

#include "Runtime_State.h"
#include <string.h>

typedef struct {
    ADC_HealthData_t latest_adc_sample;
    bool latest_adc_valid;
    uint32_t telem_tx_complete_count;
    uint32_t telem_error_count;
} RuntimeState_Data_t;

static RuntimeState_Data_t g_runtime_state;

static uint32_t RuntimeState_EnterCritical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void RuntimeState_ExitCritical(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

void RuntimeState_Init(void)
{
    uint32_t primask = RuntimeState_EnterCritical();
    memset(&g_runtime_state, 0, sizeof(g_runtime_state));
    RuntimeState_ExitCritical(primask);
}

void RuntimeState_RecordTelemetryTxComplete(void)
{
    uint32_t primask = RuntimeState_EnterCritical();
    g_runtime_state.telem_tx_complete_count++;
    RuntimeState_ExitCritical(primask);
}

void RuntimeState_RecordTelemetryError(void)
{
    uint32_t primask = RuntimeState_EnterCritical();
    g_runtime_state.telem_error_count++;
    RuntimeState_ExitCritical(primask);
}

void RuntimeState_GetTelemetryCounters(RuntimeTelemetryCounters_t *counters)
{
    uint32_t primask;

    if (counters == NULL)
    {
        return;
    }

    primask = RuntimeState_EnterCritical();
    counters->tx_complete_count = g_runtime_state.telem_tx_complete_count;
    counters->error_count = g_runtime_state.telem_error_count;
    RuntimeState_ExitCritical(primask);
}

void RuntimeState_SetLatestAdcSample(const ADC_HealthData_t *sample)
{
    uint32_t primask;

    if (sample == NULL)
    {
        return;
    }

    primask = RuntimeState_EnterCritical();
    g_runtime_state.latest_adc_sample = *sample;
    g_runtime_state.latest_adc_valid = true;
    RuntimeState_ExitCritical(primask);
}

bool RuntimeState_GetLatestAdcSample(ADC_HealthData_t *sample)
{
    uint32_t primask;
    bool is_valid;

    if (sample == NULL)
    {
        return false;
    }

    primask = RuntimeState_EnterCritical();
    is_valid = g_runtime_state.latest_adc_valid;
    if (is_valid)
    {
        *sample = g_runtime_state.latest_adc_sample;
    }
    RuntimeState_ExitCritical(primask);

    return is_valid;
}

void RuntimeState_InvalidateLatestAdcSample(void)
{
    uint32_t primask = RuntimeState_EnterCritical();
    g_runtime_state.latest_adc_valid = false;
    RuntimeState_ExitCritical(primask);
}
