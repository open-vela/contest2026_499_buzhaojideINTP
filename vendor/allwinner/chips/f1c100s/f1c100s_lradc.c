/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_lradc.c
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

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/arch.h>
#include <nuttx/input/buttons.h>

#include "arm_internal.h"
#include "hardware/f1c100s_lradc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/



/****************************************************************************
 * Private Types
 ****************************************************************************/

struct f1c100s_lradc_s
{
  struct btn_lowerhalf_s lower;             /* Button lower-half interface */
  FAR const struct lradc_key_map_s *keymap; /* Key mapping table */
  int nkeys;                                /* Number of keys */
  btn_buttonset_t supported;                /* Supported buttons mask */
  btn_buttonset_t current;                  /* Current button state */
  btn_handler_t handler;                    /* Interrupt handler callback */
  FAR void *arg;                            /* Handler argument */
  btn_buttonset_t press_mask;               /* Press interrupt mask */
  btn_buttonset_t release_mask;             /* Release interrupt mask */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static btn_buttonset_t lradc_supported(
                         FAR const struct btn_lowerhalf_s *lower);
static btn_buttonset_t lradc_buttons(
                         FAR const struct btn_lowerhalf_s *lower);
static void lradc_enable(FAR const struct btn_lowerhalf_s *lower,
                         btn_buttonset_t press, btn_buttonset_t release,
                         btn_handler_t handler, FAR void *arg);
static int lradc_interrupt(int irq, FAR void *context, FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct f1c100s_lradc_s g_lradc;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lradc_adc_to_button
 *
 * Description:
 *   Convert ADC value to button bit using the key mapping table.
 *
 ****************************************************************************/

static int lradc_adc_to_button(FAR struct f1c100s_lradc_s *priv, uint8_t adc)
{
  int i;

  for (i = 0; i < priv->nkeys; i++)
    {
      if (adc >= priv->keymap[i].adc_min && adc <= priv->keymap[i].adc_max)
        {
          return priv->keymap[i].button_bit;
        }
    }

  return -1;  /* No button matched */
}

/****************************************************************************
 * Name: lradc_supported
 *
 * Description:
 *   Return the set of buttons supported by the board.
 *
 ****************************************************************************/

static btn_buttonset_t lradc_supported(
                         FAR const struct btn_lowerhalf_s *lower)
{
  FAR struct f1c100s_lradc_s *priv = (FAR struct f1c100s_lradc_s *)lower;
  return priv->supported;
}

/****************************************************************************
 * Name: lradc_buttons
 *
 * Description:
 *   Return the current state of button data.
 *
 ****************************************************************************/

static btn_buttonset_t lradc_buttons(
                         FAR const struct btn_lowerhalf_s *lower)
{
  FAR struct f1c100s_lradc_s *priv = (FAR struct f1c100s_lradc_s *)lower;
  return priv->current;
}

/****************************************************************************
 * Name: lradc_enable
 *
 * Description:
 *   Enable interrupts on the selected set of buttons.
 *
 ****************************************************************************/

static void lradc_enable(FAR const struct btn_lowerhalf_s *lower,
                         btn_buttonset_t press, btn_buttonset_t release,
                         btn_handler_t handler, FAR void *arg)
{
  FAR struct f1c100s_lradc_s *priv = (FAR struct f1c100s_lradc_s *)lower;
  irqstate_t flags;
  uint32_t intc;

  flags = enter_critical_section();

  priv->handler = handler;
  priv->arg = arg;
  priv->press_mask = press;
  priv->release_mask = release;

  if (handler != NULL && (press != 0 || release != 0))
    {
      /* Enable KEYDOWN and KEYUP interrupts */

      intc = LRADC_INTC_CH0_KEYDOWN_EN | LRADC_INTC_CH0_KEYUP_EN;
      putreg32(intc, LRADC_INTC);
      up_enable_irq(F1C_IRQ_LRADC);
    }
  else
    {
      /* Disable all interrupts */

      putreg32(0, LRADC_INTC);
      up_disable_irq(F1C_IRQ_LRADC);
    }

  leave_critical_section(flags);
}

/****************************************************************************
 * Name: lradc_interrupt
 *
 * Description:
 *   LRADC interrupt handler.
 *
 ****************************************************************************/

static int lradc_interrupt(int irq, FAR void *context, FAR void *arg)
{
  FAR struct f1c100s_lradc_s *priv = (FAR struct f1c100s_lradc_s *)arg;
  uint32_t ints;
  uint32_t data;
  btn_buttonset_t old_state;
  int button;

  ints = getreg32(LRADC_INTS);

  /* Key down event */

  if (ints & LRADC_INTS_CH0_KEYDOWN_PEND)
    {
      data = getreg32(LRADC_DATA0) & LRADC_DATA_MASK;
      button = lradc_adc_to_button(priv, (uint8_t)data);

      if (button >= 0)
        {
          old_state = priv->current;
          priv->current |= (1 << button);

          /* Notify if state changed and handler registered */

          if (priv->current != old_state && priv->handler != NULL)
            {
              if (priv->press_mask & (1 << button))
                {
                  priv->handler(&priv->lower, priv->arg);
                }
            }
        }
    }

  /* Key up event */

  if (ints & LRADC_INTS_CH0_KEYUP_PEND)
    {
      old_state = priv->current;
      priv->current = 0;  /* All buttons released */

      if (priv->current != old_state && priv->handler != NULL)
        {
          if (priv->release_mask & old_state)
            {
              priv->handler(&priv->lower, priv->arg);
            }
        }
    }

  /* Clear pending interrupts (write 1 to clear) */

  putreg32(ints, LRADC_INTS);

  return OK;
}

/****************************************************************************
 * Name: lradc_hw_init
 *
 * Description:
 *   Initialize LRADC hardware.
 *
 ****************************************************************************/

static void lradc_hw_init(void)
{
  uint32_t reg;

  /* Note: On Allwinner suniv-f1c100s, LRADC does not use CCU bus clock gate
   * or soft reset (per Linux suniv-f1c100s.dtsi).
   */

  /* Reset LRADC controller */

  putreg32(0, LRADC_CTRL);
  putreg32(0, LRADC_INTC);
  putreg32(LRADC_INTS_ALL, LRADC_INTS);  /* Clear all pending */

  /* Configure LRADC:
   * - Continue mode
   * - 125Hz sample rate
   * - Enable CH0
   * - First convert delay = 2
   */

  reg = (LRADC_KEY_MODE_CONTINUE << LRADC_CTRL_KEY_MODE_SHIFT) |
        (LRADC_SAMPLE_125HZ << LRADC_CTRL_SAMPLE_RATE_SHIFT) |
        (2 << LRADC_CTRL_FIRST_DLY_SHIFT) |
        LRADC_CTRL_CH0_EN;
  putreg32(reg, LRADC_CTRL);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_lradc_initialize
 *
 * Description:
 *   Initialize the LRADC driver and register it as a buttons device.
 *
 ****************************************************************************/

int f1c100s_lradc_initialize(FAR const char *devpath,
                             FAR const struct lradc_key_map_s *keymap,
                             int nkeys)
{
  FAR struct f1c100s_lradc_s *priv = &g_lradc;
  int i;
  int ret;

  /* Initialize driver state */

  priv->lower.bl_supported = lradc_supported;
  priv->lower.bl_buttons = lradc_buttons;
  priv->lower.bl_enable = lradc_enable;
  priv->lower.bl_write = NULL;

  priv->keymap = keymap;
  priv->nkeys = nkeys;
  priv->current = 0;
  priv->handler = NULL;
  priv->arg = NULL;

  /* Build supported buttons mask */

  priv->supported = 0;
  for (i = 0; i < nkeys; i++)
    {
      priv->supported |= (1 << keymap[i].button_bit);
    }

  /* Initialize hardware */

  lradc_hw_init();

  /* Attach interrupt handler */

  ret = irq_attach(F1C_IRQ_LRADC, lradc_interrupt, priv);
  if (ret < 0)
    {
      ierr("ERROR: Failed to attach LRADC IRQ: %d\n", ret);
      return ret;
    }

  /* Register the buttons driver */

  ret = btn_register(devpath, &priv->lower);
  if (ret < 0)
    {
      ierr("ERROR: btn_register failed: %d\n", ret);
      irq_detach(F1C_IRQ_LRADC);
      return ret;
    }

  iinfo("LRADC buttons registered as %s\n", devpath);
  return OK;
}

#endif /* CONFIG_F1C100S_LRADC */
