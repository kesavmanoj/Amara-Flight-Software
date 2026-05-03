# Nucleo-Only Bench Checklist

This checklist is for **today's hardware reality**:

- STM32 Nucleo-64 F446RE board only
- USB connection to the board
- no SX1278 radio attached
- no OLED attached
- no microSD attached
- no external battery-divider input on `PA0`
- no external telemetry listener on `USART1`

The goal is to validate as much of the current firmware as possible using only:

- the board
- the built-in user button
- the built-in LED
- the `USART2` console

This checklist is derived from the current firmware behavior and the existing phase docs:

- [PHASE3_IPMS_TESTING.md](/C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/PHASE3_IPMS_TESTING.md)
- [PHASE5_RTOS_TESTING.md](/C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/PHASE5_RTOS_TESTING.md)

## What This Checklist Will Validate

This bench session is meant to validate:

- RTOS bring-up
- task ownership at runtime
- CLI path over `USART2`
- logger output
- ADC monitor path for internal channels
- IPMS simulation and policy logic
- sleep/stop entry and wakeup handling
- RTC wakeup path
- user-button wake bookkeeping
- watchdog staying healthy during normal operation

This checklist will **not** validate:

- LoRa / G2S / SX1278 hardware
- OLED hardware
- SDIO / microSD hardware
- real battery-voltage measurement on `PA0`
- external telemetry UART reception on `USART1`

## Bench Setup

Before starting:

- connect the Nucleo board over USB
- flash the current firmware
- open a serial terminal on the board console UART
- configure the serial terminal for:
  - `115200`
  - `8-N-1`
- make sure you can see boot logs

Recommended terminal log capture:

- save the full session to a text file

## Expected Built-In Hardware You Can Use

- `LD2_HEARTBEAT` LED on the Nucleo board
- `B1_USER_BUTTON`
- `USART2` console path
- internal ADC channels:
  - `VREFINT`
  - MCU temperature sensor
- RTC
- watchdog

## Checklist Format

For each step:

- perform the action
- observe the expected result
- mark pass/fail
- if it fails, stop and diagnose before moving much further

---

## Step 1. Flash And Open Console

Action:

- program the board with the current firmware
- open the `USART2` console
- reset/power-cycle the board if needed

Expected:

- boot log appears
- no hard fault loop
- board remains responsive

Pass if:

- you consistently see startup logs after reset

Notes:

- external peripheral-related status may show failure or not-ready and that is acceptable for this session

---

## Step 2. Verify Scheduler Start And Heartbeat LED

Action:

- watch the board for 10 to 20 seconds after boot

Expected:

- `LD2_HEARTBEAT` continues toggling
- logs continue appearing periodically
- board does not freeze after `osKernelStart()`

Pass if:

- LED heartbeat continues
- no immediate reset loop or hang appears

What this proves:

- scheduler is running
- `HealthPowerTask` is alive
- normal runtime ownership moved out of `main()`

---

## Step 3. Verify CLI Command Path

Action:

- type:

```text
PING
```

Expected:

- you get a valid command response on the console

Then run:

```text
PWR_STATUS
```

Expected:

- status prints successfully

Pass if:

- both commands respond correctly
- console remains responsive after each command

What this proves:

- UART RX ISR path is working
- `CommTask` is waking correctly
- `CommandParser_Process()` is functioning
- `Command_DispatchLine()` is functioning

---

## Step 4. Verify ADC Health Path

Action:

- run:

```text
GET_ADC
```

Expected:

- valid `VDDA` value
- valid MCU temperature reading
- battery voltage field may be unstable or meaningless without external wiring on `PA0`

Pass if:

- command executes
- `VDDA` and temperature look sane

Important interpretation:

- do **not** treat the battery voltage result as trustworthy right now unless `PA0` is intentionally driven

What this proves:

- ADC DMA path is alive
- `ADC_Monitor_GetData()` works
- command path can fetch health data correctly

---

## Step 5. Verify Periodic Runtime Logs

Action:

- leave the console open for at least 10 seconds

Expected:

- periodic telemetry queue/status logs
- periodic radio/G2S status logs
- periodic ADC health or ADC-related reporting logs

Pass if:

- recurring logs continue without freezing the CLI

What this proves:

- `TelemetryRadioTask` is alive
- `HealthPowerTask` is alive
- logger is stable in RTOS context

---

## Step 6. Verify Baseline IPMS Status

Action:

- run:

```text
PWR_STATUS
```

Expected:

- policy should start in monitor-only mode
- simulation should start in auto mode
- current state should print cleanly
- no forced low-power action should already be pending on a healthy session

Pass if:

- status fields are readable and consistent

What this proves:

- IPMS initialized correctly
- command path to `IPMS_GetStatus()` is working

---

## Step 7. Verify Simulation Mode Change To SUNLIGHT

Action:

- run:

```text
PWR_POLICY MONITOR
PWR_SIM SUNLIGHT
```

- wait a few seconds
- then run:

```text
PWR_STATUS
```

Expected:

- simulation mode becomes `SUNLIGHT`
- effective battery voltage should reflect the simulated healthy value
- state should move toward a healthy path
- IPMS mode event logs should appear

Pass if:

- simulation mode changes cleanly
- state/log behavior is deterministic

What this proves:

- runtime IPMS control requests work
- `HealthPowerTask` applies them correctly
- IPMS mode event generation works

