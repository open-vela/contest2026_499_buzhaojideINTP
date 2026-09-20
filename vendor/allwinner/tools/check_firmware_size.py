#!/usr/bin/env python3
"""
Firmware Size Predictor & Safety Threshold Guard for Allwinner F1C100s.

Inspects NuttX AP firmware (ELF / binary) and verifies layout constraints
against physical memory limits:
  - Flash Partition: AP budget (3.0 MiB / 3,145,728 bytes)
  - Flash Internal Storage: DATA partition (12.308 MiB / 12,906,496 bytes)
  - RAM Budget: Physical DDR1 (32.0 MiB / 33,554,432 bytes)
  - Heap Prediction: Dynamic memory headroom for RTOS heap & buffers

Exits with code 1 if AP firmware exceeds the partition limit, preventing
accidental overwrite of the /data partition.
"""

import sys
import os
import subprocess
import re
import struct
import zlib

# Physical & Partition Geometry Constants
FLASH_TOTAL_SIZE = 16 * 1024 * 1024        # 16 MiB SPI NOR
FLASH_SPL_SIZE   = 24 * 1024               # 24 KiB
FLASH_RESV_SIZE  = 40 * 1024               # 40 KiB
FLASH_BL_SIZE    = 640 * 1024              # 640 KiB
FLASH_KV_SIZE    = 4 * 1024                # 4 KiB
FLASH_AP_SIZE    = 3 * 1024 * 1024         # 3.00 MiB (0x300000)
FLASH_DATA_SIZE  = FLASH_TOTAL_SIZE - (FLASH_SPL_SIZE + FLASH_RESV_SIZE + FLASH_BL_SIZE + FLASH_KV_SIZE + FLASH_AP_SIZE) # 12.308 MiB

RAM_TOTAL_SIZE   = 32 * 1024 * 1024        # 32 MiB SIP DDR1
RAM_PGTABLE_SIZE = 16 * 1024               # 16 KiB MMU page table at RAM top

WARN_THRESHOLD_PERCENT = 85.0              # Warn if > 85% capacity


def render_bar(pct: float, width: int = 24) -> str:
    filled = int(round(width * min(pct, 100.0) / 100.0))
    empty = width - filled
    return "█" * filled + "░" * empty


def stamp_egon_header(bin_path: str):
    """Update eGON.NTX header in nuttx.bin with actual length and checksum."""
    if not bin_path or not os.path.exists(bin_path):
        return None
    with open(bin_path, "r+b") as f:
        data = bytearray(f.read())
        if len(data) < 96:
            return None
        magic = bytes(data[4:12])
        if magic not in (b"eGON.NTX", b"eGON.BT0"):
            return None

        # 4-byte align the binary
        pad = (4 - (len(data) % 4)) % 4
        if pad > 0:
            data += b"\x00" * pad
        length = len(data)

        # 1. Update length at +0x10
        struct.pack_into("<I", data, 0x10, length)

        # 2. Compute eGON checksum with stamp 0x5F0A6C39
        struct.pack_into("<I", data, 0x0C, 0x5F0A6C39)
        words = struct.unpack(f"<{length // 4}I", data)
        csum = sum(words) & 0xFFFFFFFF
        struct.pack_into("<I", data, 0x0C, csum)

        # 3. Calculate full IEEE 802.3 CRC32 for BL verification
        full_crc = zlib.crc32(data) & 0xFFFFFFFF

        f.seek(0)
        f.write(data)
        f.truncate(length)
        return length, csum, full_crc


