/****************************************************************************
 * vendor/allwinner/boards/f1c100s/demo/src/f1c_adcbuttons.c
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

#ifdef CONFIG_F1C100S_LRADC

#include <errno.h>
#include <debug.h>

#include "f1c100s_lradc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Button bit definitions */

#define BUTTON_KEY1_BIT    0
#define BUTTON_KEY2_BIT    1
#define BUTTON_KEY3_BIT    2
#define BUTTON_KEY4_BIT    3
#define BUTTON_KEY5_BIT    4

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* ADC value to button mapping table
 * Based on 6-bit ADC (0-63) with 2.0V internal reference
 * Adjust thresholds based on actual hardware resistor values
 */

static const struct lradc_key_map_s g_keymap[] =
{
  { 0,   5,  BUTTON_KEY1_BIT },   /* K1: ~0V,    ADC 0-5   */
  { 8,  18,  BUTTON_KEY2_BIT },   /* K2: ~0.5V,  ADC 8-18  */
  { 20, 30,  BUTTON_KEY3_BIT },   /* K3: ~1.0V,  ADC 20-30 */
  { 33, 43,  BUTTON_KEY4_BIT },   /* K4: ~1.3V,  ADC 33-43 */
  { 45, 55,  BUTTON_KEY5_BIT },   /* K5: ~1.7V,  ADC 45-55 */
};

#define NUM_KEYS (sizeof(g_keymap) / sizeof(g_keymap[0]))

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_adcbuttons_initialize
 *
 * Description:
 *   Initialize ADC buttons and register as /dev/buttons
 *
 ****************************************************************************/

int f1c100s_adcbuttons_initialize(void)
{
  return f1c100s_lradc_initialize("/dev/buttons", g_keymap, NUM_KEYS);
}

#endif /* CONFIG_F1C100S_LRADC */
