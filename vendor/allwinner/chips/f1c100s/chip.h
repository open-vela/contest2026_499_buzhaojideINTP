/****************************************************************************
 * vendor/allwinner/chips/f1c100s/chip.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_CHIP_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_CHIP_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include "hardware/f1c100s_memorymap.h"
#include "f1c100s_partitions.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Page Table Configuration */

#define PGTABLE_BASE_PADDR  (CONFIG_RAM_START + CONFIG_RAM_SIZE - PGTABLE_SIZE)
#define PGTABLE_BASE_VADDR  (CONFIG_RAM_VSTART + CONFIG_RAM_SIZE - PGTABLE_SIZE)

/* NuttX Start Address */

#define NUTTX_START_PADDR   CONFIG_RAM_START
#define NUTTX_START_VADDR   CONFIG_RAM_VSTART

/* Vector Table Addresses */

#define VECTOR_LOW_BASE     0x00000000  /* Low vectors */
#define VECTOR_HIGH_BASE    0xFFFF0000  /* High vectors */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

/****************************************************************************
 * Name: f1c100s_board_initialize
 *
 * Description:
 *   All F1C100s architectures must provide the following entry point.
 *   This entry point is called early in the initialization before any
 *   devices have been initialized.
 *
 ****************************************************************************/

void f1c100s_board_initialize(void);

/****************************************************************************
 * Name: up_copyvectorblock
 *
 * Description:
 *   Copy the interrupt vector table to the target address.
 *
 ****************************************************************************/

void up_copyvectorblock(void);

/****************************************************************************
 * Name: up_vectormapping
 *
 * Description:
 *   Setup the vector table mapping in CP15 control register.
 *
 ****************************************************************************/

void up_vectormapping(void);

#endif /* __ASSEMBLY__ */

#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_CHIP_H */
