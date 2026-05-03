# Phase 5 FreeRTOS Regression Guide

This guide is the bench checklist for the Phase 5 migration from the old superloop runtime to the current FreeRTOS-owned runtime.

The goal is to prove two things:

- `main.c` is no longer the hidden runtime owner
- the same functional behavior from earlier phases still works after moving ownership into RTOS tasks

## Current RTOS Ownership

The runtime is now split like this:

- `CommTask`
  - owns `CommandParser_Process()`
  - owns `G2S_Link_Process()`
- `TelemetryRadioTask`
  - owns `Telemetry_Process()`
  - owns periodic heartbeat/system-status/radio reporting
- `HealthPowerTask`
  - owns ADC sampling consumption
  - owns IPMS sampling/event draining/action execution
  - owns watchdog refresh
- `StorageLogTask`
  - owns storage service requests
  - owns SD/FatFs access used by `SD_STATUS` and `SD_TEST`

`main.c` should now be read as:

- peripheral init
- module init
- RTOS object init
- scheduler start
- HAL callback bridges only

## What To Verify First

Before checking behavior, verify the architecture on code inspection:

- [main.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/main.c) has no large application superloop after `osKernelStart()`
- [freertos.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/freertos.c) contains the real runtime ownership
- [Command_List.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Command_List.c) no longer calls FatFs/BSP SD APIs directly for SD command handling
- [Storage_Service.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Storage_Service.c) is the single-owner storage path for SD command requests

## Bench Setup

Use the same setup from earlier phases:

- Nucleo-F446RE board
- USB/serial console on `USART2`
- telemetry receiver on `USART1` if available
- SX1278 module attached if testing radio command flow
- microSD card inserted if testing storage commands

Recommended terminal setup:

- one terminal for `USART2` console/logging
- one capture for telemetry if available

## RTOS Regression Order

Run tests in this order so failures are easy to localize.

### 1. Boot And Scheduler Ownership

Expected:

- startup logs appear on `USART2`
- no hang after `osKernelStart()`
- LED heartbeat continues

What this proves:

- scheduler started
- `HealthPowerTask` is alive
- logger still works in RTOS context

### 2. CLI Path Through `CommTask`

Run:

- `PING`
- `GET_ADC`
- `PWR_STATUS`

Expected:

- commands respond on `USART2`
- no parser lockup
- command ACK telemetry still queues

What this proves:

- UART RX ISR path is still feeding the parser
- `CommTask` owns CLI processing correctly

### 3. Telemetry Path Through `TelemetryRadioTask`

Observe for several seconds.

Expected:

- system status telemetry continues periodically
- heartbeat telemetry continues periodically
- telemetry/radio stats logs still appear

What this proves:

- `TelemetryRadioTask` is the active transport scheduler
- `Telemetry.c` remains the transport owner underneath

### 4. Health/IPMS Path Through `HealthPowerTask`

Run:

- `GET_ADC`
- `PWR_STATUS`
- `PWR_SIM ECLIPSE`
- `PWR_SIM SUNLIGHT`

Expected:

- ADC values stay valid
- IPMS state transitions remain deterministic
- power events still log and emit telemetry

What this proves:

- ADC/IPMS logic survived the RTOS migration
- low-power policy stayed owned by `HealthPowerTask`

### 5. SD Path Through `StorageLogTask`

Run:

- `SD_STATUS`
- `SD_TEST`

Expected:

- the commands still work
- card status/smoke test responses still appear on the console
- no direct FatFs race symptoms show up while other tasks continue running

What this proves:

- storage ownership moved out of command handlers
- `StorageLogTask` owns SD/FatFs request execution

### 6. Radio Command Path Through `CommTask`

Run the Phase 2 radio checklist from [PHASE2_TESTING.md](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/PHASE2_TESTING.md).

Expected:

- command packets still execute
- ACK/NACK still return

What this proves:

- G2S command ingress still works after RTOS migration

## Failure Patterns To Watch

- CLI works only before scheduler start:
  - likely task ownership or scheduler start issue
- telemetry stops but CLI still works:
  - likely `TelemetryRadioTask` not progressing
- ADC and IPMS stop updating:
  - likely `HealthPowerTask` stalled
- `SD_STATUS` or `SD_TEST` hangs:
  - likely storage service queue/task issue
- heavily interleaved or garbled logs:
  - investigate logger serialization and task write pressure

## What Is Still Not Fully Closed

Phase 5 is much closer now, but these items still need follow-up:

- whole-system watchdog supervision based on task heartbeats
- further cleanup of shared globals into clearer task-owned state
- optional use of task notifications for event-driven wakeups instead of short polling delays

## Acceptance Snapshot

Phase 5 should be considered functionally complete only when:

- the tasks have clear ownership and bounded responsibilities
- `main.c` is init + scheduler + callbacks only
- no placeholder tasks remain
- SD/FatFs access is no longer triggered directly from command handlers
- the earlier phase demos still pass under FreeRTOS
