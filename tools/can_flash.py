#!/usr/bin/env python3
"""
can_flash.py — CAN firmware uploader for stm32-CANBootloader
Compatible with jsphuebner/stm32-CANBootloader protocol.

Usage:
    python3 can_flash.py -d can0 -f build/hover.bin [-i <uid_hex>]

Requirements:
    pip install python-can

Protocol summary (stm32-CANBootloader):
  - Bootloader broadcasts heartbeat on 0x7DE for ~500 ms after reset.
  - Host sends page data in 8-byte chunks on 0x7DD.
  - Bootloader ACKs each page on 0x7DF.
  - Flash page size: 1024 bytes (STM32F103 medium/high-density).
"""

import argparse
import struct
import sys
import time

try:
    import can
except ImportError:
    sys.exit("Error: python-can not installed.  Run: pip install python-can")

# CANBootloader frame IDs
BL_HEARTBEAT_ID  = 0x7DE   # bootloader -> host: alive (contains UID)
BL_DATA_ID       = 0x7DD   # host -> bootloader: firmware data chunks
BL_ACK_ID        = 0x7DF   # bootloader -> host: page ACK / NACK
BL_CMD_ID        = 0x7DC   # host -> bootloader: command frames

FLASH_PAGE_SIZE  = 1024    # STM32F103 page size in bytes
CHUNK_SIZE       = 8       # CAN frame payload

# Command bytes (sent on BL_CMD_ID)
CMD_IDENTIFY     = 0x00
CMD_GO           = 0x01


def discover_bootloader(bus, uid_filter=None, timeout=2.0):
    """
    Listen for a bootloader heartbeat on BL_HEARTBEAT_ID.
    Returns the 32-bit UID reported by the bootloader, or raises TimeoutError.
    """
    deadline = time.monotonic() + timeout
    print(f"Waiting for bootloader heartbeat on 0x{BL_HEARTBEAT_ID:03X} ...")
    while time.monotonic() < deadline:
        msg = bus.recv(timeout=0.1)
        if msg is None:
            continue
        if msg.arbitration_id == BL_HEARTBEAT_ID and len(msg.data) >= 4:
            uid = struct.unpack_from("<I", msg.data, 0)[0]
            if uid_filter is not None and uid != uid_filter:
                continue
            print(f"  Found bootloader: UID=0x{uid:08X}")
            return uid
    raise TimeoutError(
        "No bootloader heartbeat received within timeout.\n"
        "  Check: board is in reset window, CAN bus is up, baud rate matches."
    )


def send_command(bus, cmd_byte):
    """Send a single-byte command on BL_CMD_ID."""
    msg = can.Message(
        arbitration_id=BL_CMD_ID,
        data=[cmd_byte],
        is_extended_id=False,
    )
    bus.send(msg)


def flash_firmware(bus, data: bytes) -> bool:
    """
    Upload firmware binary to the bootloader.
    Returns True on success.
    """
    # Pad to full page boundary
    pad_len = (-len(data)) % FLASH_PAGE_SIZE
    data = data + b"\xff" * pad_len
    n_pages = len(data) // FLASH_PAGE_SIZE
    print(f"Firmware: {len(data)} bytes  ({n_pages} pages of {FLASH_PAGE_SIZE} B)")

    for page_idx in range(n_pages):
        page_data = data[page_idx * FLASH_PAGE_SIZE : (page_idx + 1) * FLASH_PAGE_SIZE]
        n_chunks  = FLASH_PAGE_SIZE // CHUNK_SIZE

        # Send all chunks for this page
        for chunk_idx in range(n_chunks):
            chunk = page_data[chunk_idx * CHUNK_SIZE : (chunk_idx + 1) * CHUNK_SIZE]
            seq   = (page_idx * n_chunks + chunk_idx) & 0xFF
            payload = bytes([seq]) + chunk[:7]   # seq + up to 7 data bytes
            msg = can.Message(
                arbitration_id=BL_DATA_ID,
                data=payload,
                is_extended_id=False,
            )
            bus.send(msg)
            time.sleep(0.0002)   # ~200 µs inter-chunk gap

        # Wait for ACK
        ack_deadline = time.monotonic() + 1.0
        acked = False
        while time.monotonic() < ack_deadline:
            ack = bus.recv(timeout=0.1)
            if ack is None:
                continue
            if ack.arbitration_id == BL_ACK_ID:
                if len(ack.data) >= 1 and ack.data[0] != 0:
                    print(f"\nNACK on page {page_idx}: code 0x{ack.data[0]:02X}")
                    return False
                acked = True
                break

        if not acked:
            print(f"\nTimeout waiting for ACK on page {page_idx}")
            return False

        pct = (page_idx + 1) * 100 // n_pages
        print(f"  Page {page_idx + 1:3d}/{n_pages}  [{pct:3d}%]", end="\r", flush=True)

    print()  # newline after progress
    return True


def main():
    parser = argparse.ArgumentParser(
        description="CAN firmware uploader (stm32-CANBootloader compatible)"
    )
    parser.add_argument("-d", "--device",   required=True, help="SocketCAN interface, e.g. can0")
    parser.add_argument("-f", "--firmware", required=True, help="Firmware .bin file")
    parser.add_argument("-i", "--uid",      default=None,  help="Device UID in hex (auto-discover if omitted)")
    args = parser.parse_args()

    uid_filter = int(args.uid, 16) if args.uid else None

    with open(args.firmware, "rb") as fh:
        firmware = fh.read()
    print(f"Loaded {args.firmware}: {len(firmware)} bytes")

    bus = can.interface.Bus(channel=args.device, bustype="socketcan")
    try:
        uid = discover_bootloader(bus, uid_filter=uid_filter, timeout=2.0)

        ok = flash_firmware(bus, firmware)

        if ok:
            # Send GO command to boot the new firmware
            send_command(bus, CMD_GO)
            print("Flash complete. Sent GO command — board is booting.")
        else:
            print("Flash FAILED. Use ST-Link to recover.")
            sys.exit(1)

    except TimeoutError as exc:
        print(f"Error: {exc}")
        sys.exit(1)
    finally:
        bus.shutdown()


if __name__ == "__main__":
    main()
