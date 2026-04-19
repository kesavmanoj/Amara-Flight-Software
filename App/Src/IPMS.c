/**
 * @file IPMS.c
 * @brief Intelligent Power Management System implementation.
 */

#include "IPMS.h"
#include "Ring_Buffer.h"
#include "rtc.h"
#include <string.h>

#define IPMS_EVENT_QUEUE_SIZE         8U
#define IPMS_RTC_WAKEUP_TICK_HZ       2048U

typedef struct {
    bool initialized;
    IPMS_Config_t config;
    IPMS_PowerState_t power_state;
    IPMS_SimulationMode_t simulation_mode;
    IPMS_PolicyMode_t policy_mode;
    IPMS_ActionType_t pending_action;
    IPMS_WakeSource_t last_wake_source;
    IPMS_PowerState_t candidate_state;
    uint8_t candidate_count;
    bool action_armed_for_state;
    float measured_battery_voltage;
    float effective_battery_voltage;
    uint32_t sample_count;
    uint32_t transition_count;
    uint32_t sleep_entries;
    uint32_t stop_entries;
    uint32_t rtc_wakeups;
    uint32_t button_wakeups;
} IPMS_Context_t;

static IPMS_Context_t g_ipms;
static IPMS_Event_t g_event_queue_storage[IPMS_EVENT_QUEUE_SIZE];
static FrameQueue_t g_event_queue;
static volatile bool g_rtc_wakeup_seen = false;
static volatile bool g_button_wakeup_seen = false;

