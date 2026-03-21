# CubeSat Flight Computer Emulator

--- AI GENERATED README ---
## 1. Project Overview
This firmware is a modular STM32F446RE-based embedded platform for a CubeSat flight computer emulator, built in C on top of STM32 HAL. The codebase is organized like a production-oriented flight software stack: peripheral drivers in `Hardware`, reusable utilities in `Utils`, application logic in `App`, and CubeMX-generated board support in `Core`.

Key capabilities present in the code today are interrupt-driven UART reception, structured UART logging, continuous ADC health monitoring with DMA, a table-driven CLI command framework, and an early telemetry packet module with CRC support. The project also has generated scaffolding for SPI, I2C, SDIO/FatFs, RTC, CRC, watchdog, and FreeRTOS.

## 2. System Architecture
High-level flow:
`USART2 RX interrupt -> ring buffer -> command line assembly -> tokenization -> command table dispatch -> module handler -> UART response/log output`

ADC path:
`ADC1 scan sequence -> DMA circular buffer -> ADC completion callback -> processing buffer -> engineering-unit conversion -> command/query output`

Planned telemetry path:
`application status -> telemetry frame builder -> CRC -> USART1 DMA transmit`

Layer separation is good overall:
- `Core`: MCU/peripheral initialization and ISR entry points.
- `Hardware`: device-facing drivers and bus abstractions.
- `Utils`: shared infrastructure such as logging and buffering.
- `App`: parser, command dispatch, and telemetry framing.

## 3. Firmware Modules

### UART Driver
Purpose: provide a simple console/transport layer over HAL UART.

Internal design:
- RX is interrupt-driven, one byte at a time.
- ISR callback pushes bytes into a fixed ring buffer and immediately rearms reception.
- Application code drains the buffer through `UART_ReadByte()`.

Key details:
- RX path is appropriately minimal in interrupt context.
- TX uses blocking `HAL_UART_Transmit()`, so the driver is only half non-blocking today.
- The active instance is `USART2` in `main()`, which makes USART2 the current CLI/log port.

Embedded considerations:
- Good ISR discipline on RX.
- Deterministic RX buffering.
- No TX queue, no DMA/IT transmit path, and silent byte loss is possible if the ring buffer overflows.

### Logger
Purpose: format timestamped diagnostic messages and emit them over UART.

Internal design:
- Uses `snprintf()` plus `vsnprintf()` into a static buffer.
- Pulls time/date from the RTC before formatting.
- Writes through the UART driver.

Key details:
- Log line format is `[HH:MM:SS] LEVEL message`.
- Timestamping is present, but RTC time is reset to `00:00:00 01-Jan-2000` at each boot, so logs are boot-relative unless RTC retention/set logic is added.
- Logger itself is not used from ISR paths in the current application.

Embedded considerations:
- Formatting is clean and bounded by a fixed buffer.
- Output is blocking because UART TX is blocking.
- Not currently protected for concurrent task use if FreeRTOS is enabled later.

### ADC Monitor
Purpose: continuously monitor internal references and a battery sense channel.

Internal design:
- ADC1 scans three channels: `VREFINT`, internal temperature sensor, and `ADC1_IN0` on `PA0`.
- DMA runs in circular mode.
- Completion callback copies the DMA buffer into a processing buffer.
- Conversion helpers derive VDDA, MCU temperature, and scaled battery voltage.

Key details:
- Uses factory calibration constants from ST's ADC definitions.
- Battery scaling assumes a 10k/10k divider, so reported battery voltage is doubled from the sensed node.
- The implementation is aligned with the CubeMX scan order.

Embedded considerations:
- Good choice of DMA circular mode for low-CPU continuous monitoring.
- ISR work is small and bounded.
- Fresh-data tracking is incomplete: the module defines a `NOT_READY` state but currently never returns it, so callers can read stale/initial values.

### Command Parser
Purpose: turn the UART byte stream into executable CLI commands.

Internal design:
- Drains bytes from the UART ring buffer.
- Builds a line until `CR` or `LF`.
- Tokenizes using `strtok()` with space delimiters.
- Hands the token array to the command dispatcher.

Key details:
- Command buffer is 128 bytes.
- Maximum token count is 5.
- Unknown commands return `ERR: Unknown Command`.

Embedded considerations:
- Clean separation between line building and command dispatch.
- Suitable for a superloop design.
- The parser exists but is never called from the main loop, so the CLI is currently unreachable at runtime.

### Command System (`Command_List`)
Purpose: provide table-driven command dispatch via function pointers.

Internal design:
- Static command table maps command names to handlers.
- Parser performs a linear lookup on the command name and invokes the handler.

Implemented commands:
- `PING`: returns `PONG`.
- `GET_ADC`: returns `VDDA`, `TEMP`, and `BATT` in text form.
- `SET_RATE <value>`: currently only echoes the requested rate.

Embedded considerations:
- Good extensibility pattern for CLI growth.
- Decouples parser mechanics from business logic.
- `SET_RATE` is incomplete and currently unsafe on missing arguments.

## 4. Command Interface
Input format:
- Commands are ASCII text, space-delimited, terminated by `\r`, `\n`, or both.
- Matching is case-sensitive; implemented command names are uppercase.

Output format:
- Success and error responses are plain UART text with CR/LF line endings in most cases.
- `GET_ADC` emits: `VDDA=<volts>, TEMP=<degC>, BATT=<volts>`

Implemented commands:
- `PING`: connectivity check.
- `GET_ADC`: reports converted ADC health values.
- `SET_RATE <int>`: placeholder for future telemetry/logging rate control.

