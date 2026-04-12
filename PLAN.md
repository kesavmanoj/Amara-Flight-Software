# CubeSat Flight Computer Emulator Roadmap and Interview Prep Plan

## Summary
Use a hybrid roadmap: start from the current repo truth, then phase it toward the full resume-level system. The current codebase already has strong foundations in UART transport, binary telemetry framing, ADC monitoring, OLED, and modular HAL wrappers, but the resume description still requires major work in FreeRTOS integration, LoRa/G2S protocol, intelligent power management, and robust SD logging.

The plan should be executed in two parallel tracks:
- `Development track`: implement the firmware in phases that preserve modularity and produce demonstrable milestones.
- `Interview track`: after each phase, learn the design deeply enough to explain architecture, tradeoffs, failure modes, and test strategy without bluffing.

## Phase Plan

### Phase 0: Re-baseline the Current Firmware
Goal: make the current project internally consistent and measurable before adding new features.

Implementation:
- Re-enable and validate the existing runtime path in `main.c` for UART driver init, command parser init, ADC monitor start, telemetry init, and watchdog refresh.
- Reconcile current code vs README so the documented behavior matches the actual compiled behavior.
- Add one stable bring-up configuration that proves: CLI on `USART2`, telemetry on `USART1`, ADC DMA active, OLED active, watchdog refreshed.
- Freeze the interface boundaries of the current reusable drivers:
  - UART driver owns RX/TX callbacks and DMA TX
  - I2C wrapper owns bus instance abstraction
  - OLED uses I2C wrapper
  - ADC monitor owns ADC/DMA conversion path
  - telemetry owns frame building and queueing, not UART HAL calls

Acceptance:
- clean build
- readable CLI/log output on `USART2`
- binary telemetry leaving `USART1`
- periodic ADC data available
- no watchdog reset during idle run

Interview learning:
- explain superloop architecture
- explain why DMA is used for ADC and UART TX
- explain module ownership and ISR minimization
- explain what is currently complete vs scaffolded

### Phase 1: Harden the Existing Transport and Observability Layer
Goal: turn the current foundation into a dependable flight-software base.

Implementation:
- Finish telemetry timestamp policy using RTC consistently and define exactly what the `uint32_t timestamp` means on the wire.
- Add telemetry packet coverage for:
  - system status
  - ADC health
  - heartbeat
  - event/fault
  - command acknowledgment
- Add queue-depth and drop/failure instrumentation for telemetry and logger paths.
- Audit command handling and fix unsafe behaviors such as missing argument handling and weak response framing.
- Formalize error reporting for UART, I2C, SPI, ADC, and telemetry using driver-local status enums.

Acceptance:
- every major module can report success/failure state cleanly
- telemetry packet set is stable and documented
- command interface is safe against malformed input
- logs and telemetry can be correlated during a bench test

Interview learning:
- explain binary telemetry framing and CRC use
- explain queueing vs direct transmit
- explain why transport ownership should stay in the UART driver
- explain how you prevent mixed binary/text UART corruption

### Phase 2: Add a Real Device Layer for LoRa / G2S
Goal: implement the radio subsystem that the resume description depends on.

Implementation:
- Build an `SX1278` driver on top of the existing SPI bus abstraction.
- Implement register read/write, reset, mode transitions, FIFO access, IRQ/status readout, and basic TX/RX paths.
- Define the first real G2S packet protocol:
  - frame header
  - packet type
  - source/destination
  - sequence number
  - payload length
  - payload
  - CRC-16 policy
- Decide and document the CRC policy clearly:
  - either keep STM32 hardware CRC for internal telemetry only and use a software CRC-16 for radio packets (use hardware crc as it is better and designed for these situations)
  - or redesign the radio protocol around a supported hardware polynomial
- Implement command-processing flow from radio packet to command execution and acknowledgment.

Acceptance:
- radio register read/write verified on hardware
- ground-to-board packet round-trip demonstrated
- valid/invalid packet CRC handling demonstrated
- command packet can trigger a real firmware action and produce an ACK/NACK

Interview learning:
- explain SPI transaction design and chip-select timing
- explain burst reads/writes and radio FIFO handling
- explain command protocol structure and why packetization matters
- explain hardware CRC limitations vs protocol CRC requirements

### Phase 3: Build the Intelligent Power Management System (IPMS)
Goal: make the battery/energy claim on the resume real and defensible.

Implementation:
- Define the power-state machine explicitly:
  - normal
  - low-power warning
  - sleep candidate
  - stop-mode candidate
  - recovery
- Extend ADC monitoring into a power policy module that uses battery voltage thresholds, hysteresis, sampling windows, and fault conditions.
- Add simulated eclipse/sunlight modes using either commands, timers, or a test harness so behavior can be demonstrated on the bench.
- Integrate MCU low-power entry/exit paths carefully:
  - identify which peripherals must be suspended or reinitialized
  - define wake sources
  - define what state must persist across wake
