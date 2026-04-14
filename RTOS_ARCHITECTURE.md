# Phase 5 RTOS Architecture Plan

This document defines the target FreeRTOS architecture for the CubeSat Flight Computer Emulator and the migration path from the current mixed superloop / RTOS state.

The goal is not to "sprinkle tasks" onto the current firmware. The goal is to:

- preserve the working module boundaries already built in earlier phases
- move runtime ownership from the large superloop in [main.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/main.c) into real tasks
- keep ISR work minimal
- avoid hidden concurrency bugs
- keep a clear validation path against the current firmware behavior

## 1. Current Reality

Right now the project is in a half-migrated state:

- [main.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/main.c) contains the real application logic:
  - command parsing
  - telemetry processing
  - G2S/radio polling
  - ADC/IPMS sampling
  - watchdog refresh
  - periodic reporting
  - low-power action execution
- [freertos.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/freertos.c) creates placeholder tasks that mostly do nothing
- `osKernelStart()` is called before the large `while(1)` loop in `main.c`

That means the firmware is architecturally inconsistent:

- the superloop contains the real behavior
- the RTOS scheduler is also started

Phase 5 fixes that by making FreeRTOS the actual runtime owner and simplifying `main.c` down to:

- peripheral init
- module init
- RTOS init
- scheduler start
- HAL callback bridges only

## 2. Design Principles

The migration should follow these rules:

- one subsystem should have one clear runtime owner
- tasks should be grouped by responsibility, not by "whatever code was nearby"
- ISRs should only capture/flag/notify
- drivers that already own transport logic should keep that ownership
- use queues, notifications, and mutexes only where they solve a real concurrency problem
- prefer single-owner task models over multi-task shared mutation

## 3. Final Task Model

The target runtime should use four application tasks plus the FreeRTOS idle task.

### 3.1 `CommTask`

Purpose:

- own command ingress and routing

Responsibilities:

- run `CommandParser_Process()` for UART CLI input
- run `G2S_Link_Process()` for LoRa command ingress
- own shared command dispatch timing and routing behavior
- emit command-related telemetry events or ACKs through existing app APIs

Why this grouping:

- both UART CLI and G2S radio packets are command sources
- the shared command path already exists in [Command_List.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Command_List.c)
- grouping command ingress in one task avoids multi-task command execution races

### 3.2 `TelemetryRadioTask`

Purpose:

- own telemetry transport scheduling and radio status housekeeping

Responsibilities:

- run `Telemetry_Process()`
- schedule heartbeat and system status telemetry
- publish telemetry stats periodically
- perform periodic SX1278 status/version checks
- own non-command radio housekeeping if still needed in polling mode

Why this grouping:

- telemetry and radio transport are both outbound observability/communications concerns
- the current superloop already couples telemetry timing and radio reporting

### 3.3 `HealthPowerTask`

Purpose:

- own sensor-health sampling and IPMS policy decisions

Responsibilities:

- periodically consume ADC monitor snapshots
- update cached health state
- call `IPMS_ProcessBatterySample()`
- queue ADC health telemetry/events
- handle power-related status reporting
- request low-power actions when IPMS arms them

Why this grouping:

- ADC health and IPMS are already tightly related
- battery-driven decisions should come from one place, not several tasks

### 3.4 `StorageLogTask`

Purpose:

- own persistent logging and SD/FatFs interactions

Responsibilities:

- mount/unmount the SD card
- perform file creation/open/append/sync/rotation
- drain buffered log/storage requests from a queue
- report SD status and storage failures

Why this grouping:

- FatFs becomes much safer if one task owns all file I/O
- this avoids random file access from command handlers, telemetry paths, or health logic

## 4. Recommended Priorities

Use a simple and explainable priority model first.

Recommended initial priorities:

- `HealthPowerTask`: `AboveNormal`
- `CommTask`: `Normal`
- `TelemetryRadioTask`: `Normal`
- `StorageLogTask`: `BelowNormal`

