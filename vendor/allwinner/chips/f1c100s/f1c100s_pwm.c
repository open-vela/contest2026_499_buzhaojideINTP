/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_pwm.c
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

#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/timers/pwm.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <arch/board/board.h>

#include "arm_internal.h"
#include "hardware/f1c100s_pwm.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* GPIO configuration */

#define F1C_PIO_BASE              0x01c20800
#define GPIO_CFG0                 0x00
#define GPIO_CFG1                 0x04

/* Port A offset: 0x00 from PIO base, Port E offset: 0x90 */

#define GPIOA_BASE                (F1C_PIO_BASE + 0x00)
#define GPIOE_BASE                (F1C_PIO_BASE + 0x90)

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Spinlock for multi-channel protection */

static spinlock_t g_pwm_lock = SP_UNLOCKED;

/* Prescaler divider table */

static const uint32_t g_pwm_prescaler[] =
{
  120,    /* 0x0 */
  180,    /* 0x1 */
  240,    /* 0x2 */
  360,    /* 0x3 */
  480,    /* 0x4 */
  0,      /* 0x5 reserved */
  0,      /* 0x6 reserved */
  0,      /* 0x7 reserved */
  12000,  /* 0x8 */
  24000,  /* 0x9 */
  36000,  /* 0xa */
  48000,  /* 0xb */
  72000,  /* 0xc */
  0,      /* 0xd reserved */
  0,      /* 0xe reserved */
  1,      /* 0xf */
};

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct f1c100s_pwm_s
{
  const struct pwm_ops_s *ops;
  uint8_t channel;
  uint32_t frequency;
  ub16_t duty;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int f1c100s_pwm_setup(FAR struct pwm_lowerhalf_s *dev);
static int f1c100s_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev);
static int f1c100s_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                             FAR const struct pwm_info_s *info);
static int f1c100s_pwm_stop(FAR struct pwm_lowerhalf_s *dev);
static int f1c100s_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev,
                             int cmd, unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct pwm_ops_s g_pwm_ops =
{
  .setup    = f1c100s_pwm_setup,
  .shutdown = f1c100s_pwm_shutdown,
  .start    = f1c100s_pwm_start,
  .stop     = f1c100s_pwm_stop,
  .ioctl    = f1c100s_pwm_ioctl,
};

#ifdef CONFIG_F1C100S_PWM0
static struct f1c100s_pwm_s g_pwm0 =
{
  .ops     = &g_pwm_ops,
  .channel = 0,
};
#endif

