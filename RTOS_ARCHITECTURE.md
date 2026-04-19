# RTOS Architecture

This document describes the firmware architecture that exists in the codebase today. It is not a phase plan and it is not a migration target. The goal here is to explain the current runtime clearly:

- how boot hands off to FreeRTOS
- which task owns which subsystem
- which functions each task actually runs
- what shared runtime state exists and why
- how ISR callbacks hand work into the task world
- how telemetry, command handling, power management, and storage interact

The key architectural idea is that `main()` performs one-time board bring-up, then FreeRTOS becomes the runtime owner. Periodic behavior is no longer driven from the `while(1)` loop in `main.c`; it is driven by four application tasks:

- `CommTask`
- `TelemetryRadioTask`
- `HealthPowerTask`
- `StorageLogTask`

## 1. System Overview

At a high level, the current firmware is split into five layers:

### 1.1 Boot / HAL layer

CubeMX-generated peripheral init and HAL callback entrypoints live mostly in:

- [main.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/main.c)

This layer is responsible for:

- MCU reset and HAL startup
- clock configuration
- peripheral init
- creation of the FreeRTOS runtime
- forwarding HAL callbacks into project-owned modules

### 1.2 Runtime resource layer

Runtime-owned handles and configuration structs are centralized in:

- [Runtime_Resources.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Runtime_Resources.c)
- [Runtime_Resources.h](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Inc/Runtime_Resources.h)

This layer provides one shared place to hold:

- `I2C_Bus_Handle_t`
- `OLED_HandleTypeDef`
- `SX1278_Handle_t`
- `G2S_Link_Handle_t`
- `IPMS_Config_t`
- `SystemRuntimeContext_t`
- `SystemRuntimeHooks_t`

It exists so tasks and runtime helpers can access the same project-owned objects without making `main.c` the permanent owner of everything.

### 1.3 Runtime shared-state layer

Small pieces of cross-task runtime state live in:

- [Runtime_State.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Runtime_State.c)
- [Runtime_State.h](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Inc/Runtime_State.h)

This layer holds only the shared values that really need to survive across task boundaries, such as:

- latest cached ADC sample
- telemetry UART completion/error counters
- queued IPMS control requests from command handling into the health task

This is intentionally small. It is not meant to be a giant dumping ground for globals.

### 1.4 Service / subsystem layer

The application modules implement the actual system behaviors:

- [Telemetry.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Telemetry.c)
- [Command_Parser.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Command_Parser.c)
- [Command_List.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Command_List.c)
- [G2S_Link.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/G2S_Link.c)
- [IPMS.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/IPMS.c)
- [Storage_Service.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Storage_Service.c)
- [System_Runtime.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/System_Runtime.c)
- hardware-facing drivers such as `UART_Driver`, `ADC_Monitor`, `I2C_Bus`, `SX1278`

These modules own the logic of each subsystem. The FreeRTOS tasks decide when to call them.

### 1.5 Task ownership layer

FreeRTOS task creation and scheduling live in:

- [freertos.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/freertos.c)

This file is the runtime ownership map. It decides:

- which task exists
- what each task runs
- how often each task runs
- how watchdog supervision works

## 2. Boot And Scheduler Handoff

The current boot sequence is defined in [main.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/main.c).

### 2.1 `main()` responsibilities

`main()` does the following in order:

1. `HAL_Init()`
2. `SystemClock_Config()`
3. initialize all configured peripherals:
   - GPIO
   - DMA
   - SDIO
   - UART1 / UART2
   - I2C1
   - SPI1
   - ADC1
   - TIM2
   - CRC
   - IWDG
   - RTC
   - FatFs
4. initialize runtime resource storage with `RuntimeResources_Init()`
5. fetch resource pointers from the runtime resource layer
6. populate `SystemRuntimeHooks_t` so stop-mode restore code knows which Cube init functions to call later
7. initialize runtime shared state with `RuntimeState_Init()`
8. initialize project modules:
   - UART driver for console and telemetry channels
   - command parser
   - ADC monitor
   - telemetry
   - IPMS config + IPMS engine
   - SX1278 radio
   - G2S link
   - I2C bus and OLED