Rationale:

- health/power decisions affect watchdog safety and low-power entry, so they should not starve
- command and telemetry work are important but not hard real-time in the current design
- SD logging is the most likely to block and should be lower priority

Do not add more priorities unless measurements show a real need.

## 5. Timer / Wake Strategy

Do not create a separate FreeRTOS software timer for every periodic action immediately.

For the first migration:

- use `vTaskDelayUntil()` in each task for deterministic periodic work
- reserve notifications for event-driven wakeups

Recommended cadences based on current `main.c` behavior:

- `CommTask`: short loop, e.g. `5-10 ms`
- `HealthPowerTask`: `1000 ms`
- `TelemetryRadioTask`:
  - heartbeat/status scheduling internally at `2000/3000/4000 ms`
  - short service loop for `Telemetry_Process()`
- `StorageLogTask`: event-driven with periodic flush, e.g. `250-1000 ms`

## 6. ISR-To-Task Handoff Model

Keep ISR work minimal.

### UART RX ISR

Current behavior to preserve:

- RX complete callback pushes one byte into the UART RX ring buffer
- re-arms `HAL_UART_Receive_IT()`

Phase 5 behavior:

- keep that in the UART driver
- optionally notify `CommTask` that new RX data is available

### UART TX Complete / Error ISR

Current behavior to preserve:

- remain inside [UART_Driver.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Hardware/Src/UART_Driver.c)
- do not move UART DMA state handling into tasks

### ADC DMA Complete ISR

Current behavior to preserve:

- callback updates the latest ADC snapshot in `ADC_Monitor`

Phase 5 behavior:

- keep callback short
- optionally notify `HealthPowerTask` that fresh ADC data is available

### RTC Wakeup ISR

Current behavior to preserve:

- callback only records that RTC wake occurred through IPMS

### GPIO EXTI Button ISR

Current behavior to preserve:

- callback only records button wake through IPMS

## 7. Synchronization Plan

Synchronization should be deliberate and minimal.

### 7.1 Logger

Current issue:

- any task could eventually call `Logger_Info()` / `Logger_Warn()` / `Logger_Error()`

Plan:

- add one logger/output mutex around formatting + console write submission
- keep the UART driver as the transport owner underneath

Alternative future improvement:

- convert logger into a message queue + dedicated log consumer task

For first migration, a mutex is enough.

### 7.2 UART Driver

Current strength:

- already protects ISR/shared TX state internally

Plan:

- keep UART transport ownership in the driver
- allow multi-task callers only if logger/command/telemetry ownership rules are clear
- avoid adding a separate UART mutex unless measurements show a need

### 7.3 Telemetry

Current issue:

- several modules can enqueue telemetry packets

Plan:

- keep frame queue ownership in [Telemetry.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Telemetry.c)
- add a telemetry mutex only if multiple tasks directly enqueue and races show up
- preferred design is to let `TelemetryRadioTask` own `Telemetry_Process()` while other tasks only enqueue packets

### 7.4 Storage / FatFs

Plan:

- one task should own all filesystem access
- no direct FatFs calls from command handlers or health logic once migrated
- command handlers should enqueue storage requests instead of touching files directly

This is the most important single-owner rule in the whole RTOS migration.

## 8. Watchdog Strategy

Do not let every task refresh the watchdog independently.

Target strategy:

- each critical task updates a heartbeat counter or timestamp
- one watchdog owner checks that all required tasks are making progress
- only then refresh `IWDG`

Recommended watchdog owner:

- `HealthPowerTask`

Why:

- it already wakes periodically
- it is semantically close to system health supervision

Required heartbeats:

- `CommTask`
- `TelemetryRadioTask`
- `HealthPowerTask`
- `StorageLogTask` when storage is enabled

If any required task stalls beyond its allowed window:

- watchdog refresh is withheld
- reset becomes meaningful rather than cosmetic

## 9. What Moves Out Of `main.c`

The following runtime work should leave `main.c` during Phase 5:

