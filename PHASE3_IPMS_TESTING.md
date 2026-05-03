# Phase 3 IPMS And Runtime Validation Guide

This document captures the current Phase 3 implementation context for:

- the Intelligent Power Management System (`IPMS`)
- the runtime low-power helpers in [System_Runtime.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/System_Runtime.c)
- the expected bench setup and validation flow to prove the power-state machine, sleep/stop path, and restore behavior work as intended

It is meant to answer four practical questions:

1. what the current firmware is doing
2. how the power-state machine is designed
3. how low-power entry and wakeup are orchestrated
4. how to test the full path safely on hardware

## 1. Current Firmware Scope

Phase 3 is implemented at the source/runtime integration level with these pieces active:

- explicit IPMS power states
- battery-driven power policy decisions based on ADC battery voltage
- hysteresis and multi-sample confirmation windows
- simulation modes for bench testing
- policy modes that gate whether sleep/stop is actually allowed
- RTC wakeup support
- user-button wake detection
- runtime prepare/restore helpers for sleep and stop entry
- logger and telemetry events for power transitions and wakeups

The current design is intentionally conservative:

- boot policy is `MONITOR_ONLY`
- simulation mode defaults to `AUTO`
- no sleep or stop entry occurs unless you explicitly change policy at runtime

That keeps normal bring-up safe while still allowing controlled power-management testing.

## 2. Files That Matter

Core Phase 3 files:

- [IPMS.h](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Inc/IPMS.h)
- [IPMS.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/IPMS.c)
- [System_Runtime.h](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Inc/System_Runtime.h)
- [System_Runtime.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/System_Runtime.c)
- [main.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/main.c)
- [Command_List.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Command_List.c)
- [Telemetry.h](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Inc/Telemetry.h)
- [stm32f4xx_it.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/stm32f4xx_it.c)

Supporting modules involved during low-power entry/restore:

- [ADC_Monitor.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Hardware/Src/ADC_Monitor.c)
- [UART_Driver.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Hardware/Src/UART_Driver.c)
- [SX1278.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Hardware/Src/SX1278.c)
- [OLED_Display.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Hardware/Src/OLED_Display.c)
- [I2C_Bus.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Hardware/Src/I2C_Bus.c)

## 3. Power-State Machine

The IPMS state machine is explicit and deterministic.

Current states:

- `NORMAL`
- `LOW_POWER_WARNING`
- `SLEEP_CANDIDATE`
- `STOP_CANDIDATE`
- `RECOVERY`

### Default threshold policy

From [IPMS.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/IPMS.c), the default configuration is:

- warning enter: `3.75 V`
- warning exit: `3.85 V`
- sleep enter: `3.55 V`
- sleep exit: `3.70 V`
- stop enter: `3.35 V`
- stop exit: `3.50 V`
- recovery exit: `3.95 V`

### Sample confirmation windows

The firmware does not transition immediately on one sample.

Required consecutive samples:

- warning: `2`
- sleep candidate: `3`
- stop candidate: `4`
- recovery/normal path: `2`

This is there to prevent noisy ADC readings from causing rapid power-state oscillation.

### Hysteresis behavior

The system uses separate enter and exit thresholds. That means:

- a low-voltage state is entered at one voltage
- it is not exited until voltage has recovered past a higher threshold

This is one of the main reasons the policy should be stable on real hardware.

## 4. Simulation Modes

IPMS supports three simulation modes:

- `AUTO`
- `SUNLIGHT`
- `ECLIPSE`

Meaning:

- `AUTO`: use the measured battery voltage from ADC
- `SUNLIGHT`: ignore measured voltage and force the effective battery value to a configured healthy level
- `ECLIPSE`: ignore measured voltage and force the effective battery value to a configured low level

Default simulated values:

- sunlight: `4.05 V`
- eclipse: `3.25 V`

These are useful because they let you validate state transitions without needing to physically vary the battery rail on the bench.

## 5. Policy Modes

Policy mode decides whether IPMS only observes power conditions or is allowed to act.

Current modes:

