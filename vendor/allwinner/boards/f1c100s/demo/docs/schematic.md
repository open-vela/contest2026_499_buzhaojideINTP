# F1C100s demo 板接线（软件用）

原件: `arch_porting_methodology/docs/sunxi_doc/boards_sch/SCH_F1C200S小电脑.pdf`
板名: 嘉立创 TITLE F1C200S（丝印「小电脑」）
核对: 2026-09-19 原理图 + 真机

SoC 手册只说明脚 *能* mux 成什么。本表是这块板 *实际* 接到哪。`board.h` 与 bringup 以本表为准。

## 用户键

不是 LRADC 分压。不要开 `CONFIG_F1C100S_LRADC`。

| 丝印 | 开关 | 脚 | 电路 |
|---|---|---|---|
| L | SW1 | PE2 | 47k 上拉到 3.3V，按下接地 |
| CK | SW2 | PE3 | 同上 |
| R | SW3 | PE4 | 同上 |

SW4 接芯片 RESET，不是 GPIO。PE4 只接 SW3。

## ST7789 240×240（SPI1）

| 网名 | 脚 | 功能 |
|---|---|---|
| SPI1_CS | PE7 | CS，function 4 |
| SPI1_MOSI | PE8 | MOSI，function 4 |
| SPI1_SCLK | PE9 | CLK，function 4 |
| SPI1_DC | PE5 | D/C，GPIO |
| SPI1_RST | PE10 | Reset，GPIO |

PA0–PA3 function 6 也是合法 SPI1 mux，这块板接到电阻触摸（TPX/TPY），不要当屏用。

## 显示与背光

| 用途 | 脚 |
|---|---|
| RGB LCD 数据/同步 | PD0–PD21，function 2 |
| 背光 AP3019AKTR CTRL | PE6 = PWM1 |

## 控制台与存储

| 用途 | 脚 |
|---|---|
| UART0 | PE0=RX，PE1=TX，function 5；板载 CH340 |
| SDIO / TF | PF0–PF5，function 2 |
| JTAG | 同 PF，function 3；与 SD 卡互斥 |
| SPI0 NOR（W25） | SPI0_CS / SCLK / MOSI / MISO（见原理图 SPI0 网名） |

I2C0 默认 PE11/PE12。PD0/PD12 也能走 I2C0，与 RGB 冲突。

## 音频 CN1（PJ-342）

| 网名 | 说明 |
|---|---|
| HPL / HPR | 耳机左右 |
| HPCOM / HPCOMFB | 无电容耳机地，经反馈环 |
| LINL | 原理图有焊盘，本板线路未当输入用 |

## 电源与复位（软件相关）

- 主晶振 24 MHz
- USB Type-C 到芯片 USB_DP/DM
- 用户键以外的 RESET 为硬件复位

Flash 分区（SPL / BL / KV / AP / DATA）见 `chips/f1c100s/f1c100s_partitions.h`。那是产品布局，不是本原理图页上的走线。
