/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c_mmu.c
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
#include "f1c100s_mmu.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* MMU L1 Page Table Descriptors */
#define MMU_L1_SECTION       0x00000002  /* Section descriptor */
#define MMU_L1_CACHEABLE     0x00000008  /* Cacheable bit */
#define MMU_L1_BUFFERABLE    0x00000004  /* Bufferable bit */
#define MMU_L1_AP_RW         0x00000400  /* Access permission: R/W */

/* Section attributes */
#define SECTION_NCNB         (MMU_L1_SECTION | MMU_L1_AP_RW)
#define SECTION_CB           (MMU_L1_SECTION | MMU_L1_AP_RW | \
                              MMU_L1_CACHEABLE | MMU_L1_BUFFERABLE)

/* Memory sizes */
#define SZ_1M                (1024 * 1024)
#define SZ_2G                (2048UL * 1024 * 1024)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* L1 page table (4096 entries for 4GB address space) */
/* Each entry maps 1MB */
static uint32_t g_pgtable[4096] __attribute__((aligned(16384)));

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mmu_map_l1_entry
 *
 * Description:
 *   Map a memory region using L1 page table entries
 *
 ****************************************************************************/

static void mmu_map_l1_entry(uint32_t *table, uint32_t vaddr,
                              uint32_t paddr, uint32_t size,
                              uint32_t attr)
{
  uint32_t start = vaddr >> 20;  /* Section index */
  uint32_t end = (vaddr + size) >> 20;
  uint32_t pa = paddr & 0xfff00000;

  for (uint32_t i = start; i < end; i++)
    {
      table[i] = pa | attr;
      pa += SZ_1M;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_mmu_initialize
 *
 * Description:
 *   Initialize MMU and configure memory mappings.
 *
 *   NOTE: This function is currently NOT called from any boot code
 *   (f1c100s_head.S explicitly clears CR_M, and arm_boot() does not invoke
 *   MMU init). It is retained as a standalone subsystem for future silicon
 *   bringup. Enabling MMU requires physical verification.
 *
 ****************************************************************************/

void f1c100s_mmu_initialize(void)
{
  /* Clear page table - unmapped entries will cause data abort */
  for (int i = 0; i < 4096; i++)
    {
      g_pgtable[i] = 0;
    }

  /* Map only actual memory regions:
   * - SRAM: 0x00000000, 32KB (1MB section)
   * - Peripherals: 0x01c00000 - 0x01ffffff (4MB)
   * - DRAM: 0x80000000, 32MB
   */

  /* SRAM region (1MB section containing 32KB SRAM) */
  mmu_map_l1_entry(g_pgtable, 0x00000000, 0x00000000, SZ_1M, SECTION_NCNB);

  /* Peripheral region (0x01c00000 - 0x01ffffff) */
  mmu_map_l1_entry(g_pgtable, 0x01c00000, 0x01c00000, SZ_1M * 4, SECTION_NCNB);

  /* DRAM region (32MB, first 16MB cacheable) */
  mmu_map_l1_entry(g_pgtable, 0x80000000, 0x80000000, SZ_1M * 16, SECTION_CB);
  mmu_map_l1_entry(g_pgtable, 0x81000000, 0x81000000, SZ_1M * 16, SECTION_NCNB);

  /* Set TTB (Translation Table Base) - CP15 c2 */
  __asm__ __volatile__
    (
      "mcr p15, 0, %0, c2, c0, 0"
      :
      : "r" (g_pgtable)
      : "memory"
    );

  /* Invalidate TLB - CP15 c8 */
  __asm__ __volatile__
    (
      "mcr p15, 0, %0, c8, c7, 0"
      :
      : "r" (0)
      : "memory"
    );

  /* Set domain access (Domain 0 = manager/full access) - CP15 c3 */
  __asm__ __volatile__
    (
      "mcr p15, 0, %0, c3, c0, 0"
      :
      : "r" (0x00000001)
      : "memory"
    );
}

/****************************************************************************
 * Name: f1c100s_mmu_enable
 *
 * Description:
 *   Enable MMU
 *
 ****************************************************************************/

void f1c100s_mmu_enable(void)
{
  uint32_t sctlr;

  /* Read current SCTLR - CP15 c1 */
  __asm__ __volatile__
    (
      "mrc p15, 0, %0, c1, c0, 0"
      : "=r" (sctlr)
      :
      : "memory"
    );

  /* Enable MMU (bit 0) */
  sctlr |= 0x00000001;

  /* Write back SCTLR - CP15 c1 */
  __asm__ __volatile__
    (
      "mcr p15, 0, %0, c1, c0, 0"
      :
      : "r" (sctlr)
      : "memory"
    );
}

/****************************************************************************
 * Name: f1c100s_cache_enable
 *
 * Description:
 *   Enable I-Cache and D-Cache
 *
 ****************************************************************************/

void f1c100s_cache_enable(void)
{
  uint32_t sctlr;

  /* Read current SCTLR - CP15 c1 */
  __asm__ __volatile__
    (
      "mrc p15, 0, %0, c1, c0, 0"
      : "=r" (sctlr)
      :
      : "memory"
    );

  /* Enable I-Cache (bit 12) and D-Cache (bit 2) */
  sctlr |= (1 << 12) | (1 << 2);

  /* Write back SCTLR - CP15 c1 */
  __asm__ __volatile__
    (
      "mcr p15, 0, %0, c1, c0, 0"
      :
      : "r" (sctlr)
      : "memory"
    );
}
