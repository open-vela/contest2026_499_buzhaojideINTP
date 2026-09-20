/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_vectors.c
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
#include <nuttx/irq.h>
#include "chip.h"
#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Vector table size in bytes (8 entries * 4 bytes each) */

#define VECTOR_TABLE_SIZE 32

/****************************************************************************
 * External Symbols
 ****************************************************************************/

/* These symbols are defined in arm_vectortab.S */

extern uint32_t _vector_start;
extern uint32_t _vector_end;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_copyvectorblock
 *
 * Description:
 *   Copy the interrupt vector table to the target address. This function
 *   supports both low vector mode (0x00000000) and high vector mode
 *   (0xFFFF0000) as configured by CONFIG_ARCH_LOWVECTORS.
 *
 *   The vector table is copied from the link-time location to the runtime
 *   location. This is necessary when:
 *   - Running from RAM and vectors need to be at address 0
 *   - Using high vectors (0xFFFF0000) on ARM926EJ-S
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void up_copyvectorblock(void)
{
  uint32_t *src;
  uint32_t *dest;
  uint32_t *end;

  /* Get source vector table location (link-time address) */

  src = &_vector_start;
  end = &_vector_end;

  /* Determine destination based on vector mode configuration */

#ifdef CONFIG_ARCH_LOWVECTORS
  /* Low vector mode: Copy to address 0x00000000 */

  dest = (uint32_t *)VECTOR_LOW_BASE;
#else
  /* High vector mode: Copy to address 0xFFFF0000 */

  dest = (uint32_t *)VECTOR_HIGH_BASE;
#endif

  /* Copy vector table word by word */

  while (src < end)
    {
      *dest++ = *src++;
    }

  /* Memory barrier to ensure all writes complete */

  __asm__ __volatile__ ("" ::: "memory");  /* Compiler barrier */

  /* Instruction synchronization barrier to flush pipeline */

  __asm__ __volatile__ ("" ::: "memory");  /* Compiler barrier */

  /* Invalidate instruction cache to ensure new vectors are used */

#ifndef CONFIG_ARM_ICACHE_DISABLE
  __asm__ __volatile__
    (
      "mov r0, #0\n"
      "mcr p15, 0, r0, c7, c5, 0"  /* Invalidate entire I-cache */
      ::: "r0", "memory"
    );
#endif
}

/****************************************************************************
 * Name: up_vectormapping
 *
 * Description:
 *   Setup the vector table mapping. This function is called during early
 *   boot to configure the CP15 control register for vector table location.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void up_vectormapping(void)
{
  uint32_t sctlr;

  /* Read current SCTLR (System Control Register) */

  __asm__ __volatile__
    (
      "mrc p15, 0, %0, c1, c0, 0"
      : "=r" (sctlr)
      :: "memory"
    );

#ifdef CONFIG_ARCH_LOWVECTORS
  /* Clear V bit (bit 13) for low vectors at 0x00000000 */

  sctlr &= ~(1 << 13);
#else
  /* Set V bit (bit 13) for high vectors at 0xFFFF0000 */

  sctlr |= (1 << 13);
#endif

  /* Write back modified SCTLR */

  __asm__ __volatile__
    (
      "mcr p15, 0, %0, c1, c0, 0"
      :
      : "r" (sctlr)
      : "memory"
    );

  /* Ensure the change takes effect */

  __asm__ __volatile__ ("" ::: "memory");  /* Compiler barrier */
}