9. fill `SystemRuntimeContext_t` with HAL handles and subsystem handles
10. emit startup logs and boot telemetry
11. call `osKernelInitialize()`
12. call `MX_FREERTOS_Init()`
13. call `osKernelStart()`

After `osKernelStart()`, the firmware is under scheduler control. The `while(1)` loop remains only as a safety fallback and is not supposed to own runtime behavior.

### 2.2 Why `SystemRuntimeHooks_t` exists

The stop-mode restore path in [System_Runtime.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/System_Runtime.c) needs to call Cube-generated init functions again after STOP mode, such as:

- `SystemClock_Config`
- `MX_DMA_Init`
- `MX_USART1_UART_Init`
- `MX_USART2_UART_Init`
- `MX_I2C1_Init`
- `MX_SPI1_Init`
- `MX_ADC1_Init`
- `MX_CRC_Init`

Rather than hardcoding those calls directly inside `System_Runtime.c`, `main()` passes those function pointers through `SystemRuntimeHooks_t`. That keeps the restore logic decoupled from direct file-level dependencies on `main.c`.

## 3. FreeRTOS Task Model

The active application tasks are all created in [freertos.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/freertos.c) by `MX_FREERTOS_Init()`.

### 3.1 `MX_FREERTOS_Init()`

This function creates the scheduler-owned runtime objects:

- initializes the storage service with `StorageService_Init()`
- resets task heartbeat supervision with `RTOS_ResetTaskHeartbeats(HAL_GetTick())`
- creates:
  - `ConsoleMutex`
  - `StorageMutex`
- creates the four worker tasks:
  - `CommTask`
  - `TelemetryRadioTask`
  - `HealthPowerTask`
  - `StorageLogTask`

The task priorities are:

- `HealthPowerTask`: `osPriorityAboveNormal`
- `CommTask`: `osPriorityNormal`
- `TelemetryRadioTask`: `osPriorityNormal`
- `StorageLogTask`: `osPriorityBelowNormal`

This priority split reflects the current architecture:

- power and watchdog supervision should not starve
- communications should stay responsive
- storage is intentionally lower-priority because it can block on SD/FatFs work

## 4. Task Ownership In Detail

### 4.1 `CommTask`

Entrypoint:

- `StartCommTask()`

Loop behavior:

- marks its heartbeat with `RTOS_MarkTaskAlive(RTOS_TASK_ID_COMM)`
- blocks on `osThreadFlagsWait(RTOS_COMM_WAKE_FLAG, osFlagsWaitAny, RTOS_COMM_POLL_PERIOD_MS)`
- runs `RTOS_RunCommTaskCycle()`

Important constants:

- `RTOS_COMM_WAKE_FLAG = 0x00000001U`
- `RTOS_COMM_POLL_PERIOD_MS = 5U`

That means `CommTask` is mostly event-driven by UART RX wakeups, but it still polls at a short interval so transport progress never stalls forever.

#### What `RTOS_RunCommTaskCycle()` does

This helper is the task’s actual ownership boundary.

It calls:

- `CommandParser_Process()`
- `G2S_Link_Process(g2s_link)`
- `Telemetry_ProcessStep(g2s_link)`

So `CommTask` owns three things:

1. console command ingress from UART CLI
2. radio uplink processing from the G2S/LoRa path
3. one step of outbound telemetry transport progress

That third point is subtle and important. `TelemetryRadioTask` schedules telemetry production, but `CommTask` advances the transport state machine that actually sends queued frames.

#### Why `CommTask` owns these functions

These three functions all belong to the communication/control side of the system:

- `CommandParser_Process()` consumes bytes already captured by the UART driver
- `G2S_Link_Process()` checks radio receive state and handles inbound packets
- `Telemetry_ProcessStep()` advances one queued telemetry frame through radio/UART output

So `CommTask` is both the command-ingress owner and the communication-progress owner.