static uint32_t IPMS_EnterCritical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void IPMS_ExitCritical(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/* Must be called with IPMS critical section held. */
static void IPMS_ResetContext(void)
{
    memset(&g_ipms, 0, sizeof(g_ipms));
    g_ipms.power_state = IPMS_POWER_STATE_NORMAL;
    g_ipms.simulation_mode = IPMS_SIMULATION_AUTO;
    g_ipms.policy_mode = IPMS_POLICY_MONITOR_ONLY;
    g_ipms.pending_action = IPMS_ACTION_NONE;
    g_ipms.last_wake_source = IPMS_WAKE_SOURCE_NONE;
    g_ipms.candidate_state = IPMS_POWER_STATE_NORMAL;
}

/* Must be called with IPMS critical section held. */
static void IPMS_PushEvent(const IPMS_Event_t *event)
{
    if (event == NULL)
    {
        return;
    }

    (void)FrameQueue_Push(&g_event_queue, (void *)event);
}

/* Must be called with IPMS critical section held. */
static void IPMS_PostStateTransitionEvent(IPMS_PowerState_t old_state,
                                          IPMS_PowerState_t new_state,
                                          IPMS_Reason_t reason,
                                          uint32_t now_ms)
{
    IPMS_Event_t event;

    event.type = IPMS_EVENT_STATE_TRANSITION;
    event.old_state = old_state;
    event.new_state = new_state;
    event.simulation_mode = g_ipms.simulation_mode;
    event.policy_mode = g_ipms.policy_mode;
    event.wake_source = g_ipms.last_wake_source;
    event.reason = reason;
    event.measured_battery_voltage = g_ipms.measured_battery_voltage;
    event.effective_battery_voltage = g_ipms.effective_battery_voltage;
    event.timestamp_ms = now_ms;
    IPMS_PushEvent(&event);
}

/* Must be called with IPMS critical section held. */
static void IPMS_PostModeEvent(IPMS_EventType_t type, IPMS_Reason_t reason, uint32_t now_ms)
{
    IPMS_Event_t event;

    event.type = type;
    event.old_state = g_ipms.power_state;
    event.new_state = g_ipms.power_state;
    event.simulation_mode = g_ipms.simulation_mode;
    event.policy_mode = g_ipms.policy_mode;
    event.wake_source = g_ipms.last_wake_source;
    event.reason = reason;
    event.measured_battery_voltage = g_ipms.measured_battery_voltage;
    event.effective_battery_voltage = g_ipms.effective_battery_voltage;
    event.timestamp_ms = now_ms;
    IPMS_PushEvent(&event);
}

/**
 * @brief Resolve the voltage that the IPMS state machine should act on.
 *
 * Must be called with the IPMS critical section held.
 *
 * @param measured_voltage Latest measured battery voltage.
 * @return Effective voltage used for power-state decisions.
 */
static float IPMS_ResolveEffectiveBattery(float measured_voltage)
{
    switch (g_ipms.simulation_mode)
    {
        case IPMS_SIMULATION_SUNLIGHT:
            return g_ipms.config.simulated_sunlight_voltage;
        case IPMS_SIMULATION_ECLIPSE:
            return g_ipms.config.simulated_eclipse_voltage;
        case IPMS_SIMULATION_AUTO:
        default:
            return measured_voltage;
    }
}

/**
 * @brief Choose the next desired state from the current effective battery voltage.
 *
 * Must be called with the IPMS critical section held.
 *
 * @param battery_voltage Effective battery voltage after simulation override.
 * @return Desired power state before debounce is applied.
 */
static IPMS_PowerState_t IPMS_GetDesiredState(float battery_voltage)
{
    switch (g_ipms.power_state)
    {
        case IPMS_POWER_STATE_NORMAL:
            if (battery_voltage <= g_ipms.config.stop_enter_v)
            {
                return IPMS_POWER_STATE_STOP_CANDIDATE;
            }
            if (battery_voltage <= g_ipms.config.sleep_enter_v)
            {
                return IPMS_POWER_STATE_SLEEP_CANDIDATE;
            }
            if (battery_voltage <= g_ipms.config.warn_enter_v)
            {
                return IPMS_POWER_STATE_LOW_POWER_WARNING;
            }
            return IPMS_POWER_STATE_NORMAL;

        case IPMS_POWER_STATE_LOW_POWER_WARNING:
            if (battery_voltage <= g_ipms.config.stop_enter_v)
            {
                return IPMS_POWER_STATE_STOP_CANDIDATE;
            }
            if (battery_voltage <= g_ipms.config.sleep_enter_v)
            {
                return IPMS_POWER_STATE_SLEEP_CANDIDATE;
            }
            if (battery_voltage >= g_ipms.config.warn_exit_v)
            {
                return IPMS_POWER_STATE_RECOVERY;
            }
            return IPMS_POWER_STATE_LOW_POWER_WARNING;

        case IPMS_POWER_STATE_SLEEP_CANDIDATE:
            if (battery_voltage <= g_ipms.config.stop_enter_v)
            {
                return IPMS_POWER_STATE_STOP_CANDIDATE;
            }
            if (battery_voltage >= g_ipms.config.sleep_exit_v)
            {
                return IPMS_POWER_STATE_RECOVERY;
            }
            return IPMS_POWER_STATE_SLEEP_CANDIDATE;

        case IPMS_POWER_STATE_STOP_CANDIDATE:
            if (battery_voltage >= g_ipms.config.stop_exit_v)
            {
                return IPMS_POWER_STATE_RECOVERY;
            }
            return IPMS_POWER_STATE_STOP_CANDIDATE;

        case IPMS_POWER_STATE_RECOVERY:
        default:
            if (battery_voltage <= g_ipms.config.stop_enter_v)
            {
                return IPMS_POWER_STATE_STOP_CANDIDATE;
            }
            if (battery_voltage <= g_ipms.config.sleep_enter_v)
            {
                return IPMS_POWER_STATE_SLEEP_CANDIDATE;
            }
            if (battery_voltage <= g_ipms.config.warn_enter_v)
            {
                return IPMS_POWER_STATE_LOW_POWER_WARNING;
            }
            if (battery_voltage >= g_ipms.config.recovery_exit_v)
            {
                return IPMS_POWER_STATE_NORMAL;
            }
            return IPMS_POWER_STATE_RECOVERY;
    }
}

/**
 * @brief Return the required number of consecutive samples for a candidate state.
 *
 * Must be called with the IPMS critical section held.
 *
 * @param state Candidate state being evaluated.
 * @return Required sample count before the state is committed.
 */
static uint8_t IPMS_GetRequiredSamples(IPMS_PowerState_t state)
{
    switch (state)
    {
        case IPMS_POWER_STATE_LOW_POWER_WARNING:
            return g_ipms.config.warn_samples;
        case IPMS_POWER_STATE_SLEEP_CANDIDATE:
            return g_ipms.config.sleep_samples;
        case IPMS_POWER_STATE_STOP_CANDIDATE:
            return g_ipms.config.stop_samples;
        case IPMS_POWER_STATE_RECOVERY:
        case IPMS_POWER_STATE_NORMAL:
        default:
            return g_ipms.config.recovery_samples;
    }
}

/**
 * @brief Commit a power-state transition and arm any matching low-power action.
 *
 * Must be called with the IPMS critical section held.
 *
 * @param new_state State to commit.
 * @param reason Transition reason for logging and telemetry.
 * @param now_ms Timestamp associated with the transition.
 */
static void IPMS_ApplyState(IPMS_PowerState_t new_state, IPMS_Reason_t reason, uint32_t now_ms)
{
    IPMS_PowerState_t old_state = g_ipms.power_state;

    if (old_state == new_state)
    {
        return;
    }

    g_ipms.power_state = new_state;
    g_ipms.transition_count++;
    g_ipms.candidate_state = new_state;
    g_ipms.candidate_count = 0U;
    g_ipms.action_armed_for_state = false;
    g_ipms.pending_action = IPMS_ACTION_NONE;

    if (new_state == IPMS_POWER_STATE_SLEEP_CANDIDATE)
    {
        if (g_ipms.policy_mode >= IPMS_POLICY_ENABLE_SLEEP)
        {
            g_ipms.pending_action = IPMS_ACTION_ENTER_SLEEP;
            g_ipms.action_armed_for_state = true;
            g_ipms.sleep_entries++;
        }
    }
    else if (new_state == IPMS_POWER_STATE_STOP_CANDIDATE)
    {
        if (g_ipms.policy_mode >= IPMS_POLICY_ENABLE_SLEEP_AND_STOP)
        {
            g_ipms.pending_action = IPMS_ACTION_ENTER_STOP;
            g_ipms.action_armed_for_state = true;
            g_ipms.stop_entries++;
        }
    }

    IPMS_PostStateTransitionEvent(old_state, new_state, reason, now_ms);
}

void IPMS_GetDefaultConfig(IPMS_Config_t *config)
{
    if (config == NULL)
    {
        return;
    }

    config->warn_enter_v = 3.75f;
    config->warn_exit_v = 3.85f;
    config->sleep_enter_v = 3.55f;
    config->sleep_exit_v = 3.70f;
    config->stop_enter_v = 3.35f;
    config->stop_exit_v = 3.50f;
    config->recovery_exit_v = 3.95f;
    config->warn_samples = 2U;
    config->sleep_samples = 3U;
    config->stop_samples = 4U;
    config->recovery_samples = 2U;
    config->sleep_duration_ms = 500U;
    config->stop_duration_ms = 2000U;
    config->simulated_sunlight_voltage = 4.05f;
    config->simulated_eclipse_voltage = 3.25f;
}

IPMS_Status_t IPMS_Init(const IPMS_Config_t *config)
{
    uint32_t primask;

    if (config == NULL)
    {
        return IPMS_STATUS_INVALID_PARAM;
    }

    primask = IPMS_EnterCritical();
    IPMS_ResetContext();
    g_ipms.config = *config;
    g_ipms.initialized = true;
    FrameQueue_InitWithPolicy(&g_event_queue,
                              (uint8_t *)g_event_queue_storage,
                              (uint16_t)sizeof(IPMS_Event_t),
                              IPMS_EVENT_QUEUE_SIZE,
                              FRAME_QUEUE_DROP_OLDEST_ON_FULL);
    g_rtc_wakeup_seen = false;
    g_button_wakeup_seen = false;
    IPMS_ExitCritical(primask);

    HAL_NVIC_SetPriority(RTC_WKUP_IRQn, 6U, 0U);
    HAL_NVIC_EnableIRQ(RTC_WKUP_IRQn);

    return IPMS_STATUS_OK;
}

/**
 * @brief Feed one battery sample into the IPMS state machine.
 *
 * This function is the decision-engine entrypoint for battery-driven power policy.
 * It updates measured and effective battery voltage, derives the target state from
 * the configured threshold bands, debounces transitions across samples, and commits
 * a new state once the confirmation threshold is met. It does not enter low power
 * directly; instead it may queue IPMS events and arm a pending action that
 * System_Runtime executes later.
 */
IPMS_Status_t IPMS_ProcessBatterySample(float measured_battery_voltage, uint32_t now_ms)
{
    IPMS_PowerState_t desired_state;
    uint8_t required_samples;
    uint32_t primask;

    primask = IPMS_EnterCritical();
    if (g_ipms.initialized == false)
    {
        IPMS_ExitCritical(primask);
        return IPMS_STATUS_NOT_INITIALIZED;
    }

    g_ipms.measured_battery_voltage = measured_battery_voltage;
    g_ipms.effective_battery_voltage = IPMS_ResolveEffectiveBattery(measured_battery_voltage);
    g_ipms.sample_count++;

    desired_state = IPMS_GetDesiredState(g_ipms.effective_battery_voltage);
    if (desired_state == g_ipms.power_state)
    {
        g_ipms.candidate_state = desired_state;
        g_ipms.candidate_count = 0U;
        IPMS_ExitCritical(primask);
        return IPMS_STATUS_OK;
    }

    if (desired_state != g_ipms.candidate_state)
    {
        g_ipms.candidate_state = desired_state;
        g_ipms.candidate_count = 1U;
        IPMS_ExitCritical(primask);
        return IPMS_STATUS_OK;
    }

    g_ipms.candidate_count++;
    required_samples = IPMS_GetRequiredSamples(desired_state);
    if (g_ipms.candidate_count >= required_samples)
    {
        IPMS_ApplyState(desired_state,
                        (desired_state == IPMS_POWER_STATE_RECOVERY) ? IPMS_REASON_BATTERY_RECOVERED : IPMS_REASON_BATTERY_LOW,
                        now_ms);
    }

    IPMS_ExitCritical(primask);
    return IPMS_STATUS_OK;
}

IPMS_Status_t IPMS_SetSimulationMode(IPMS_SimulationMode_t mode, uint32_t now_ms)
{
    uint32_t primask = IPMS_EnterCritical();

    if (g_ipms.initialized == false)
    {
        IPMS_ExitCritical(primask);
        return IPMS_STATUS_NOT_INITIALIZED;
    }

    if (mode > IPMS_SIMULATION_ECLIPSE)
    {
        IPMS_ExitCritical(primask);
        return IPMS_STATUS_INVALID_PARAM;
    }

    g_ipms.simulation_mode = mode;
    g_ipms.candidate_count = 0U;
    g_ipms.candidate_state = g_ipms.power_state;
    IPMS_PostModeEvent(IPMS_EVENT_SIMULATION_MODE_CHANGE, IPMS_REASON_SIMULATION, now_ms);
    IPMS_ExitCritical(primask);
    return IPMS_STATUS_OK;
}

IPMS_Status_t IPMS_SetPolicyMode(IPMS_PolicyMode_t mode, uint32_t now_ms)
{
    uint32_t primask = IPMS_EnterCritical();

    if (g_ipms.initialized == false)
    {
        IPMS_ExitCritical(primask);
        return IPMS_STATUS_NOT_INITIALIZED;
    }

    if (mode > IPMS_POLICY_ENABLE_SLEEP_AND_STOP)
    {
        IPMS_ExitCritical(primask);
        return IPMS_STATUS_INVALID_PARAM;
    }

    g_ipms.policy_mode = mode;
    if (mode == IPMS_POLICY_MONITOR_ONLY)
    {
        g_ipms.pending_action = IPMS_ACTION_NONE;
        g_ipms.action_armed_for_state = false;
    }
    IPMS_PostModeEvent(IPMS_EVENT_POLICY_MODE_CHANGE, IPMS_REASON_POLICY, now_ms);
    IPMS_ExitCritical(primask);
    return IPMS_STATUS_OK;
}

void IPMS_GetStatus(IPMS_StatusSnapshot_t *snapshot)
{
    uint32_t primask;

    if ((g_ipms.initialized == false) || (snapshot == NULL))
    {
        return;
    }

    primask = IPMS_EnterCritical();
    snapshot->power_state = g_ipms.power_state;
    snapshot->simulation_mode = g_ipms.simulation_mode;
    snapshot->policy_mode = g_ipms.policy_mode;
    snapshot->pending_action = g_ipms.pending_action;
    snapshot->last_wake_source = g_ipms.last_wake_source;
    snapshot->measured_battery_voltage = g_ipms.measured_battery_voltage;
    snapshot->effective_battery_voltage = g_ipms.effective_battery_voltage;
    snapshot->sample_count = g_ipms.sample_count;
    snapshot->transition_count = g_ipms.transition_count;
    snapshot->sleep_entries = g_ipms.sleep_entries;
    snapshot->stop_entries = g_ipms.stop_entries;
    snapshot->rtc_wakeups = g_ipms.rtc_wakeups;
    snapshot->button_wakeups = g_ipms.button_wakeups;
    IPMS_ExitCritical(primask);
}

bool IPMS_PopEvent(IPMS_Event_t *event)
{
    uint32_t primask;

    if ((event == NULL) || FrameQueue_IsEmpty(&g_event_queue))
    {
        return false;
    }

    primask = IPMS_EnterCritical();
    if (FrameQueue_IsEmpty(&g_event_queue))
    {
        IPMS_ExitCritical(primask);
        return false;
    }

    (void)FrameQueue_Pop(&g_event_queue, event);
    IPMS_ExitCritical(primask);
    return true;
}

bool IPMS_GetPendingAction(IPMS_ActionRequest_t *request)
{
    uint32_t primask = IPMS_EnterCritical();

    if ((g_ipms.initialized == false) || (request == NULL) || (g_ipms.pending_action == IPMS_ACTION_NONE))
    {
        IPMS_ExitCritical(primask);
        return false;
    }

    request->type = g_ipms.pending_action;
    if (g_ipms.pending_action == IPMS_ACTION_ENTER_SLEEP)
    {
        request->duration_ms = g_ipms.config.sleep_duration_ms;
    }
    else
    {
        request->duration_ms = g_ipms.config.stop_duration_ms;
    }

    g_ipms.pending_action = IPMS_ACTION_NONE;
    IPMS_ExitCritical(primask);
    return true;
}

IPMS_Status_t IPMS_ArmRtcWakeup(RTC_HandleTypeDef *hrtc_handle, uint32_t duration_ms)
{
    uint32_t ticks;
    uint32_t primask;

    primask = IPMS_EnterCritical();
    if ((g_ipms.initialized == false) || (hrtc_handle == NULL) || (duration_ms == 0U))
    {
        IPMS_ExitCritical(primask);
        return IPMS_STATUS_INVALID_PARAM;
    }
    IPMS_ExitCritical(primask);

    ticks = (duration_ms * IPMS_RTC_WAKEUP_TICK_HZ) / 1000U;
    if (ticks == 0U)
    {
        ticks = 1U;
    }
    if (ticks > 0xFFFFU)
    {
        ticks = 0xFFFFU;
    }

    HAL_RTCEx_DeactivateWakeUpTimer(hrtc_handle);
    primask = IPMS_EnterCritical();
    g_rtc_wakeup_seen = false;
    g_button_wakeup_seen = false;
    IPMS_ExitCritical(primask);

    if (HAL_RTCEx_SetWakeUpTimer_IT(hrtc_handle, ticks, RTC_WAKEUPCLOCK_RTCCLK_DIV16) != HAL_OK)
    {
        return IPMS_STATUS_HAL_ERROR;
    }

    return IPMS_STATUS_OK;
}

void IPMS_DisarmRtcWakeup(RTC_HandleTypeDef *hrtc_handle)
{
    if (hrtc_handle == NULL)
    {
        return;
    }

    HAL_RTCEx_DeactivateWakeUpTimer(hrtc_handle);
}

void IPMS_OnRtcWakeup(void)
{
    g_rtc_wakeup_seen = true;
}

void IPMS_OnButtonWakeup(void)
{
    g_button_wakeup_seen = true;
}

void IPMS_RecordWakeup(IPMS_WakeSource_t wake_source, uint32_t now_ms)
{
    IPMS_Event_t event;
    uint32_t primask = IPMS_EnterCritical();

    if (g_ipms.initialized == false)
    {
        IPMS_ExitCritical(primask);
        return;
    }

    if (wake_source == IPMS_WAKE_SOURCE_UNKNOWN)
    {
        if (g_button_wakeup_seen)
        {
            wake_source = IPMS_WAKE_SOURCE_BUTTON;
        }
        else if (g_rtc_wakeup_seen)
        {
            wake_source = IPMS_WAKE_SOURCE_RTC;
        }
        else
        {
            wake_source = IPMS_WAKE_SOURCE_UNKNOWN;
        }
    }

    g_ipms.last_wake_source = wake_source;
    if (wake_source == IPMS_WAKE_SOURCE_RTC)
    {
        g_ipms.rtc_wakeups++;
    }
    else if (wake_source == IPMS_WAKE_SOURCE_BUTTON)
    {
        g_ipms.button_wakeups++;
    }

    event.type = IPMS_EVENT_WAKEUP;
    event.old_state = g_ipms.power_state;
    event.new_state = IPMS_POWER_STATE_RECOVERY;
    event.simulation_mode = g_ipms.simulation_mode;
    event.policy_mode = g_ipms.policy_mode;
    event.wake_source = wake_source;
    event.reason = IPMS_REASON_WAKEUP;
    event.measured_battery_voltage = g_ipms.measured_battery_voltage;
    event.effective_battery_voltage = g_ipms.effective_battery_voltage;
    event.timestamp_ms = now_ms;
    IPMS_PushEvent(&event);

    IPMS_ApplyState(IPMS_POWER_STATE_RECOVERY, IPMS_REASON_WAKEUP, now_ms);
    g_rtc_wakeup_seen = false;
    g_button_wakeup_seen = false;
    IPMS_ExitCritical(primask);
}

const char *IPMS_StatusToString(IPMS_Status_t status)
{
    switch (status)
    {
        case IPMS_STATUS_OK:
            return "OK";
        case IPMS_STATUS_IDLE:
            return "IDLE";
        case IPMS_STATUS_INVALID_PARAM:
            return "INVALID_PARAM";
        case IPMS_STATUS_NOT_INITIALIZED:
            return "NOT_INITIALIZED";
        case IPMS_STATUS_HAL_ERROR:
            return "HAL_ERROR";
        case IPMS_STATUS_NO_ACTION:
        default:
            return "NO_ACTION";
    }
}

const char *IPMS_PowerStateToString(IPMS_PowerState_t state)
{
    switch (state)
    {
        case IPMS_POWER_STATE_NORMAL:
            return "NORMAL";
        case IPMS_POWER_STATE_LOW_POWER_WARNING:
            return "LOW_POWER_WARNING";
        case IPMS_POWER_STATE_SLEEP_CANDIDATE:
            return "SLEEP_CANDIDATE";
        case IPMS_POWER_STATE_STOP_CANDIDATE:
            return "STOP_CANDIDATE";
        case IPMS_POWER_STATE_RECOVERY:
        default:
            return "RECOVERY";
    }
}

const char *IPMS_SimulationModeToString(IPMS_SimulationMode_t mode)
{
    switch (mode)
    {
        case IPMS_SIMULATION_AUTO:
            return "AUTO";
        case IPMS_SIMULATION_SUNLIGHT:
            return "SUNLIGHT";
        case IPMS_SIMULATION_ECLIPSE:
        default:
            return "ECLIPSE";
    }
}

const char *IPMS_PolicyModeToString(IPMS_PolicyMode_t mode)
{
    switch (mode)
    {
        case IPMS_POLICY_MONITOR_ONLY:
            return "MONITOR_ONLY";
        case IPMS_POLICY_ENABLE_SLEEP:
            return "ENABLE_SLEEP";
        case IPMS_POLICY_ENABLE_SLEEP_AND_STOP:
        default:
            return "ENABLE_SLEEP_AND_STOP";
    }
}

const char *IPMS_ActionTypeToString(IPMS_ActionType_t action)
{
    switch (action)
    {
        case IPMS_ACTION_NONE:
            return "NONE";
        case IPMS_ACTION_ENTER_SLEEP:
            return "ENTER_SLEEP";
        case IPMS_ACTION_ENTER_STOP:
        default:
            return "ENTER_STOP";
    }
}

const char *IPMS_WakeSourceToString(IPMS_WakeSource_t source)
{
    switch (source)
    {
        case IPMS_WAKE_SOURCE_NONE:
            return "NONE";
        case IPMS_WAKE_SOURCE_RTC:
            return "RTC";
        case IPMS_WAKE_SOURCE_BUTTON:
            return "BUTTON";
        case IPMS_WAKE_SOURCE_UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

const char *IPMS_EventTypeToString(IPMS_EventType_t type)
{
    switch (type)
    {
        case IPMS_EVENT_NONE:
            return "NONE";
        case IPMS_EVENT_STATE_TRANSITION:
            return "STATE_TRANSITION";
        case IPMS_EVENT_WAKEUP:
            return "WAKEUP";
        case IPMS_EVENT_SIMULATION_MODE_CHANGE:
            return "SIMULATION_MODE_CHANGE";
        case IPMS_EVENT_POLICY_MODE_CHANGE:
        default:
            return "POLICY_MODE_CHANGE";
    }
}

const char *IPMS_ReasonToString(IPMS_Reason_t reason)
{
    switch (reason)
    {
        case IPMS_REASON_NONE:
            return "NONE";
        case IPMS_REASON_BATTERY_LOW:
            return "BATTERY_LOW";
        case IPMS_REASON_BATTERY_RECOVERED:
            return "BATTERY_RECOVERED";
        case IPMS_REASON_WAKEUP:
            return "WAKEUP";
        case IPMS_REASON_SIMULATION:
            return "SIMULATION";
        case IPMS_REASON_POLICY:
        default:
            return "POLICY";
    }
}
