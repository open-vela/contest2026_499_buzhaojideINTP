/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_allocateheap.c
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
#include <nuttx/arch.h>
#include <nuttx/mm/mm.h>

#include <sys/types.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* F1C100S Memory Map:
 * - DDR Start: 0x80000000
 * - DDR Size:  32MB (F1C100S) or 64MB (F1C200S)
 */

#define F1C100S_DDR_START   0x80000000
#define F1C100S_DDR_SIZE    (32 * 1024 * 1024)  /* 32MB for F1C100S */
#define F1C100S_DDR_END     (F1C100S_DDR_START + F1C100S_DDR_SIZE)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Linker script symbols */

extern uint32_t _ebss[];

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_allocate_heap
 *
 * Description:
 *   This function will be called to dynamically set aside the heap region.
 *   The heap starts after the end of BSS + IDLE stack.
 *
 ****************************************************************************/

void up_allocate_heap(FAR void **heap_start, size_t *heap_size)
{
  /* The heap starts at the end of BSS + IDLE thread stack */

  uintptr_t heap_base = (uintptr_t)_ebss + CONFIG_IDLETHREAD_STACKSIZE;

  /* Align to 8 bytes */

  heap_base = (heap_base + 7) & ~7;

  *heap_start = (FAR void *)heap_base;
  *heap_size  = F1C100S_DDR_END - heap_base;
}

/****************************************************************************
 * Name: arm_addregion
 *
 * Description:
 *   Memory may be added in non-contiguous chunks. Additional chunks are
 *   added by calling this function.
 *
 ****************************************************************************/

#if CONFIG_MM_REGIONS > 1
void arm_addregion(void)
{
  /* F1C100S has only one contiguous DDR region, no additional regions */
}
#endif
