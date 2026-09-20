/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_softreset.c
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
#include <nuttx/arch.h>
#include "chip.h"
#include "arm_internal.h"  /* For getreg32/putreg32 */
#include "hardware/f1c100s_ccu.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_softreset
 *
 * Description:
 *   Perform software reset for specified peripheral.
 *   This function asserts reset, waits, then deasserts reset.
 *
 * Input Parameters:
 *   reset_reg - Reset register offset (e.g., CCU_BUS_SOFT_RST0)
 *   reset_bit - Bit position in reset register
 *
 ****************************************************************************/

void f1c100s_softreset(uint32_t reset_reg, uint8_t reset_bit)
{
  uint32_t reg = F1C_CCU_BASE + reset_reg;
  uint32_t val;

  /* Assert reset (clear bit) */

  val = getreg32(reg);
  val &= ~(1 << reset_bit);
  putreg32(val, reg);

  /* Wait for reset to take effect */

  up_udelay(10);

  /* Deassert reset (set bit) */

  val = getreg32(reg);
  val |= (1 << reset_bit);
  putreg32(val, reg);
}

/****************************************************************************
 * Name: f1c100s_reset_uart0
 ****************************************************************************/

void f1c100s_reset_uart0(void)
{
  f1c100s_softreset(CCU_BUS_SOFT_RST2, 20);
}

/****************************************************************************
 * Name: f1c100s_reset_spi0
 ****************************************************************************/

void f1c100s_reset_spi0(void)
{
  f1c100s_softreset(CCU_BUS_SOFT_RST0, 20);
}

/****************************************************************************
 * Name: f1c100s_reset_audio
 *
 * Description:
 *   Reset Audio Codec peripheral
 *
 ****************************************************************************/

void f1c100s_reset_audio(void)
{
  /* Audio Codec is on BUS_SOFT_RST2 (0x2d0), bit 0 */
  f1c100s_softreset(CCU_BUS_SOFT_RST2, 0);
}

/****************************************************************************
 * Name: f1c100s_reset_sdc0
 *
 * Description:
 *   Reset SDC0 peripheral
 *
 ****************************************************************************/

void f1c100s_reset_sdc0(void)
{
  f1c100s_softreset(CCU_BUS_SOFT_RST0, 8);
}

/****************************************************************************
 * Name: f1c100s_reset_spi1
 *
 * Description:
 *   Reset SPI1 peripheral
 *
 ****************************************************************************/

void f1c100s_reset_spi1(void)
{
  f1c100s_softreset(CCU_BUS_SOFT_RST0, 21);
}

/****************************************************************************
 * Name: f1c100s_reset_i2c0
 *
 * Description:
 *   Reset I2C0 peripheral
 *
 ****************************************************************************/

void f1c100s_reset_i2c0(void)
{
  /* I2C0 reset is on BUS_SOFT_RST2 (0x2d0), bit 16 */
  f1c100s_softreset(CCU_BUS_SOFT_RST2, 16);
}

/****************************************************************************
 * Name: f1c100s_reset_i2c1
 *
 * Description:
 *   Reset I2C1 peripheral
 *
 ****************************************************************************/

void f1c100s_reset_i2c1(void)
{
  /* I2C1 reset is on BUS_SOFT_RST2 (0x2d0), bit 17 */
  f1c100s_softreset(CCU_BUS_SOFT_RST2, 17);
}

/****************************************************************************
 * Name: f1c100s_reset_i2c2
 *
 * Description:
 *   Reset I2C2 peripheral
 *
 ****************************************************************************/

void f1c100s_reset_i2c2(void)
{
  /* I2C2 reset is on BUS_SOFT_RST2 (0x2d0), bit 18 */
  f1c100s_softreset(CCU_BUS_SOFT_RST2, 18);
}


/****************************************************************************
 * Name: f1c100s_reset_gpio
 *
 * Description:
 *   Reset GPIO peripheral
 *
 ****************************************************************************/

void f1c100s_reset_gpio(void)
{
  f1c100s_softreset(CCU_BUS_SOFT_RST2, 5);
}

