/**
 * @file IPMS.h
 * @brief Public API for the Intelligent Power Management System.
 */

#ifndef INC_IPMS_H_
#define INC_IPMS_H_

#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    IPMS_STATUS_OK = 0,
    IPMS_STATUS_IDLE,
    IPMS_STATUS_INVALID_PARAM,
    IPMS_STATUS_NOT_INITIALIZED,
    IPMS_STATUS_HAL_ERROR,
    IPMS_STATUS_NO_ACTION
} IPMS_Status_t;

typedef enum {
    IPMS_POWER_STATE_NORMAL = 0,
    IPMS_POWER_STATE_LOW_POWER_WARNING,
    IPMS_POWER_STATE_SLEEP_CANDIDATE,
    IPMS_POWER_STATE_STOP_CANDIDATE,
    IPMS_POWER_STATE_RECOVERY
} IPMS_PowerState_t;

typedef enum {
    IPMS_SIMULATION_AUTO = 0,
    IPMS_SIMULATION_SUNLIGHT,
    IPMS_SIMULATION_ECLIPSE
} IPMS_SimulationMode_t;

typedef enum {
    IPMS_POLICY_MONITOR_ONLY = 0,
    IPMS_POLICY_ENABLE_SLEEP,
    IPMS_POLICY_ENABLE_SLEEP_AND_STOP
} IPMS_PolicyMode_t;

typedef enum {
    IPMS_ACTION_NONE = 0,
    IPMS_ACTION_ENTER_SLEEP,
    IPMS_ACTION_ENTER_STOP
} IPMS_ActionType_t;

typedef enum {
    IPMS_WAKE_SOURCE_NONE = 0,
    IPMS_WAKE_SOURCE_RTC,
    IPMS_WAKE_SOURCE_BUTTON,
    IPMS_WAKE_SOURCE_UNKNOWN
} IPMS_WakeSource_t;

typedef enum {
    IPMS_EVENT_NONE = 0,
    IPMS_EVENT_STATE_TRANSITION,
    IPMS_EVENT_WAKEUP,
    IPMS_EVENT_SIMULATION_MODE_CHANGE,
    IPMS_EVENT_POLICY_MODE_CHANGE
} IPMS_EventType_t;

typedef enum {
    IPMS_REASON_NONE = 0,
    IPMS_REASON_BATTERY_LOW,
    IPMS_REASON_BATTERY_RECOVERED,
    IPMS_REASON_WAKEUP,
    IPMS_REASON_SIMULATION,
    IPMS_REASON_POLICY
} IPMS_Reason_t;

typedef struct {
    IPMS_ActionType_t type;
    uint32_t duration_ms;
} IPMS_ActionRequest_t;

typedef struct {
    IPMS_EventType_t type;
    IPMS_PowerState_t old_state;
    IPMS_PowerState_t new_state;
    IPMS_SimulationMode_t simulation_mode;
    IPMS_PolicyMode_t policy_mode;
    IPMS_WakeSource_t wake_source;
    IPMS_Reason_t reason;
    float measured_battery_voltage;
    float effective_battery_voltage;
    uint32_t timestamp_ms;
} IPMS_Event_t;

typedef struct {
    float warn_enter_v;
    float warn_exit_v;
    float sleep_enter_v;
    float sleep_exit_v;
    float stop_enter_v;
    float stop_exit_v;
    float recovery_exit_v;
    uint8_t warn_samples;
    uint8_t sleep_samples;
    uint8_t stop_samples;
    uint8_t recovery_samples;
    uint32_t sleep_duration_ms;
    uint32_t stop_duration_ms;
    float simulated_sunlight_voltage;
    float simulated_eclipse_voltage;
} IPMS_Config_t;

typedef struct {
    IPMS_PowerState_t power_state;
    IPMS_SimulationMode_t simulation_mode;
    IPMS_PolicyMode_t policy_mode;
    IPMS_ActionType_t pending_action;
    IPMS_WakeSource_t last_wake_source;
    float measured_battery_voltage;
    float effective_battery_voltage;
    uint32_t sample_count;
    uint32_t transition_count;
    uint32_t sleep_entries;
    uint32_t stop_entries;
    uint32_t rtc_wakeups;
    uint32_t button_wakeups;
} IPMS_StatusSnapshot_t;

