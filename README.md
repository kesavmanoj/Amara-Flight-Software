# CubeSat Flight Computer Emulator (UNFINISHED)

### AI Generated README (i am lazy)
## 1. Project Overview
This firmware is a modular STM32F446RE-based embedded platform for a CubeSat flight computer emulator, written in C on top of STM32 HAL. The codebase follows a layered embedded structure:

- `Core`: CubeMX-generated MCU and peripheral initialization
- `Hardware`: board/peripheral-facing drivers
- `Utils`: reusable infrastructure such as logging and buffering
- `App`: application logic, command handling, and telemetry framing

The current firmware provides:

- interrupt-driven CLI reception on `USART2`
- DMA-backed non-blocking UART TX for both console and telemetry
- UART text logging
- continuous ADC monitoring with DMA
- a table-driven command system
- binary telemetry frame generation with CRC on `USART1`
- OLED bring-up on `I2C1` through the modular I2C bus wrapper
- SX1278 LoRa device initialization on `SPI1`
- a first Ground-to-Space (`G2S`) radio packet layer on top of SX1278
- an Intelligent Power Management System (`IPMS`) with simulated sunlight/eclipse modes and RTC-based wakeup

## Note
Import the `.ioc` file into STM32CubeMX, regenerate code as needed, then build/flash with STM32CubeIDE.

Radio and G2S bench validation context is documented in [PHASE2_TESTING.md](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/PHASE2_TESTING.md).

## 2. System Architecture
High-level data flow:

- CLI/logging path: `USART2 RX IRQ -> RX ring buffer -> CommandParser_Process() -> command dispatch -> Logger/UART console TX`
- telemetry path: `application status -> telemetry frame builder -> frame queue -> UART driver telemetry channel -> USART1 DMA TX`
- ADC path: `ADC1 scan + DMA -> ADC conversion callback -> processed engineering values -> CLI/log output`

Current UART ownership:

- `USART2 @ 115200`: CLI + logger text
- `USART1 @ 57600`: binary telemetry

This split keeps readable console traffic separate from binary telemetry frames.

## 3. Firmware Modules

### UART Driver
Purpose: provide a transport layer over HAL UART with interrupt RX and DMA-backed non-blocking TX.

Internal design:

- the driver now manages two logical TX channels:
  - `UART_DRIVER_CHANNEL_CONSOLE`
  - `UART_DRIVER_CHANNEL_TELEMETRY`
- each channel owns:
  - bound HAL UART handle
  - TX ring buffer
  - DMA staging buffer
  - `tx_dma_len`
  - `dma_busy`
- console RX remains separate and interrupt-driven through a dedicated RX ring buffer

Key functions:

- `UART_Driver_Init()`: binds the console channel and starts 1-byte interrupt RX
- `UART_Driver_InitChannel()`: binds an additional logical channel to a HAL UART
- `UART_Write()`: writes to the console channel
- `UART_WriteChannel()`: writes to a selected logical channel
- `UART_RxCpltCallback()`: stores a received byte and rearms RX
- `UART_TxCpltCallback()`: clears DMA busy state and starts the next queued TX chunk
- `UART_ErrorCallback()`: clears busy state and retries queued TX work

Important implementation details:

- TX is non-blocking: callers enqueue data into the channel TX ring buffer
- `UART_StartTxDMA()` pops up to `128` bytes into a stable DMA buffer, then starts `HAL_UART_Transmit_DMA()`
- if a DMA start fails, the staged chunk remains in the DMA buffer and is retried later
- short critical sections disable interrupts while shared TX state is updated
- driver-local status reporting now distinguishes invalid parameters, uninitialized channels, buffer-full conditions, and generic transport errors

Embedded design considerations:

- minimal ISR work on RX
- DMA chunking avoids long blocking transmits
- channelized transport keeps console traffic and telemetry physically separated

### Logger
Purpose: format and emit readable system diagnostics over the console UART.

Internal design:

- uses `snprintf()` / `vsnprintf()` into a bounded buffer
- prefixes messages with a timestamp and level
- writes through `UART_Write()` on the console channel

Important implementation details:

- the logger no longer transmits directly through blocking HAL UART calls
- logger output now rides on the UART driver console DMA TX path
- the logger can fall back to boot-relative style timestamps during startup
- logger stats now track attempted writes, dropped writes, and the last console UART status

Embedded design considerations:

- fixed-size formatting buffer prevents overflow
- console logging is asynchronous at the UART transport layer
- log ordering is preserved per write call, but long bursts can still fill the console TX ring buffer

