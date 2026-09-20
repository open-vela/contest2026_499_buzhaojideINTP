/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_dram.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_DRAM_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_DRAM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* DRAM Controller Base Address */

#define DRAM_BASE             0x01c01000

/* Default DRAM Size */

#define F1C100S_DRAM_SIZE     (32 * 1024 * 1024)  /* 32MB */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: f1c100s_dram_initialize
 *
 * Description:
 *   Initialize DRAM controller and perform memory training.
 *   This function configures the DDR PLL, memory controller timing
 *   parameters, and performs delay scanning for optimal operation.
 *
 * Returned Value:
 *   Size of initialized DRAM in bytes, or 0 on failure
 *
 ****************************************************************************/

uint32_t f1c100s_dram_initialize(void);

#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_DRAM_H */
