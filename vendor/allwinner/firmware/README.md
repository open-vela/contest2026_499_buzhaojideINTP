# F1C100s firmware blobs

Prebuilt images for later flashing. awboot source stays unpublished for now.

**xfel is not firmware.** It is a host FEL tool (`libusb`, x86-64). Do not embed it in the image or vendor the binary. Use the machine-local `xfel` (`~/.local/bin/xfel` → `~/mywork/xfel/xfel`).

## Layout (SPI-NOR 16 MiB)

| Offset     | Size   | File              |
|------------|--------|-------------------|
| `0x000000` | 24 KiB | `awboot.bin`      |
| `0x010000` | 640 KiB| `bl/nuttx.bin`    |
| `0x0B0000` | 4 KiB  | KV (runtime, empty)|
| `0x0B1000` | 3 MiB  | one of `nsh*/nuttx.bin` |

Do not write anything except `awboot.bin` to `0x0`. Host scripts: `../tools/README.md`.

## Images (contest workspace, 2026-09-20)

| File | Size | SHA-256 |
|------|------|---------|
| `awboot.bin` | 24576 | `c9d1f433450ce207f7ddaf6e95e344aee4feeb94c3b9577d22cf703b0a03e188` |
| `bl/nuttx.bin` | 173268 | `98cd1a3fbced49c28ed6fda0c1929bcd560d189dc7479bf5f3c005c115a5f820` |
| `nsh/nuttx.bin` | 793964 | `cd06959122a5e5fc5c1e9589b3378f35676bbb4478aa0debdc3261049547c94e` |
| `nsh-net/nuttx.bin` | 840952 | `97e8446a9c40c44ce319710cb61ffed9e9148b6455bc3791b66a19d9eab514e6` |
| `nsh-cdc/nuttx.bin` | 676224 | `71d6bc5a333a254d8986bf37732a591cf7720fc3978cd135b3e4f10bf52e4f94` |
| `nsh-sdio/nuttx.bin` | 782680 | `a1c51c351e2882dc8108a31fb15fdf512b0cccf3fbe140c017f74b50704219c4` |