## 5. Peripheral Configuration
- ADC1: 12-bit, scan mode, 3 conversions, continuous conversion, circular DMA on `DMA2_Stream0`; channels are `VREFINT`, temperature sensor, and `PA0`.
- USART1: 57600 baud, TX/RX with DMA configured; this looks like the intended telemetry port.
- USART2: 115200 baud, TX/RX with IRQ enabled; this is the actively used CLI/log port.
- SPI1: master, mode 0, software NSS, prescaler 32, DMA streams configured.
- I2C1: 400 kHz fast mode on `PB8/PB9`, RX DMA configured.
- SDIO: 1-bit bus mode on `PB2/PC8/PD2`.
- RTC: LSE-backed calendar enabled.
- GPIO: `PA5` heartbeat LED, `PB0` SD chip select, `PB1` sensor power enable, `PC9` SD detect input, `PC13` user button EXTI.
- IOC also defines `PC5` as `LORA_CS`, but that pin is not yet reflected in generated GPIO code.

## 6. Data Flow Explanation
Primary CLI path:
`USART2 RX IRQ -> HAL UART callback -> UART_Driver callback -> Ring_Buffer push -> CommandParser_Process() -> line buffer -> strtok() -> command table -> handler -> UART reply`

ADC query path:
`ADC1 + DMA -> DMA complete IRQ -> ADC_Monitor callback -> processing buffer -> ADC_Monitor_GetData() -> GET_ADC response`

Planned telemetry path:
`status source -> Telemetry_SendSystemStatus() -> CRC calculation -> USART DMA transmit`

## 7. Design Decisions
- Interrupts for UART RX: correct choice for responsive console input without polling latency.
- DMA for ADC: correct choice for continuous low-overhead health monitoring.
- Command table: scalable and maintainable compared with large `if/else` parser chains.
- Superloop vs RTOS: the codebase is conceptually still superloop-first, but FreeRTOS scaffolding has already been generated. Right now the scheduler is disabled, so runtime behavior is effectively bare-metal.

## 8. Current System Status
- Fully implemented at source level: USART2 RX buffering, ring buffer, logger formatting, ADC conversion pipeline, command parser, command table, telemetry frame builder.
- Partially implemented or partially integrated: SPI bus wrapper, I2C bus wrapper, OLED driver, telemetry transport usage, FatFs/SDIO stack, FreeRTOS task model.
- Not functionally wired into the current runtime: command processing, telemetry transmission, watchdog servicing through tasks, SD card operations, most non-UART peripheral abstractions.

## 9. Known Limitations / Issues
- The main loop is empty, so `CommandParser_Process()` is never called; the CLI framework is compiled in but unreachable.
- The watchdog is enabled during initialization, but its refresh exists only inside the disabled FreeRTOS `TelemetryTask`; the current image will likely reset periodically after boot.
- `ADC_Monitor_GetData()` never checks `data_ready`, so `ADC_MONITOR_NOT_READY` is effectively unused and stale/zero data can be returned.
- `SET_RATE` prints an error for a missing argument but still dereferences `argv[1]`, which is undefined behavior.
- UART TX and therefore logger output are blocking; this is acceptable for early bring-up but not ideal for deterministic flight-style telemetry/logging.
- RTC timestamping exists, but RTC time is reinitialized on every boot, so timestamps are not persistent or mission-meaningful yet.
- FatFs time support is stubbed with `get_fattime() = 0`, so filesystem timestamps are invalid.
- The generated SD disk layer assumes the RTOS kernel is running; with the scheduler disabled, SD/FatFs integration is not ready for use.
- The SPI wrapper header and source disagree on the `TransmitReceive` function name/signature, indicating that the SPI abstraction is not yet validated.
- `OLED_WriteString()` is declared but not implemented.

## 10. Future Work / Roadmap
- Validate the SPI bus abstraction end-to-end.
- Implement and verify the SX1278 LoRa driver, likely using SPI1 and the planned `LORA_CS` pin.
- Complete SD card + FatFs integration and add mount/read/write test coverage.
- Connect the telemetry module to real producers and a scheduled transmit policy.
- Add watchdog servicing strategy plus fault logging and reset-cause reporting.
- Decide on a clean FreeRTOS migration path or keep the design intentionally superloop-based.
- Improve error propagation and status reporting across all drivers.
- Expand the CLI with `HELP`, status, peripheral test, time-setting, and configuration commands.

## 11. Code Quality Assessment
- Modularity: strong. The `App` / `Hardware` / `Utils` / `Core` split is appropriate and easy for a new engineer to navigate.
- Scalability: promising. The command table, telemetry framing, and driver wrappers support incremental growth.
- Safety: mixed. ISR work is minimal where it matters, but blocking TX, incomplete watchdog integration, and weak readiness/error handling reduce runtime robustness.
- Embedded best practices: generally solid choices are visible, especially DMA ADC, IRQ RX buffering, use of fixed buffers, and limited ISR logic. The next step is integration discipline, not a rewrite.

## 12. Suggested Improvements
- Add an explicit application scheduler in the superloop: command parser servicing, periodic telemetry, watchdog refresh, and health polling.
- Convert UART TX to an interrupt- or DMA-driven queued backend.
- Make logger backend-aware and protect it for future multi-tasked use.
- Enforce ADC freshness semantics and return `NOT_READY` until the first completed DMA sequence.
- Harden command validation and give each command a clear success/error contract.
- Preserve RTC state across resets or add a command/API to set time.
- Add hardware bring-up tests for SPI, I2C, SDIO, and telemetry.
- Document port ownership clearly: `USART2` for CLI/logging, `USART1` for telemetry, SPI1 for radio/storage expansion.
