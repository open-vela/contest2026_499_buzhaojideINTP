/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c_clock.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <arch/board/board.h>
#include "chip.h"
#include "arm_internal.h"
#include "f1c100s_softreset.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CCU_BASE 0x01c20000

#define CCU_PLL_CPU_CTRL      (CCU_BASE + 0x000)
#define CCU_PLL_AUDIO_CTRL    (CCU_BASE + 0x008)
#define CCU_PLL_VIDEO_CTRL    (CCU_BASE + 0x010)
#define CCU_PLL_VE_CTRL       (CCU_BASE + 0x018)
#define CCU_PLL_DDR_CTRL      (CCU_BASE + 0x020)
#define CCU_PLL_PERIPH_CTRL   (CCU_BASE + 0x028)
#define CCU_CPU_CFG           (CCU_BASE + 0x050)
#define CCU_AHB_APB_CFG       (CCU_BASE + 0x054)
#define CCU_BUS_CLK_GATE0     (CCU_BASE + 0x060)
#define CCU_BUS_CLK_GATE1     (CCU_BASE + 0x064)
#define CCU_BUS_CLK_GATE2     (CCU_BASE + 0x068)
#define CCU_BUS_SOFT_RST0     (CCU_BASE + 0x2c0)
#define CCU_BUS_SOFT_RST1     (CCU_BASE + 0x2c4)
#define CCU_BUS_SOFT_RST2     (CCU_BASE + 0x2d0)

#define CLK_CPU_SRC_OSC24M    1
#define CLK_CPU_SRC_PLL_CPU   2

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void sdelay(int loops)
{
  volatile int i;
  for (i = 0; i < loops; i++);
}

static void clk_cpu_config(int src)
{
  uint32_t val;
  val = getreg32(CCU_CPU_CFG);
  val &= ~(0x3 << 16);
  val |= ((src & 0x3) << 16);
  putreg32(val, CCU_CPU_CFG);
}

static void clk_pll_init(uint32_t reg, uint32_t mul, uint32_t div)
{
  uint32_t val;
  val = getreg32(reg);
  val &= ~((0x1f << 8) | (0x3 << 4)); // Clear mul and div
  val |= ((mul - 1) << 8) | ((div - 1) << 4);
  putreg32(val, reg);
}

static void clk_pll_enable(uint32_t reg)
{
  uint32_t val;
  val = getreg32(reg);
  val |= (1 << 31);
  putreg32(val, reg);
}

static int clk_pll_is_locked(uint32_t reg)
{
  return (getreg32(reg) & (1 << 28));
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void f1c100s_clock_init(void)
{
  /* Set CPU clock to 24MHz */
  clk_cpu_config(CLK_CPU_SRC_OSC24M);
  sdelay(100);

  /* PLL_PERIPH = 24M*25/1 = 600M */
  clk_pll_init(CCU_PLL_PERIPH_CTRL, 25, 1);
  clk_pll_enable(CCU_PLL_PERIPH_CTRL);
  while (!clk_pll_is_locked(CCU_PLL_PERIPH_CTRL));

  /* AHB = PLL_PERIPH(600M) / 3 / 1 = 200M */
  /* APB = AHB(200M) / 2 = 100M */
  /* AHB_APB_CFG (0x054):
   * Bit 13-12: AHB_SRC (3=PLL_PERIPH)
   * Bit 9-8:   APB_DIV (1=/2)
   * Bit 7-6:   AHB_PRE_DIV (2=/3)
   * Bit 5-4:   AHB_DIV (0=/1)
   * Combined: 0x00003180 (matches Linux ccu-suniv-f1c100s.c and xboot)
   */
  putreg32((3 << 12) | (1 << 8) | (2 << 6) | (0 << 4), CCU_AHB_APB_CFG);
  sdelay(100);

  /* PLL_CPU = 24MHz * 17 = 408MHz, matching awboot's own configuration
   * (see get_cpu_freq() below - keep the two in sync). This function
   * is not on the default boot path (arm_boot() trusts the loader's
   * clock tree, see f1c100s_boot.c); it exists for boards that need
   * to bring up clocks from a cold/FEL start without awboot. */
  clk_pll_init(CCU_PLL_CPU_CTRL, 17, 1);
  clk_pll_enable(CCU_PLL_CPU_CTRL);
  while (!clk_pll_is_locked(CCU_PLL_CPU_CTRL));

  /* Switch CPU to PLL_CPU */
  clk_cpu_config(CLK_CPU_SRC_PLL_CPU);
  sdelay(100);
}

void f1c100s_uart0_enable(void)
{
  /* Open clock gate for UART0 (Bus Clock Gate 2, Bit 20) */
  uint32_t val = getreg32(CCU_BUS_CLK_GATE2);
  val |= (1 << 20);
  putreg32(val, CCU_BUS_CLK_GATE2);
  sdelay(100);

  /* Pulse UART0 reset. clock_init() rewrites AHB/APB and can leave the
   * DW 8250 wedged; OR-1 is a no-op if awboot already deasserted bit 20. */
  f1c100s_reset_uart0();
}

/****************************************************************************
 * Name: f1c100s_clk_enable
 *
 * Description:
 *   Enable clock for specified peripheral
 *
 ****************************************************************************/

void f1c100s_clk_enable(uint32_t gate_reg, uint8_t bit)
{
  uint32_t reg = CCU_BASE + gate_reg;
  putreg32(getreg32(reg) | (1 << bit), reg);
}

/****************************************************************************
 * Name: f1c100s_clk_disable
 *
 * Description:
 *   Disable clock for specified peripheral
 *
 ****************************************************************************/

void f1c100s_clk_disable(uint32_t gate_reg, uint8_t bit)
{
  uint32_t reg = CCU_BASE + gate_reg;
  putreg32(getreg32(reg) & ~(1 << bit), reg);
}

/****************************************************************************
 * Name: f1c100s_get_apb_freq
 *
 * Description:
 *   Get current APB clock frequency
 *
 ****************************************************************************/

uint32_t f1c100s_get_apb_freq(void)
{
  /* APB = 100MHz - matches both awboot's boot-time AHB_APB_CFG and
   * f1c100s_clock_init()'s setting (0x00003180, see that function). */
  return 100000000;
}

/****************************************************************************
 * Name: f1c100s_get_ahb_freq
 *
 * Description:
 *   Get current AHB clock frequency
 *
 ****************************************************************************/

uint32_t f1c100s_get_ahb_freq(void)
{
  /* AHB = 200MHz - matches both awboot's boot-time AHB_APB_CFG and
   * f1c100s_clock_init()'s setting (0x00003180, see that function). */
  return 200000000;
}

/****************************************************************************
 * Name: f1c100s_get_cpu_freq
 *
 * Description:
 *   Get current CPU clock frequency
 *
 ****************************************************************************/

uint32_t f1c100s_get_cpu_freq(void)
{
  /* 408MHz: matches both awboot's boot-time configuration (the
   * default boot path) and f1c100s_clock_init()'s PLL_CPU setting. */
  return 408000000;
}
