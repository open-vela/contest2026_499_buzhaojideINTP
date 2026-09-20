#!/bin/sh
# Flash AP + KV over Fastboot. Device must already be in BL fastboot.
# Reliable NOR path is flash-spinor.sh (xfel); use this when USB gadget is up.
set -eu

AP_IMAGE=${1:?usage: $0 AP_IMAGE}
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
KV_IMAGE=${AP_IMAGE}.kv.bin
WAIT_SEC=${WAIT_SEC:-60}

"$SCRIPT_DIR/check_firmware_size.py" "$AP_IMAGE"
"$SCRIPT_DIR/mkbootkv.py" "$AP_IMAGE" "$KV_IMAGE"

printf '%s\n' "Waiting up to ${WAIT_SEC}s for F1C100s fastboot..."
i=0
while ! fastboot devices 2>/dev/null | grep -q '[[:alnum:]]'; do
  i=$((i + 1))
  if [ "$i" -ge "$WAIT_SEC" ]; then
    rm -f "$KV_IMAGE"
    printf '%s\n' 'No fastboot device.' >&2
    exit 1
  fi
  sleep 1
done

fastboot flash ap "$AP_IMAGE"
fastboot erase kv
fastboot flash kv "$KV_IMAGE"
fastboot reboot
rm -f "$KV_IMAGE"
printf '%s\n' 'AP flash complete; device reboot requested.'