- `CommandParser_Process()`
- `Telemetry_Process()`
- `G2S_Link_Process()`
- periodic heartbeat LED logic
- periodic ADC/IPMS sampling
- periodic telemetry queue/report logic
- periodic radio status logging
- low-power action polling and execution trigger

What should stay in `main.c`:

- HAL / CubeMX init
- module init
- task creation
- scheduler start
- HAL callback forwarding functions
- `SystemClock_Config()`
- `Error_Handler()`

## 10. Migration Slices

Do not migrate everything in one patch.

### Slice 1: Freeze The Reference Behavior

Before moving any runtime logic:

- document the exact periodic behaviors currently implemented in `main.c`
- use that as the regression checklist

Reference timings from current code:

- LED heartbeat: `500 ms`
- ADC/IPMS sample: `1000 ms`
- system status telemetry queue: `2000 ms`
- telemetry stats report: `3000 ms`
- radio status report: `4000 ms`
- ADC health report: `5000 ms`

### Slice 2: Build The Real Task Skeleton

Rewrite [freertos.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/freertos.c) so the task names and responsibilities match the final model:

- `CommTask`
- `TelemetryRadioTask`
- `HealthPowerTask`
- `StorageLogTask`

At this stage, leave `main.c` superloop active only if scheduler start is disabled for the transition branch.

### Slice 3: Migrate `HealthPowerTask`

Move into the task:

- ADC sample polling
- runtime ADC cache updates
- `IPMS_ProcessBatterySample()`
- periodic ADC report/event logic
- watchdog supervision start

Why first:

- smallest ownership boundary
- lowest coupling to CLI and storage

### Slice 4: Migrate `TelemetryRadioTask`

Move into the task:

- `Telemetry_Process()`
- periodic system status queueing
- heartbeat queueing
- telemetry stats logging
- radio version/status reporting

### Slice 5: Migrate `CommTask`

Move into the task:

- `CommandParser_Process()`
- `G2S_Link_Process()`
- command-centric reporting

### Slice 6: Migrate `StorageLogTask`

Move into the task:

- SD mount/write/flush logic
- future persistent logging service

This slice should also replace any remaining direct FatFs access in command handlers with queue-driven requests.

### Slice 7: Remove Superloop Logic

Once all behavior is task-owned:

- remove the large application body from `main.c`
- keep only scheduler startup and callbacks

## 11. Shared State Cleanup

These shared runtime values should be reduced or re-owned during migration:

- ADC cache state
- telemetry ISR counters
- radio reporting state

Preferred end state:

- task-owned state where possible
- shared state only when several tasks truly need read access
- clearly documented ownership for each shared object

## 12. Regression Checklist

Phase 5 is not done until all pre-RTOS demos still work.

Required regression checks:

- CLI over `USART2` still works
- logger output still appears on `USART2`
- telemetry still transmits on `USART1`
- ADC monitor still produces valid data
- IPMS still transitions deterministically
- sleep/stop wake path still restores runtime correctly
- G2S command flow still works
- SD smoke test still works once storage task is active

## 13. Acceptance Criteria

Phase 5 is complete when:

- tasks have clear ownership and bounded responsibilities
- `main.c` is no longer the hidden runtime owner
- no placeholder RTOS tasks remain
- no hidden concurrency exists on shared buffers or storage access
- watchdog refresh reflects whole-system health
- earlier phase demos still work after migration

## 14. Deliverables

The expected Phase 5 deliverables should be:

1. this architecture document
2. rewritten [freertos.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/freertos.c) with real task ownership
3. simplified [main.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/main.c)
4. queue/mutex/task-notification additions only where justified
5. updated testing docs for RTOS regression

## 15. Recommended Immediate Next Step

Before any Phase 5 code changes:

- create a small parity checklist from the current `main.c` superloop
- then rewrite `freertos.c` task definitions to match the model in this document

That gives us a clean migration target without continuing the current half-RTOS / half-superloop state.