### 4.2 `TelemetryRadioTask`

Entrypoint:

- `StartTelemetryRadioTask()`

Loop behavior:

- marks heartbeat with `RTOS_MarkTaskAlive(RTOS_TASK_ID_TELEMETRY)`
- runs `RTOS_RunTelemetryRadioTaskCycle()`
- delays with `vTaskDelayUntil(...)`

Task period constant:

- `RTOS_TELEMETRY_TASK_PERIOD_MS = 10U`

This task is not sending telemetry directly to hardware. It is the producer-side scheduler for recurring telemetry and radio reporting work.

#### What `RTOS_RunTelemetryRadioTaskCycle()` does

This helper keeps several periodic schedules using `HAL_GetTick()`:

- every `2000 ms`
  - queue system status telemetry with `Telemetry_SendSystemStatusEx(...)`
- every `3000 ms`
  - queue a telemetry heartbeat with `Telemetry_SendHeartbeatEx(...)`
  - snapshot telemetry stats via `Telemetry_GetStats(...)`
  - snapshot logger stats via `Logger_GetStats(...)`
  - snapshot runtime telemetry counters via `RuntimeState_GetTelemetryCounters(...)`
  - log those stats to the console/logging path
- every `4000 ms`
  - read SX1278 version with `SX1278_ReadVersion(...)`
  - snapshot G2S stats with `G2S_Link_GetStats(...)`
  - log radio and link health

So `TelemetryRadioTask` owns:

- recurring telemetry production
- observability/reporting cadence
- periodic radio health reporting

It does not directly call UART HAL or SX1278 SPI transmit for telemetry frames. It just queues telemetry packets and emits diagnostic logs.

### 4.3 `HealthPowerTask`

Entrypoint:

- `StartHealthPowerTask()`

Loop behavior:

- runs `RTOS_RunHealthPowerTaskCycle()`
- delays with `vTaskDelayUntil(...)`

Task period constant:

- `RTOS_HEALTH_TASK_PERIOD_MS = 20U`

This is the system health supervisor. It owns watchdog decisions, ADC/IPMS sampling cadence, power-management event reporting, and actual low-power action handoff.

#### What `RTOS_RunHealthPowerTaskCycle()` does

This function is the densest runtime helper in the system. It performs several separate jobs.

##### 1. Task heartbeat and watchdog supervision

It marks `HealthPowerTask` alive, then checks whether the system-wide watchdog may be refreshed:

- `RTOS_WatchdogCanRefresh(now, &fault_mask)`

If all required tasks are healthy, it refreshes the independent watchdog:

- `HAL_IWDG_Refresh(&hiwdg)`

If not, it withholds the refresh and logs which tasks are stale through:

- `RTOS_LogWatchdogFaultMask(fault_mask)`

The current supervision model checks:

- `CommTask`
- `TelemetryRadioTask`
- `StorageLogTask`

`HealthPowerTask` is the watchdog owner, so it supervises the others rather than requiring a separate task to do that.

##### 2. IPMS event reporting

It calls:

- `SystemRuntime_ReportIpmsEvents()`

That function drains the internal IPMS event queue and mirrors those events into:

- logger output
- telemetry event packets

This makes the IPMS module internally queue-based, while the health task is the integration point that publishes those queued events outward.

##### 3. Apply queued IPMS mode/policy control requests

Commands do not directly mutate IPMS mode/policy state. Instead they enqueue control requests into `Runtime_State`.

`HealthPowerTask` drains them with:

- `RuntimeState_PopIpmsControlRequest(...)`

and applies them through:

- `IPMS_SetSimulationMode(...)`
- `IPMS_SetPolicyMode(...)`

This means the health task is the single owner of mutating IPMS control state.

##### 4. Heartbeat LED

Every `500 ms`, the task toggles:

- `LD2_HEARTBEAT_Pin`

So the visible heartbeat LED is owned here, not by `main.c`.

##### 5. ADC sampling and IPMS state-machine feed

Every `1000 ms`, the task:

- reads ADC health through `ADC_Monitor_GetData(...)`
- caches the latest valid sample into `RuntimeState_SetLatestAdcSample(...)`
- feeds battery voltage into the IPMS state machine via `IPMS_ProcessBatterySample(...)`

This is the critical bridge between ADC monitoring and power policy.

##### 6. Periodic ADC health telemetry/reporting

Every `5000 ms`, the task:

- reads the latest cached ADC sample from `RuntimeState_GetLatestAdcSample(...)`
- if valid:
  - queues ADC health telemetry with `Telemetry_SendADCHealthEx(...)`
  - logs measured voltages and temperature
- if invalid:
  - queues an ADC error telemetry event with `Telemetry_SendEventEx(...)`
  - logs a warning

So health telemetry is scheduled from here, not from the ADC driver itself.

##### 7. Execute pending low-power actions

If IPMS arms a low-power action, the task checks:

- `IPMS_GetPendingAction(&power_action)`

If one exists, it hands that action to:

- `SystemRuntime_ExecuteIpmsAction(...)`

using:

- `RuntimeResources_GetSystemRuntimeContext()`
- `RuntimeResources_GetSystemRuntimeHooks()`

After low-power entry/restore completes, the task resets heartbeat supervision with:

- `RTOS_ResetTaskHeartbeats(HAL_GetTick())`

That reset is important because entering STOP mode naturally creates a time gap that would otherwise look like task starvation to the watchdog supervision logic.

### 4.4 `StorageLogTask`

Entrypoint:

- `StartStorageLogTask()`

Loop behavior:

- marks heartbeat with `RTOS_MarkTaskAlive(RTOS_TASK_ID_STORAGE)`
- repeatedly calls `StorageService_ProcessNext(250U)`

This task is the single owner of SD/FatFs execution.

#### What `StorageService_ProcessNext(250U)` means architecturally

This one call hides the whole storage service runtime.

`StorageService_ProcessNext(...)` does three things in priority order:

1. service one explicit storage request if one is waiting
   - SD status
   - smoke test
2. otherwise service one queued log record
3. even if idle, evaluate whether buffered file data should be flushed

So `StorageLogTask` owns:

- command-driven SD request execution
- queued persistent logging
- mount/open/append/sync/rotation policy

No other task should be touching FatFs directly in the current architecture.

## 5. Shared Runtime Helpers

### 5.1 `RuntimeResources`

This module is simple but important. It stores the shared subsystem objects that are allocated once during boot and then reused everywhere else.

Examples:

- `CommTask` gets the G2S link handle from `RuntimeResources_GetG2SLink()`
- telemetry/radio reporting gets the radio and link handles from `RuntimeResources`
- `HealthPowerTask` gets `SystemRuntimeContext_t` and `SystemRuntimeHooks_t` from `RuntimeResources`

The point is to let tasks fetch stable subsystem objects without passing giant parameter bundles through every task entrypoint.

### 5.2 `RuntimeState`

This module holds the small amount of truly shared mutable runtime state:

- latest ADC sample
- telemetry TX complete/error counters
- queued IPMS control requests

It uses interrupt masking rather than RTOS mutexes for these tiny snapshots/counters because:

- some updates come from callbacks
- the data structures are small
- the access patterns are short

The current IPMS control request queue is implemented using the shared `FrameQueue_t` abstraction with `FRAME_QUEUE_FAIL_ON_FULL`, so command handlers can enqueue mode/policy changes and the health task can drain them safely.

## 6. ISR And Callback Handoff Model

The callback model in [main.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/main.c) is intentionally minimal.

### 6.1 UART RX complete callback

Function:

- `HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)`

It does two things:

1. forwards into the UART driver:
   - `UART_RxCpltCallback(huart)`
2. wakes the communication task:
   - `RTOS_NotifyCommTaskRxFromISR()`

So ISR work is:

- capture byte into UART driver state
- notify `CommTask`

`CommTask` later does the heavier work by running `CommandParser_Process()`.

### 6.2 ADC conversion complete callback

