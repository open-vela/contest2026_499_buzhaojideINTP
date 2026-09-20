/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_start.c
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
#include <nuttx/init.h>

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* g_idle_topstack is defined in f1c100s_head.S */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: (none)
 *
 * Description:
 *   This file originally contained startup code, but the actual reset
 *   entry point is now in f1c100s_head.S.
 *
 *   The real startup sequence is:
 *   1. f1c100s_head.S: __start -> MMU setup -> .Lvstart
 *   2. f1c100s_board_initialize() in f1c100s_boot.c
 *   3. nx_start() in NuttX kernel
 *
 ****************************************************************************/
