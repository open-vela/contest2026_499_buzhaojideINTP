/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c_dram.c
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
#include "chip.h"
#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DRAMC_BASE           0x01c01000
#define CCU_BASE             0x01c20000

/* DRAM Controller Registers */
#define DRAM_SCTLR           (DRAMC_BASE + 0x00)
#define DRAM_SREFR           (DRAMC_BASE + 0x04)
#define DRAM_SCONR           (DRAMC_BASE + 0x08)
#define DRAM_STMG0R          (DRAMC_BASE + 0x0c)
#define DRAM_STMG1R          (DRAMC_BASE + 0x10)
#define DRAM_SCTLR1          (DRAMC_BASE + 0x14)
#define DRAM_SREFR1          (DRAMC_BASE + 0x18)
#define DRAM_DDLYR           (DRAMC_BASE + 0x24)
#define DRAM_DADRR           (DRAMC_BASE + 0x28)
#define DRAM_DVALR           (DRAMC_BASE + 0x2c)
#define DRAM_DRPTR0          (DRAMC_BASE + 0x30)
#define DRAM_DRPTR1          (DRAMC_BASE + 0x34)
#define DRAM_DRPTR2          (DRAMC_BASE + 0x38)
#define DRAM_DRPTR3          (DRAMC_BASE + 0x3c)
#define DRAM_SEFR            (DRAMC_BASE + 0x40)
#define DRAM_MAE             (DRAMC_BASE + 0x44)

/* CCU DRAM Registers */
#define CCU_PLL_DDR_CTRL     (CCU_BASE + 0x020)
#define CCU_PLL_DDR_PAT      (CCU_BASE + 0x024)
#define CCU_DRAM_CLK         (CCU_BASE + 0x100)
#define CCU_BUS_CLK_GATE0    (CCU_BASE + 0x060)
#define CCU_BUS_SOFT_RST0    (CCU_BASE + 0x2c0)

