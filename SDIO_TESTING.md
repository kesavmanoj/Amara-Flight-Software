# SDIO microSD Bring-Up And Test Guide

This document captures the current SD card implementation context in the firmware, the expected hardware wiring for the STM32F446RE setup, and the practical bench procedure to verify that SDIO + FatFs is functional.

It is meant to answer three things clearly:

1. how the microSD socket should be wired
2. how the current firmware handles SDIO and FatFs
3. how to test the implementation safely on hardware

## 1. Current Firmware State

The project is now configured for `SDIO 4-bit` mode in CubeMX, with the following important behavior:

- `MX_SDIO_SD_Init()` still initializes `hsd.Init.BusWide` as `SDIO_BUS_WIDE_1B`
- this is expected for STM32 HAL SD bring-up
- the bus is then widened to `4-bit` inside `BSP_SD_Init()` by calling:
  - `HAL_SD_ConfigWideBusOperation(&hsd, SDIO_BUS_WIDE_4B)`

That means the real operational path is:

1. SD peripheral starts in `1-bit`
2. card is initialized
3. bus is switched to `4-bit`

The project also now includes:

- RTC-backed `get_fattime()` for file timestamps
- a non-RTOS fallback in `sd_diskio.c` so SD/FatFs can work even though the project is still running as a superloop
- CLI commands for basic SD validation:
  - `SD_STATUS`
  - `SD_TEST`

## 2. Physical SDIO Wiring

Current CubeMX pin mapping is:

- `PB2  -> SDIO_CK`
- `PC8  -> SDIO_D0`
- `PC9  -> SDIO_D1`
- `PC10 -> SDIO_D2`
- `PC11 -> SDIO_D3`
- `PD2  -> SDIO_CMD`
- `PC7  -> SD_DETECT_DUMMY` (optional card-detect input placeholder)

You also need:

- `3.3V` to microSD `VDD`
- `GND` to microSD `VSS`

Recommended physical wiring summary:

- `STM32 PB2` -> `microSD CLK`
- `STM32 PD2` -> `microSD CMD`
- `STM32 PC8` -> `microSD DAT0`
- `STM32 PC9` -> `microSD DAT1`
- `STM32 PC10` -> `microSD DAT2`
- `STM32 PC11` -> `microSD DAT3`
- `STM32 PC7` -> `card detect switch` if your socket has one
- `3.3V` -> socket power
- `GND` -> socket ground

## 3. Hardware Notes

Important electrical notes:

- microSD is `3.3V only`
- do not drive the socket with `5V`
- add local decoupling near the socket:
  - at least `100 nF`
  - ideally also a small bulk capacitor nearby
- SDIO lines should be kept reasonably short and clean
- `CMD` and `DAT0..DAT3` normally need pull-ups
- many sockets/modules already include these pull-ups, but not all do

Good practice:

- verify whether your socket breakout already has pull-ups
- if it does not, add them on:
  - `CMD`
  - `DAT0`
  - `DAT1`
  - `DAT2`
  - `DAT3`

`CLK` does not get a pull-up.

## 4. Important Current Quirks In This Project

There are two implementation details worth remembering:

1. `SD_CS` still exists in the project as a legacy GPIO artifact.
   - it is not used for SDIO
   - it is not part of the SDIO data path
   - it can be ignored for SDIO operation

2. `SD_DETECT_DUMMY` on `PC7` is only a simple detect placeholder right now.
   - if your socket does not have a detect switch, this signal may need to be tied or handled differently
   - if card detection is unreliable, mount/init may fail even when the wiring is otherwise correct

## 5. Files In The Firmware Relevant To SDIO

Current key files:

- [sdio.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/sdio.c)
  - SDIO peripheral init and pin setup

- [bsp_driver_sd.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/FATFS/Target/bsp_driver_sd.c)
  - card initialization and switch to `4-bit` bus mode

- [sd_diskio.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/FATFS/Target/sd_diskio.c)
  - FatFs block I/O layer
  - now supports non-RTOS fallback access

- [fatfs.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/FATFS/App/fatfs.c)
  - links the SD driver and provides FatFs timestamps

- [Command_List.c](C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/App/Src/Command_List.c)
  - exposes `SD_STATUS` and `SD_TEST`

## 6. What The Commands Do

`SD_STATUS`

- checks whether the card is detected
- runs `BSP_SD_Init()`
- reports whether the card initialized successfully
- prints card block count and block size when successful

`SD_TEST`

- checks detect and SD init
- mounts the filesystem
- opens `phase4_smoke.txt`
- appends a line with the current tick value
- syncs the file
- reads back a small sample
- closes and unmounts

This makes `SD_TEST` a good first smoke test before any larger logging service is built.

## 7. Bench Test Order

Recommended first bench sequence:

1. power the board with the microSD inserted
2. open the `USART2` console
3. confirm normal boot still works
4. run `SD_STATUS`
5. run `SD_TEST`
6. power-cycle and repeat once
7. inspect the card on a PC and confirm `phase4_smoke.txt` exists

## 8. Expected Results

Expected `SD_STATUS` success pattern:

- card detected as present
- init returns OK
- transfer state is OK
- block count and block size print correctly

Expected `SD_TEST` success pattern:

- mount succeeds
- file open succeeds
- write succeeds
- readback succeeds
- response prints `SD test OK ...`

Expected file artifact:

- `phase4_smoke.txt` should exist on the card
- it should contain one or more lines like:
  - `tick=12345`

## 9. Failure Patterns And What They Usually Mean

`ERR: SD Not Detected`

- card-detect signal is wrong
- socket switch is wired differently than expected
- no card inserted

`ERR: SD Init Failed`

- SDIO wiring issue
- power problem
- missing pull-ups
- poor socket/module quality

`ERR: SD Mount FR_NO_FILESYSTEM`

- card is blank or not FAT formatted
- format the card as FAT32 and retry

`ERR: SD Mount FR_DISK_ERR`

- low-level communication problem
- wiring, power, or signal integrity issue

`ERR: SD Open ...` or `ERR: SD Test ...`

- filesystem-level issue
- card may be write-protected or corrupted

## 10. Practical Pre-Test Checklist

Before blaming firmware, verify:

- card is formatted as FAT/FAT32
- card is known-good in another device
- socket gets stable `3.3V`
- ground is solid
- SDIO lines are correctly mapped
- pull-ups exist on `CMD` and `DAT0..DAT3`
- no accidental use of `5V`

## 11. Scope Of What Is Already Done

What is already implemented:

- SDIO pin configuration in CubeMX
- HAL SD initialization path
- bus-width switch to `4-bit`
- FatFs driver linkage
- non-RTOS disk I/O fallback
- timestamp support through RTC
- command-driven smoke testing

What is not yet the final storage subsystem:

- no log rotation yet
- no background buffered logging service yet
- no robust fault-tolerant SD service yet
- no persistent event log design yet

So this document is for validating the transport and filesystem foundation first, before the larger Phase 4 logging system is built.