- Add telemetry and log events for every power-state transition.

Acceptance:
- threshold behavior is deterministic
- no rapid oscillation between power states
- wakeup path is documented and repeatable
- telemetry/logs clearly show why a power-state transition occurred

Interview learning:
- explain Sleep vs Stop on STM32F4
- explain wake sources and peripheral reinitialization requirements
- explain hysteresis and why threshold-only logic is unsafe
- explain energy tradeoffs in a CubeSat context

### Phase 4: Add Reliable SD Logging with Fault Containment
Goal: make storage and watchdog claims real.

Implementation:
- Validate the generated SDIO + FATFS stack on actual hardware before layering new logic on top.
- Build a logging service that owns:
  - mount/unmount
  - file creation/rotation
  - buffered writes
  - flush policy
  - error counters
- Decide exactly what gets logged:
  - boot events
  - commands
  - faults
  - ADC health snapshots
  - radio packet summaries
- Make the watchdog strategy explicit:
  - which execution context refreshes it
  - what counts as a healthy system
  - what happens if SD or radio stalls
- Add reset-cause reporting and persistent “last known fault” diagnostics if feasible.

Acceptance:
- mount/write/readback test passes
- logging survives repeated write cycles
- watchdog refresh policy is centralized and documented
- an intentionally stalled path can be shown to trigger safe recovery or reset

Interview learning:
- explain FATFS integration risks on embedded systems
- explain why watchdog refresh should reflect system health, not just periodic code execution
- explain blocking I/O vs buffered logging tradeoffs
- explain failure containment and post-reset diagnostics

### Phase 5: Migrate From Superloop to FreeRTOS Cleanly
Goal: reach the resume-level architecture without turning the current codebase into a half-RTOS/half-superloop hybrid.

Implementation:
- Treat the current `freertos.c` as scaffolding only; do not incrementally bolt features into placeholder tasks.
- Define final task model before implementation:
  - telemetry/radio task
  - sensor/health task
  - logging/storage task
  - command/router task if needed
- Convert shared modules to RTOS-safe usage where needed:
  - logger
  - UART driver shared channels
  - telemetry queues
  - storage service
- Introduce queues/event flags/mutexes only where truly needed; avoid unnecessary RTOS complexity.
- Keep ISR-to-task handoff explicit and minimal.

Acceptance:
- tasks have clear ownership and bounded responsibilities
- no hidden concurrency on shared buffers
- watchdog strategy still works in RTOS mode
- same functional demo from earlier phases still works after migration

Interview learning:
- explain when an RTOS is justified over a superloop
- explain task partitioning and priority choices
- explain ISR-to-task design
- explain concurrency risks introduced by migration

## Interview Study Plan
For each phase, prepare four things:
- `Architecture story`: what the subsystem does, where it sits in the stack, and why it is designed that way.
- `Tradeoffs`: why you chose DMA vs polling, superloop vs RTOS, binary telemetry vs text, SDIO vs SPI SD, etc.
- `Failure modes`: what can go wrong, how it is detected, and how the system recovers or degrades safely.
- `Validation story`: how you proved it works on the bench.

Core topics to master before interviews:
- STM32 clocking, DMA, interrupt flow, watchdogs, ADC calibration, low-power modes
- UART/I2C/SPI protocol mechanics and embedded driver patterns
- RTOS primitives, scheduling, race conditions, and shared-resource protection
- telemetry protocol design, CRC rationale, and command/ACK semantics
- FATFS/SD logging risks and mitigation
- system-state machines, fault handling, and mission-style reliability thinking

Practice answers you should be able to give cleanly:
- “Why did you separate console UART and telemetry UART?”
- “How does your ADC health monitor work end to end?”
- “How does your telemetry protocol ensure reliability?”
- “Why use DMA here but not everywhere?”
- “How would you prevent a logging task from hanging the system?”
- “How would you migrate this from superloop to RTOS safely?”

## Test Plan
Bench validation should be phase-gated:
- Phase 0: boot, CLI, telemetry UART, ADC, watchdog, OLED
- Phase 1: telemetry packet correctness, command robustness, queue overflow handling
- Phase 2: SPI radio register tests, TX/RX packet loopback, CRC rejection tests
- Phase 3: battery-threshold simulation, sleep/stop entry and wake tests
- Phase 4: SD card mount/write/readback, prolonged logging, forced storage error handling
- Phase 5: RTOS regression suite proving prior functionality still works

For interview prep, create one demo script per phase:
- setup
- expected behavior
- failure injection
- interpretation of logs/telemetry
- design rationale

## Assumptions and Defaults
- Use the current repo as the starting truth, not the resume text.
- Treat FreeRTOS, LoRa, IPMS, and SD logging as phased targets, not already-finished features.
- Keep the existing modular layering and driver ownership model.
- Keep telemetry binary and transport-agnostic.
- Prefer proving one subsystem end to end before starting the next.
- Delay RTOS migration until the superloop-based functional slices are validated and observable.
