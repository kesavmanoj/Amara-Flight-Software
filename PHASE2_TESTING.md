# Phase 2 Bench Testing Guide

This guide closes the bench-validation side of Phase 2:

- SX1278 register read/write on hardware
- ground-to-board command packet round trip
- valid and invalid CRC handling
- command execution with ACK/NACK over LoRa

The current firmware already builds with the SX1278 device layer and G2S link layer integrated. This document is the practical test script to verify the radio path on hardware later.

## 1. Test Objectives

Acceptance items to close:

- radio register read/write verified on hardware
- ground-to-board packet round-trip demonstrated
- valid packet accepted and invalid packet rejected
- command packet triggers real firmware action and produces ACK/NACK

## 2. Hardware Setup

Required hardware:

- Nucleo-F446RE running this firmware
- SX1278 LoRa module connected to the board
- second SX1278 node for the ground side
- USB console connection for `USART2`
- antenna on both radios

Current firmware assumptions:

- `SPI1` is the radio transport
- `PC5` is `LORA_CS`
- no dedicated SX1278 reset pin is currently wired in firmware
- no SX1278 DIO interrupt pin is currently used; radio handling is polling-based

Before testing:

- confirm `SCK`, `MISO`, `MOSI`, `NSS/CS`, power, and ground wiring
- confirm both radios share the same frequency and sync word
- confirm the board console is visible on `USART2 @ 115200`

## 3. Firmware Behavior To Expect

On boot, the board should log:

- initialization complete
- startup status including `RADIO=OK` and `G2S=OK`
- periodic radio status logs
- periodic G2S statistics logs

Expected healthy radio status:

- SX1278 version read should return `0x12`
- radio mode should remain valid and frequency should report `433000000 Hz`

## 4. Test Sequence

Recommended execution order:

1. radio presence and version check
2. valid `PING` command over LoRa
3. invalid CRC rejection
4. `GET_ADC` over LoRa
5. `SET_RATE 10` over LoRa
6. malformed command handling
7. unknown command handling

## 5. Test 1: Radio Register Read/Write Validation

Goal:

- prove the SX1278 device layer is alive on hardware

Procedure:

1. Flash the board.
2. Open the `USART2` console.
3. Wait for startup logs.
4. Inspect the radio status log.

Pass criteria:

- `version_status=OK`
- `version=0x12`
- no repeating radio error logs

If this fails, check:

- `LORA_CS` wiring
- SPI wiring
- radio power voltage
- common ground
- antenna connection

## 6. Test 2: Ground-To-Board Command Round Trip

Goal:

- prove a valid command packet reaches the board and an ACK comes back

Start with:

- `PING`

Expected flow:

1. ground node sends a valid `COMMAND` packet
2. board validates sync/version/destination/CRC
3. board dispatches the command through the shared command system
4. board transmits an `ACK` packet back

Pass criteria:

- ground node receives an ACK
- ACK reports success
- board `G2S` stats increment:
  - `rx_packets`
  - `command_packets`
  - `ack_packets`

## 7. Test 3: CRC Validation

Goal:

- prove the board accepts good packets and rejects corrupted ones

Important protocol note:

- the current G2S protocol uses STM32 hardware-backed `CRC-32`
- it does **not** use `CRC-16`

Valid CRC procedure:

1. send a correctly formed `PING` command packet
2. confirm ACK is returned

Invalid CRC procedure:

1. send the same packet with one byte modified after CRC calculation
2. or send an intentionally wrong CRC field
3. observe board behavior

Pass criteria:

- valid packet: command executes and ACK is returned
- invalid packet: command does not execute and `crc_failures` increments

## 8. Test 4: Command Execution Over Radio

Goal:

- prove the radio command path drives real firmware behavior

Commands to test:

- `PING`
- `GET_ADC`
- `SET_RATE 10`
- malformed `SET_RATE`
- unknown command such as `BOGUS`

Expected results:

- `PING` -> success ACK
- `GET_ADC` -> success ACK and command path runs normally
- `SET_RATE 10` -> success ACK with argument reflected
- `SET_RATE` -> error ACK
- `BOGUS` -> unknown-command path, no false success

Pass criteria:

- command handlers are shared between UART CLI and radio path
- ACK/NACK status matches real command outcome
- no crash, lockup, or corrupted state

## 9. Console Logs To Watch

Relevant runtime logs:

- `Radio status: ...`
- `G2S stats: ...`
- command response logs on `USART2`

Useful counters:

- `rx`
- `tx`
- `crc_fail`
- `cmd`
- `ack`
- `unsupported`
- `cmd_fail`

## 10. What To Record

For each test capture:

- exact packet sent from the ground node
- console log from `USART2`
- received ACK packet contents
- G2S stats before and after

Recommended evidence set:

- photo or screenshot of startup radio status
- screenshot of valid `PING` ACK
- screenshot of invalid CRC rejection behavior
- screenshot of `GET_ADC` and `SET_RATE 10` command ACKs

## 11. Common Failure Patterns

If version is not `0x12`:

- SPI or chip-select problem

If no packets are received:

- frequency or sync word mismatch
- antenna/wiring issue

If packets are received but CRC always fails:

- ground-node packet layout mismatch
- CRC computation mismatch
- byte-order or padding mismatch

If commands are received but no ACK returns:

- radio TX path issue
- destination/source field handling issue

## 12. Exit Criteria

Phase 2 hardware validation is complete when all of the following are true:

- SX1278 version read is stable and correct
- a valid command packet round-trips successfully
- a corrupted packet is rejected reliably
- at least one real command produces the expected ACK/NACK behavior
