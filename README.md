# CubeSat Flight Computer Emulator (UNFINISHED)

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

## Note
Import the `.ioc` file into STM32CubeMX, regenerate code as needed, then build/flash with STM32CubeIDE.

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

### Command System (`Command_List`)
Purpose: provide table-driven command dispatch through function pointers.

Implemented commands:

- `PING`
- `GET_ADC`
- `SET_RATE <value>`

Important implementation details:

- the parser and command table remain decoupled
- handlers can call logger, ADC monitor, and other modules without parser changes

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
- `SET_RATE <int>`: placeholder configuration command

## 5. Peripheral Configuration

- `USART2`: `115200`, CLI/logger text, RX interrupt, DMA TX required
- `USART1`: `57600`, binary telemetry, DMA TX configured
- `ADC1`: scan mode, continuous conversion, circular DMA on `DMA2_Stream0`
- `SPI1`: initialized with DMA scaffolding
- `I2C1`: initialized with RX DMA scaffolding
- `RTC`: enabled, but timestamps are still startup/default-time biased

## 6. Data Flow Explanation

Console path:

`USART2 RX IRQ -> HAL_UART_RxCpltCallback() -> UART_RxCpltCallback() -> RX ring buffer -> CommandParser_Process() -> command handler -> Logger/UART_Write() -> USART2 DMA TX`

Telemetry path:

`Telemetry_SendSystemStatus() / Telemetry_QueuePacket() -> Telemetry_BuildFrame() -> FrameQueue_Push() -> Telemetry_Process() -> UART_WriteChannel(UART_DRIVER_CHANNEL_TELEMETRY, ...) -> USART1 DMA TX`

ADC path:

`ADC1 -> DMA circular buffer -> HAL_ADC_ConvCpltCallback() -> ADC monitor update -> CLI/log query`

## 7. Design Decisions

- interrupt RX on `USART2` keeps CLI input responsive with minimal ISR work
- DMA TX is used to avoid blocking console/telemetry transmits in the superloop
- separate UART channels prevent binary telemetry from corrupting the human-readable console
- a command table keeps CLI growth manageable
- a superloop remains the execution model for now; no RTOS scheduler is active

## 8. Current System Status

Fully working at source/runtime integration level:

- console UART RX on `USART2`
- non-blocking DMA TX on both UART channels
- logger output on `USART2`
- telemetry frame queueing and binary TX on `USART1`
- ADC monitoring with DMA
- command parser and command dispatch in the main loop
- watchdog refresh in the main loop

Partially implemented or still early-stage:

- SPI bus validation
- I2C feature-level validation
- SD card / FatFs runtime integration
- richer telemetry payloads and scheduling
- OLED driver completeness

## 9. Known Limitations / Issues

- `Telemetry_Init()` still carries an unused UART parameter for API compatibility; that should be removed in a cleanup pass
- `Telemetry_OnTxComplete()` and `Telemetry_OnError()` are retained as no-op stubs from the older transport design
- logger timestamp semantics are still imperfect because RTC startup/default-time handling is not fully resolved
- `SET_RATE` remains a placeholder command
- ring buffers are fixed-size, so sustained bursts beyond available queue space still return an error instead of blocking
- no RTOS-aware locking exists yet around logger/UART usage beyond interrupt masking

## 10. Future Work / Roadmap

- remove stale telemetry transport parameters/callback stubs
- validate and expand telemetry packet types
- add CLI commands for telemetry status, RTC set/read, and fault reporting
- validate SPI-based radio integration
- complete SD card + FatFs support
- add watchdog fault reporting and reset-cause diagnostics
- decide whether to remain superloop-based or migrate cleanly to FreeRTOS

## 11. Code Quality Assessment

- Modularity: good. The UART split and channelized transport preserve clean module boundaries.
- Scalability: improved. Multiple logical UART channels now fit naturally without pushing HAL details into application modules.
- Safety: improved. TX no longer blocks, ISR work remains short, and telemetry is isolated from the console path.
- Embedded best practices: solid direction. DMA TX, interrupt RX, fixed buffers, and explicit driver ownership of callback state are good architectural choices.

## 12. Suggested Improvements

- remove deprecated telemetry transport API pieces after call sites are cleaned up
- document UART channel usage directly in code comments near initialization
- add explicit queue depth and dropped-write diagnostics for both UART channels
- consider a small RTOS-safe lock strategy if multi-tasked logging/telemetry is added later
- tighten logger timestamp policy so logs stay clearly boot-relative until RTC is explicitly set