def parse_elf_sections(elf_path: str):
    """Attempt to parse sections via arm-none-eabi-size or readelf."""
    tool = "arm-none-eabi-size"
    try:
        res = subprocess.run([tool, "-A", "-d", elf_path],
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                             text=True, check=True)
    except Exception:
        return None

    sections = {}
    for line in res.stdout.splitlines():
        parts = line.strip().split()
        if len(parts) >= 3 and parts[1].isdigit() and parts[2].isdigit():
            sec_name = parts[0]
            sec_size = int(parts[1])
            sec_addr = int(parts[2])
            if sec_addr >= 0x80000000:  # Loaded in DRAM
                sections[sec_name] = sec_size
    return sections


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <nuttx.bin or nuttx ELF> [optional: nuttx ELF]")
        sys.exit(1)

    target_path = sys.argv[1]
    if not os.path.exists(target_path):
        print(f"Error: File '{target_path}' not found.", file=sys.stderr)
        sys.exit(1)

    bin_path = None
    elf_path = None

    if target_path.endswith(".bin"):
        bin_path = target_path
        candidate_elf = target_path[:-4]  # strip .bin
        if os.path.exists(candidate_elf):
            elf_path = candidate_elf
    else:
        elf_path = target_path
        candidate_bin = target_path + ".bin"
        if os.path.exists(candidate_bin):
            bin_path = candidate_bin

    if len(sys.argv) >= 3 and os.path.exists(sys.argv[2]):
        elf_path = sys.argv[2]

    # Measure binary size
    if bin_path and os.path.exists(bin_path):
        ap_bin_size = os.path.getsize(bin_path)
    elif elf_path:
        # Fallback: estimate binary size from ELF loadable sections
        ap_bin_size = os.path.getsize(elf_path)
    else:
        ap_bin_size = os.path.getsize(target_path)

    # Calculate Flash metrics
    flash_pct = (ap_bin_size / FLASH_AP_SIZE) * 100.0
    flash_remain = FLASH_AP_SIZE - ap_bin_size

    # RAM metrics
    sections = parse_elf_sections(elf_path) if elf_path else None
    text_size = 0
    data_size = 0
    bss_size = 0
    static_ram_total = ap_bin_size

    if sections:
        for name, sz in sections.items():
            if name in (".text", ".egon_header", ".ARM.extab", ".ARM.exidx", ".rodata"):
                text_size += sz
            elif name in (".data", ".sdata"):
                data_size += sz
            elif name in (".bss", ".sbss", ".crashlog"):
                bss_size += sz
        static_ram_total = text_size + data_size + bss_size

    ram_pct = (static_ram_total / RAM_TOTAL_SIZE) * 100.0
    pred_heap = max(0, RAM_TOTAL_SIZE - static_ram_total - RAM_PGTABLE_SIZE)

    # Print Report
    print("=" * 68)
    print("       ALLWINNER F1C100S FIRMWARE SIZE & MEMORY BUDGET REPORT")
    print("=" * 68)

    # 1. Flash Partition Table
    print("  [SPI-NOR FLASH 16 MiB PARTITION LAYOUT]")
    print(f"    0x000000 - 0x006000 | SPL (awboot)     : {FLASH_SPL_SIZE // 1024:6d} KiB")
    print(f"    0x006000 - 0x010000 | RESV (align)     : {FLASH_RESV_SIZE // 1024:6d} KiB")
    print(f"    0x010000 - 0x0B0000 | BL (miniboot)    : {FLASH_BL_SIZE // 1024:6d} KiB")
    print(f"    0x0B0000 - 0x0B1000 | KV (boot config) : {FLASH_KV_SIZE // 1024:6d} KiB")
    print(f"    0x0B1000 - 0x3B1000 | AP (NuttX RTOS)  : {FLASH_AP_SIZE // (1024*1024):6.2f} MiB ({FLASH_AP_SIZE:,} B) [TARGET]")
    print(f"    0x3B1000 - 0x1000000| DATA (LittleFS)  : {FLASH_DATA_SIZE / (1024*1024):6.2f} MiB ({FLASH_DATA_SIZE:,} B) [/data]")
    print("-" * 68)

    # 2. Flash AP Utilization
    flash_bar = render_bar(flash_pct, width=20)
    print("  [AP FLASH PARTITION BUDGET (Target Limit: 3.00 MiB)]")
    print(f"    Current Size : {ap_bin_size:,} Bytes ({ap_bin_size/1024:.2f} KiB / {ap_bin_size/(1024*1024):.3f} MiB)")
    print(f"    Max Capacity : {FLASH_AP_SIZE:,} Bytes (3,072.00 KiB / 3.000 MiB)")
    print(f"    Utilization  : [{flash_bar}] {flash_pct:5.2f}%")
    if flash_remain >= 0:
        print(f"    Free Margin  : {flash_remain:,} Bytes ({flash_remain/1024:.2f} KiB / {flash_remain/(1024*1024):.3f} MiB remaining)")
    else:
        overflow = -flash_remain
        print(f"    OVERFLOW     : {overflow:,} Bytes ({overflow/1024:.2f} KiB EXCEEDED!)")
    print("-" * 68)

    # 3. RAM Budget & Heap Prediction
    ram_bar = render_bar(ram_pct, width=20)
    print("  [RAM / SIP DDR1 BUDGET (32.0 MiB Physical DRAM)]")
    if sections:
        print(f"    Code/ReadOnly: {text_size:,} B ({text_size/1024:.1f} KiB)  [.text, rodata]")
        print(f"    Initialized  : {data_size:,} B ({data_size/1024:.1f} KiB)  [.data]")
        print(f"    Uninitialized: {bss_size:,} B ({bss_size/1024:.1f} KiB)  [.bss, crashlog]")
    print(f"    Static Total : {static_ram_total:,} Bytes ({static_ram_total/1024:.2f} KiB / {static_ram_total/(1024*1024):.3f} MiB)")
    print(f"    Utilization  : [{ram_bar}] {ram_pct:5.2f}%")
    print(f"    MMU PgTable  : {RAM_PGTABLE_SIZE // 1024} KiB reserved at top of RAM (0x81FFC000)")
    print(f"    Predicted Heap: ~{pred_heap:,} Bytes ({pred_heap/(1024*1024):.2f} MiB dynamic memory)")
    print("=" * 68)

    # 4. Evaluation & Interception
    if ap_bin_size > FLASH_AP_SIZE:
        print("\n\033[1;31m>>> FATAL ERROR: FIRMWARE EXCEEDS 3.0 MB PARTITION BUDGET! <<<\033[0m")
        print(f"Firmware size ({ap_bin_size:,} B) is {ap_bin_size - FLASH_AP_SIZE:,} B larger than partition.")
        print("Flashing this image WOULD DESTROY data in the /data LittleFS partition!")
        print("Aborting build/flash pipeline.\n")
        sys.exit(1)

    # Stamp eGON header in nuttx.bin if available
    if bin_path:
        stamped = stamp_egon_header(bin_path)
        if stamped:
            length, csum, full_crc = stamped
            print(f"  [eGON Header] Auto-stamped {os.path.basename(bin_path)}:")
            print(f"    Length       : {length:,} Bytes")
            print(f"    eGON Checksum: 0x{csum:08X}")
            print(f"    CRC-32 (BL)  : 0x{full_crc:08X}")
            print("-" * 68)

    if flash_pct >= WARN_THRESHOLD_PERCENT:
        print(f"\n\033[1;33m>>> WARNING: AP firmware is using {flash_pct:.1f}% of partition! <<<\033[0m")
        print(f"Only {flash_remain/1024:.1f} KiB headroom remains before hitting 3.0 MB.\n")
        sys.exit(0)
    else:
        print(f"\n\033[1;32m[PASS] Size checks OK: AP uses {flash_pct:.1f}% of 3MB partition (headroom: {flash_remain/1024:.1f} KiB).\033[0m\n")
        sys.exit(0)


if __name__ == "__main__":
    main()
