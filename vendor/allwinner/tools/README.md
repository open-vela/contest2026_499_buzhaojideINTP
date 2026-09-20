# F1C100s host tools

xfel stays on the host (`~/.local/bin/xfel`). Do not vendor that binary.

| Script | What |
|--------|------|
| `boot-dram.sh IMAGE` | FEL RAM boot: `ddr` + write `0x80000000` + stub `0x80100000` + `exec`. No NOR write. Image < 1 MiB. |
| `flash-spinor.sh` | FEL SPI-NOR write. `--awboot` / `--bl` / `--ap` / `--erase-kv` / `--reset`. Refuses `0x0` unless `--awboot`. |
| `flash-ap-fastboot.sh IMAGE` | Fastboot `ap` + generated KV. Waits for gadget. Prefer spinor if USB erase is flaky. |
| `mkbootkv.py` | 4 KiB KV (NuttX CRC on AP body, IEEE CRC on record). |
| `check_firmware_size.py` | AP 3 MiB budget (also POSTBUILD). |
| `wait_fel.sh` | Block until `AWUSBFEX`. |
| `bl_ap_boot_test.sh` | `xfel reset` + UART check for `st7789:` / `nsh>`. NOR must already be programmed. |

Not copied: T113 `linux_verify` / `fel_ctrl.py`, stale QEMU `mkflash.sh`, OpenOCD/JLink one-offs.