#ifdef CONFIG_F1C100S_PWM1
static struct f1c100s_pwm_s g_pwm1 =
{
  .ops     = &g_pwm_ops,
  .channel = 1,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_pwm_getreg
 ****************************************************************************/

static inline uint32_t f1c100s_pwm_getreg(uint32_t offset)
{
  return getreg32(F1C100S_PWM_BASE + offset);
}

/****************************************************************************
 * Name: f1c100s_pwm_putreg
 ****************************************************************************/

static inline void f1c100s_pwm_putreg(uint32_t offset, uint32_t value)
{
  putreg32(value, F1C100S_PWM_BASE + offset);
}

/****************************************************************************
 * Name: f1c100s_pwm_select_prescaler
 ****************************************************************************/

static int f1c100s_pwm_select_prescaler(uint32_t frequency,
                                        uint32_t *prescaler,
                                        uint32_t *period)
{
  int i;
  uint32_t clk;
  uint32_t prd;

  for (i = 0; i < 16; i++)
    {
      if (g_pwm_prescaler[i] == 0)
        {
          continue;
        }

      clk = F1C100S_PWM_CLK_FREQ / g_pwm_prescaler[i];
      prd = clk / frequency;

      if (prd > 0 && prd <= 65536)
        {
          *prescaler = i;
          *period = prd - 1;
          return OK;
        }
    }

  return -EINVAL;
}

/****************************************************************************
 * Name: f1c100s_pwm_setup
 ****************************************************************************/

static int f1c100s_pwm_setup(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct f1c100s_pwm_s *priv = (FAR struct f1c100s_pwm_s *)dev;
  uint32_t regval;

  pwminfo("PWM%d setup\n", priv->channel);

  /* Configure GPIO for PWM function */

  if (priv->channel == 0)
    {
#ifdef CONFIG_F1C100S_PWM0_PA2
      /* PA2 -> PWM0, function 3 (F1C100S UM 4.2.3: "PA2 Select")
       * PA2 is in GPIO_CFG0 (pins 0-7), bits [11:8]
       */

      regval = getreg32(GPIOA_BASE + GPIO_CFG0);
      regval &= ~(0xf << 8);   /* Clear PA2 function */
      regval |= (3 << 8);      /* Set function 3 (PWM0) */
      putreg32(regval, GPIOA_BASE + GPIO_CFG0);
#else
      /* PE12 -> PWM0, function 4
       * PE12 is in GPIO_CFG1 (pins 8-15), bits [19:16]
       */

      regval = getreg32(GPIOE_BASE + GPIO_CFG1);
      regval &= ~(0xf << 16);  /* Clear PE12 function */
      regval |= (4 << 16);     /* Set function 4 (PWM0) */
      putreg32(regval, GPIOE_BASE + GPIO_CFG1);
#endif
    }
  else
    {
      /* PE6 -> PWM1, function 3 (func 4 is I2S OUT)
       * PE6 is in GPIO_CFG0 (pins 0-7), bits [27:24]
       * Matches Linux pinctrl-suniv-f1c100s.c:303
       */

      regval = getreg32(GPIOE_BASE + GPIO_CFG0);
      regval &= ~(0x7 << 24);  /* Clear PE6 function */
      regval |= (3 << 24);     /* Set function 3 (PWM1) */
      putreg32(regval, GPIOE_BASE + GPIO_CFG0);
    }

  return OK;
}

/****************************************************************************
 * Name: f1c100s_pwm_shutdown
 ****************************************************************************/

static int f1c100s_pwm_shutdown(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct f1c100s_pwm_s *priv = (FAR struct f1c100s_pwm_s *)dev;
  uint32_t regval;

  pwminfo("PWM%d shutdown\n", priv->channel);

  f1c100s_pwm_stop(dev);

  /* Restore GPIO to input mode */

  if (priv->channel == 0)
    {
#ifdef CONFIG_F1C100S_PWM0_PA2
      regval = getreg32(GPIOA_BASE + GPIO_CFG0);
      regval &= ~(0xf << 8);
      putreg32(regval, GPIOA_BASE + GPIO_CFG0);
#else
      regval = getreg32(GPIOE_BASE + GPIO_CFG1);
      regval &= ~(0xf << 16);
      putreg32(regval, GPIOE_BASE + GPIO_CFG1);
#endif
    }
  else
    {
      regval = getreg32(GPIOE_BASE + GPIO_CFG0);
      regval &= ~(0x7 << 24);
      putreg32(regval, GPIOE_BASE + GPIO_CFG0);
    }

  return OK;
}

/****************************************************************************
 * Name: f1c100s_pwm_start
 ****************************************************************************/

static int f1c100s_pwm_start(FAR struct pwm_lowerhalf_s *dev,
                             FAR const struct pwm_info_s *info)
{
  FAR struct f1c100s_pwm_s *priv = (FAR struct f1c100s_pwm_s *)dev;
  uint32_t regval;
  uint32_t prescaler;
  uint32_t period;
  uint32_t duty_cycles;
  irqstate_t flags;
  int ret;

  pwminfo("PWM%d start: freq=%lu duty=%u\n",
          priv->channel, (unsigned long)info->frequency,
          (unsigned int)info->duty);

  ret = f1c100s_pwm_select_prescaler(info->frequency, &prescaler, &period);
  if (ret < 0)
    {
      pwmerr("Failed to find valid prescaler for freq=%lu\n",
             (unsigned long)info->frequency);
      return ret;
    }

  duty_cycles = ((uint32_t)(period + 1) * info->duty) >> 16;

  flags = spin_lock_irqsave(&g_pwm_lock);

  /* Write period register */

  regval = (duty_cycles << PWM_ACT_CYS_SHIFT) |
           (period << PWM_ENTIRE_CYS_SHIFT);

  if (priv->channel == 0)
    {
      f1c100s_pwm_putreg(PWM_CH0_PERIOD_OFFSET, regval);
    }
  else
    {
      f1c100s_pwm_putreg(PWM_CH1_PERIOD_OFFSET, regval);
    }

  /* Configure and enable PWM */

  regval = f1c100s_pwm_getreg(PWM_CTRL_REG_OFFSET);

  if (priv->channel == 0)
    {
      regval &= ~(PWM_CH0_PRESCAL_MASK | PWM_CH0_EN | PWM_CH0_ACT_STATE |
                  PWM_CH0_CLK_GATING | PWM_CH0_MODE | PWM_CH0_BYPASS);
      regval |= (prescaler << PWM_CH0_PRESCAL_SHIFT) |
                PWM_CH0_CLK_GATING |
                PWM_CH0_ACT_STATE |
                PWM_CH0_EN;
    }
  else
    {
      regval &= ~(PWM_CH1_PRESCAL_MASK | PWM_CH1_EN | PWM_CH1_ACT_STATE |
                  PWM_CH1_CLK_GATING | PWM_CH1_MODE | PWM_CH1_BYPASS);
      regval |= (prescaler << PWM_CH1_PRESCAL_SHIFT) |
                PWM_CH1_CLK_GATING |
                PWM_CH1_ACT_STATE |
                PWM_CH1_EN;
    }

  f1c100s_pwm_putreg(PWM_CTRL_REG_OFFSET, regval);

  /* Wait for ready bit */

  {
    int timeout = 1000;
    uint32_t rdy_bit = (priv->channel == 0) ? PWM_CH0_RDY : PWM_CH1_RDY;

    while (!(f1c100s_pwm_getreg(PWM_CTRL_REG_OFFSET) & rdy_bit))
      {
        if (--timeout == 0)
          {
            pwmerr("Timeout waiting for PWM%d ready\n", priv->channel);
            spin_unlock_irqrestore(&g_pwm_lock, flags);
            return -ETIMEDOUT;
          }

        up_udelay(1);
      }
  }

  spin_unlock_irqrestore(&g_pwm_lock, flags);

  priv->frequency = info->frequency;
  priv->duty = info->duty;

  return OK;
}

/****************************************************************************
 * Name: f1c100s_pwm_stop
 ****************************************************************************/

static int f1c100s_pwm_stop(FAR struct pwm_lowerhalf_s *dev)
{
  FAR struct f1c100s_pwm_s *priv = (FAR struct f1c100s_pwm_s *)dev;
  irqstate_t flags;
  uint32_t regval;

  pwminfo("PWM%d stop\n", priv->channel);

  flags = spin_lock_irqsave(&g_pwm_lock);

  regval = f1c100s_pwm_getreg(PWM_CTRL_REG_OFFSET);

  if (priv->channel == 0)
    {
      regval &= ~(PWM_CH0_EN | PWM_CH0_CLK_GATING);
    }
  else
    {
      regval &= ~(PWM_CH1_EN | PWM_CH1_CLK_GATING);
    }

  f1c100s_pwm_putreg(PWM_CTRL_REG_OFFSET, regval);

  spin_unlock_irqrestore(&g_pwm_lock, flags);

  return OK;
}

/****************************************************************************
 * Name: f1c100s_pwm_ioctl
 ****************************************************************************/

static int f1c100s_pwm_ioctl(FAR struct pwm_lowerhalf_s *dev,
                             int cmd, unsigned long arg)
{
  return -ENOTTY;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_pwm_initialize
 ****************************************************************************/

FAR struct pwm_lowerhalf_s *f1c100s_pwm_initialize(int channel)
{
  FAR struct f1c100s_pwm_s *priv;

  pwminfo("PWM%d initialize\n", channel);

  switch (channel)
    {
#ifdef CONFIG_F1C100S_PWM0
      case 0:
        priv = &g_pwm0;
        break;
#endif

#ifdef CONFIG_F1C100S_PWM1
      case 1:
        priv = &g_pwm1;
        break;
#endif

      default:
        pwmerr("Invalid PWM channel: %d\n", channel);
        return NULL;
    }

  return (FAR struct pwm_lowerhalf_s *)priv;
}
