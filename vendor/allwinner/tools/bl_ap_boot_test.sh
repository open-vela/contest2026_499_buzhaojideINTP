#!/bin/sh
# Reset NOR-resident BL/AP and check UART for st7789 / nsh.
# Assumes FEL is up and flash already holds the images. Never writes 0x0.
set -eu

export PATH="${HOME}/.local/bin:${PATH}"
XFEL=${XFEL:-xfel}
LOGDIR=${LOGDIR:-/tmp/f1c-test}
UART0=${UART0:-/dev/ttyUSB0}
UART2=${UART2:-/dev/ttyUSB2}

mkdir -p "$LOGDIR"
export LOGDIR UART0 UART2
date --iso-8601=ns | tee "$LOGDIR/t0.txt"

python3 - <<'PY' &
import os, serial, threading, time
from pathlib import Path
logdir = Path(os.environ["LOGDIR"])
logdir.mkdir(parents=True, exist_ok=True)
Path(logdir / "cap.pid").write_text(str(os.getpid()))
ports = {
    os.environ["UART0"]: logdir / "uart0.bin",
    os.environ["UART2"]: logdir / "uart2.bin",
}

def cap(dev, path):
    try:
        ser = serial.Serial(dev, 115200, timeout=0.2)
    except Exception as e:
        Path(str(path) + ".err").write_text(repr(e))
        return
    with open(path, "wb", buffering=0) as f:
        while True:
            try:
                data = ser.read(4096)
            except Exception as e:
                f.write(b"\n#ERR %s\n" % repr(e).encode())
                break
            if data:
                f.write(data)
                f.flush()

for d, p in ports.items():
    Path(p).write_bytes(b"")
    threading.Thread(target=cap, args=(d, p), daemon=True).start()
Path(logdir / "cap.ready").write_text("ready\n")
while True:
    time.sleep(1)
PY
echo $! > "$LOGDIR/cap_wrapper.pid"
i=0
while [ "$i" -lt 20 ]; do
  [ -f "$LOGDIR/cap.ready" ] && break
  i=$((i + 1))
  sleep 0.1
done

echo "FEL_BEFORE=$("$XFEL" version 2>&1 | tr '\n' ' ')" | tee "$LOGDIR/fel_before.txt"
echo RESET_AT="$(date --iso-8601=ns)" | tee "$LOGDIR/reset_at.txt"
"$XFEL" reset
echo RESET_EXIT=$? | tee -a "$LOGDIR/reset_at.txt"

i=0
while [ "$i" -lt 40 ]; do
  i=$((i + 1))
  sleep 1
  if lsusb | grep -q 18d1; then
    echo "USB_GADGET_AT_SEC=$i $(date --iso-8601=ns)" | tee "$LOGDIR/usb_at.txt"
    sleep 5
    break
  fi
done
lsusb | tee "$LOGDIR/lsusb.txt"
journalctl -k --since "50 sec ago" --no-pager \
  | grep -iE "usb 1-2.2|1f3a|18d1|NuttX|Debug|Composite" \
  | tee "$LOGDIR/journal.txt" || true

if [ -f "$LOGDIR/cap.pid" ]; then kill "$(cat "$LOGDIR/cap.pid")" 2>/dev/null || true; fi
if [ -f "$LOGDIR/cap_wrapper.pid" ]; then kill "$(cat "$LOGDIR/cap_wrapper.pid")" 2>/dev/null || true; fi
sleep 0.3

python3 - <<'PY'
import os, re
from pathlib import Path
logdir = Path(os.environ["LOGDIR"])
chunks = []
for name in ("uart0.bin", "uart2.bin"):
    p = logdir / name
    d = p.read_bytes() if p.exists() else b""
    print(f"FILE {name} bytes={len(d)}")
    ss = re.findall(rb"[\x20-\x7e]{4,}", d)
    Path(str(p) + ".strings").write_text("\n".join(x.decode() for x in ss) + "\n")
    for x in ss[:150]:
        print(f"{name}: {x.decode()}")
    chunks.append(d)
blob = b"\n".join(chunks)
ok_st = b"st7789:" in blob
ok_nsh = b"nsh>" in blob or b"NuttShell" in blob
print("PASS_ST7789", ok_st)
print("PASS_NSH", ok_nsh)
(logdir / "result.txt").write_text(
    f"st7789={ok_st} nsh={ok_nsh} u0={len(chunks[0])} u2={len(chunks[1])}\n")
PY
"$XFEL" version 2>&1 | tee "$LOGDIR/fel_after.txt" || true
