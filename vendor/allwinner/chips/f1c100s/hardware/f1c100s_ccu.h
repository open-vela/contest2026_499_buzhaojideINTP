/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_ccu.h
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_CCU_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_CCU_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Clock Control Unit (CCU) Base Address */

#define CCU_BASE              0x01c20000

/* Clock Gating Registers */

#define CCU_BUS_CLK_GATING0   0x60
#define CCU_BUS_CLK_GATING1   0x64
#define CCU_BUS_CLK_GATING2   0x68

/* Bus Clock Gating 0 Bits */

#define CCU_BUS_CLK_GATING0_USBOTG    (1 << 24)
#define CCU_BUS_CLK_GATING0_SPI1      (1 << 21)
#define CCU_BUS_CLK_GATING0_SPI0      (1 << 20)
#define CCU_BUS_CLK_GATING0_SDC0      (1 << 8)
#define CCU_BUS_CLK_GATING0_DMA       (1 << 6)

/* SD/MMC Clock Register */

#define CCU_SDMMC0_CLK        0x88  /* SD/MMC0 Clock Register */

/* SD/MMC Clock Register Bits */

#define CCU_SDMMC_CLK_ENABLE      (1 << 31)  /* Clock Enable */
#define CCU_SDMMC_CLK_SRC_OSC24M  (0 << 24)  /* Clock Source: OSC24M */
#define CCU_SDMMC_CLK_SRC_PLL     (1 << 24)  /* Clock Source: PLL_PERIPH */
#define CCU_SDMMC_CLK_SRC_MASK    (3 << 24)  /* Clock Source Mask */
#define CCU_SDMMC_CLK_RATIO_N(n)  (((n) & 0x3) << 16)  /* Pre-divider N (0-3) */
#define CCU_SDMMC_SAMPLE_PHASE(p) (((p) & 0x7) << 20)  /* Sample Phase (0-7: 0=0, 1=75/90, 2=180) */
#define CCU_SDMMC_OUTPUT_PHASE(p) (((p) & 0x7) << 8)   /* Output Phase (0-7: 0=0, 1=90, 2=180) */
#define CCU_SDMMC_CLK_RATIO_M(m)  (((m) & 0xf) << 0)   /* Divider M (0-15) */

/* Bus Clock Gating 2 Bits (0x068) - APB peripherals */

#define CCU_BUS_CLK_GATING2_CODEC     (1 << 0)
#define CCU_BUS_CLK_GATING2_I2C0      (1 << 16)
#define CCU_BUS_CLK_GATING2_I2C1      (1 << 17)
#define CCU_BUS_CLK_GATING2_I2C2      (1 << 18)
#define CCU_BUS_CLK_GATING2_GPIO      (1 << 19)
#define CCU_BUS_CLK_GATING2_UART0     (1 << 20)
#define CCU_BUS_CLK_GATING2_UART1     (1 << 21)
#define CCU_BUS_CLK_GATING2_UART2     (1 << 22)

/* Soft Reset Registers */

#define CCU_BUS_SOFT_RST0     0x2c0
#define CCU_BUS_SOFT_RST1     0x2c4
#define CCU_BUS_SOFT_RST2     0x2d0

/* Bus Soft Reset 0 Bits (0x2c0) */

#define CCU_BUS_SOFT_RST0_USBOTG      (1 << 24)
#define CCU_BUS_SOFT_RST0_SPI1        (1 << 21)
#define CCU_BUS_SOFT_RST0_SPI0        (1 << 20)
#define CCU_BUS_SOFT_RST0_SDC0        (1 << 8)
#define CCU_BUS_SOFT_RST0_DMA         (1 << 6)

/* Bus Soft Reset 2 Bits (0x2d0) */

#define CCU_BUS_SOFT_RST2_CODEC       (1 << 0)
#define CCU_BUS_SOFT_RST2_I2C0        (1 << 16)
#define CCU_BUS_SOFT_RST2_I2C1        (1 << 17)
#define CCU_BUS_SOFT_RST2_I2C2        (1 << 18)
#define CCU_BUS_SOFT_RST2_UART0       (1 << 20)
#define CCU_BUS_SOFT_RST2_UART1       (1 << 21)
#define CCU_BUS_SOFT_RST2_UART2       (1 << 22)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: f1c100s_clock_init
 *
 * Description:
 *   Initialize system clocks including PLLs and bus clocks.
 *   Configures CPU, AHB, and APB clocks to standard frequencies.
 *
 ****************************************************************************/

void f1c100s_clock_init(void);

/****************************************************************************
 * Name: f1c100s_uart0_enable
 *
 * Description:
 *   Enable UART0 clock and deassert reset.
 *
 ****************************************************************************/

void f1c100s_uart0_enable(void);

/****************************************************************************
 * Name: f1c100s_clk_enable
 *
 * Description:
 *   Enable clock for a specific peripheral.
 *
 * Input Parameters:
 *   gate_reg - Clock gating register offset (e.g., CCU_BUS_CLK_GATING0)
 *   bit      - Bit position in the register
 *
 ****************************************************************************/

void f1c100s_clk_enable(uint32_t gate_reg, uint8_t bit);

/****************************************************************************
 * Name: f1c100s_clk_disable
 *
 * Description:
 *   Disable clock for a specific peripheral.
 *
 * Input Parameters:
 *   gate_reg - Clock gating register offset (e.g., CCU_BUS_CLK_GATING0)
 *   bit      - Bit position in the register
 *
 ****************************************************************************/

void f1c100s_clk_disable(uint32_t gate_reg, uint8_t bit);

/****************************************************************************
 * Name: f1c100s_get_cpu_freq
 *
 * Description:
 *   Get current CPU clock frequency in Hz.
 *
 * Returned Value:
 *   CPU frequency in Hz
 *
 ****************************************************************************/

uint32_t f1c100s_get_cpu_freq(void);

/****************************************************************************
 * Name: f1c100s_get_ahb_freq
 *
 * Description:
 *   Get current AHB bus clock frequency in Hz.
 *
 * Returned Value:
 *   AHB frequency in Hz
 *
 ****************************************************************************/

uint32_t f1c100s_get_ahb_freq(void);

/****************************************************************************
 * Name: f1c100s_get_apb_freq
 *
 * Description:
 *   Get current APB bus clock frequency in Hz.
 *
 * Returned Value:
 *   APB frequency in Hz
 *
 ****************************************************************************/

uint32_t f1c100s_get_apb_freq(void);

#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_CCU_H */