### ADC Monitor
Purpose: continuously monitor internal health channels and battery sense voltage.

Internal design:

- ADC1 scans:
  - `VREFINT`
  - internal temperature sensor
  - battery input on `PA0`
- DMA runs in circular mode
- conversion-complete callback copies/updates the latest sample set

Key functions:

- `ADC_Monitor_Init()`
- `ADC_Monitor_Start()`
- `ADC_Monitor_GetData()`
- `ADC_Monitor_ConvCpltCallback()`

Important implementation details:

- uses factory calibration-based conversions
- battery scaling assumes a `10k/10k` divider
- sample updates are driven by DMA completion, not polling
- status reporting is explicit through `ADC_Monitor_Status_t` and helper string conversion for logs

### Command Parser
Purpose: consume CLI bytes from the console RX ring buffer, build lines, tokenize commands, and dispatch handlers.

Internal design:

- bytes arrive through `USART2` RX interrupt
- the parser drains the ring buffer in the main loop
- lines are assembled until newline termination
- arguments are tokenized with `strtok()`

Important implementation details:

- parser is active in the current superloop
- CLI is now reachable at runtime through the console UART
- command lines are trimmed for leading/trailing spaces and tokenized on spaces or tabs
- malformed command cases now emit clearer console errors and telemetry fault events

### Command System (`Command_List`)
Purpose: provide table-driven command dispatch through function pointers.

Implemented commands:

- `PING`
- `GET_ADC`
- `SET_RATE <value>`
- `PWR_STATUS`
- `PWR_SIM <AUTO|SUNLIGHT|ECLIPSE>`
- `PWR_POLICY <MONITOR|SLEEP|FULL>`

Important implementation details:

- the parser and command table remain decoupled
- handlers can call logger, ADC monitor, and other modules without parser changes
- command handlers now emit binary command-ack telemetry packets alongside console responses where appropriate
- response writes and ACK queue failures are logged explicitly so bench traces show both command and transport outcomes
- the same command dispatch path is now shared by UART CLI input and radio command packets

### SX1278 Radio Driver
Purpose: provide a real LoRa device layer on top of the project SPI abstraction.

Current features:

- register read/write
- burst FIFO access
- version readback and device presence validation
- mode transitions: sleep, standby, TX, RX continuous, RX single
- frequency, sync word, and preamble configuration
- polling-based TX complete and RX packet retrieval through IRQ flag registers

Implementation notes:

- built on top of `SPI_Device_t` / `SPI_Bus`
- uses `PC5` as `LORA_CS`
- supports an optional reset GPIO, but current board configuration initializes without a dedicated reset pin
- defaults to LoRa mode at `433 MHz` with a standard sync word and continuous receive after initialization

### G2S Link Layer
Purpose: move binary command traffic over LoRa without coupling radio packets to UART text formatting.

Packet format:

- `sync_word`
- `version`
- `packet_type`
- `source`
- `destination`
- `sequence`
- `payload_length`
- `flags`
- fixed-size payload region
- `crc32`

Current packet types:

- `COMMAND`
- `ACK`
- `EVENT`

Command flow:

- SX1278 receives a fixed-size G2S packet
- `G2S_Link` validates header, version, destination, and CRC
- command payload is converted into a command line
- the shared command dispatcher executes the command
- an ACK/NACK radio packet is returned with command ID, status code, and argument/result metadata

### Intelligent Power Management System (IPMS)
Purpose: make battery-aware power behavior explicit, deterministic, and testable.

Current power states:

- `NORMAL`
- `LOW_POWER_WARNING`
- `SLEEP_CANDIDATE`
- `STOP_CANDIDATE`
- `RECOVERY`

Current capabilities:

- hysteresis-based battery thresholds
- multi-sample confirmation windows before state changes
- simulation modes:
  - `AUTO`
  - `SUNLIGHT`
  - `ECLIPSE`
- policy modes:
  - `MONITOR_ONLY`
  - `ENABLE_SLEEP`
  - `ENABLE_SLEEP_AND_STOP`
- RTC wakeup timer support
- user-button wake source support

Runtime behavior:

- IPMS consumes battery voltage from the ADC monitor path
- state transitions are logged and also emitted as telemetry events
- sleep/stop actions are only armed when the selected policy mode allows them
- the default boot policy is `MONITOR_ONLY`, so low-power entry is opt-in during bring-up

### Telemetry
Purpose: frame binary status data on `USART1` so observability stays machine-readable and separate from the console.