/**
 * @brief Load the default voltage thresholds, debounce counts, and dwell times.
 *
 * @param config Destination configuration structure.
 */
void IPMS_GetDefaultConfig(IPMS_Config_t *config);

/**
 * @brief Initialize the IPMS state machine and runtime event queue.
 *
 * @param config Configuration to apply.
 * @return Module status.
 */
IPMS_Status_t IPMS_Init(const IPMS_Config_t *config);

/**
 * @brief Feed one battery sample into the IPMS state machine.
 *
 * The function updates measured/effective voltage, derives the desired state,
 * debounces transitions across samples, and commits a state change once the
 * required confirmation threshold is met.
 *
 * @param measured_battery_voltage Latest measured battery voltage.
 * @param now_ms Timestamp associated with this sample.
 * @return Module status.
 *
 * @callgraph
 * @callergraph
 */
IPMS_Status_t IPMS_ProcessBatterySample(float measured_battery_voltage, uint32_t now_ms);

/**
 * @brief Change the active simulation mode.
 *
 * @param mode Requested simulation mode.
 * @param now_ms Timestamp associated with the mode change.
 * @return Module status.
 */
IPMS_Status_t IPMS_SetSimulationMode(IPMS_SimulationMode_t mode, uint32_t now_ms);

/**
 * @brief Change the active power policy mode.
 *
 * @param mode Requested policy mode.
 * @param now_ms Timestamp associated with the mode change.
 * @return Module status.
 */
IPMS_Status_t IPMS_SetPolicyMode(IPMS_PolicyMode_t mode, uint32_t now_ms);

/**
 * @brief Snapshot externally visible IPMS status.
 *
 * @param snapshot Destination snapshot structure.
 */
void IPMS_GetStatus(IPMS_StatusSnapshot_t *snapshot);

/**
 * @brief Pop the oldest pending IPMS event from the internal queue.
 *
 * @param event Destination event structure.
 * @return true if an event was returned.
 */
bool IPMS_PopEvent(IPMS_Event_t *event);

/**
 * @brief Retrieve and clear the next pending low-power action request.
 *
 * @param request Destination action structure.
 * @return true if an action was available.
 */
bool IPMS_GetPendingAction(IPMS_ActionRequest_t *request);

/**
 * @brief Arm the RTC wakeup timer for a low-power dwell interval.
 *
 * @param hrtc RTC handle used for wakeup programming.
 * @param duration_ms Requested wakeup interval in milliseconds.
 * @return Module status.
 */
IPMS_Status_t IPMS_ArmRtcWakeup(RTC_HandleTypeDef *hrtc, uint32_t duration_ms);

/**
 * @brief Disable the RTC wakeup timer.
 *
 * @param hrtc RTC handle used for wakeup programming.
 */
void IPMS_DisarmRtcWakeup(RTC_HandleTypeDef *hrtc);

/**
 * @brief Record that an RTC wakeup interrupt occurred.
 */
void IPMS_OnRtcWakeup(void);

/**
 * @brief Record that a button wakeup interrupt occurred.
 */
void IPMS_OnButtonWakeup(void);

/**
 * @brief Finalize wakeup bookkeeping after the MCU resumes.
 *
 * @param wake_source Wake source if already known, or UNKNOWN to infer it.
 * @param now_ms Timestamp associated with the wakeup.
 */
void IPMS_RecordWakeup(IPMS_WakeSource_t wake_source, uint32_t now_ms);
const char *IPMS_StatusToString(IPMS_Status_t status);
const char *IPMS_PowerStateToString(IPMS_PowerState_t state);
const char *IPMS_SimulationModeToString(IPMS_SimulationMode_t mode);
const char *IPMS_PolicyModeToString(IPMS_PolicyMode_t mode);
const char *IPMS_ActionTypeToString(IPMS_ActionType_t action);
const char *IPMS_WakeSourceToString(IPMS_WakeSource_t source);
const char *IPMS_EventTypeToString(IPMS_EventType_t type);
const char *IPMS_ReasonToString(IPMS_Reason_t reason);

#endif /* INC_IPMS_H_ */