- `MONITOR_ONLY`
- `ENABLE_SLEEP`
- `ENABLE_SLEEP_AND_STOP`

Meaning:

- `MONITOR_ONLY`: transitions and events are tracked, but no low-power entry is armed
- `ENABLE_SLEEP`: `SLEEP_CANDIDATE` may trigger a sleep action, but stop is not allowed
- `ENABLE_SLEEP_AND_STOP`: both sleep and stop actions may be armed

This split is important for safe bring-up. It lets us verify state logic before letting the firmware enter low-power modes.

## 6. How IPMS Is Fed At Runtime

The current sampling path in [main.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/main.c) is:

`ADC DMA -> ADC_Monitor_GetData() -> latest battery voltage -> IPMS_ProcessBatterySample()`

The main loop takes a fresh ADC health snapshot approximately every `1000 ms`.

If ADC data is available:

- the runtime ADC cache is updated
- the latest ADC sample becomes valid for periodic reporting
- `IPMS_ProcessBatterySample(adc_sample.battery_voltage, now)` runs

That means IPMS decision-making is intentionally centralized in the superloop, not interrupt context.

## 7. Runtime Helper Ownership

The helpers moved out of `main.c` into [System_Runtime.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/System_Runtime.c) are there to keep the entrypoint readable and to isolate low-power orchestration logic.

### `SystemRuntime_ReportIpmsEvents()`

Purpose:

- drain the IPMS event queue
- print human-readable logger messages
- emit corresponding telemetry events

This is the main bridge between IPMS internal state changes and external observability.

### `SystemRuntime_PrepareForLowPower()`

Purpose:

- stop ADC monitoring
- place the SX1278 into sleep mode
- clear the cached “latest ADC valid” flag

This is the pre-entry cleanup step used before both sleep and stop.

### `SystemRuntime_RestoreAfterSleep()`

Purpose:

- reinitialize and restart the ADC monitor
- restart SX1278 continuous receive mode

This is the lighter restore path because normal sleep does not require full peripheral reinitialization.

### `SystemRuntime_RestoreAfterStop()`

Purpose:

- reconfigure clocks
- reinitialize DMA and key peripherals
- rebind UART driver channels
- restart ADC monitor
- reinitialize I2C bus and OLED
- reinitialize the SX1278 radio

This is the heavier restore path because stop mode requires more state recovery.

### `SystemRuntime_ExecuteIpmsAction()`

Purpose:

- arm RTC wakeup
- prepare for low power
- refresh watchdog
- suspend tick
- enter sleep or stop
- restore runtime state after wake
- disarm RTC wakeup
- record wake source through IPMS

This is the core execution bridge between “IPMS decided an action is needed” and “the MCU actually entered low power and came back.”

## 8. Wake Sources

Current wake sources recorded by IPMS:

- `RTC`
- `BUTTON`
- `UNKNOWN`

Current implementation:

- RTC wakeup is handled through `HAL_RTCEx_WakeUpTimerEventCallback()`
- button wake is handled through `HAL_GPIO_EXTI_Callback()` on `B1_USER_BUTTON_Pin`
- if the actual wake source is not directly known at restore time, IPMS infers it from the stored flags

## 9. Current Command Interface For Phase 3

These commands are already implemented:

- `PWR_STATUS`
- `PWR_SIM <AUTO|SUNLIGHT|ECLIPSE>`
- `PWR_POLICY <MONITOR|SLEEP|FULL>`

### `PWR_STATUS`

Prints:

- current power state
- simulation mode
- policy mode
- pending action
- measured battery voltage
- effective battery voltage
- last wake source
- transition count

### `PWR_SIM`

Changes simulation mode for bench testing.

Examples:

- `PWR_SIM AUTO`
- `PWR_SIM SUNLIGHT`
- `PWR_SIM ECLIPSE`

### `PWR_POLICY`

Changes whether low-power actions are allowed.

Examples:

- `PWR_POLICY MONITOR`
- `PWR_POLICY SLEEP`
- `PWR_POLICY FULL`

Aliases already supported:

