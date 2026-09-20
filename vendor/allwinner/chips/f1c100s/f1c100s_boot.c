/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c_boot.c
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
#include <assert.h>

#include <nuttx/init.h>
#include <arch/board/board.h>

#include "chip.h"
#include "arm_internal.h"
#include "hardware/f1c100s_dma.h"
#include "hardware/f1c100s_ccu.h"
#include "f1c100s_boot.h"

/* External function declarations */
void f1c100s_clock_init(void);
void f1c100s_uart0_enable(void);
void f1c_early_puts(const char *str);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_memory_initialize
 *
 * Description:
 *   Initialize DRAM if configured. Called early in arm_boot() before
 *   board initialization. This follows NuttX standard memory initialization
 *   pattern.
 *
 ****************************************************************************/

void f1c100s_memory_initialize(void)
{
#ifdef CONFIG_F1C100S_DRAM_INIT
  f1c100s_dram_initialize();
#endif
}

/****************************************************************************
 * Name: f1c100s_jtag_configure
 *
 * Description:
 *   Configure JTAG pins if enabled via Kconfig.
 *   JTAG pins (PF port, Function 3 per Linux pinctrl-suniv-f1c100s;
 *   Function 4 is IR/PWM1 on this chip, not JTAG):
 *     PF0 = TMS, PF1 = TDI, PF3 = TDO, PF5 = TCK
 *   WARNING: JTAG and SD Card share PF port pins.
 *
 ****************************************************************************/

void f1c100s_jtag_configure(void)
{
#ifdef CONFIG_F1C100S_JTAG_ENABLE
  uint32_t pf_cfg0 = F1C_PIO_BASE + 0xb4;  /* PF_CFG0 */
  uint32_t val = getreg32(pf_cfg0);

  /* Linux pinctrl-suniv-f1c100s: JTAG is function 3, not 4.
   * PF0=TMS(MS), PF1=TDI(dbg DI), PF3=TDO(DO), PF5=TCK(CK).
   * Function 4 on PF0 is IR, on PF5 is PWM1 — that disconnects the TAP. */

  val &= ~((0x7 << 0) | (0x7 << 4) | (0x7 << 12) | (0x7 << 20));
  val |= ((3 << 0) | (3 << 4) | (3 << 12) | (3 << 20));
  putreg32(val, pf_cfg0);
#endif
}

/****************************************************************************
 * Name: arm_boot
 *
 * Description:
 *   Complete boot operations started in f1c100s_head.S.
 *   This is the standard NuttX C-level boot entry point.
 *
 ****************************************************************************/

void arm_boot(void)
{
  /* The BL→AP trampoline is a software memcpy+jump, not an SoC reset.
   * BL starts the 5s hardware WDT in bringup; that counter keeps
   * running across the jump. AP then spends seconds in
   * st7789_lcdinitialize()'s full-screen fill (57600 SPI_SEND calls)
   * before it can start its own feeder — WDT fires, looks like a
   * hang after "crashlog: empty", FEL unreachable until power cycle.
   * Always kill a leftover WDT here, before any long init.
   */

  putreg32((0xa57 << 1) | 1, 0x01c20cb0);
  putreg32(0, 0x01c20cb8);

  /* Initialize memory (DRAM) if configured.
   * This follows NuttX standard memory initialization pattern.
   */

  f1c100s_memory_initialize();

  /* Configure JTAG pins before any later driver can claim PF
   * (SDIO uses the same port).
   */

  f1c100s_jtag_configure();

  /* This board boots through awboot, which has already brought up
   * PLL_CPU/PLL_PERIPH and AHB/APB (see f1c100s_clock_init() for the
   * equivalent standalone sequence, kept for boards that boot NuttX
   * directly without a first-stage loader). Re-running clock_init()
   * here is unnecessary on this boot path and re-parents the CPU
   * clock through OSC24M, which drops the JTAG TAP mid-switch -
   * matches the pattern already used by t113_boot.c, which likewise
   * trusts the loader's clock tree instead of reinitializing it.
   */

  f1c100s_uart0_enable();
  f1c_early_puts("[AP arm_boot]\n");
#ifdef USE_EARLYSERIALINIT
  arm_earlyserialinit();
#endif

#ifdef CONFIG_F1C100S_DMA
  f1c100s_dma_init();
#endif

  /* Configure vector mapping and copy vector table to runtime location */

  up_vectormapping();
  up_copyvectorblock();
}
