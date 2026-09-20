/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_boot.h
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

#ifndef __F1C100S_BOOT_H
#define __F1C100S_BOOT_H

/****************************************************************************
 * Public Function Prototypes
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

void f1c100s_memory_initialize(void);

/****************************************************************************
 * Name: f1c100s_jtag_configure
 *
 * Description:
 *   Configure JTAG pins if enabled via Kconfig.
 *   JTAG pins (PF port, Function 4):
 *     PF0 = TMS, PF1 = TCK, PF3 = TDO, PF5 = TDI
 *   WARNING: JTAG and SD Card share PF port pins.
 *
 ****************************************************************************/

void f1c100s_jtag_configure(void);

#endif /* __F1C100S_BOOT_H */
