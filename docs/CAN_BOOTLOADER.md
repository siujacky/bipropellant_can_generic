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

## Flash Memory Layout (A/B Dual-Slot)

| Address range              | Size   | Contents                                        |
|----------------------------|--------|-------------------------------------------------|
| 0x08000000 – 0x08001FFF   | 8 KB   | Bootloader (bootloader.bin) — never overwritten |
| **0x08002000 – 0x0801FFFF** | **120 KB** | **Slot A — Primary** (compiled at 0x08002000) |
| **0x08020000 – 0x0803DFFF** | **120 KB** | **Slot B — Secondary** (compiled at 0x08020000) |
| 0x0803E000 – 0x0803FFFF   | 8 KB   | Bootloader config (A/B flag) + App NVRAM        |

- `STM32F103RCTx_FLASH.ld` → Slot A binary (ORIGIN=0x08002000, LENGTH=120K)
- `STM32F103RCTx_FLASH_SLOTB.ld` → Slot B binary (ORIGIN=0x08020000, LENGTH=120K)

App NVRAM (FlashContent / saved settings) lives at 0x0803F000–0x0803FFFF (4 KB).
Bootloader A/B config lives at 0x0803E000–0x0803EFFF (2 KB).

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

---

## UART Interactive Menu

Open a serial terminal (115200 baud, 8N1) on **PA9 (TX) / PA10 (RX)** of the board. Power it on (or reset it). Press **any key** within 500 ms to enter the menu. (Sending `0xAA` byte instead goes directly to silent auto-upload mode — compatible with `uart-updater.py`.)

```
===================================
  biPropellant CAN Generic BL v1
  UID: AABBCCDD...
  Active slot: A (0x08002000)
  Next boot:   A
===================================
 [1] Flash -> Slot A  (primary,   0x08002000)
 [2] Flash -> Slot B  (secondary, 0x08020000)
 [3] Flash -> custom address
 [4] Set next boot: Slot A
 [5] Set next boot: Slot B
 [6] Clear NVRAM (settings at 0x0803F000)
 [0] Boot application
===================================
Choice [0-6]:
```

### Menu options

| Option | Action |
|---|---|
| **[1]** | Flash Slot A — waits for `0xAA` then uploads; Slot A always compiled at 0x08002000 |
| **[2]** | Flash Slot B — waits for `0xAA` then uploads; use Slot B binary (see below) |
| **[3]** | Flash custom address — you type 8 hex digits; writing into bootloader region requires typing `YES` to confirm |
| **[4]** | Mark Slot A as next boot — saves to bootloader config flash, boots Slot A |
| **[5]** | Mark Slot B as next boot — saves to bootloader config flash, boots Slot B next power-on |
| **[6]** | Clear NVRAM — erases saved PID/CAN settings (0x0803F000, 4 KB); app resets to defaults |
| **[0]** | Boot immediately from whichever slot is configured |

After flashing Slot B via option [2], the menu asks:
```
Set Slot B as next boot? (y/n):
```
Pressing `y` saves the boot flag and Slot B will be used on next power-on.

---

## Building a Slot B Image

Slot A and Slot B images must be compiled at **different addresses** because the STM32 firmware has hard-coded vector tables and absolute addresses:

```bash
# Slot A (default, primary):
make clean && make
# → build/hover.bin  (linked at 0x08002000)

# Slot B (secondary):
make clean && make LDSCRIPT=STM32F103RCTx_FLASH_SLOTB.ld
# → build/hover.bin  (linked at 0x08020000)
```

> **Why two builds?** The Cortex-M3 vector table and `SCB->VTOR` are set to the compile-time origin. A Slot A binary flashed to Slot B will jump to the wrong address and crash immediately.

---

## A/B Update Workflow (Safe OTA)

This lets you test new firmware without risking your current working image:

```
1. Build Slot B image:
   make LDSCRIPT=STM32F103RCTx_FLASH_SLOTB.ld

2. Flash to Slot B via UART menu option [2]:
   (terminal at 115200 baud → press any key → [2] → 0xAA →
    uart-updater.py sends build/hover.bin → confirm "set as next boot? y")

3. Reboot. Board boots Slot B.
   → If it works: run [5] from the menu to make Slot B permanent, or leave as-is.
   → If it fails/crashes: power-cycle without any UART input → bootloader
     runs [4] automatically after timeout → falls back to Slot A.

4. (Optional) After confirming Slot B works, promote it to Slot A:
   Flash Slot B content → Slot A with option [1].
```

---

## NVRAM Clear

Option [6] erases the application settings sector (FlashContent / PID gains / CAN IDs) at 0x0803F000. The bootloader's own A/B config at 0x0803E000 is **not** erased by this command — to reset the boot slot, use option [4] (set to Slot A).

---

## Recovery

If the application flash is corrupt (both slots bad), the bootloader still runs and offers the menu. Flash a known-good image via UART option [1] or CAN.

If the **bootloader itself** is corrupt, use ST-Link:
```bash
st-flash write bootloader/bootloader.bin 0x08000000
```