Current packet coverage:

- `TELEM_ID_SYSTEM_STATUS`
- `TELEM_ID_ADC_HEALTH`
- `TELEM_ID_HEARTBEAT`
- `TELEM_ID_EVENT`
- `TELEM_ID_COMMAND_ACK`

Timestamp policy:

- `TelemetryFrame_t.timestamp` is a `uint32_t`
- when RTC holds a non-default value, the field contains seconds since `2000-01-01 00:00:00`
- when RTC is still at the CubeMX default epoch or an RTC read fails, the field falls back to uptime seconds
- telemetry stats record whether the most recent frame used RTC time or uptime fallback

Observability features:

- queue depth, peak depth, queued/sent/dropped counters
- retry accounting for telemetry TX backpressure
- transport error counters
- RTC fallback counters
- last enqueue/process status snapshots for bench correlation

### Ring Buffer
Purpose: provide reusable FIFO infrastructure for both byte streams and fixed-size frame queues.

Byte ring buffer features:

- fixed capacity `RING_BUFFER_SIZE = 256`
- usable byte capacity is `255`
- used for:
  - console RX bytes
  - per-channel UART TX queues

Frame queue features:

- stores whole objects rather than bytes
- used by telemetry to queue complete `TelemetryFrame_t` packets
- `TELEM_QUEUE_SIZE = 9`, so usable queued frames are `8`

Important implementation details:

- `RingBuffer_PushArray()` now pre-checks space before writing
- `RingBuffer_PopArray()` correctly drains available data into a caller buffer
- `FrameQueue_Push()` / `FrameQueue_Pop()` copy whole telemetry frames in and out

## 4. Command Interface
Input format:

- ASCII commands over `USART2`
- space-delimited tokens
- terminated by `\r`, `\n`, or both

Output format:

- readable text responses on `USART2`
- logger and command replies share the same console UART

Current commands:

- `PING`: connectivity check
- `GET_ADC`: prints `VDDA`, `TEMP`, and `BATT`
- `SET_RATE <int>`: validated example configuration command with argument checking
- `PWR_STATUS`: prints the current IPMS state, simulation mode, policy, and wake source
- `PWR_SIM <AUTO|SUNLIGHT|ECLIPSE>`: changes the simulated power environment for bench testing
- `PWR_POLICY <MONITOR|SLEEP|FULL>`: selects whether IPMS only monitors, may sleep, or may sleep/stop

## 5. Peripheral Configuration

- `USART2`: `115200`, CLI/logger text, RX interrupt, DMA TX required
- `USART1`: `57600`, binary telemetry, DMA TX configured
- `ADC1`: scan mode, continuous conversion, circular DMA on `DMA2_Stream0`
- `SPI1`: initialized with DMA scaffolding
- `PC5`: LoRa chip select (`LORA_CS`)
- `I2C1`: initialized with RX DMA scaffolding
- `RTC`: enabled; telemetry frame timestamps now encode seconds since `2000-01-01 00:00:00`, with uptime-seconds fallback if RTC reads fail
- `RTC Wakeup`: used by IPMS for timed sleep/stop exit

## 6. Data Flow Explanation

Console path:

`USART2 RX IRQ -> HAL_UART_RxCpltCallback() -> UART_RxCpltCallback() -> RX ring buffer -> CommandParser_Process() -> command handler -> Logger/UART_Write() -> USART2 DMA TX`

Telemetry path:

`Telemetry_SendSystemStatus() / Telemetry_QueuePacket() -> Telemetry_BuildFrame() -> FrameQueue_Push() -> Telemetry_Process() -> UART_WriteChannel(UART_DRIVER_CHANNEL_TELEMETRY, ...) -> USART1 DMA TX`

ADC path:

`ADC1 -> DMA circular buffer -> HAL_ADC_ConvCpltCallback() -> ADC monitor update -> CLI/log query`

Radio command path:

`SX1278 RX polling -> G2S packet validation -> shared command dispatch -> command ACK packet build -> SX1278 TX -> return to RX continuous`

## 7. Design Decisions

- interrupt RX on `USART2` keeps CLI input responsive with minimal ISR work
- DMA TX is used to avoid blocking console/telemetry transmits in the superloop
- separate UART channels prevent binary telemetry from corrupting the human-readable console
- a command table keeps CLI growth manageable
- a superloop remains the execution model for now; no RTOS scheduler is active
- the G2S link uses the SX1278 polling path for now because no radio DIO IRQ pins are configured in the current hardware map
- the G2S link is intentionally fixed-size and 32-bit aligned so it can use the STM32 hardware CRC peripheral efficiently
- IPMS evaluates power policy in the superloop, keeping ISR work minimal and wakeup handling explicit
- low-power transitions are opt-in through IPMS policy mode so power-management testing does not surprise normal bring-up

