# Windows Setup Guide

## Why Windows shows "DEVICE_DESCRIPTOR_FAILURE"

When you plug an STM32 into Windows you may see this in Device Manager:

```
Device:  USB\VID_0000&PID_0002
Driver:  usb.inf  (BADDEVICE.Dev.NT)
Matching Device ID: USB\DEVICE_DESCRIPTOR_FAILURE
```

**What this means:** Windows detected the USB D+ pullup on the STM32 (it knows
a device is present) but the device didn't respond to the standard "Get Device
Descriptor" request. The firmware doesn't implement USB, so the STM32 ignores it.

**Why it happens with this firmware:** `bipropellant_can_generic` uses
CAN (via MCP2515 SPI) and UART for communication — not USB. If the board has
a USB connector on PA11/PA12 (like a Blue Pill or Black Pill dev board), Windows
sees the hardware but gets no response.

---

## Immediate Fix — Option A: STM32CubeProgrammer (recommended)

1. Download and install **STM32CubeProgrammer** from STMicroelectronics:
   https://www.st.com/en/development-tools/stm32cubeprog.html
   (free, requires account, includes ST-Link + USB DFU drivers)

2. Once installed, the ST-Link and USB DFU devices work automatically.

3. To flash via **ST-Link**: connect the ST-Link/V2 to SWD pins, open
   STM32CubeProgrammer, select "ST-Link" interface, connect, and flash.

4. To flash via **USB DFU**: see the "Enter USB DFU mode" section below.

---

## Immediate Fix — Option B: Zadig + dfu-util

If you just want USB DFU without installing STM32CubeProgrammer:

1. Download **Zadig**: https://zadig.akeo.ie/
2. In Zadig: `Options → List All Devices`
3. Select `STM32 BOOTLOADER` or `Unknown Device (VID_0000 PID_0002)` from the list
4. Set driver to **WinUSB** and click "Install Driver"
5. Install **dfu-util**: `winget install dfu-util` or download from http://dfu-util.sourceforge.net/
6. Flash: `dfu-util -a 0 -s 0x08002000:leave -D build/hover.bin`

---

## How to enter USB DFU mode

The STM32 has a factory ROM bootloader (at 0x1FFFF000) that handles USB and
appears as **VID_0483:PID_DF11** — which STM32CubeProgrammer and dfu-util recognise.

### Method 1: BOOT0 pin (hardware)
Hold **BOOT0** HIGH while powering on the board.
- On Blue Pill: bridge the BOOT0 jumper to 3.3V
- On a custom board: check the schematic for the BOOT0 pin
The board enumerates as VID_0483:PID_DF11 automatically.

### Method 2: From our UART bootloader menu
Open a serial terminal (115200 baud, 8N1) on PA9/PA10, press any key within 500 ms,
select **[7]**: `Enter USB DFU (ROM bootloader, VID_0483:PID_DF11)`.

### Method 3: From the running application (serial command)
Type `RD` in the UART terminal. The application writes the BKP_DR2 magic flag
and resets into the ROM DFU bootloader.

### Method 4: From the application code
```c
#include "bootloader_request.h"
bootloader_request_dfu();   // does not return
```

### Method 5: Via CAN
```bash
python3 tools/can_flash.py -d can0 --enter-dfu
# Device will reappear as VID_0483:PID_DF11
```

---

## Flash via USB DFU (after entering DFU mode)

### Using STM32CubeProgrammer
1. Open STM32CubeProgrammer
2. Select **USB** interface (not ST-Link)
3. Click **Connect** (device must be in DFU mode)
4. Click **Open file**, select `build/hover.bin`
5. Set start address to `0x08002000` (after the CAN+UART bootloader)
6. Click **Download**

### Using dfu-util
```bash
# Install driver first via Zadig (WinUSB for VID_0483:PID_DF11)
dfu-util -l                              # list DFU devices
dfu-util -a 0 -s 0x08002000:leave -D build/hover.bin
```

---

## Normal flashing workflow without USB (recommended)

Use the **ST-Link/V2** programmer via SWD pins and ST-Link tools:

**One-time bootloader flash (when you have ST-Link):**
```bash
# Linux / Raspberry Pi:
st-flash write bootloader/bootloader.bin 0x08000000
st-flash write build/hover.bin 0x08002000
```

**Subsequent updates (no programmer needed):**
```bash
# Via CAN (from Pi):
python3 tools/can_flash.py -d can0 -f build/hover.bin

# Via UART (USB-serial adapter on PA9/PA10):
python3 uart-updater.py -p /dev/ttyUSB0 -f build/hover.bin

# Via USB DFU (Windows, BOOT0 pin):
dfu-util -a 0 -s 0x08002000:leave -D build/hover.bin
```

---

## Detect what's connected (detect_chip.py)

```bash
# Shows ST-Link version + connected chip info:
python3 tools/detect_chip.py

# With CAN: also shows bootloader banner + chip DevID from firmware:
python3 tools/detect_chip.py -d can0
```

---

## STM32F4xx (F401, F411) — will this firmware work?

**Partially yes, with one important caveat (same answer for F401 and F411).**

| Chip | DevID | Flash | SRAM | Max MHz | TIM8? | Windows USB |
|---|---|---|---|---|---|---|
| STM32F401CCU6 (Black Pill v1) | 0x0423 | 256 KB | 64 KB | 84 | ❌ | Driver-free |
| STM32F401CEU6 | 0x0433 | 512 KB | 96 KB | 84 | ❌ | Driver-free |
| STM32F411CEU6 (Black Pill v2) | 0x0431 | 512 KB | 128 KB | 100 | ❌ | Driver-free |
| STM32F103RCT6 (hoverboard) | 0x0414 | 256 KB | 48 KB | 72 | ✅ | Needs Zadig |

| Feature | F103 | F401 / F411 | Works? |
|---|---|---|---|
| Bootloader (CAN+UART+USB DFU) | ✅ | ✅ | Yes — ROM DFU at 0x1FFF0000 (DevID now in table) |
| chip_detect.c | ✅ | ✅ | Yes — 0x0423/0x0433 (F401) + 0x0431 (F411) added |
| uart_hdsel, PhaseMap, chip_detect | ✅ | ✅ | Yes — HAL-based, family-agnostic |
| Single-motor drive (TIM1) | ✅ | ✅ | Yes — TIM1 pin mapping is **identical** on all three |
| **Dual-motor drive (TIM1+TIM8)** | ✅ | **❌** | **No — F401/F411 have no TIM8** |
| USB on Windows | Needs Zadig | **Driver-free** | F4 USB OTG = no driver install needed |

**TIM1 pins are identical across F103, F401, F411:**
```
High-side: PA8(CH1)  PA9(CH2)  PA10(CH3)
Low-side:  PB13(CH1N) PB14(CH2N) PB15(CH3N)
```
So the right-motor wiring from a hoverboard board drops straight in.
Left motor needs external dead-time gate driver (F401/F411 have no TIM8 equivalent).

**F401 vs F411 for this project:**
- Flash: F401CC = 256KB (same as F103), F411CE = 512KB
- Both need F4 HAL (`stm32f4xx_hal`) not F1 HAL
- Both enumerate USB as VID_0483:PID_DF11 in DFU mode (no driver needed)

See `bootloader/hal_motor_stm32f4.c` for the single-motor stub. To build for F401/F411:
create a Makefile env with `-DSTM32F401xC` or `-DSTM32F411xE` + F4 HAL + startup/ld.