---

## Step 8. Verify Simulation Mode Change To ECLIPSE

Action:

- run:

```text
PWR_SIM ECLIPSE
```

- wait long enough for multiple 1-second battery-sample intervals
- then run:

```text
PWR_STATUS
```

Expected:

- simulation mode becomes `ECLIPSE`
- effective battery voltage should reflect the simulated low value
- state should move through low-power warning and possibly toward sleep/stop candidate depending on elapsed sample windows
- logs should show ordered IPMS state transitions

Pass if:

- transitions happen after a delay, not instantly
- logs show sensible sequence

What this proves:

- IPMS debounce and hysteresis logic are alive
- state machine is reacting to effective voltage rather than raw battery input alone

---

## Step 9. Verify Recovery Behavior

Action:

- after forcing `ECLIPSE`, switch back to:

```text
PWR_SIM SUNLIGHT
```

- wait a few seconds
- run:

```text
PWR_STATUS
```

Expected:

- state should not snap back immediately on one cycle
- it should move through `RECOVERY` behavior before normalizing
- logs should show ordered transition back toward healthy state

Pass if:

- state changes are stable and not oscillating rapidly

What this proves:

- hysteresis and recovery logic are working

---

## Step 10. Test Sleep Path

Action:

- enable sleep policy:

```text
PWR_POLICY SLEEP
```

- then force low-power condition:

```text
PWR_SIM ECLIPSE
```

- watch the console and board

Expected:

- after enough sample windows, IPMS should arm a sleep action
- logs should show:
  - state transitions
  - IPMS action log for sleep
- the MCU should enter sleep
- console output will pause briefly
- RTC wake should return the system
- console should become responsive again

After wake:

- run:

```text
PWR_STATUS
```

Pass if:

- system wakes back up
- CLI works after wake
- status reflects recovery/wakeup bookkeeping

What this proves:

- RTC wakeup arm path works
- `SystemRuntime_ExecuteIpmsAction()` sleep branch works
- `SystemRuntime_RestoreAfterSleep()` works

---

## Step 11. Try Button Wake Bookkeeping

Action:

- repeat the sleep test
- while the board is in low power, try using the user button as the wake trigger if practical on your setup
- after the system resumes, run:

```text
PWR_STATUS
```

Expected:

- if the board/wakeup path supports it as configured, wake source may become `BUTTON`
- otherwise RTC may still remain the effective wake source

Pass if:

- at minimum, the board still resumes cleanly
- if button wake is supported in practice, the wake bookkeeping reflects it

Important note:

- treat button wake as a useful extra check, not the main pass/fail gate for today's session

---

## Step 12. Test Stop Path Carefully

Action:

- enable full low-power policy:

```text
PWR_POLICY FULL
```

- force low-power condition again:

```text
PWR_SIM ECLIPSE
```

- watch for:
  - stop-candidate transitions
  - stop action log
  - wake and restore logs

Expected:

- system may enter stop after enough samples
- after wake, runtime restore should occur
- console should come back
- restore logs should appear

After wake:

- run:

```text
PING
GET_ADC
PWR_STATUS
```

Pass if:

- board wakes from stop
- console works after wake
- commands still work

Important caution:

- this is more invasive than sleep
- if something fails here, you may need a reset or reflash

---

## Step 13. Observe Watchdog Health During Normal Operation

Action:

- let the firmware run for a few minutes without interacting

Expected:

- no spontaneous resets
- no recurring watchdog fault logs under normal operation

Pass if:

- system remains stable

What this proves:

- normal task heartbeat supervision is not obviously broken

---

## Step 14. Optional Passive Watchdog Check During Low-Power Testing

Action:

- while running the sleep and stop tests, watch for:
  - unexpected watchdog resets
  - repeated reboot loops

Expected:

- normal sleep/stop entry and restore should not immediately cause watchdog failure

Pass if:

- low-power tests complete without repeated reset loops

Do not intentionally force hangs yet unless you specifically want to do watchdog fault injection.

---

## What To Skip Today

Do not spend time on these today without hardware:

- LoRa/G2S tests from [PHASE2_TESTING.md](/C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/PHASE2_TESTING.md)
- SD card tests from [SDIO_TESTING.md](/C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/SDIO_TESTING.md)
- OLED validation
- real battery-threshold validation on `PA0`
- external telemetry UART capture on `USART1`

---

## Suggested Log Capture Checklist

Try to capture these exact evidence points:

- boot log
- successful `PING`
- successful `GET_ADC`
- `PWR_STATUS` in baseline state
- `PWR_SIM SUNLIGHT` effect
- `PWR_SIM ECLIPSE` effect
- sleep action log and post-wake status
- stop restore log and post-wake status

---

## Minimum Success Criteria For Today

Call today successful if all of these are true:

- board boots and scheduler runs
- LED heartbeat continues
- CLI works over `USART2`
- `GET_ADC` returns sane internal health data
- IPMS simulation commands change behavior as expected
- sleep entry and wake work
- stop entry and restore work or at least fail in a diagnosable way
- no unexplained reset loop appears during normal operation

## Best Mental Model For This Bench Session

Today you are not trying to prove the whole satellite stack.

Today you are proving:

- the **RTOS runtime is alive**
- the **command path is alive**
- the **health/power logic is alive**
- the **low-power orchestration path is alive**

That is already a very meaningful bench day with only the Nucleo board.