## 8. CRC Policy

The internal telemetry path and the G2S radio protocol both use the STM32 hardware CRC peripheral.

Why this project uses hardware CRC for G2S:

- the STM32F446 already has a dedicated CRC peripheral enabled
- the existing telemetry stack already depends on it
- the hardware unit is fast, deterministic, and well-suited to repetitive packet validation

Protocol consequence:

- the first G2S protocol revision uses `CRC-32`, not `CRC-16`
- packet layout is fixed-size and 32-bit aligned so `HAL_CRC_Calculate()` can be used directly over the packet region excluding the CRC field
- this is a deliberate redesign of the radio protocol around the supported hardware polynomial instead of adding a separate software CRC-16 path

## 9. Current System Status

Fully working at source/runtime integration level:

- console UART RX on `USART2`
- non-blocking DMA TX on both UART channels
- logger output on `USART2`
- telemetry frame queueing and binary TX on `USART1`
- ADC monitoring with DMA
- command parser and command dispatch in the main loop
- watchdog refresh in the main loop
- OLED initialization and screen updates on `I2C1`
- startup status logging that reports UART, ADC, telemetry, I2C, and OLED bring-up state
- telemetry and logger stats reporting that expose queue pressure, drops, retries, and timestamp-source fallback behavior
- SX1278 device-layer initialization and register readback at source/build level
- G2S packet parsing, CRC validation, and command-to-ACK flow at source/build level
- IPMS battery-state evaluation with hysteresis and sample windows
- simulated sunlight/eclipse test hooks through the command interface
- RTC wakeup and user-button wake handling for sleep/stop transitions
- telemetry and logger event reporting for IPMS state, mode, and wakeup events

Partially implemented or still early-stage:

- physical radio link validation on hardware
- I2C feature-level validation
- SD card / FatFs runtime integration
- richer telemetry payloads and scheduling
- OLED driver completeness

## 10. Known Limitations / Issues

- RTC is still reset to the default epoch on boot in CubeMX-generated `rtc.c`, so both logger and telemetry fall back to boot-relative time until RTC is explicitly set
- ring buffers are fixed-size, so sustained bursts beyond available queue space still return an error instead of blocking
- no RTOS-aware locking exists yet around logger/UART usage beyond interrupt masking
- the current hardware map exposes `LORA_CS` but not a dedicated SX1278 reset or DIO interrupt pin, so radio operation is polling-based
- radio round-trip behavior, CRC rejection, and over-the-air command ACK flow still need bench verification
- stop-mode restore currently reinitializes the core runtime peripherals, UART bindings, ADC monitor, and radio path; future subsystems such as SD/FatFs will need their own restore hooks
- RTC wakeup timing is deterministic, but the RTC still starts from the default epoch unless a real time-set flow is added

## 11. Future Work / Roadmap

- add CLI commands for telemetry status, RTC set/read, and fault reporting
- validate SPI-based radio integration
- run the bench checklist in `PHASE2_TESTING.md`
- complete SD card + FatFs support
- add watchdog fault reporting and reset-cause diagnostics
- decide whether to remain superloop-based or migrate cleanly to FreeRTOS

## 12. Code Quality Assessment

- Modularity: good. The UART split and channelized transport preserve clean module boundaries.
- Scalability: improved. Multiple logical UART channels now fit naturally without pushing HAL details into application modules.
- Safety: improved. TX no longer blocks, ISR work remains short, and telemetry is isolated from the console path.
- Embedded best practices: solid direction. DMA TX, interrupt RX, fixed buffers, and explicit driver ownership of callback state are good architectural choices.

## 13. Suggested Improvements

- remove deprecated telemetry transport API pieces after call sites are cleaned up
- document UART channel usage directly in code comments near initialization
- add explicit queue depth and dropped-write diagnostics for both UART channels
- consider a small RTOS-safe lock strategy if multi-tasked logging/telemetry is added later
- tighten logger timestamp policy so logs stay clearly boot-relative until RTC is explicitly set
- add a dedicated SX1278 reset pin and DIO interrupt wiring in CubeMX so the radio can move from polling to event-driven handling
