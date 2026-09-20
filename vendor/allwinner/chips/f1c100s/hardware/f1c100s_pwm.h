/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_pwm.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_PWM_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_PWM_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* PWM Base Address */

#define F1C100S_PWM_BASE          0x01c21000

/* PWM Register Offsets */

#define PWM_CTRL_REG_OFFSET       0x00  /* PWM Control Register */
#define PWM_CH0_PERIOD_OFFSET     0x04  /* PWM Channel 0 Period Register */
#define PWM_CH1_PERIOD_OFFSET     0x08  /* PWM Channel 1 Period Register */

/* PWM Control Register Bits - Channel 0 (bits 0-14) */

#define PWM_CH0_PRESCAL_SHIFT     0
#define PWM_CH0_PRESCAL_MASK      (0xf << PWM_CH0_PRESCAL_SHIFT)
#define PWM_CH0_EN                (1 << 4)   /* Channel 0 Enable */
#define PWM_CH0_ACT_STATE         (1 << 5)   /* Channel 0 Active State (polarity) */
#define PWM_CH0_CLK_GATING        (1 << 6)   /* Channel 0 Clock Gating */
#define PWM_CH0_MODE              (1 << 7)   /* Channel 0 Mode (0=cycle, 1=pulse) */
#define PWM_CH0_PULSE             (1 << 8)   /* Channel 0 Pulse Start */
#define PWM_CH0_BYPASS            (1 << 9)   /* Channel 0 Bypass */

/* PWM Control Register Bits - Channel 1 (bits 15-29) */

#define PWM_CH1_PRESCAL_SHIFT     15
#define PWM_CH1_PRESCAL_MASK      (0xf << PWM_CH1_PRESCAL_SHIFT)
#define PWM_CH1_EN                (1 << 19)  /* Channel 1 Enable */
#define PWM_CH1_ACT_STATE         (1 << 20)  /* Channel 1 Active State (polarity) */
#define PWM_CH1_CLK_GATING        (1 << 21)  /* Channel 1 Clock Gating */
#define PWM_CH1_MODE              (1 << 22)  /* Channel 1 Mode (0=cycle, 1=pulse) */
#define PWM_CH1_PULSE             (1 << 23)  /* Channel 1 Pulse Start */
#define PWM_CH1_BYPASS            (1 << 24)  /* Channel 1 Bypass */

/* PWM Control Register Bits - Ready Status */

#define PWM_CH0_RDY               (1 << 28)  /* Channel 0 Period Register Ready */
#define PWM_CH1_RDY               (1 << 29)  /* Channel 1 Period Register Ready */

/* PWM Period Register Bits (matches Linux pwm-sun4i.c and xboot) */

#define PWM_ENTIRE_CYS_SHIFT      16
#define PWM_ENTIRE_CYS_MASK       (0xffff << PWM_ENTIRE_CYS_SHIFT)
#define PWM_ACT_CYS_SHIFT         0
#define PWM_ACT_CYS_MASK          (0xffff << PWM_ACT_CYS_SHIFT)

/* Prescaler Values (24MHz OSC input) */

#define PWM_PRESCAL_120           0x0   /* 24MHz / 120 = 200kHz */
#define PWM_PRESCAL_180           0x1   /* 24MHz / 180 = 133.3kHz */
#define PWM_PRESCAL_240           0x2   /* 24MHz / 240 = 100kHz */
#define PWM_PRESCAL_360           0x3   /* 24MHz / 360 = 66.7kHz */
#define PWM_PRESCAL_480           0x4   /* 24MHz / 480 = 50kHz */
#define PWM_PRESCAL_12000         0x8   /* 24MHz / 12000 = 2kHz */
#define PWM_PRESCAL_24000         0x9   /* 24MHz / 24000 = 1kHz */
#define PWM_PRESCAL_36000         0xa   /* 24MHz / 36000 = 667Hz */
#define PWM_PRESCAL_48000         0xb   /* 24MHz / 48000 = 500Hz */
#define PWM_PRESCAL_72000         0xc   /* 24MHz / 72000 = 333Hz */
#define PWM_PRESCAL_1             0xf   /* 24MHz / 1 = 24MHz (direct) */

/* PWM Channel Count */

#define F1C100S_PWM_CHANNELS      2

/* PWM Clock Source (24MHz OSC) */

#define F1C100S_PWM_CLK_FREQ      24000000

/* GPIO Pin Configuration for PWM
 * F1C100s PWM pins:
 *   PWM0: PE12 (function 4)
 *   PWM1: PE6  (function 4)
 */

#define F1C100S_PWM0_GPIO_PORT    4     /* Port E */
#define F1C100S_PWM0_GPIO_PIN     12    /* PE12 */
#define F1C100S_PWM0_GPIO_MUX     4     /* Function 4 */

#define F1C100S_PWM1_GPIO_PORT    4     /* Port E */
#define F1C100S_PWM1_GPIO_PIN     6     /* PE6 */
#define F1C100S_PWM1_GPIO_MUX     4     /* Function 4 */

#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_PWM_H */