Function:

- `HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)`

It forwards to:

- `ADC_Monitor_ConvCpltCallback(hadc)`

So the ISR updates ADC-monitor-owned conversion state, while `HealthPowerTask` later reads that processed state and turns it into runtime decisions.

### 6.3 UART TX complete callback

Function:

- `HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)`

If this is the telemetry UART (`huart1`), it records runtime telemetry counters through:

- `RuntimeState_RecordTelemetryTxComplete()`

Then it forwards into the UART driver:

- `UART_TxCpltCallback(huart)`

So the callback both feeds observability counters and advances UART driver DMA state.

### 6.4 UART error callback

Function:

- `HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)`

If this is telemetry UART, it records:

- `RuntimeState_RecordTelemetryError()`

Then forwards to:

- `UART_ErrorCallback(huart)`

### 6.5 RTC wakeup callback

Function:

- `HAL_RTCEx_WakeUpTimerEventCallback(...)`

It simply calls:

- `IPMS_OnRtcWakeup()`

That means the callback records the wake source, but does not attempt to restore the whole runtime on the spot.

### 6.6 Button EXTI callback

Function:

- `HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)`

When the user button pin matches, it calls:

- `IPMS_OnButtonWakeup()`

Again, callback work is intentionally tiny: just mark the wake source.

## 7. Communication Architecture

The current communications architecture is split across command ingress and telemetry egress.

### 7.1 UART roles

- `USART2` is the CLI/logger console channel
- `USART1` is the telemetry UART channel

The UART driver owns:

- interrupt-driven RX buffering
- DMA-backed TX progression

Tasks do not manipulate HAL UART state directly.

### 7.2 Command ingress

There are two command sources:

- UART CLI bytes captured by the UART driver and parsed by `CommandParser_Process()`
- LoRa/G2S packets processed by `G2S_Link_Process()`

Both command sources converge on command-dispatch logic in the command modules.

Mode/policy changes that affect IPMS are not applied directly in the command path; they are queued through `RuntimeState_QueueIpmsControlRequest(...)` and later applied by `HealthPowerTask`.

### 7.3 Telemetry egress

Telemetry has two conceptual parts:

1. producers
   - system status
   - heartbeat
   - ADC health
   - event packets
   - command ACK packets
2. transport owner
   - `Telemetry_ProcessStep()`

Most tasks only queue telemetry packets. `CommTask` is the task that repeatedly calls `Telemetry_ProcessStep()` and therefore owns actual frame-progress through radio/UART downlink.

The current default downlink mode configured in `main()` is:

- `TELEM_DOWNLINK_RADIO_WITH_UART_MIRROR`

So telemetry tries to use radio as the primary path while also mirroring to the telemetry UART.

## 8. Power-Management Architecture

The power-management path is split between decision logic and execution logic.

### 8.1 Decision layer: `IPMS`

`IPMS.c` owns:

- battery-state evaluation
- power-state transitions
- low-power action arming
- IPMS event generation

It does not directly enter sleep or stop mode.

### 8.2 Integration layer: `HealthPowerTask`

`HealthPowerTask` owns:

- feeding ADC battery samples into IPMS
- draining queued IPMS control requests
- draining and reporting queued IPMS events
- checking whether IPMS has armed an action

### 8.3 Execution layer: `System_Runtime`

`SystemRuntime_ExecuteIpmsAction(...)` in [System_Runtime.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/System_Runtime.c) turns an IPMS action into actual MCU behavior.

That function is responsible for:

- arming RTC wakeup through `IPMS_ArmRtcWakeup(...)`
- preparing peripherals for low power
- entering `HAL_PWR_EnterSLEEPMode(...)` or `HAL_PWR_EnterSTOPMode(...)`
- recording wakeup with `IPMS_RecordWakeup(...)`
- doing minimal or full restore depending on context
- re-sampling ADC/IPMS after STOP wake to determine whether another low-power chunk is needed

So the current architecture is intentionally layered:

- IPMS decides
- health task supervises and triggers
- system runtime executes HAL low-power entry/restore

