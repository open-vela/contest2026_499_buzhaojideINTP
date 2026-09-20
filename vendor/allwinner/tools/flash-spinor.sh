#!/bin/sh
# Write F1C100s SPI-NOR via xfel. Never writes 0x0 unless --awboot is given.
# Layout: awboot 0x0 (24KiB) / BL 0x10000 (640KiB) / KV 0xB0000 (4KiB) / AP 0xB1000 (3MiB)
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
export PATH="${HOME}/.local/bin:${PATH}"
XFEL=${XFEL:-xfel}

AWBOOT=
BL=
AP=
ERASE_KV=0
DO_RESET=0

usage() {
  printf '%s\n' "usage: $0 [--awboot FILE] [--bl FILE] [--ap FILE] [--erase-kv] [--reset]" >&2
  exit 2
}

while [ $# -gt 0 ]; do
  case "$1" in
    --awboot) [ $# -ge 2 ] || usage; AWBOOT=$2; shift 2 ;;
    --bl)     [ $# -ge 2 ] || usage; BL=$2; shift 2 ;;
    --ap)     [ $# -ge 2 ] || usage; AP=$2; shift 2 ;;
    --erase-kv) ERASE_KV=1; shift ;;
    --reset)    DO_RESET=1; shift ;;
    -h|--help) usage ;;
    *) usage ;;
  esac
done

if [ -z "$AWBOOT" ] && [ -z "$BL" ] && [ -z "$AP" ] && [ "$ERASE_KV" -eq 0 ]; then
  usage
fi

need_file() {
  [ -f "$1" ] || { printf '%s\n' "missing file: $1" >&2; exit 1; }
}

need_size() {
  sz=$(wc -c < "$1")
  if [ "$sz" -gt "$2" ]; then
    printf '%s\n' "$1 is ${sz} bytes, max $2" >&2
    exit 1
  fi
  if [ "$sz" -eq 0 ]; then
    printf '%s\n' "$1 is empty" >&2
    exit 1
  fi
}

"$XFEL" version 2>/dev/null | grep -q AWUSBFEX || {
  printf '%s\n' 'FEL not present (xfel version).' >&2
  exit 1
}

if [ -n "$AWBOOT" ]; then
  need_file "$AWBOOT"
  need_size "$AWBOOT" $((24 * 1024))
  printf '%s\n' "spinor write 0x0 $AWBOOT"
  "$XFEL" spinor write 0x0 "$AWBOOT"
fi

if [ -n "$BL" ]; then
  need_file "$BL"
  need_size "$BL" $((640 * 1024))
  printf '%s\n' "spinor write 0x10000 $BL"
  "$XFEL" spinor write 0x10000 "$BL"
fi

if [ "$ERASE_KV" -eq 1 ]; then
  printf '%s\n' "spinor erase 0xB0000 0x1000"
  "$XFEL" spinor erase 0xB0000 0x1000
fi

if [ -n "$AP" ]; then
  need_file "$AP"
  "$SCRIPT_DIR/check_firmware_size.py" "$AP"
  printf '%s\n' "spinor write 0xB1000 $AP"
  "$XFEL" spinor write 0xB1000 "$AP"
fi

if [ "$DO_RESET" -eq 1 ]; then
  "$XFEL" reset
fi
