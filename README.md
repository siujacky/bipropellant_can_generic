![Bipropellant](.github/logo.png)

# bipropellant_can_generic — board-generic hoverboard firmware

**A fork of [bipropellant_can](https://github.com/siujacky/bipropellant_can) (itself a fork of
[bipropellant-hoverboard-firmware](https://github.com/bipropellant/bipropellant-hoverboard-firmware) + MCP2515 CAN)
that makes the firmware *board-generic*: one binary per STM32 family that works across many hoverboard
mainboards, with pin maps supplied by [stm32_auto_detect](https://github.com/siujacky/stm32_auto_detect) profiles.**

Hoverboard mainboards use a handful of MCU families (STM32F103, GD32F1x0/E2, MM32, AT32) but every brand wires
the GPIOs differently. Instead of hand-editing `defines.h` per board, this firmware **embeds the whole profile
library** as a generated board table, **selects one active profile at boot**, and lets you **override individual
pins at runtime** (persisted to NVRAM) — within what the silicon allows.

## How it works

```
stm32_auto_detect          codegen/ (this repo)              firmware
  discovers a board   ->    profiles/*.toml  --c_board_table-->  board_table_<family>.c   ->  one .bin per family
  (or import defines.h)     af_tables/*.json                     (embedded const table)       embeds ALL family boards
```

At boot: `board_select` resolves ONE profile into a mutable RAM `ACTIVE` (stored index / serial-forced; falls back
to a **motor-disabled safe default**). Every pin read/write sources from `ACTIVE`. `board_override` lets you remap a
pin at runtime via serial/CAN, gated by three validators (silicon legality, profile-wide uniqueness, EXTI-line) and
routed **live** for GPIO-class pins or **applied-on-reboot** for silicon-bound pins (phase PWM / ADC / hall).

> **Pin-binding reality:** phase-PWM (TIM1/TIM8 complementary channels), ADC channels and hall EXTI lines are
> silicon-bound — those overrides are validated, persisted, and applied on the next reboot, never live. GPIO-class
> pins (LED/buzzer/button/charger/CAN soft-SPI) re-init live. The firmware returns explicit
> `APPLIED_LIVE / PENDING_REBOOT / REJECTED_SILICON / REJECTED_CONFLICT / REJECTED_EXTI` and never lies about it.
> Board *auto-detect* is not possible from silicon alone (no per-board UID), so selection is an explicit choice.

## Build (STM32F1 family)

```bash
python -m codegen.emit_board_table --all-families --profiles profiles --out generated   # regenerate board tables
make                                                                                     # -> build/hover.{elf,bin}
python -m pytest codegen/tests/                                                          # codegen unit tests
```

Requires `arm-none-eabi-gcc` (≥10; the Makefile sets `-fcommon`). Flash `build/hover.bin` to `0x8000000`.

## Status

| Milestone | State |
|---|---|
| M1 codegen (TOML → C board table, all 4 families) | ✅ 12/12 tests, generated C `gcc`-clean |
| M2 runtime `ACTIVE` pins (STM32F1) | ✅ builds; safe-default = byte-for-byte baseline |
| M3 boot board selection | ✅ |
| M4 NVRAM override + 3 validators | ✅ adversarially reviewed |
| M5–M7 GD32F130 / GD32E230 / MM32 + GPL vendor libs | ⏳ needs real silicon |

See [`docs/PLAN.md`](docs/PLAN.md) for the full design, risks, and milestones. GPL-3.0 (inherited from upstream).

---

<details><summary>Upstream bipropellant_can (CAN Bus Edition) README</summary>

**A fork of the [bipropellant-hoverboard-firmware](https://github.com/bipropellant/bipropellant-hoverboard-firmware) with MCP2515 CAN bus support for multi-board RC vehicle control.**

[![Build Status](https://travis-ci.com/bipropellant/bipropellant-hoverboard-firmware.svg?branch=master)](https://travis-ci.com/bipropellant/bipropellant-hoverboard-firmware)

---

## Project Overview

This firmware enables **hoverboard mainboards** (STM32F103-based) to be controlled via **CAN bus** using an MCP2515 module. It's designed for building **4-wheel drive RC vehicles** using two hoverboard mainboards (front + rear), each controlling two BLDC motors.

### Use Case: 4WD RC Car

```
                    ┌─────────────────┐
                    │  ESP32-S3       │
                    │  Controller     │
                    │  (Gamepad/UI)   │
                    └───────┬─────────┘
                            │ CAN Bus
            ┌───────────────┼───────────────┐
            │               │               │
   ┌────────▼────────┐     │      ┌────────▼────────┐
   │ Front Hoverboard│     │      │ Rear Hoverboard │
   │ + MCP2515       │◄────┴─────►│ + MCP2515       │
   │ Board ID: 0     │            │ Board ID: 1     │
   │ Cmd: 0x100      │            │ Cmd: 0x110      │
   └───────┬─────────┘            └───────┬─────────┘
           │                              │
     ┌─────┴─────┐                  ┌─────┴─────┐
     │ FL    FR  │                  │ RL    RR  │
     │ Motor Motor│                 │Motor Motor│
     └───────────┘                  └───────────┘
```

---

## What Has Been Accomplished

### MCP2515 CAN Bus Integration (v1.0 - v1.14)

- **Software SPI Implementation**: Uses PA2/PA3/PB10/PB11 pins for SPI communication with MCP2515
- **Auto-Detection**: Firmware automatically detects if MCP2515 is present; falls back to USART mode if not
- **Optimized SPI Timing**: Extensive debugging and fixes for reliable communication (v1.14)
  - Correct SPI Mode 0 timing with proper MISO sampling
  - Removed internal pull-ups that interfered with MCP2515 output
  - -O3 compiler optimization for consistent timing

### CAN Protocol

**Command Messages (Controller → Hoverboard):**
| ID Offset | Function | Data Format |
|-----------|----------|-------------|
| 0x00 | PWM Command | 2× int32: left PWM, right PWM (-1000 to 1000) |
| 0x01 | Speed Command | 2× int32: left speed, right speed (mm/s) |
| 0x02 | Position Command | 2× int32: left pos, right pos (encoder counts) |
| 0x03 | Enable/Disable | uint8: 0=disable, 1=enable |

**Status Messages (Hoverboard → Controller, every 100ms):**
| ID Offset | Function | Data Format |
|-----------|----------|-------------|
| 0x00 | Speed Status | 2× int32: left/right motor RPM |
| 0x01 | Position Status | 2× int32: left/right encoder counts |
| 0x02 | Battery/Temp | int16 voltage (V×100), int16 temp (°C) |
| 0x0F | Boot Message | Sent once at startup |

### Multi-Board Support

- **Board ID System**: Each board has a configurable ID (0-15)
- **ID Offset Calculation**: `Actual_ID = Base_ID + (Board_ID × 0x10)`
- **Runtime Configuration**: Board ID and base IDs stored in flash, configurable via serial
- **Example Setup**:
  - Front (ID=0): Commands 0x100-0x103, Status 0x200-0x202
  - Rear (ID=1): Commands 0x110-0x113, Status 0x210-0x212

### Debug & Diagnostics

- **Software Serial Debug**: Output via PA13/PA14 (SWD pins) at 115200 baud
- **CAN Statistics**: Monitor TX/RX counts, errors, bus status
- **Boot Banner**: Shows firmware version, CAN IDs, and configuration

---

## Hardware Requirements

### Hoverboard Mainboard
- STM32F103RCT6-based hoverboard controller
- Standard split hoverboard mainboard (2 motors per board)

### MCP2515 CAN Module
- MCP2515 with SPI interface
- 8MHz or 16MHz crystal (configured in firmware)
- TJA1050 or similar CAN transceiver

### Wiring (Software SPI)

| STM32F103RC | MCP2515 | Function |
|-------------|---------|----------|
| PA2 | SI (MOSI) | SPI Data Out |
| PA3 | SO (MISO) | SPI Data In |
| PB10 | SCK | SPI Clock |
| PB11 | CS | Chip Select |
| PB12 | INT | Interrupt (optional) |
| 3.3V | VCC | Power |
| GND | GND | Ground |

**Note:** PA2, PA3, PB10, PB11 are shared with USART2/USART3. The firmware auto-detects MCP2515 and falls back to USART if not found.

### CAN Bus Wiring
- 120Ω termination resistors at both ends
- Twisted pair for CAN_H and CAN_L
- All grounds connected
- Default speed: 500kbps

---

## Quick Start

### 1. Build and Flash

```bash
# Clone with submodules
git clone --recurse-submodules https://github.com/siujacky/bipropellant_can.git
cd bipropellant_can

# Build CAN_BUS firmware
pio run -e CAN_BUS

# Flash via ST-Link
pio run -e CAN_BUS --target upload
```

### 2. Configure Board ID

Connect via serial (115200 baud) and configure:

**Front Board (ID=0):**
```
wC0 0        // Board ID = 0
wC1 256      // Command Base = 0x100
wC2 512      // Status Base = 0x200
w80 1238     // Save to flash
```

**Rear Board (ID=1):**
```
wC0 1        // Board ID = 1
wC1 256      // Command Base = 0x100
wC2 512      // Status Base = 0x200
w80 1238     // Save to flash
```

Power cycle after configuration.

### 3. Send Test Command

From your CAN controller, send:
```
CAN ID: 0x100 (Front) or 0x110 (Rear)
Data: F4 01 00 00 F4 01 00 00  (PWM=500 both motors)
```

---

## Documentation

| Document | Description |
|----------|-------------|
| [CHANGELOG.md](CHANGELOG.md) | Version history and bug fixes |
| [CAN_BUS_README.md](CAN_BUS_README.md) | Complete CAN protocol documentation |
| [DUAL_BOARD_SETUP.md](DUAL_BOARD_SETUP.md) | Front/rear board configuration guide |
| [MCP2515_WIRING.md](MCP2515_WIRING.md) | Hardware wiring guide |
| [CAN_INTEGRATION_SUMMARY.md](CAN_INTEGRATION_SUMMARY.md) | Quick reference |

---

## Features

- **PWM Control**: Direct motor power control (-1000 to 1000)
- **Speed Control**: PID-controlled speed in mm/s
- **Position Control**: PID-controlled position (encoder counts)
- **Real-time Feedback**: Motor speed, position, battery voltage, temperature
- **Auto-Detection**: Single firmware works with or without CAN module
- **Flash Storage**: Configuration persists across power cycles
- **Sinusoidal Control**: Smooth BLDC motor commutation

---

## Version History

| Version | Key Changes |
|---------|-------------|
| v1.14 | **Critical SPI timing fix** - correct Mode 0 sampling |
| v1.7 | Removed MISO pull-up interfering with MCP2515 |
| v1.6 | Direct register SPI for deterministic timing |
| v1.5 | Softer startup/shutdown beeps |
| v1.0 | Initial CAN_BUS implementation |

See [CHANGELOG.md](CHANGELOG.md) for complete history.

---

## Related Projects

- **Controller**: [ESP32 Display Controller](https://github.com/siujacky/Esp32_display) - Touchscreen UI with gamepad support
- **Auxiliary Board**: [STM32H743XIH6](https://github.com/siujacky/STM32H743XIH6) - CAN gateway with GPS/IMU

---

## Original Project

This firmware is based on [bipropellant-hoverboard-firmware](https://github.com/bipropellant/bipropellant-hoverboard-firmware), which is a heavily modified version of [Niklas Fauth's hoverboard firmware](https://github.com/NiklasFauth/hoverboard-firmware-hack).

**Original Features Retained:**
- Hoverboard sensor board reading (USART2/3)
- Software serial for debug output
- ASCII serial protocol
- PID control for speed and position
- Hall sensor interrupts
- Flash settings storage
- Multiple ADC input configurations

---

## Building

### Prerequisites
- PlatformIO
- ST-Link programmer

### Build Environments

```bash
# Standard CAN_BUS build
pio run -e CAN_BUS

# With specific board ID (pre-configured)
pio run -e CAN_BUS_board0  # Front board
pio run -e CAN_BUS_board1  # Rear board
```

### Firmware Location
```
.pio/build/CAN_BUS/firmware.bin
```

---

## Troubleshooting

### CAN not detected on boot
- Check MCP2515 wiring (especially MISO/MOSI)
- Verify 3.3V power to MCP2515
- Check crystal frequency matches firmware config

### Motors not responding
- Verify Board ID matches controller expectations (`rC0` to read)
- Check CAN bus termination (120Ω at both ends)
- Monitor serial debug output for errors

### Corrupt CAN messages
- Ensure matching CAN bus speed (500kbps default)
- Check for loose connections
- Verify ground connections between all nodes

---

## License

See original [bipropellant-hoverboard-firmware](https://github.com/bipropellant/bipropellant-hoverboard-firmware) for license information.

</details>