/* DRAM Types */
#define DRAM_TYPE_DDR        1
#define DRAM_TYPE_SDR        0

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct dram_para_s
{
  uint32_t base;
  uint32_t size;
  uint32_t clk;
  uint32_t access_mode;
  uint32_t cs_num;
  uint32_t ddr8_remap;
  uint32_t sdr_ddr;
  uint32_t bwidth;
  uint32_t col_width;
  uint32_t row_width;
  uint32_t bank_size;
  uint32_t cas;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void dram_delay(int ms)
{
  volatile int i;
  for (i = 0; i < ms * 1000; i++);
}

static int dram_initial(void)
{
  uint32_t time = 0xffffff;

  putreg32(getreg32(DRAM_SCTLR) | 0x1, DRAM_SCTLR);

  while ((getreg32(DRAM_SCTLR) & 0x1) && time--)
    {
      if (time == 0)
        {
          return 0;
        }
    }

  return 1;
}

static int dram_delay_scan(void)
{
  uint32_t time = 0xffffff;

  putreg32(getreg32(DRAM_DDLYR) | 0x1, DRAM_DDLYR);

  while ((getreg32(DRAM_DDLYR) & 0x1) && time--)
    {
      if (time == 0)
        {
          return 0;
        }
    }

  return 1;
}

static void dram_set_autofresh_cycle(uint32_t clk)
{
  uint32_t val = 0;
  uint32_t row = 0;
  uint32_t temp = 0;

  row = getreg32(DRAM_SCONR);
  row &= 0x1e0;
  row >>= 0x5;

  if (row == 0xc)
    {
      if (clk >= 1000000)
        {
          temp = clk + (clk >> 3) + (clk >> 4) + (clk >> 5);
          while (temp >= (10000000 >> 6))
            {
              temp -= (10000000 >> 6);
              val++;
            }
        }
      else
        {
          val = (clk * 499) >> 6;
        }
    }
  else if (row == 0xb)
    {
      if (clk >= 1000000)
        {
          temp = clk + (clk >> 3) + (clk >> 4) + (clk >> 5);
          while (temp >= (10000000 >> 7))
            {
              temp -= (10000000 >> 7);
              val++;
            }
        }
      else
        {
          val = (clk * 499) >> 5;
        }
    }

  putreg32(val, DRAM_SREFR);
  putreg32(getreg32(DRAM_SREFR) | 0x8000000, DRAM_SREFR);
}

static int dram_para_setup(struct dram_para_s *para)
{
  uint32_t val = 0;

  val = (para->ddr8_remap) | (0x1 << 1) |
        ((para->bank_size >> 2) << 3) |
        ((para->cs_num >> 1) << 4) |
        ((para->row_width - 1) << 5) |
        ((para->col_width - 1) << 9) |
        ((para->sdr_ddr ? (para->bwidth >> 4) : (para->bwidth >> 5)) << 13) |
        (para->access_mode << 15) |
        (para->sdr_ddr << 16);

  putreg32(val, DRAM_SCONR);
  putreg32(getreg32(DRAM_SCTLR) | (0x1 << 19), DRAM_SCTLR);

  return dram_initial();
}

static uint32_t dram_check_delay(uint32_t bwidth)
{
  uint32_t dsize;
  uint32_t i, j;
  uint32_t num = 0;
  uint32_t dflag = 0;

  dsize = ((bwidth == 16) ? 4 : 2);

  for (i = 0; i < dsize; i++)
    {
      if (i == 0)
        {
          dflag = getreg32(DRAM_DRPTR0);
        }
      else if (i == 1)
        {
          dflag = getreg32(DRAM_DRPTR1);
        }
      else if (i == 2)
        {
          dflag = getreg32(DRAM_DRPTR2);
        }
      else if (i == 3)
        {
          dflag = getreg32(DRAM_DRPTR3);
        }

      for (j = 0; j < 32; j++)
        {
          if (dflag & 0x1)
            {
              num++;
            }
          dflag >>= 1;
        }
    }

  return num;
}

/****************************************************************************
 * Name: dram_test
 *
 * Description:
 *   Test DRAM read/write functionality
 *
 ****************************************************************************/

static bool dram_test(uint32_t base)
{
  uint32_t i;

  /* Write test pattern */
  for (i = 0; i < 128; i++)
    {
      ((volatile uint32_t*)base)[i] = base + i * 4;
    }

  /* Read and verify */
  for (i = 0; i < 128; i++)
    {
      if (((volatile uint32_t*)base)[i] != base + i * 4)
        {
          return false;
        }
    }

  return true;
}

static int dram_init(struct dram_para_s *para)
{
  uint32_t val = 0;

  /* Configure DDR PLL */
  if (para->clk <= 96)
    {
      val = (0x1 << 0) | (0x0 << 4) |
            (((para->clk * 2) / 12 - 1) << 8) | (0x1u << 31);
    }
  else
    {
      val = (0x0 << 0) | (0x0 << 4) |
            (((para->clk * 2) / 24 - 1) << 8) | (0x1u << 31);
    }

  /* Set pattern for fractional PLL */
  if (para->cas & (0x1 << 4))
    {
      putreg32(0xd1303333, CCU_PLL_DDR_PAT);
    }
  else if (para->cas & (0x1 << 5))
    {
      putreg32(0xcce06666, CCU_PLL_DDR_PAT);
    }
  else if (para->cas & (0x1 << 6))
    {
      putreg32(0xc8909999, CCU_PLL_DDR_PAT);
    }
  else if (para->cas & (0x1 << 7))
    {
      putreg32(0xc440cccc, CCU_PLL_DDR_PAT);
    }

  if (para->cas & (0xf << 4))
    {
      val |= 0x1 << 24;
    }

  putreg32(val, CCU_PLL_DDR_CTRL);
  putreg32(getreg32(CCU_PLL_DDR_CTRL) | (0x1 << 20), CCU_PLL_DDR_CTRL);

  while ((getreg32(CCU_PLL_DDR_CTRL) & (1 << 28)) == 0);

  dram_delay(5);

  /* Configure DRAM clock */
  putreg32((0x1 << 31) | (0x1 << 0), CCU_DRAM_CLK);

  /* Enable DRAM clock gate and deassert reset */
  putreg32(getreg32(CCU_BUS_CLK_GATE0) | (0x1 << 14), CCU_BUS_CLK_GATE0);
  putreg32(getreg32(CCU_BUS_SOFT_RST0) | (0x1 << 14), CCU_BUS_SOFT_RST0);
  dram_delay(1);

  /* Setup DRAM parameters */
  if (!dram_para_setup(para))
    {
      return 0;
    }

  /* Set refresh cycle */
  dram_set_autofresh_cycle(para->clk);

  /* Delay scan */
  putreg32(0x00000000, DRAM_DDLYR);
  if (!dram_delay_scan())
    {
      return 0;
    }

  /* Check delay */
  if (dram_check_delay(para->bwidth) == 0)
    {
      return 0;
    }

  /* Test DRAM read/write */
  if (!dram_test(para->base))
    {
      return 0;
    }

  /* Set size */
  para->size = (1 << (para->row_width + para->col_width)) *
               para->bank_size * para->cs_num * para->bwidth / 8;

  return 1;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_dram_initialize
 *
 * Description:
 *   Initialize DRAM controller
 *
 ****************************************************************************/

uint32_t f1c100s_dram_initialize(void)
{
  struct dram_para_s dram;

  /* F1C100s default DRAM parameters */
  dram.base        = 0x80000000;
  dram.size        = 0;
  dram.clk         = 156;  /* 156MHz DDR clock */
  dram.access_mode = 1;
  dram.cs_num      = 1;
  dram.ddr8_remap  = 0;
  dram.sdr_ddr     = DRAM_TYPE_DDR;
  dram.bwidth      = 16;
  dram.col_width   = 10;
  dram.row_width   = 13;
  dram.bank_size   = 4;
  dram.cas         = 0x3;

  if (dram_init(&dram))
    {
      return dram.size;
    }
  else
    {
      return 0;
    }
}
