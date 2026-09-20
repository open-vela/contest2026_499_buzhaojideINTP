#!/bin/sh
# FEL DRAM boot: ddr init, load AP at 0x80000000, jump stub at 0x80100000.
# Does not touch SPI-NOR. Image must be < 1 MiB so it does not overlap the stub.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export PATH="${HOME}/.local/bin:${PATH}"
XFEL=${XFEL:-xfel}
IMAGE=${1:?usage: $0 NUTTX.bin}
STUB=${STUB:-$SCRIPT_DIR/jump_stub.bin}
LOAD_ADDR=0x80000000
STUB_ADDR=0x80100000
MAX_SIZE=$((0x100000))

[ -f "$IMAGE" ] || { printf '%s\n' "missing $IMAGE" >&2; exit 1; }
[ -f "$STUB" ] || { printf '%s\n' "missing $STUB" >&2; exit 1; }

sz=$(wc -c < "$IMAGE")
if [ "$sz" -ge "$MAX_SIZE" ]; then
  printf '%s\n' "image ${sz} bytes overlaps stub at $STUB_ADDR (max $MAX_SIZE)" >&2
  exit 1
fi

"$XFEL" version 2>/dev/null | grep -q AWUSBFEX || {
  printf '%s\n' 'FEL not present (xfel version).' >&2
  exit 1
}

"$XFEL" ddr
"$XFEL" write "$LOAD_ADDR" "$IMAGE"
"$XFEL" write "$STUB_ADDR" "$STUB"
"$XFEL" exec "$STUB_ADDR"
