# CubeSat Flight Computer Emulator (STM32F446RE)

This project is a mission-style embedded firmware stack for a CubeSat flight computer emulator.  
It is built on STM32 HAL + FreeRTOS and organized as a phased architecture from basic bring-up to radio, power management, storage, and RTOS ownership.

The goal of this README is to help you understand the system end-to-end, phase by phase, from boot to runtime behavior.

---

## 1) What This Firmware Does

At runtime, the firmware currently provides:

- Console command + logging path on `USART2` (text, human-readable)
- Binary telemetry path on `USART1` (framed packets with CRC)
- ADC health monitoring (VDDA, temperature, battery) via DMA
- LoRa radio device layer (`SX1278`) and G2S command packet flow
- IPMS (Intelligent Power Management System) with battery-driven state machine
- SDIO/FatFs storage service owned by a dedicated RTOS task
- Buffered persistent log append with flush/rotation policy
- RTOS task ownership for comm, telemetry/radio, health/power, and storage

---

## 2) Repository Layout

- `Core/`  
  CubeMX-generated startup and peripheral init (`main.c`, `freertos.c`, interrupt handlers)
- `Hardware/`  
  Driver layer (`UART_Driver`, `SPI_Bus`, `I2C_Bus`, `ADC_Monitor`, `SX1278`, OLED)
- `App/`  
  Application logic (`Telemetry`, `Command_*`, `G2S_Link`, `IPMS`, `Storage_Service`, runtime state/resources)
- `Utils/`  
  Reusable utilities (`Logger`, ring buffers)
- `FATFS/`  
  FatFs middleware + SD BSP glue

---

## 3) Runtime Architecture (Current)

### RTOS Tasks

- `CommTask`  
  Owns command ingress (`CommandParser_Process`) and G2S receive processing (`G2S_Link_Process`)
- `TelemetryRadioTask`  
  Owns `Telemetry_Process` and periodic telemetry/radio status reporting
- `HealthPowerTask`  
  Owns ADC/IPMS periodic logic, low-power action execution, watchdog supervision
- `StorageLogTask`  
  Owns SD/FatFs operations and drains storage queues (commands + persistent log records)

### ISR Ownership

- UART RX/TX/error callbacks remain transport callbacks and stay minimal
- ADC DMA complete callback remains minimal and updates monitor snapshot
- RTC wakeup + EXTI wake callbacks remain event signaling only

This keeps heavy work in tasks, not in interrupts.

---

## 4) Communication Split (Why Two UARTs)

- `USART2 @ 115200`  
  CLI + logger text
- `USART1 @ 57600`  
  Binary telemetry transport

This physically separates human console text from machine telemetry bytes, preventing corruption and making debug much easier.

---

## 5) Phase-by-Phase Walkthrough

## Phase 0: Re-baseline Firmware

**Goal:** stable bring-up baseline with clear module boundaries.

### What was established

- `main.c` initializes peripherals + module stack in a deterministic order
- UART driver initialized for:
  - console channel on `USART2`
  - telemetry channel on `USART1`
- command parser, telemetry, ADC monitor, OLED, radio/G2S, IPMS all initialized
- startup logs report subsystem init status for quick diagnosis

### Boundary rules established

- UART transport ownership stays in `UART_Driver`
- I2C bus ownership stays in `I2C_Bus`
- OLED uses I2C wrapper, not direct HAL calls
- ADC monitor owns ADC DMA conversion path
- telemetry owns frame build/queue/process and does not call HAL UART directly

---

## Phase 1: Harden Transport + Observability

**Goal:** make the baseline dependable and diagnosable.

### Key improvements

- Telemetry timestamp policy defined:
  - frame timestamp is `uint32_t`
  - RTC-based epoch seconds (`2000-01-01`) when RTC is valid
  - fallback to uptime seconds when RTC unavailable/default
- Telemetry packet coverage implemented for:
  - system status
  - ADC health
  - heartbeat
  - event/fault
  - command ACK
- Telemetry and logger stats improved:
  - queue depth/peak
  - drops/retries/errors
  - last status snapshots for correlation
- Command handling hardened:
  - argument validation
  - better response/error framing
  - unknown/malformed handling
- Driver-local status enums and status-to-string helpers formalized

---

## Phase 2: LoRa Device Layer + G2S Protocol

**Goal:** real radio subsystem and command transport over LoRa.

### SX1278 device layer

Implemented in `Hardware/Src/SX1278.c`:

- register read/write
- burst FIFO access
- mode transitions (sleep/standby/tx/rx)
- IRQ/status flag polling
- basic TX/RX packet path
- version readback / presence checks

### G2S protocol layer

Implemented in `App/Src/G2S_Link.c`:

- fixed-size packet structure
- source/destination + sequence + type + payload length
- CRC validation
- command packet ingestion into shared command dispatch
- ACK/NACK emission path

### CRC policy

Protocol is intentionally aligned to hardware CRC usage in this project stack.  
Telemetry + G2S both use the STM32 CRC peripheral path.

### Command routing integration

Both UART CLI and G2S commands route through shared command dispatch logic (`Command_List`), so behavior remains consistent regardless of ingress path.

---

## Phase 3: Intelligent Power Management System (IPMS)

**Goal:** battery-aware, explainable, testable power behavior.

### State machine

- `NORMAL`
- `LOW_POWER_WARNING`
- `SLEEP_CANDIDATE`
- `STOP_CANDIDATE`
- `RECOVERY`

### Policy model

- simulation modes: `AUTO`, `SUNLIGHT`, `ECLIPSE`
- policy modes: monitor only / sleep-enabled / sleep+stop-enabled
- hysteresis + sample-window logic to avoid oscillation

### Low-power runtime integration

