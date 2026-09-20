/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_mmu.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_F1C100S_MMU_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_F1C100S_MMU_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/


/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: f1c100s_mmu_initialize
 *
 * Description:
 *   Initialize MMU page tables for the F1C100s.
 *   This function sets up the initial page table mappings.
 *   NOTE: Currently uninvoked in boot flow (retained standalone subsystem).
 *         Do not enable in arm_boot without physical silicon validation.
 *
 ****************************************************************************/

void f1c100s_mmu_initialize(void);

/****************************************************************************
 * Name: f1c100s_mmu_enable
 *
 * Description:
 *   Enable the MMU with the configured page tables.
 *
 ****************************************************************************/

void f1c100s_mmu_enable(void);

/****************************************************************************
 * Name: f1c100s_cache_enable
 *
 * Description:
 *   Enable instruction and data caches.
 *
 ****************************************************************************/

void f1c100s_cache_enable(void);

#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_F1C100S_MMU_H */
