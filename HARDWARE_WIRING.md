# Hardware Wiring Reference

This document was derived from the current CubeMX project file:

- [amara-flight-software2.0.ioc](/C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/amara-flight-software2.0.ioc)

and the generated project sources such as:

- [main.h](/C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Inc/main.h)
- [main.c](/C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/main.c)
- [adc.c](/C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/adc.c)
- [usart.c](/C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Core/Src/usart.c)
- [ADC_Monitor.h](/C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Hardware/Inc/ADC_Monitor.h)
- [OLED_Display.h](/C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Hardware/Inc/OLED_Display.h)
- [SX1278.h](/C:/Users/KESAV/projects/Amara-Flight-Software/amara-flight-software2.0/Hardware/Inc/SX1278.h)

The goal is to answer two questions:

1. What external modules/hardware are expected by this firmware?
2. What MCU pins should be connected to what?

## 1. Expected Hardware Modules

The firmware currently expects or supports the following hardware blocks:

| Module | Needed | Notes |
|---|---|---|
| STM32F446RE target board | Yes | Main MCU target for the project. |
| SSD1306-style I2C OLED display | Yes if you want display output | Firmware initializes an OLED at I2C address `0x3C`. |
| SX1278 LoRa radio module | Yes if you want radio/G2S link | Firmware uses SPI1 and a chip-select pin. Default RF frequency is 433 MHz. |
| microSD card and SDIO wiring/socket | Yes if you want storage/logging to SD | Storage service uses FatFs over SDIO 4-bit mode. |
| Battery voltage sense input | Yes for ADC battery monitoring/IPMS | Firmware expects battery voltage on `PA0` through a resistor divider. |
| Telemetry UART peer on USART1 | Optional but supported | Dedicated telemetry UART at 57600 baud. |
| Console/CLI UART on USART2 | Yes for interactive console | 115200 baud. On many Nucleo boards this is commonly routed to ST-LINK VCP. |
| User button | Built-in / optional external equivalent | Used as a wake source. |
| Heartbeat LED | Built-in | `LD2_HEARTBEAT` on `PA5`. |
| Sensor power enable line | Optional external load switch | `PWR_EN_SENSORS` output on `PB1`. |

## 2. Important Firmware Assumptions

Before wiring anything, keep these constraints in mind:

- MCU I/O is 3.3 V logic.
- The OLED and SX1278 should be powered from 3.3 V and use 3.3 V logic.
- The battery sense input must not be connected directly to `PA0`.
- The current ADC code assumes a resistor divider:
  - `ADC_BATTERY_R1 = 10k`
  - `ADC_BATTERY_R2 = 10k`
- The OLED is initialized at I2C address `0x3C`.
- The SX1278 driver is currently using:
  - SPI only
  - CS only
  - no external reset pin
  - no DIO interrupt pins
- SD storage uses SDIO 4-bit mode, not SPI mode.

## 3. Pin Map By Function

### Console UART

| MCU Pin | Signal | Connect To | Notes |
|---|---|---|---|
| `PA2` | `USART2_TX` | Console RX on host/UART adapter | Console/logging/CLI TX from MCU, 115200 baud |
| `PA3` | `USART2_RX` | Console TX on host/UART adapter | Console/CLI RX into MCU, 115200 baud |

Notes:

- UART lines cross:
  - MCU TX -> adapter RX
  - MCU RX -> adapter TX
- Ground must be shared.

### Telemetry UART

| MCU Pin | Signal | Connect To | Notes |
|---|---|---|---|
| `PA9` | `USART1_TX` | Telemetry receiver RX | Dedicated telemetry UART TX, 57600 baud |
| `PA10` | `USART1_RX` | Telemetry source TX | Dedicated telemetry UART RX, 57600 baud |

### OLED Display on I2C1

| MCU Pin | Signal | Connect To | Notes |
|---|---|---|---|
| `PB8` | `I2C1_SCL` | OLED `SCL` | I2C fast mode |
| `PB9` | `I2C1_SDA` | OLED `SDA` | I2C fast mode |
| `3V3` | Power | OLED `VCC` | Use 3.3 V OLED module if possible |
| `GND` | Ground | OLED `GND` | Common ground required |

Firmware expectation:

- OLED address: `0x3C`

### SX1278 LoRa Module on SPI1

| MCU Pin | Signal | Connect To | Notes |
|---|---|---|---|
| `PB3` | `SPI1_SCK` | SX1278 `SCK` | SPI clock |
| `PA6` | `SPI1_MISO` | SX1278 `MISO` | Radio -> MCU |
| `PA7` | `SPI1_MOSI` | SX1278 `MOSI` | MCU -> radio |
| `PC5` | `LORA_CS` | SX1278 `NSS` / `CS` | Active chip-select |
| `3V3` | Power | SX1278 `VCC` | 3.3 V only |
| `GND` | Ground | SX1278 `GND` | Common ground required |

Notes:

- The current `main.c` initialization passes `NULL` for reset, so the radio reset pin is not used by this firmware right now.
- No SX1278 DIO pins are currently required by the driver because the implementation is polling-based rather than IRQ-driven.
- Default radio configuration in code uses 433 MHz.

### microSD / SDIO