- RTC wakeup timer path integrated
- user-button wake source integrated
- sleep/stop entry/restore flows consolidated via `System_Runtime`
- state transitions emit telemetry events + logs

---

## Phase 4: SDIO Storage + Persistent Logging

**Goal:** storage ownership + robust write behavior without race-prone FatFs access.

### SD interface

- SDIO + FatFs path is active
- storage interactions are routed through `Storage_Service`
- command handlers use storage requests instead of direct FatFs calls

### Storage service ownership

`StorageLogTask` is the single owner of filesystem work:

- mount/unmount
- status/smoke-test command servicing
- persistent log append path

### Buffered persistent logging

Logger now has dual output behavior:

1. immediate console output (UART DMA path)
2. enqueue formatted line to storage queue for persistent append

### Flush/rotation policy

Current defaults in `Storage_Service.c`:

- log queue length: 16 records
- flush every ~1000 ms or when buffered bytes exceed threshold
- rotate when file reaches configured max size
- rotating file set: `flight_log_0.txt ... flight_log_3.txt`

This gives bounded on-card growth and periodic durability without synchronous blocking in normal tasks.

---

## Phase 5: RTOS Migration (From Superloop to Task Ownership)

**Goal:** remove half-superloop/half-RTOS ambiguity.

### What changed

- old superloop runtime behavior moved into task-owned execution in `freertos.c`
- `main.c` is now init + scheduler start + HAL callback bridges
- task cadences now use deliberate scheduling:
  - `vTaskDelayUntil()` for periodic tasks
  - task notification wake for command task (`CommTask`) from UART RX callback
- watchdog supervision now uses per-task heartbeats, not blind refreshing
- shared runtime state moved into dedicated modules:
  - `Runtime_State` (ADC cache, telemetry counters)
  - `Runtime_Resources` (radio/G2S/runtime objects)

### Why this matters

- clearer task ownership
- fewer hidden races
- better fault semantics (watchdog tied to whole-system liveness)

---

## 6) Module Deep Dive

### UART Driver

Non-blocking DMA TX with channelized transport:

- console channel (`USART2`)
- telemetry channel (`USART1`)

TX queues are ring-buffered per channel, DMA staged, and advanced in callbacks.

### Telemetry

Frame queue + process loop:

- build frame
- enqueue
- process via telemetry channel writer
- maintain stats for queue pressure and transport health

### Logger

- formatted timestamped messages
- mutex-protected console write section under RTOS
- tracks attempted/dropped console writes
- tracks persistent enqueue failures via storage status

### ADC Monitor

DMA-based continuous sampling:

- `VREFINT`
- internal temp sensor
- battery input

Converts raw values into engineering units and exposes statused read API.

### Command System

Table-driven command dispatch:

- `PING`
- `GET_ADC`
- `SET_RATE <value>`
- `PWR_STATUS`
- `PWR_SIM <AUTO|SUNLIGHT|ECLIPSE>`
- `PWR_POLICY <MONITOR|SLEEP|FULL>`
- `SD_STATUS`
- `SD_TEST`

### Storage Service

Queue-driven SD owner with two request classes:

- synchronous command requests (`SD_STATUS`, `SD_TEST`) via command queue + response flag
- asynchronous persistent log records via log queue

---

## 7) Current Data Flows

### Console command path

`USART2 RX ISR -> UART RX ring -> CommTask -> Command parser/dispatch -> response + ACK telemetry`

### Telemetry path

`App events/status -> Telemetry queue -> TelemetryRadioTask -> UART telemetry channel DMA TX`

### Power path

`ADC DMA -> ADC monitor -> HealthPowerTask -> IPMS state machine -> optional low-power action`

### Persistent logging path

`Logger format -> UART console write + StorageService log enqueue -> StorageLogTask append/flush/rotate`

---

## 8) Build + Run

### Build

- Open project in STM32CubeIDE
- Regenerate from `.ioc` if needed
- Build `Debug`

### Runtime prerequisites

- CLI terminal on `USART2`
- telemetry listener on `USART1`
- SD card inserted for storage tests
- LoRa hardware connected for G2S/radio tests

---

## 9) Test Documentation Map

- Phase 2 radio/G2S bench: `PHASE2_TESTING.md`
- SDIO + SD card setup/validation: `SDIO_TESTING.md`
- Phase 3 IPMS bench: `PHASE3_IPMS_TESTING.md`
- RTOS migration architecture: `RTOS_ARCHITECTURE.md`
- Phase 5 RTOS regression: `PHASE5_RTOS_TESTING.md`
- roadmap and completion framing: `PLAN.md`

---

## 10) What Is Source-Integrated vs Bench-Verified

### Integrated at source/build level

- RTOS task ownership model
- dual-UART transport split
- telemetry framing and queueing
- ADC/IPMS runtime integration
- storage service and persistent log queue path
- watchdog heartbeat supervision

### Still requiring hardware verification focus

- end-to-end radio round-trip resilience in real RF conditions
- long-duration SD logging endurance under power/transient events
- low-power wake/restore behavior under full subsystem load

---

## 11) Known Constraints

- RTC starts at default epoch unless explicitly set, so timestamp fallback behavior is still relevant on cold boot
- SX1278 path is polling-oriented in current hardware configuration
- fixed-size queues/buffers mean burst overload is handled by drop/backpressure status, not unbounded blocking

---

## 12) Suggested Next Steps

1. Add RTC set/get command workflow to anchor real timestamps early in bring-up.
2. Extend storage stats reporting into a command (`SD_LOG_STATS`) for faster bench visibility.
3. Add radio DIO interrupt wiring path to move from polling to event-driven RX/TX signaling.
4. Add reset-cause + last-fault persistence to strengthen watchdog/post-mortem diagnostics.