- `MONITOR_ONLY`
- `ENABLE_SLEEP`
- `ENABLE_SLEEP_AND_STOP`

## 10. Logger And Telemetry Expectations

During a healthy Phase 3 test run, you should see:

- startup log showing IPMS default thresholds and modes
- state transition logs when simulation mode or battery condition drives a change
- telemetry event logs for:
  - `POWER_STATE`
  - `POWER_MODE`
  - `POWER_WAKEUP`

The system is designed so logger output on `USART2` and binary telemetry on `USART1` both reflect the same power-management events.

## 11. Safe Bench Setup

Recommended setup:

- Nucleo-F446RE board
- USB console on `USART2`
- debugger or serial terminal available
- battery-sense input wired as expected for ADC monitor, or use simulation mode
- user button available for wake testing
- RTC clock sources enabled as already configured in CubeMX

For first validation, use simulation mode rather than forcing real battery voltage changes.

This is the lowest-risk path because it proves the logic, eventing, and restore flow before you test with real power variation.

## 12. Test Strategy Overview

Recommended order:

1. baseline status and no-action verification
2. simulation-mode transitions in `MONITOR_ONLY`
3. sleep path in `ENABLE_SLEEP`
4. stop path in `ENABLE_SLEEP_AND_STOP`
5. button wake verification
6. runtime restore verification after stop

## 13. Test 1: Baseline Bring-Up

Goal:

- confirm IPMS initializes cleanly and does not surprise the board on boot

Procedure:

1. flash the firmware
2. open `USART2`
3. let the board boot normally
4. run `PWR_STATUS`

Expected:

- startup log shows IPMS initialized successfully
- policy is `MONITOR_ONLY`
- simulation mode is `AUTO`
- power state is normally `NORMAL` unless measured battery is already low
- no immediate low-power entry occurs

Pass criteria:

- CLI remains responsive
- no unexpected sleep/stop entry
- `PWR_STATUS` returns valid state information

## 14. Test 2: Simulation-Only State Transitions

Goal:

- prove the state machine, thresholds, and event logging work before enabling real low-power actions

Procedure:

1. ensure policy is monitor-only:
   - `PWR_POLICY MONITOR`
2. set simulated healthy condition:
   - `PWR_SIM SUNLIGHT`
3. wait a few seconds
4. run `PWR_STATUS`
5. set simulated low-power condition:
   - `PWR_SIM ECLIPSE`
6. wait several sample intervals
7. run `PWR_STATUS` again

Expected:

- in `SUNLIGHT`, effective voltage should look healthy and state should move toward `NORMAL`
- in `ECLIPSE`, effective voltage should eventually move toward `STOP_CANDIDATE`
- transitions should not happen instantly if sample windows are still being accumulated
- logger should report IPMS mode and state events

Pass criteria:

- deterministic state changes
- no oscillation between states
- telemetry/log events match command-driven simulation changes

## 15. Test 3: Sleep Entry Path

Goal:

- prove `ENABLE_SLEEP` allows sleep entry but not stop

Procedure:

1. set:
   - `PWR_POLICY SLEEP`
2. set:
   - `PWR_SIM ECLIPSE`
3. wait for enough sample windows to drive the state machine
4. observe logger output
5. allow the board to enter sleep and wake via RTC
6. after wake, run `PWR_STATUS`

Expected:

- IPMS transitions through low-power warning and sleep candidate
- a sleep action is armed
- `SystemRuntime_ExecuteIpmsAction()` enters sleep
- RTC wake brings the board back
- `SystemRuntime_RestoreAfterSleep()` restarts ADC and radio RX path

Pass criteria:

- board wakes after the configured sleep duration
- CLI becomes responsive again after wake
- logger shows wakeup event
- `PWR_STATUS` shows recovery state and updated wake statistics

## 16. Test 4: Stop Entry Path

Goal:

- prove full stop-mode restore path works

Procedure:

1. set:
   - `PWR_POLICY FULL`
2. set:
   - `PWR_SIM ECLIPSE`
