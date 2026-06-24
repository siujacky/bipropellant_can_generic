# CAN + UART Bootloader — Flash Workflow

This document describes how to flash the STM32F103RCT6 hoverboard controller
over **CAN bus** (via MCP2515) **or UART** (USART1 PA9/PA10) without a programmer,
using the companion `tools/can_flash.py` script and/or
[jsphuebner/stm32-CANBootloader](https://github.com/jsphuebner/stm32-CANBootloader)'s
`can-updater.py` / `uart-updater.py`.

The bootloader polls **both transports simultaneously** for 500 ms on every power-on;
whichever master connects first wins. If neither connects, the application boots normally.

| Transport | Pins | Tool |
|---|---|---|
| CAN via MCP2515 | CS=PB11 SCK=PB10 MOSI=PA2 MISO=PA3 | `tools/can_flash.py` or `can-updater.py` |
| UART (USART1)   | TX=PA9 RX=PA10, 115200 baud, 8N1   | `uart-updater.py` or any serial tool |

> **Pin note:** USART1 (PA9/PA10) does not conflict with MCP2515 pins — they are
> safe to use simultaneously in the bootloader. PA9/PA10 are phase-PWM outputs only
> in the running application, not in the bootloader.

---

## Flash Memory Layout

| Address range              | Size   | Contents                              |
|----------------------------|--------|---------------------------------------|
| 0x08000000 – 0x08001FFF   | 8 KB   | CAN bootloader (bootloader.bin)       |
| 0x08002000 – 0x0803F7FF   | ~248 KB| Application firmware (hover.bin)      |
| 0x0803F800 – 0x0803FFFF   | 2 KB   | Reserved flash (myFlashSection / NV)  |

The linker script `STM32F103RCTx_FLASH.ld` sets `FLASH ORIGIN = 0x08002000`,
so every `make` build produces a binary linked at that offset.

---

## One-Time Setup — Flash the Bootloader via ST-Link

This step is only needed once (or after a recovery reflash):

```bash
# Requires st-link (stlink-tools package) and the board connected via ST-Link/V2
st-flash write bootloader/bootloader.bin 0x08000000
```

Verify:

```bash
st-flash read /tmp/bl_verify.bin 0x08000000 0x2000
```

---

## Build the Application

The linker script is already configured for 0x08002000:

```bash
make clean && make
# Output: build/hover.bin  (linked at 0x08002000)
```

Verify start address:

```bash
arm-none-eabi-objdump -f build/hover.elf | grep start
# Expected: start address 0x08002001  (Thumb bit set, base = 0x08002000)
```

---

## Update Firmware via CAN — Method 1 (Running Firmware)

When the firmware is already running you can trigger a reboot into the bootloader
over CAN, then immediately start the update tool.

### Step 1 — Send the SDO reboot trigger

The frame format is the stm32-CANBootloader SDO-style trigger
(write 1 byte to object index 0x5002, sub-index 0):

| Field   | Value                              |
|---------|------------------------------------|
| CAN ID  | 0x600 + node_id (= CAN_BoardID)    |
| DLC     | 3 (minimum)                        |
| data[0] | 0x22 — SDO download, 1 byte        |
| data[1] | 0x02 — index low byte              |
| data[2] | 0x50 — index high byte (→ 0x5002)  |

Example using `cansend` (node_id = 1):

```bash
cansend can0 601#22.02.50.00.00.00.00.00
```

### Step 2 — Start the updater within 500 ms

```bash
python3 tools/can_flash.py -d can0 -f build/hover.bin
# or using the original upstream tool:
python3 /path/to/stm32-CANBootloader/can-updater.py can0 build/hover.bin
```

The bootloader broadcasts on 0x7DE during its 500 ms window. `can_flash.py`
discovers it automatically when no `-i` (UID) flag is given.

---

## Update Firmware via CAN — Method 2 (Manual / Power Cycle)

1. Power cycle the board (or press RESET).
2. Within 500 ms, run:

```bash
python3 tools/can_flash.py -d can0 -f build/hover.bin
```

The bootloader is active immediately after reset and waits ~500 ms before
jumping to the application.

---

## Host Tool — can_flash.py

Minimal Python 3 uploader using the `python-can` library:

```bash
pip install python-can

# Auto-discover node:
python3 tools/can_flash.py -d can0 -f build/hover.bin

# Specify node UID explicitly:
python3 tools/can_flash.py -d can0 -f build/hover.bin -i 0x12345678
```

Arguments:

| Flag | Meaning                                               |
|------|-------------------------------------------------------|
| `-d` | SocketCAN interface (e.g. `can0`)                     |
| `-f` | Firmware binary (raw .bin, linked at 0x08002000)      |
| `-i` | Optional: 32-bit device UID in hex (auto if omitted)  |

---

## Recovery

If a CAN update fails midway (partial flash), the bootloader itself (0x08000000)
is unaffected. Use ST-Link to reflash the application:

```bash
st-flash write build/hover.bin 0x08002000
```

Or reflash everything from scratch:

```bash
st-flash erase
st-flash write bootloader/bootloader.bin 0x08000000
st-flash write build/hover.bin 0x08002000
```

---

## VTOR Relocation

The startup file `startup_stm32f103xe.s` sets `SCB->VTOR = 0x08002000` at the
very start of `Reset_Handler` (before `SystemInit`). This is required so that
the NVIC uses the application's vector table rather than the bootloader's after
the bootloader jumps to the application.

Without this step, all interrupts (SysTick, CAN, UART, …) would vector into
the bootloader's (now-invalid) handler table.
