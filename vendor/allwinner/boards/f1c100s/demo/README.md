# F1C100s Demo Board for NuttX

## Overview

This is the NuttX port for the Allwinner F1C100s SoC (ARM926EJ-S core).

## Hardware

Board wiring used by software: [docs/schematic.md](docs/schematic.md).

Original schematic (archive, not a build input):
`arch_porting_methodology/docs/sunxi_doc/boards_sch/SCH_F1C200S小电脑.pdf`.

Vendor review and submit split: [2026-09-20 plan](../../../docs/plans/2026-09-20-f1c100s-vendor-review-submit.md).

## Features

- **CPU**: ARM926EJ-S @ 720MHz
- **SRAM**: 32KB internal SRAM
- **DRAM**: 32MB DDR (external)
- **Peripherals**: UART, GPIO, Timer, SPI, I2C, USB OTG

## Board Configuration

### Memory Layout
- **SRAM**: 0x00000000 - 0x00008000 (32KB)
- **DRAM**: 0x80000000 - 0x82000000 (32MB)

### Console
- **UART0**: 115200 8N1 on PE0/PE1

## Building

### Prerequisites
```bash
sudo apt-get install gcc-arm-none-eabi
```

### Build Steps
```bash
cd /path/to/nuttx
./build.sh vendor/allwinner/boards/f1c100s/demo/configs/nsh
```

### Output
- `nuttx`: ELF executable
- `nuttx.bin`: Raw binary for loading

## Loading and Running

### Using OpenOCD (JTAG)
```bash
openocd -f allwinner_f1c100s.cfg
```

In another terminal:
```bash
telnet localhost 4444
> halt
> load_image nuttx.bin 0x80000000
> reg pc 0x80000000
> resume
```

### Using FEL Mode (USB Boot)
```bash
sunxi-fel -p spl f1c_spl.bin
sunxi-fel write 0x80000000 nuttx.bin
sunxi-fel exe 0x80000000
```

## Serial Console

Connect to UART0 at 115200 baud:
```bash
minicom -D /dev/ttyUSB0 -b 115200
```

Expected output:
```
NuttShell (NSH) NuttX-12.x.x
nsh>
```

## Configuration Options

### Enabling Features

Edit `vendor/allwinner/boards/f1c100s/demo/configs/nsh/defconfig`:

```
# Enable LEDs
CONFIG_ARCH_LEDS=y

# Enable buttons
CONFIG_ARCH_BUTTONS=y

# Enable GPIO
CONFIG_F1C_GPIO=y
CONFIG_DEV_GPIO=y

# Enable I2C
CONFIG_I2C=y

# Enable SPI
CONFIG_SPI=y
```

## Hardware Setup

### UART0 Connection
- PE0 (RX) - Connect to USB-Serial TX
- PE1 (TX) - Connect to USB-Serial RX
- GND - Connect to USB-Serial GND

### JTAG Connection (if available)
- TMS, TCK, TDI, TDO - Connect to JTAG debugger

## Current Limitations

- DRAM initialization assumes specific timings
- No support for display/LCD yet
- USB OTG not implemented
- SD/MMC driver not included

## References

- [F1C100s Datasheet](https://linux-sunxi.org/F1C100s)
- [NuttX Documentation](https://nuttx.apache.org/docs/latest/)
- [ARM926EJ-S TRM](https://developer.arm.com/documentation)

## License

Apache License 2.0
