/****************************************************************************
 * vendor/allwinner_f1c/boards/f1c100s_board/include/board.h
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

#ifndef __VENDOR_ALLWINNER_F1C_BOARDS_F1C100S_BOARD_INCLUDE_BOARD_H
#define __VENDOR_ALLWINNER_F1C_BOARDS_F1C100S_BOARD_INCLUDE_BOARD_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Clocking *****************************************************************/

#define BOARD_OSC_FREQUENCY     24000000   /* 24MHz OSC */
#define BOARD_CPU_FREQUENCY     720000000  /* 720MHz CPU */
#define BOARD_AHB_FREQUENCY     200000000  /* 200MHz AHB */
#define BOARD_APB_FREQUENCY     100000000  /* 100MHz APB */

/* UART Configuration *******************************************************/

#define BOARD_UART0_BASEADDR    0x01c25000
#define BOARD_UART0_BAUD        115200
#define BOARD_UART0_BITS        8
#define BOARD_UART0_PARITY      0
#define BOARD_UART0_2STOP       0

/* LED definitions **********************************************************/

/* The F1C100s demo board has no user LEDs by default.
 * Define these if your board has LEDs.
 */

/* Button definitions *******************************************************/

/* SCH_F1C200S小电脑.pdf: SW1/SW2/SW3 independent GPIOs, 47k pull-up,
 * press to GND (active-low). Not LRADC.
 *
 *   SW1 L  = PE2
 *   SW2 CK = PE3
 *   SW3 R  = PE4
 */

#define BUTTON_L              0
#define BUTTON_CK             1
#define BUTTON_R              2

#define BUTTON_L_BIT          (1 << BUTTON_L)
#define BUTTON_CK_BIT         (1 << BUTTON_CK)
#define BUTTON_R_BIT          (1 << BUTTON_R)

#define NUM_BUTTONS           3

#endif /* __VENDOR_ALLWINNER_F1C_BOARDS_F1C100S_BOARD_INCLUDE_BOARD_H */

