/*
 * Runtime_State.h
 *
 *  Created on: 14-Apr-2026
 *      Author: Codex
 */

#ifndef INC_RUNTIME_STATE_H_
#define INC_RUNTIME_STATE_H_

#include <stdbool.h>
#include <stdint.h>
#include "ADC_Monitor.h"

typedef struct {
    uint32_t tx_complete_count;
    uint32_t error_count;
} RuntimeTelemetryCounters_t;

typedef enum {
    RUNTIME_IPMS_CONTROL_NONE = 0,
    RUNTIME_IPMS_CONTROL_SET_SIMULATION_MODE,
    RUNTIME_IPMS_CONTROL_SET_POLICY_MODE
} RuntimeIpmsControlType_t;

typedef struct {
    RuntimeIpmsControlType_t type;
    uint32_t value;
    uint32_t timestamp_ms;
} RuntimeIpmsControlRequest_t;

void RuntimeState_Init(void);

void RuntimeState_RecordTelemetryTxComplete(void);
void RuntimeState_RecordTelemetryError(void);
void RuntimeState_GetTelemetryCounters(RuntimeTelemetryCounters_t *counters);

void RuntimeState_SetLatestAdcSample(const ADC_HealthData_t *sample);
bool RuntimeState_GetLatestAdcSample(ADC_HealthData_t *sample);
void RuntimeState_InvalidateLatestAdcSample(void);

bool RuntimeState_QueueIpmsControlRequest(RuntimeIpmsControlType_t type, uint32_t value, uint32_t timestamp_ms);
bool RuntimeState_PopIpmsControlRequest(RuntimeIpmsControlRequest_t *request);

#endif /* INC_RUNTIME_STATE_H_ */
