/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file              freertos.c
  * @brief             FreeRTOS task creation and runtime task ownership.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Telemetry.h"
#include "ADC_Monitor.h"
#include "Logger.h"
#include "UART_Driver.h"
#include "Command_Parser.h"
#include "G2S_Link.h"
#include "SX1278.h"
#include "IPMS.h"
#include "Runtime_Resources.h"
#include "Storage_Service.h"
#include "Runtime_State.h"
#include "System_Runtime.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum {
    RTOS_TASK_ID_COMM = 0,
    RTOS_TASK_ID_TELEMETRY,
    RTOS_TASK_ID_HEALTH,
    RTOS_TASK_ID_STORAGE,
    RTOS_TASK_ID_COUNT
} RTOS_TaskId_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define RTOS_WATCHDOG_STARTUP_GRACE_MS   3000U
#define RTOS_WATCHDOG_COMM_TIMEOUT_MS     250U
#define RTOS_WATCHDOG_TELEM_TIMEOUT_MS    250U
#define RTOS_WATCHDOG_STORAGE_TIMEOUT_MS 1000U
#define RTOS_COMM_WAKE_FLAG            0x00000001U
#define RTOS_COMM_POLL_PERIOD_MS         5U
#define RTOS_TELEMETRY_TASK_PERIOD_MS   10U
#define RTOS_HEALTH_TASK_PERIOD_MS      20U
#define RTOS_TELEMETRY_DETAIL_PERIOD_MS 15000U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
extern IWDG_HandleTypeDef hiwdg;
static volatile uint32_t g_task_heartbeat_ms[RTOS_TASK_ID_COUNT] = {0U};
static uint32_t g_watchdog_supervision_start_ms = 0U;
static uint32_t g_watchdog_last_fault_mask = 0U;
/* USER CODE END Variables */
/* Definitions for CommTask */
osThreadId_t CommTaskHandle;
const osThreadAttr_t CommTask_attributes = {
  .name = "CommTask",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TelemetryRadioTask */
osThreadId_t TelemetryRadioTaskHandle;
const osThreadAttr_t TelemetryRadioTask_attributes = {
  .name = "TelemetryRadioTask",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for HealthPowerTask */
osThreadId_t HealthPowerTaskHandle;
const osThreadAttr_t HealthPowerTask_attributes = {
  .name = "HealthPowerTask",
  .stack_size = 384 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for StorageLogTask */
osThreadId_t StorageLogTaskHandle;
const osThreadAttr_t StorageLogTask_attributes = {
  .name = "StorageLogTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for ConsoleMutex */
osMutexId_t ConsoleMutexHandle;
const osMutexAttr_t ConsoleMutex_attributes = {
  .name = "ConsoleMutex"
};
/* Definitions for StorageMutex */
osMutexId_t StorageMutexHandle;
const osMutexAttr_t StorageMutex_attributes = {
  .name = "StorageMutex"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void RTOS_RunCommTaskCycle(void);
static void RTOS_RunTelemetryRadioTaskCycle(void);
static void RTOS_RunHealthPowerTaskCycle(void);
static void RTOS_MarkTaskAlive(RTOS_TaskId_t task_id);
static void RTOS_ResetTaskHeartbeats(uint32_t now);
static bool RTOS_WatchdogCanRefresh(uint32_t now, uint32_t *fault_mask);
static void RTOS_LogWatchdogFaultMask(uint32_t fault_mask);
void RTOS_NotifyCommTaskRxFromISR(void);
/* USER CODE END FunctionPrototypes */

void StartCommTask(void *argument);
void StartTelemetryRadioTask(void *argument);
void StartHealthPowerTask(void *argument);
void StartStorageLogTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/* Hook prototypes */
void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName);

/* USER CODE BEGIN 4 */
/**
 * @brief Wake CommTask from ISR context when new UART RX work arrives.
 */
void RTOS_NotifyCommTaskRxFromISR(void)
{
    if ((osKernelGetState() == osKernelRunning) && (CommTaskHandle != NULL))
    {
        (void)osThreadFlagsSet(CommTaskHandle, RTOS_COMM_WAKE_FLAG);
    }
}

/**
 * @brief Record a heartbeat timestamp for one supervised task.
 */
static void RTOS_MarkTaskAlive(RTOS_TaskId_t task_id)
{
    if (task_id < RTOS_TASK_ID_COUNT)
    {
        g_task_heartbeat_ms[task_id] = HAL_GetTick();
    }
}

static void RTOS_ResetTaskHeartbeats(uint32_t now)
{
    uint32_t index;

    for (index = 0U; index < (uint32_t)RTOS_TASK_ID_COUNT; index++)
    {
        g_task_heartbeat_ms[index] = now;
    }

    g_watchdog_supervision_start_ms = now;
    g_watchdog_last_fault_mask = 0U;
}

static bool RTOS_WatchdogCanRefresh(uint32_t now, uint32_t *fault_mask)
{
    uint32_t local_fault_mask = 0U;

    if ((now - g_watchdog_supervision_start_ms) < RTOS_WATCHDOG_STARTUP_GRACE_MS)
    {
        if (fault_mask != NULL)
        {
            *fault_mask = 0U;
        }
        return true;
    }

    if ((now - g_task_heartbeat_ms[RTOS_TASK_ID_COMM]) > RTOS_WATCHDOG_COMM_TIMEOUT_MS)
    {
        local_fault_mask |= (1UL << RTOS_TASK_ID_COMM);
    }

    if ((now - g_task_heartbeat_ms[RTOS_TASK_ID_TELEMETRY]) > RTOS_WATCHDOG_TELEM_TIMEOUT_MS)
    {
        local_fault_mask |= (1UL << RTOS_TASK_ID_TELEMETRY);
    }

    if ((now - g_task_heartbeat_ms[RTOS_TASK_ID_STORAGE]) > RTOS_WATCHDOG_STORAGE_TIMEOUT_MS)
    {
        local_fault_mask |= (1UL << RTOS_TASK_ID_STORAGE);
    }

    if (fault_mask != NULL)
    {
        *fault_mask = local_fault_mask;
    }

    return (local_fault_mask == 0U);
}

static void RTOS_LogWatchdogFaultMask(uint32_t fault_mask)
{
    if ((fault_mask & (1UL << RTOS_TASK_ID_COMM)) != 0U)
    {
        Logger_Error("Watchdog withheld: CommTask heartbeat stale");
    }

    if ((fault_mask & (1UL << RTOS_TASK_ID_TELEMETRY)) != 0U)
    {
        Logger_Error("Watchdog withheld: TelemetryRadioTask heartbeat stale");
    }

    if ((fault_mask & (1UL << RTOS_TASK_ID_STORAGE)) != 0U)
    {
        Logger_Error("Watchdog withheld: StorageLogTask heartbeat stale");
    }
}

/**
 * @brief Run one communication cycle owned by CommTask.
 *
 * This helper keeps command ingress and communication egress tied to the task that
 * reacts to RX wakeups. One cycle services console command parsing, advances one G2S
 * radio receive/ack step, and gives the telemetry module one opportunity to progress
 * a queued frame through its transport state machine.
 */
static void RTOS_RunCommTaskCycle(void)
{
    G2S_Link_Handle_t *g2s_link = RuntimeResources_GetG2SLink();
    Telemetry_Status_t telemetry_status;

    CommandParser_Process();
    (void)G2S_Link_Process(g2s_link);
    telemetry_status = Telemetry_ProcessStep(g2s_link);
    if ((telemetry_status != TELEM_STATUS_OK) &&
        (telemetry_status != TELEM_STATUS_IDLE) &&
        (telemetry_status != TELEM_STATUS_TX_BUSY))
    {
        Logger_Warn("Telemetry transport step: %s", Telemetry_StatusToString(telemetry_status));
    }
}

/**
 * @brief Run one periodic telemetry and radio reporting cycle.
 *
 * This helper is the producer-side scheduler for recurring telemetry work. It does
 * not transmit frames directly; instead it decides when heartbeat, system-status,
 * ADC-health, and radio-health packets should be enqueued so the telemetry transport
 * owner can send them later from CommTask.
 */
static void RTOS_RunTelemetryRadioTaskCycle(void)
{
    static uint32_t last_telem_queue_ms = 0U;
    static uint32_t last_telem_report_ms = 0U;
    static uint32_t last_telem_detail_ms = 0U;
    static uint32_t last_radio_report_ms = 0U;
    static uint8_t telemetry_status_counter = 0U;
    uint32_t now = HAL_GetTick();

    if ((now - last_telem_queue_ms) >= 2000U)
    {
        uint8_t status_code = (uint8_t)(0x10U | (telemetry_status_counter & 0x0FU));
        Telemetry_Status_t queue_status = Telemetry_SendSystemStatusEx(status_code);

        Logger_Info("Telemetry queue attempt: result=%s status=0x%02X",
                    Telemetry_StatusToString(queue_status),
                    status_code);
        telemetry_status_counter++;
        last_telem_queue_ms = now;
    }

    if ((now - last_telem_report_ms) >= 3000U)
    {
        RuntimeTelemetryCounters_t telemetry_counters;
        TelemetryStats_t telem_stats;
        Logger_Stats_t logger_stats;
        Telemetry_Status_t heartbeat_status = Telemetry_SendHeartbeatEx();

        RuntimeState_GetTelemetryCounters(&telemetry_counters);
        Telemetry_GetStats(&telem_stats);
        Logger_GetStats(&logger_stats);

        Logger_Info("Telemetry runtime: dma_tx=%lu dma_err=%lu hb=%s log=%lu/%lu persist_drop=%lu last_uart=%s last_storage=%s",
                    (unsigned long)telemetry_counters.tx_complete_count,
                    (unsigned long)telemetry_counters.error_count,
                    Telemetry_StatusToString(heartbeat_status),
                    (unsigned long)logger_stats.messages_attempted,
                    (unsigned long)logger_stats.messages_dropped,
                    (unsigned long)logger_stats.messages_persist_dropped,
                    UART_Driver_StatusToString(logger_stats.last_uart_status),
                    StorageService_StatusToString(logger_stats.last_storage_status));

        if ((now - last_telem_detail_ms) >= RTOS_TELEMETRY_DETAIL_PERIOD_MS)
        {
            Logger_Info("Telemetry stats: depth=%u/%u peak=%u queued=%lu sent=%lu dropped=%lu tx_busy=%lu tx_err=%lu rtc_fallback=%lu pending=%u last_enq=%s last_proc=%s ts_src=%s mode=%s radio_sent=%lu uart_sent=%lu",
                        (unsigned int)telem_stats.queue_depth,
                        (unsigned int)telem_stats.queue_capacity,
                        (unsigned int)telem_stats.max_queue_depth,
                        (unsigned long)telem_stats.queued_frames,
                        (unsigned long)telem_stats.sent_frames,
                        (unsigned long)telem_stats.dropped_frames,
                        (unsigned long)telem_stats.tx_busy_retries,
                        (unsigned long)telem_stats.transport_errors,
                        (unsigned long)telem_stats.rtc_fallback_count,
                        (unsigned int)telem_stats.frame_pending,
                        Telemetry_StatusToString(telem_stats.last_enqueue_status),
                        Telemetry_StatusToString(telem_stats.last_process_status),
                        SystemRuntime_TelemetryTimestampSourceToString(telem_stats.last_timestamp_source),
                        Telemetry_DownlinkModeToString(telem_stats.downlink_mode),
                        (unsigned long)telem_stats.radio_sent_frames,
                        (unsigned long)telem_stats.uart_sent_frames);
            last_telem_detail_ms = now;
        }
        last_telem_report_ms = now;
    }

    if ((now - last_radio_report_ms) >= 4000U)
    {
        SX1278_Handle_t *radio = RuntimeResources_GetRadio();
        G2S_Link_Handle_t *g2s_link = RuntimeResources_GetG2SLink();
        G2S_Stats_t g2s_stats;
        uint8_t version = 0U;
        SX1278_Status_t version_status = SX1278_ReadVersion(radio, &version);

        G2S_Link_GetStats(g2s_link, &g2s_stats);
        Logger_Info("Radio/G2S: ver=%s/0x%02X mode=%d freq=%luHz rx=%lu tx=%lu crc_fail=%lu cmd=%lu ack=%lu unsupported=%lu cmd_fail=%lu last=%s next_seq=%u last_rx_seq=%u",
                    SX1278_StatusToString(version_status),
                    version,
                    (int)radio->state.mode,
                    (unsigned long)radio->state.frequency_hz,
                    (unsigned long)g2s_stats.rx_packets,
                    (unsigned long)g2s_stats.tx_packets,
                    (unsigned long)g2s_stats.crc_failures,
                    (unsigned long)g2s_stats.command_packets,
                    (unsigned long)g2s_stats.ack_packets,
                    (unsigned long)g2s_stats.unsupported_packets,
                    (unsigned long)g2s_stats.command_failures,
                    G2S_StatusToString(g2s_stats.last_status),
                    (unsigned int)g2s_stats.next_tx_sequence,
                    (unsigned int)g2s_stats.last_rx_sequence);
        last_radio_report_ms = now;
    }
}

/**
 * @brief Run one health, watchdog, ADC, and power-management cycle.
 *
 * This helper is the central runtime health supervisor. Each cycle drains queued
 * IPMS control requests, reports queued IPMS events, snapshots ADC data into runtime
 * state, feeds battery data into the IPMS state machine, emits health telemetry, and
 * either refreshes or withholds the watchdog according to per-task heartbeat
 * supervision. If IPMS arms a low-power action, this cycle also hands it to
 * System_Runtime for actual sleep or stop execution.
 */
static void RTOS_RunHealthPowerTaskCycle(void)
{
    static uint32_t last_heartbeat_ms = 0U;
    static uint32_t last_power_sample_ms = 0U;
    static uint32_t last_adc_report_ms = 0U;
    uint32_t now = HAL_GetTick();
    uint32_t fault_mask = 0U;
    IPMS_ActionRequest_t power_action;
    RuntimeIpmsControlRequest_t ipms_control_request;

    RTOS_MarkTaskAlive(RTOS_TASK_ID_HEALTH);

    if (RTOS_WatchdogCanRefresh(now, &fault_mask))
    {
        HAL_IWDG_Refresh(&hiwdg);
        if (g_watchdog_last_fault_mask != 0U)
        {
            Logger_Info("Watchdog supervision recovered");
            g_watchdog_last_fault_mask = 0U;
        }
    }
    else if (fault_mask != g_watchdog_last_fault_mask)
    {
        RTOS_LogWatchdogFaultMask(fault_mask);
        g_watchdog_last_fault_mask = fault_mask;
    }

    SystemRuntime_ReportIpmsEvents();

    while (RuntimeState_PopIpmsControlRequest(&ipms_control_request))
    {
        if (ipms_control_request.type == RUNTIME_IPMS_CONTROL_SET_SIMULATION_MODE)
        {
            IPMS_Status_t status = IPMS_SetSimulationMode(
                    (IPMS_SimulationMode_t)ipms_control_request.value,
                    ipms_control_request.timestamp_ms);
            Logger_Info("IPMS simulation request apply: mode=%s result=%s",
                        IPMS_SimulationModeToString((IPMS_SimulationMode_t)ipms_control_request.value),
                        IPMS_StatusToString(status));
        }
        else if (ipms_control_request.type == RUNTIME_IPMS_CONTROL_SET_POLICY_MODE)
        {
            IPMS_Status_t status = IPMS_SetPolicyMode(
                    (IPMS_PolicyMode_t)ipms_control_request.value,
                    ipms_control_request.timestamp_ms);
            Logger_Info("IPMS policy request apply: mode=%s result=%s",
                        IPMS_PolicyModeToString((IPMS_PolicyMode_t)ipms_control_request.value),
                        IPMS_StatusToString(status));
        }
    }

    if ((now - last_heartbeat_ms) >= 500U)
    {
        last_heartbeat_ms = now;
        HAL_GPIO_TogglePin(LD2_HEARTBEAT_GPIO_Port, LD2_HEARTBEAT_Pin);
    }

    if ((now - last_power_sample_ms) >= 1000U)
    {
        ADC_HealthData_t adc_sample;
        ADC_Monitor_Status_t adc_status = ADC_Monitor_GetData(&adc_sample);

        if (adc_status == ADC_MONITOR_OK)
        {
            RuntimeState_SetLatestAdcSample(&adc_sample);
            (void)IPMS_ProcessBatterySample(adc_sample.battery_voltage, now);
        }

        last_power_sample_ms = now;
    }

    if ((now - last_adc_report_ms) >= 5000U)
    {
        ADC_HealthData_t latest_adc_sample;

        if (RuntimeState_GetLatestAdcSample(&latest_adc_sample))
        {
            Telemetry_Status_t adc_telem_status = Telemetry_SendADCHealthEx(
                    latest_adc_sample.vdda_voltage,
                    latest_adc_sample.battery_voltage,
                    latest_adc_sample.mcu_temp_c);

            Logger_Info("ADC health: VDDA=%.3fV TEMP=%.2fC BATT=%.3fV queue=%s",
                        latest_adc_sample.vdda_voltage,
                        latest_adc_sample.mcu_temp_c,
                        latest_adc_sample.battery_voltage,
                        Telemetry_StatusToString(adc_telem_status));
        }
        else
        {
            Telemetry_Status_t adc_error_event = Telemetry_SendEventEx(
                    TELEM_EVENT_ADC_READ_ERROR,
                    (uint32_t)ADC_MONITOR_NOT_READY);

            Logger_Warn("ADC health unavailable: status=%s(%d)",
                        ADC_Monitor_StatusToString(ADC_MONITOR_NOT_READY),
                        ADC_MONITOR_NOT_READY);
            Logger_Warn("ADC error event queue: result=%s",
                        Telemetry_StatusToString(adc_error_event));
        }

        last_adc_report_ms = now;
    }

    if (IPMS_GetPendingAction(&power_action))
    {
        SystemRuntime_ExecuteIpmsAction(RuntimeResources_GetSystemRuntimeContext(),
                                       RuntimeResources_GetSystemRuntimeHooks(),
                                       &power_action);
        RTOS_ResetTaskHeartbeats(HAL_GetTick());
    }
}

void vApplicationStackOverflowHook(xTaskHandle xTask, signed char *pcTaskName)
{
   /* Run time stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2. This hook function is
   called if a stack overflow is detected. */
}
/* USER CODE END 4 */

/**
  * @brief  Create RTOS-owned synchronization objects, services, and worker tasks.
  *
  * This is the scheduler-side construction boundary for the application runtime. It
  * initializes the storage service, resets task-heartbeat supervision state, creates
  * the mutexes used by shared services, and instantiates the four primary worker
  * tasks: CommTask, TelemetryRadioTask, HealthPowerTask, and StorageLogTask.
  * Periodic firmware behavior begins only after these objects are created and
  * osKernelStart() is called from main().
  *
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  StorageService_Init();
  RTOS_ResetTaskHeartbeats(HAL_GetTick());

  /* USER CODE END Init */
  /* Create the mutex(es) */
  /* creation of ConsoleMutex */
  ConsoleMutexHandle = osMutexNew(&ConsoleMutex_attributes);

  /* creation of StorageMutex */
  StorageMutexHandle = osMutexNew(&StorageMutex_attributes);

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of CommTask */
  CommTaskHandle = osThreadNew(StartCommTask, NULL, &CommTask_attributes);

  /* creation of TelemetryRadioTask */
  TelemetryRadioTaskHandle = osThreadNew(StartTelemetryRadioTask, NULL, &TelemetryRadioTask_attributes);

  /* creation of HealthPowerTask */
  HealthPowerTaskHandle = osThreadNew(StartHealthPowerTask, NULL, &HealthPowerTask_attributes);

  /* creation of StorageLogTask */
  StorageLogTaskHandle = osThreadNew(StartStorageLogTask, NULL, &StorageLogTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartCommTask */
/**
  * @brief  CommTask entrypoint for command ingress and communication progress.
  *
  * The task blocks on a thread flag set from UART RX ISR context, with a bounded
  * timeout so transport progress cannot stall indefinitely if no new bytes arrive.
  * On each wake it records a task heartbeat and runs RTOS_RunCommTaskCycle(), which
  * owns command parsing, G2S uplink processing, and one telemetry transport step.
  *
  * @param  argument Not used.
  * @retval None
  */
/* USER CODE END Header_StartCommTask */
void StartCommTask(void *argument)
{
  /* USER CODE BEGIN StartCommTask */
  /* Infinite loop */
  for(;;)
  {
    RTOS_MarkTaskAlive(RTOS_TASK_ID_COMM);
    (void)osThreadFlagsWait(RTOS_COMM_WAKE_FLAG, osFlagsWaitAny, RTOS_COMM_POLL_PERIOD_MS);
    RTOS_RunCommTaskCycle();
  }
  /* USER CODE END StartCommTask */
}

/* USER CODE BEGIN Header_StartTelemetryRadioTask */
/**
* @brief Periodic producer task for telemetry scheduling and radio health reporting.
*
* This task owns the cadence of recurring telemetry producers. It marks its heartbeat,
* runs RTOS_RunTelemetryRadioTaskCycle() once per period, and uses vTaskDelayUntil()
* so heartbeat and status production stay on a stable time base rather than drifting
* with execution time.
*
* @param argument Not used.
* @retval None
*/
/* USER CODE END Header_StartTelemetryRadioTask */
void StartTelemetryRadioTask(void *argument)
{
  /* USER CODE BEGIN StartTelemetryRadioTask */
  TickType_t last_wake_time = xTaskGetTickCount();

  /* Infinite loop */
  for(;;)
  {
    RTOS_MarkTaskAlive(RTOS_TASK_ID_TELEMETRY);
    RTOS_RunTelemetryRadioTaskCycle();
    vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(RTOS_TELEMETRY_TASK_PERIOD_MS));
  }
  /* USER CODE END StartTelemetryRadioTask */
}

/* USER CODE BEGIN Header_StartHealthPowerTask */
/**
* @brief Periodic supervisor task for ADC health, IPMS, and watchdog decisions.
*
* This task owns the fixed-cadence health loop. It marks its heartbeat, executes
* RTOS_RunHealthPowerTaskCycle(), and delays with vTaskDelayUntil() so ADC/IPMS
* sampling, event reporting, and watchdog supervision occur at a predictable rate.
*
* @param argument Not used.
* @retval None
*/
/* USER CODE END Header_StartHealthPowerTask */
void StartHealthPowerTask(void *argument)
{
  /* USER CODE BEGIN StartHealthPowerTask */
  TickType_t last_wake_time = xTaskGetTickCount();

  /* Infinite loop */
  for(;;)
  {
    RTOS_RunHealthPowerTaskCycle();
    vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(RTOS_HEALTH_TASK_PERIOD_MS));
  }
  /* USER CODE END StartHealthPowerTask */
}

/* USER CODE BEGIN Header_StartStorageLogTask */
/**
* @brief Serialized storage worker for log persistence and explicit SD requests.
*
* This task isolates potentially blocking FatFs/SD interactions from the rest of the
* runtime. It marks its heartbeat and services queued log records plus command-driven
* storage requests through the StorageService layer.
*
* @param argument Not used.
* @retval None
*/
/* USER CODE END Header_StartStorageLogTask */
void StartStorageLogTask(void *argument)
{
  /* USER CODE BEGIN StartStorageLogTask */
  for(;;)
  {
    RTOS_MarkTaskAlive(RTOS_TASK_ID_STORAGE);
    (void)StorageService_ProcessNext(250U);
  }
  /* USER CODE END StartStorageLogTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */
