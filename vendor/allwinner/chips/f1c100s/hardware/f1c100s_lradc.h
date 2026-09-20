/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_lradc.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_LRADC_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_LRADC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

#ifdef CONFIG_F1C100S_LRADC

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* LRADC Base Address */

#define F1C_LRADC_BASE              0x01c23400

/* Register Offsets */

#define LRADC_CTRL_OFFSET           0x00
#define LRADC_INTC_OFFSET           0x04
#define LRADC_INTS_OFFSET           0x08
#define LRADC_DATA0_OFFSET          0x0c
#define LRADC_DATA1_OFFSET          0x10

/* Register Addresses */

#define LRADC_CTRL                  (F1C_LRADC_BASE + LRADC_CTRL_OFFSET)
#define LRADC_INTC                  (F1C_LRADC_BASE + LRADC_INTC_OFFSET)
#define LRADC_INTS                  (F1C_LRADC_BASE + LRADC_INTS_OFFSET)
#define LRADC_DATA0                 (F1C_LRADC_BASE + LRADC_DATA0_OFFSET)
#define LRADC_DATA1                 (F1C_LRADC_BASE + LRADC_DATA1_OFFSET)

/* LRADC_CTRL Register Bits (matches Linux sun4i-lradc-keys.c) */

#define LRADC_CTRL_ENABLE           (1 << 0)
#define LRADC_CTRL_CH0_EN           (1 << 0)  /* Channel 0 / module enable */
#define LRADC_CTRL_SAMPLE_RATE_SHIFT 2
#define LRADC_CTRL_SAMPLE_RATE_MASK (0x3 << 2)
#define LRADC_CTRL_LEVELB_SHIFT     4
#define LRADC_CTRL_LEVELB_MASK      (0x3 << 4)
#define LRADC_CTRL_HOLD_EN          (1 << 6)
#define LRADC_CTRL_HOLD_KEY_EN      (1 << 7)
#define LRADC_CTRL_LEVELA_SHIFT     8
#define LRADC_CTRL_LEVELA_MASK      (0xf << 8)
#define LRADC_CTRL_KEY_MODE_SHIFT   12
#define LRADC_CTRL_KEY_MODE_MASK    (0x3 << 12)
#define LRADC_CTRL_CONT_TIME_SHIFT  16
#define LRADC_CTRL_CONT_TIME_MASK   (0xf << 16)
#define LRADC_CTRL_CHAN_SEL_SHIFT   22
#define LRADC_CTRL_CHAN_SEL_MASK    (0x3 << 22)
#define LRADC_CTRL_FIRST_DLY_SHIFT  24
#define LRADC_CTRL_FIRST_DLY_MASK   (0xff << 24)

/* Key Mode Values */

#define LRADC_KEY_MODE_NORMAL       0
#define LRADC_KEY_MODE_SINGLE       1
#define LRADC_KEY_MODE_CONTINUE     2

/* Sample Rate Values (at 32.768kHz clock) */

#define LRADC_SAMPLE_250HZ          0
#define LRADC_SAMPLE_125HZ          1
#define LRADC_SAMPLE_62HZ           2
#define LRADC_SAMPLE_32HZ           3

/* LRADC_INTC Register Bits (matches Linux sun4i-lradc-keys.c) */

#define LRADC_INTC_CH0_DATA_EN        (1 << 0)
#define LRADC_INTC_CH0_KEYDOWN_EN     (1 << 1)
#define LRADC_INTC_CH0_HOLD_EN        (1 << 2)
#define LRADC_INTC_CH0_ALRDY_HOLD_EN  (1 << 3)
#define LRADC_INTC_CH0_KEYUP_EN       (1 << 4)

#define LRADC_INTC_CH1_DATA_EN        (1 << 8)
#define LRADC_INTC_CH1_KEYDOWN_EN     (1 << 9)
#define LRADC_INTC_CH1_HOLD_EN        (1 << 10)
#define LRADC_INTC_CH1_ALRDY_HOLD_EN  (1 << 11)
#define LRADC_INTC_CH1_KEYUP_EN       (1 << 12)

/* LRADC_INTS Register Bits (write 1 to clear, matches Linux sun4i-lradc-keys.c) */

#define LRADC_INTS_CH0_DATA_PEND      (1 << 0)
#define LRADC_INTS_CH0_KEYDOWN_PEND   (1 << 1)
#define LRADC_INTS_CH0_HOLD_PEND      (1 << 2)
#define LRADC_INTS_CH0_ALRDY_HOLD_PEND (1 << 3)
#define LRADC_INTS_CH0_KEYUP_PEND     (1 << 4)

#define LRADC_INTS_CH1_DATA_PEND      (1 << 8)
#define LRADC_INTS_CH1_KEYDOWN_PEND   (1 << 9)
#define LRADC_INTS_CH1_HOLD_PEND      (1 << 10)
#define LRADC_INTS_CH1_ALRDY_HOLD_PEND (1 << 11)
#define LRADC_INTS_CH1_KEYUP_PEND     (1 << 12)
#define LRADC_INTS_ALL                0x1f1f

/* ADC Data Mask (6-bit resolution) */

#define LRADC_DATA_MASK             0x3f

/* LRADC IRQ Number */

#define F1C_IRQ_LRADC               22

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* ADC value to button mapping structure */

struct lradc_key_map_s
{
  uint8_t adc_min;      /* ADC minimum threshold */
  uint8_t adc_max;      /* ADC maximum threshold */
  uint8_t button_bit;   /* Corresponding button bit */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: f1c100s_lradc_initialize
 *
 * Description:
 *   Initialize the LRADC driver and register it as a buttons device.
 *
 * Input Parameters:
 *   devpath - The device path (e.g., "/dev/buttons")
 *   keymap  - Array of ADC-to-button mappings
 *   nkeys   - Number of keys in the mapping table
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int f1c100s_lradc_initialize(FAR const char *devpath,
                             FAR const struct lradc_key_map_s *keymap,
                             int nkeys);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_F1C100S_LRADC */
#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_LRADC_H */