## 9. Storage Architecture

Storage is designed as a single-owner task model.

### 9.1 Producer side

Producers do not call FatFs directly.

Examples:

- `Logger` queues log lines via `StorageService_EnqueueLogLine(...)`
- command handlers request SD status or smoke tests via `StorageService_RequestSdStatus(...)` and `StorageService_RequestSmokeTest(...)`

### 9.2 Owner side

`StorageLogTask` owns:

- command queue draining
- log queue draining
- SD detect/init/mount behavior
- active log file open/append/flush/rotate behavior

So the architecture is:

- producers enqueue or request
- `StorageLogTask` performs actual media and filesystem work

That prevents multiple tasks from racing through FatFs.

## 10. Watchdog Supervision

The watchdog strategy is centralized in `HealthPowerTask`.

The helper functions are:

- `RTOS_MarkTaskAlive(...)`
- `RTOS_ResetTaskHeartbeats(...)`
- `RTOS_WatchdogCanRefresh(...)`
- `RTOS_LogWatchdogFaultMask(...)`

The supervision model is timestamp-based:

- each key task updates its last alive time
- `HealthPowerTask` checks whether they have updated recently enough
- only then does it refresh `IWDG`

Timeouts currently used:

- `CommTask`: `250 ms`
- `TelemetryRadioTask`: `250 ms`
- `StorageLogTask`: `1000 ms`

There is also a startup grace window:

- `RTOS_WATCHDOG_STARTUP_GRACE_MS = 3000U`

This means the watchdog is intended to reflect real system liveness, not just cosmetic periodic refreshes.

## 11. Mutexes And Synchronization

The current architecture uses a light synchronization strategy.

### `ConsoleMutex`

Created in `MX_FREERTOS_Init()`, used by the logger to serialize console/log formatting and output submission.

### `StorageMutex`

Created in `MX_FREERTOS_Init()`.

In the current architecture, the stronger protection model for storage is task ownership rather than widespread external locking. The single-owner `StorageLogTask` model is the main protection against storage concurrency.

### Critical sections in shared-state modules

Modules like `Runtime_State`, `Telemetry`, and `IPMS` use short interrupt-masking critical sections around:

- tiny counters
- queue operations
- snapshot copies

That keeps shared state small and local rather than turning everything into mutex-heavy RTOS code.

## 12. Current Reading Guide

If you want to understand the runtime in code order, the best reading sequence is:

1. [main.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/main.c)
   Boot, module init, callbacks, scheduler handoff.

2. [freertos.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/freertos.c)
   Task creation, task entrypoints, watchdog supervision, per-task runtime-cycle helpers.

3. [Runtime_Resources.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Runtime_Resources.c)
   Shared resource object ownership.

4. [Runtime_State.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Runtime_State.c)
   Small cross-task shared state and queues.

5. [Telemetry.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Telemetry.c)
   Telemetry frame queueing and transport state machine.

6. [G2S_Link.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/G2S_Link.c) and [Command_Parser.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Command_Parser.c)
   Command ingress from radio and UART.

7. [IPMS.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/IPMS.c)
   Battery/power state machine and action arming.

8. [System_Runtime.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/System_Runtime.c)
   Low-power execution and restore integration.

9. [Storage_Service.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Storage_Service.c)
   Single-owner storage runtime.

## 13. Summary

The current architecture is a task-owned FreeRTOS runtime with clear subsystem ownership:

- `main()` boots and binds resources
- `freertos.c` creates tasks and defines ownership
- `CommTask` owns command ingress and communication progress
- `TelemetryRadioTask` owns recurring telemetry/radio production and reporting
- `HealthPowerTask` owns watchdog, ADC/IPMS, health reporting, and low-power trigger logic
- `StorageLogTask` owns all SD/FatFs execution
- `System_Runtime` bridges IPMS actions into HAL low-power behavior
- HAL callbacks remain minimal and only hand work into subsystem or task-owned state

That is the architecture the rest of the project now builds on.