| MCU Pin | Signal | Connect To | Notes |
|---|---|---|---|
| `PB2` | `SDIO_CK` | SD `CLK` | SDIO clock |
| `PD2` | `SDIO_CMD` | SD `CMD` | SDIO command |
| `PC8` | `SDIO_D0` | SD `D0` | SDIO data line 0 |
| `PC9` | `SDIO_D1` | SD `D1` | SDIO data line 1 |
| `PC10` | `SDIO_D2` | SD `D2` | SDIO data line 2 |
| `PC11` | `SDIO_D3` | SD `D3` | SDIO data line 3 |
| `3V3` | Power | SD socket/module `VCC` | 3.3 V only |
| `GND` | Ground | SD socket/module `GND` | Common ground required |

Additional project pins related to SD:

| MCU Pin | Label | Notes |
|---|---|---|
| `PB0` | `SD_CS` | Configured as GPIO output, but SD data path is SDIO 4-bit. This may be board-specific or unused depending on carrier hardware. |
| `PC7` | `SD_DETECT_DUMMY` | Configured as GPIO input placeholder, not a full card-detect implementation in current firmware. |

Important note:

- Native SDIO cards do not need SPI chip select in the normal SDIO protocol.
- If your carrier board exposes a board-specific enable/CS line, `PB0` may be part of that design, but the actual data interface is SDIO.

### Battery Voltage ADC Input

| MCU Pin | Signal | Connect To | Notes |
|---|---|---|---|
| `PA0` | `ADC1_IN0` | Battery divider midpoint | Do not connect battery directly |

Expected divider:

```text
Battery+ ---- R1 ----+---- PA0
                     |
                     R2
                     |
                    GND
Battery- ------------+
```

Current code assumptions:

- `R1 = 10k`
- `R2 = 10k`
- This scales battery voltage by 1/2 before the ADC sees it.

Good use case:

- 1-cell Li-ion / LiPo battery sense is a good match for the current divider values.

### User Button / Wake Source

| MCU Pin | Signal | Connect To | Notes |
|---|---|---|---|
| `PC13` | `B1_USER_BUTTON` / EXTI13 | User button | Used as a wake source in firmware |

### Heartbeat LED

| MCU Pin | Signal | Connect To | Notes |
|---|---|---|---|
| `PA5` | `LD2_HEARTBEAT` | LED | Already board-defined on Nucleo-style targets |

### Sensor Power Enable

| MCU Pin | Signal | Connect To | Notes |
|---|---|---|---|
| `PB1` | `PWR_EN_SENSORS` | External sensor power-enable/load-switch control | Output controlled by firmware |

This is not a sensor data pin. It is a control/output pin intended to switch power to external sensors or a sensor rail.

## 4. Practical Wiring Summary By Module

### OLED Module

Connect:

- OLED `VCC` -> `3V3`
- OLED `GND` -> `GND`
- OLED `SCL` -> `PB8`
- OLED `SDA` -> `PB9`

Expected address:

- `0x3C`

### SX1278 LoRa Module

Connect:

- SX1278 `VCC` -> `3V3`
- SX1278 `GND` -> `GND`
- SX1278 `SCK` -> `PB3`
- SX1278 `MISO` -> `PA6`
- SX1278 `MOSI` -> `PA7`
- SX1278 `NSS` / `CS` -> `PC5`

Current firmware does not require:

- SX1278 `RESET`
- SX1278 `DIO0..DIOx`

### microSD

Connect:

- SD `CLK` -> `PB2`
- SD `CMD` -> `PD2`
- SD `D0` -> `PC8`
- SD `D1` -> `PC9`
- SD `D2` -> `PC10`
- SD `D3` -> `PC11`
- SD `VCC` -> `3V3`
- SD `GND` -> `GND`

Optional / board-specific:

- SD `CS` or board enable -> `PB0` if your hardware needs it

### Battery Sense

Connect:

- Battery `+` -> resistor divider top
- Divider midpoint -> `PA0`
- Divider bottom -> `GND`
- Battery `-` -> `GND`

### Telemetry UART Device

Connect:

- External TX -> `PA10`
- External RX -> `PA9`
- GND -> GND

### Console UART

Connect:

- Host TX -> `PA3`
- Host RX -> `PA2`
- GND -> GND

## 5. Modules Implied By Firmware But Not Fully Wired In Code

These are worth calling out explicitly:

- The SX1278 reset line is not currently used by `main.c`.
- SX1278 DIO interrupt lines are not currently used by the radio driver.
- `SD_DETECT_DUMMY` is present as an input label, but the storage service mainly relies on `BSP_SD_IsDetected()`.
- `PWR_EN_SENSORS` exists as a control output, but the external sensor rail design is board-specific and not fully described by the `.ioc` alone.

## 6. Quick Bring-Up Checklist

- Power all external modules from 3.3 V unless their carrier board explicitly handles level shifting/regulation correctly.
- Share ground between the STM32 board and every external module.
- Do not feed raw battery voltage directly into `PA0`.
- Cross UART TX/RX correctly.
- Use the SDIO pin set for the SD card, not SPI-mode SD wiring.
- Use the OLED at address `0x3C`.
- Use an SX1278 module compatible with 433 MHz operation if you want the default radio settings to match hardware.