3. wait for state machine to drive toward stop candidate
4. observe stop entry and wake by RTC
5. after wake, verify:
   - logger
   - CLI
   - telemetry
   - ADC updates
   - OLED state
   - radio reinitialization logs

Expected:

- `STOP_CANDIDATE` state is reached after enough samples
- stop action is armed
- `SystemRuntime_PrepareForLowPower()` runs before entry
- `SystemRuntime_RestoreAfterStop()` reinitializes clocks/peripherals and rebinds runtime state
- logger prints the stop restore summary

Pass criteria:

- board reliably wakes from stop
- console UART still works after wake
- ADC monitor resumes
- radio restore status is logged
- no hard fault, lockup, or watchdog reset

## 17. Test 5: Button Wake Verification

Goal:

- prove external wake bookkeeping is visible and correct

Procedure:

1. arm a sleep or stop path using `PWR_POLICY`
2. trigger low-power entry with `PWR_SIM ECLIPSE`
3. during low-power state, use the user button as the wake source if supported by your current board config
4. after wake, run `PWR_STATUS`

Expected:

- wake source becomes `BUTTON`
- IPMS wake event is logged
- button wake counter increments

Important note:

Depending on the exact EXTI/wakeup hardware configuration, RTC may still be the practical primary wake path during early bring-up. If button wake does not behave as expected, verify the board-level wake capability separately from the IPMS logic.

## 18. Test 6: Recovery And Hysteresis Verification

Goal:

- prove the system does not thrash between states

Procedure:

1. start in `PWR_POLICY MONITOR`
2. toggle between:
   - `PWR_SIM ECLIPSE`
   - `PWR_SIM SUNLIGHT`
3. observe repeated `PWR_STATUS` output and logger transitions

Expected:

- transitions only happen after required sample windows
- returning to healthy state should pass through `RECOVERY`
- a low-power state should not clear immediately on one good sample

Pass criteria:

- no rapid back-and-forth oscillation
- logger output clearly shows ordered transitions

## 19. What Good Logs Look Like

Healthy patterns include messages like:

- `IPMS state: NORMAL -> LOW_POWER_WARNING ...`
- `IPMS state: LOW_POWER_WARNING -> SLEEP_CANDIDATE ...`
- `IPMS action: ENTER_SLEEP duration=...`
- `IPMS wakeup: source=RTC next_state=RECOVERY ...`
- `IPMS stop restore: UART=... ADC_INIT=... RADIO=...`

If you see those in the expected order, the Phase 3 orchestration path is doing the right kind of work.

## 20. Common Failure Patterns

If state never changes:

- ADC battery sample path may not be producing valid data
- simulation mode may still be `AUTO`
- sample windows may not have elapsed yet

If simulation mode changes but no logs appear:

- verify `SystemRuntime_ReportIpmsEvents()` is still being called from the main loop

If the board sleeps/stops but never wakes:

- RTC wakeup timer path may not be configured correctly
- wakeup IRQ may not be firing
- RTC clock source may be unstable

If the board wakes but runtime is partially broken:

- stop restore sequence may be incomplete for a peripheral you are using
- the affected peripheral likely needs an explicit restore hook

If the watchdog resets the board:

- low-power duration or restore path may be too long without a refresh
- or wake/restore never completed as expected

## 21. Acceptance Checklist

Phase 3 bench validation is closed when all of the following are true:

- state transitions are deterministic
- hysteresis prevents rapid oscillation
- `PWR_STATUS`, `PWR_SIM`, and `PWR_POLICY` behave correctly
- sleep wakeup path is repeatable
- stop wakeup path is repeatable
- restore after wake returns the system to a usable state
- telemetry/logs clearly show mode changes, state transitions, and wakeups

## 22. Scope Boundary

What this guide verifies:

- Phase 3 IPMS logic
- runtime low-power orchestration
- wakeup bookkeeping
- restore-path behavior

What it does not fully prove by itself:

- long-duration battery endurance behavior
- mission-grade low-power optimization
- later SD logging restore hooks
- RTOS coexistence with low-power entry

Those belong to later phases once the current superloop-based runtime path is proven stable.
