#!/usr/bin/env python3
"""Create a 4 KiB F1C100s boot KV image for fastboot flash kv."""
import struct
import sys

KV_SIZE = 4096
MAGIC = 0x3130564B
ACTIVE_PENDING = 1

# The KV record's own CRC (offset 24) and the AP image body's CRC
# (offset 20, ap_crc32) are checked by two DIFFERENT CRC32 variants in
# this codebase -- this is not a typo, it is how the firmware actually
# works, confirmed against source:
#
#   - ap_crc32 is checked in f1c100s_boot_image.c against NuttX's own
#     crc32()/crc32part() (libs/libc/misc/lib_crc32.c): standard poly
#     0xedb88320, init=0, NO final complement. This is NOT the same as
#     Python's binascii.crc32() / zlib.crc32() (which use init effectively
#     0xffffffff and DO complement at the end) -- using binascii.crc32()
#     here silently produces a value that never matches, and BL rejects
#     every freshly flashed AP as "corrupted" even though the bytes are
#     byte-for-byte correct.
#   - the record's own crc32 (offset 24) is checked against
#     bootkv_crc32() in f1c100s_bootkv.c: standard IEEE 802.3/gzip
#     CRC-32 (init=0xffffffff, WITH final complement) -- that one DOES
#     match binascii.crc32()/zlib.crc32(), so it's left as-is below.

def nuttx_crc32(data: bytes) -> int:
    """Match libs/libc/misc/lib_crc32.c crc32(): poly 0xedb88320,
    init=0, no final complement."""
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xedb88320
            else:
                crc >>= 1
    return crc & 0xffffffff


def ieee_crc32(data: bytes) -> int:
    """Match f1c100s_bootkv.c bootkv_crc32(): standard IEEE 802.3/gzip
    CRC-32, init=0xffffffff, WITH final complement (same as
    zlib.crc32()/binascii.crc32())."""
    import zlib
    return zlib.crc32(data) & 0xffffffff


AP_PARTITION_LIMIT = 0x00300000  # 3 MiB

def main() -> int:
    if len(sys.argv) not in (3, 4):
        print(f"usage: {sys.argv[0]} AP_IMAGE OUTPUT [FAIL_COUNT]", file=sys.stderr)
        return 2
    ap = open(sys.argv[1], "rb").read()
    if len(ap) == 0:
        raise SystemExit("Error: AP image is empty")
    if len(ap) > AP_PARTITION_LIMIT:
        raise SystemExit(f"FATAL ERROR: AP image size {len(ap):,} bytes ({len(ap)/1024:.1f} KiB) exceeds "
                         f"3.0 MB partition limit ({AP_PARTITION_LIMIT:,} bytes)! "
                         f"Exceeded by {len(ap) - AP_PARTITION_LIMIT:,} bytes. "
                         f"Aborting to protect /data partition.")
    fail = int(sys.argv[3], 0) if len(sys.argv) == 4 else 0
    rec = bytearray(28)
    struct.pack_into("<6I", rec, 0, MAGIC, 1, fail, ACTIVE_PENDING,
                     len(ap), nuttx_crc32(ap))
    struct.pack_into("<I", rec, 24, ieee_crc32(bytes(rec[:24])))
    out = bytearray([0xff]) * KV_SIZE
    out[:28] = rec
    open(sys.argv[2], "wb").write(out)
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
