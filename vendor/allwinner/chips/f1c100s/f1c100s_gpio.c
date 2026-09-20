/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c_gpio.c
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
#include <errno.h>
#include <debug.h>
#include <nuttx/kmalloc.h>
#include <nuttx/irq.h>
#include <nuttx/ioexpander/gpio.h>
#include "chip.h"
#include "arm_internal.h"
#include "hardware/f1c100s_gpio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* GPIOA_BASE..GPIOF_BASE come from hardware/f1c100s_gpio.h (GPIO_BASE +
 * per-port offset); F1C_PIO_BASE there is named GPIO_BASE.
 */

/* Register Offsets (per port) */
#define GPIO_CFG0            0x00  /* Configure register 0 (pin 0-7) */
#define GPIO_CFG1            0x04  /* Configure register 1 (pin 8-15) */
#define GPIO_CFG2            0x08  /* Configure register 2 (pin 16-23) */
#define GPIO_CFG3            0x0c  /* Configure register 3 (pin 24-31) */
#define GPIO_DAT             0x10  /* Data register */
#define GPIO_DRV0            0x14  /* Drive level 0 (pin 0-15) */
#define GPIO_DRV1            0x18  /* Drive level 1 (pin 16-31) */
#define GPIO_PUL0            0x1c  /* Pull register 0 (pin 0-15) */
#define GPIO_PUL1            0x20  /* Pull register 1 (pin 16-31) */

/* enum f1c_gpio_mode_e / f1c_gpio_pull_e / f1c_gpio_drv_e now live in
 * hardware/f1c100s_gpio.h so other translation units (the /dev/gpio
 * registration below, and any consumer) can share one definition
 * instead of redeclaring their own local prototypes.
 */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct f1c_gpiodev_s
{
  struct gpio_dev_s gpio;
  uint32_t          port;
  uint8_t           pin;
  pin_interrupt_t   callback;
  int8_t            irqbank;  /* 0=PD, 1=PE, 2=PF, -1=no EINT */
};

/* suniv PIO EINT: 3 banks (PD/PE/PF), IRQ_CFG at 0x200, 0x20 per bank.
 * linux pinctrl-suniv-f1c100s.c: PD bank 0, PE bank 1, PF bank 2.
 */

#define F1C_PIO_IRQ_CFG       0x200
#define F1C_PIO_IRQ_BANK_SZ   0x20
#define F1C_PIO_IRQ_CTL       0x10
#define F1C_PIO_IRQ_STA       0x14
#define F1C_PIO_EINT_BOTH     0x4

static FAR struct f1c_gpiodev_s *g_eint_dev[3][32];
static bool g_eint_irq_attached[3];

static int f1c_port_irqbank(uint32_t port)
{
  if (port == GPIOD_BASE)
    {
      return 0;
    }
  else if (port == GPIOE_BASE)
    {
      return 1;
    }
  else if (port == GPIOF_BASE)
    {
      return 2;
    }

  return -1;
}

static int f1c_port_irqn(uint32_t port)
{
  if (port == GPIOD_BASE)
    {
      return F1C_IRQ_PIOD;
    }
  else if (port == GPIOE_BASE)
    {
      return F1C_IRQ_PIOE;
    }
  else if (port == GPIOF_BASE)
    {
      return F1C_IRQ_PIOF;
    }

  return -1;
}

static uint32_t f1c_eint_bank_base(int bank)
{
  return GPIO_BASE + F1C_PIO_IRQ_CFG + (uint32_t)bank * F1C_PIO_IRQ_BANK_SZ;
}

static int f1c_pio_eint_isr(int irq, FAR void *context, FAR void *arg)
{
  int bank = (int)(uintptr_t)arg;
  uint32_t base = f1c_eint_bank_base(bank);
  uint32_t sta;
  uint32_t bit;

  sta = getreg32(base + F1C_PIO_IRQ_STA);
  for (bit = 0; bit < 32 && sta != 0; bit++)
    {
      if ((sta & (1u << bit)) != 0)
        {
          putreg32(1u << bit, base + F1C_PIO_IRQ_STA);
          sta &= ~(1u << bit);
          if (g_eint_dev[bank][bit] != NULL &&
              g_eint_dev[bank][bit]->callback != NULL)
            {
              g_eint_dev[bank][bit]->callback(&g_eint_dev[bank][bit]->gpio,
                                              (uint8_t)bit);
            }
        }
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c_gpio_config
 *
 * Description:
 *   Configure a GPIO pin
 *
 * Parameters:
 *   port - GPIO port base address
 *   pin  - Pin number (0-31)
 *   mode - Pin mode (input/output/AF)
 *   pull - Pull up/down configuration
 *   drv  - Drive strength
 *
 ****************************************************************************/

void f1c_gpio_config(uint32_t port, uint8_t pin,
                     enum f1c_gpio_mode_e mode,
                     enum f1c_gpio_pull_e pull,
                     enum f1c_gpio_drv_e drv)
{
  uint32_t reg;
  uint32_t val;

  /* Set pin mode (function) */
  reg = port + GPIO_CFG0 + (pin / 8) * 4;
  val = getreg32(reg);
  val &= ~(0x7 << ((pin % 8) * 4));
  val |= ((mode & 0x7) << ((pin % 8) * 4));
  putreg32(val, reg);

  /* Set drive strength */
  reg = port + GPIO_DRV0 + (pin / 16) * 4;
  val = getreg32(reg);
  val &= ~(0x3 << ((pin % 16) * 2));
  val |= ((drv & 0x3) << ((pin % 16) * 2));
  putreg32(val, reg);

  /* Set pull configuration */
  reg = port + GPIO_PUL0 + (pin / 16) * 4;
  val = getreg32(reg);
  val &= ~(0x3 << ((pin % 16) * 2));
  val |= ((pull & 0x3) << ((pin % 16) * 2));
  putreg32(val, reg);
}

/****************************************************************************
 * Name: f1c_gpio_write
 *
 * Description:
 *   Write to a GPIO pin
 *
 ****************************************************************************/

void f1c_gpio_write(uint32_t port, uint8_t pin, bool value)
{
  uint32_t reg = port + GPIO_DAT;
  uint32_t val = getreg32(reg);

  if (value)
    {
      val |= (1 << pin);
    }
  else
    {
      val &= ~(1 << pin);
    }

  putreg32(val, reg);
}

/****************************************************************************
 * Name: f1c_gpio_read
 *
 * Description:
 *   Read from a GPIO pin
 *
 ****************************************************************************/

bool f1c_gpio_read(uint32_t port, uint8_t pin)
{
  uint32_t reg = port + GPIO_DAT;
  uint32_t val = getreg32(reg);

  return (val & (1 << pin)) != 0;
}

/****************************************************************************
 * Name: f1c_gpio_set_pull
 *
 * Description:
 *   Set GPIO pull up/down
 *
 ****************************************************************************/

void f1c_gpio_set_pull(uint32_t port, uint8_t pin,
                       enum f1c_gpio_pull_e pull)
{
  uint32_t reg = port + GPIO_PUL0 + (pin / 16) * 4;
  uint32_t val = getreg32(reg);

  val &= ~(0x3 << ((pin % 16) * 2));
  val |= ((pull & 0x3) << ((pin % 16) * 2));
  putreg32(val, reg);
}

/****************************************************************************
 * Name: f1c_gpio_set_drive
 *
 * Description:
 *   Set GPIO drive strength
 *
 ****************************************************************************/

void f1c_gpio_set_drive(uint32_t port, uint8_t pin,
                        enum f1c_gpio_drv_e drv)
{
  uint32_t reg = port + GPIO_DRV0 + (pin / 16) * 4;
  uint32_t val = getreg32(reg);

  val &= ~(0x3 << ((pin % 16) * 2));
  val |= ((drv & 0x3) << ((pin % 16) * 2));
  putreg32(val, reg);
}

/****************************************************************************
 * Name: f1c_gpiodev_read / f1c_gpiodev_write
 *
 * Description:
 *   struct gpio_operations_s lowerhalf for a single output pin,
 *   backing the /dev/gpio character device registered by
 *   f1c_gpio_register_output(). Just forwards to the raw register
 *   functions above.
 *
 ****************************************************************************/

static int f1c_gpiodev_read(FAR struct gpio_dev_s *dev, FAR bool *value)
{
  FAR struct f1c_gpiodev_s *priv = (FAR struct f1c_gpiodev_s *)dev;

  *value = f1c_gpio_read(priv->port, priv->pin);
  return OK;
}

static int f1c_gpiodev_write(FAR struct gpio_dev_s *dev, bool value)
{
  FAR struct f1c_gpiodev_s *priv = (FAR struct f1c_gpiodev_s *)dev;

  f1c_gpio_write(priv->port, priv->pin, value);
  return OK;
}

static const struct gpio_operations_s g_f1c_gpio_out_ops =
{
  .go_read       = f1c_gpiodev_read,
  .go_write      = f1c_gpiodev_write,
  .go_attach     = NULL,
  .go_enable     = NULL,
  .go_setpintype = NULL,
};

int f1c_gpio_register_output(uint32_t port, uint8_t pin,
                             enum f1c_gpio_drv_e drv,
                             const char *name)
{
  FAR struct f1c_gpiodev_s *priv;
  int ret;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  f1c_gpio_config(port, pin, GPIO_MODE_OUTPUT, GPIO_PULL_NONE, drv);

  priv->port            = port;
  priv->pin             = pin;
  priv->irqbank         = -1;
  priv->gpio.gp_pintype = GPIO_OUTPUT_PIN;
  priv->gpio.gp_ops     = &g_f1c_gpio_out_ops;

  ret = gpio_pin_register_byname(&priv->gpio, name);
  if (ret < 0)
    {
      kmm_free(priv);
    }

  return ret;
}

static int f1c_gpiodev_attach(FAR struct gpio_dev_s *dev,
                              pin_interrupt_t callback)
{
  FAR struct f1c_gpiodev_s *priv = (FAR struct f1c_gpiodev_s *)dev;
  int bank;
  int irqn;
  int ret;

  bank = f1c_port_irqbank(priv->port);
  if (bank < 0)
    {
      return -ENOTSUP;
    }

  priv->callback = callback;
  priv->irqbank  = (int8_t)bank;

  if (callback == NULL)
    {
      g_eint_dev[bank][priv->pin] = NULL;
      return OK;
    }

  g_eint_dev[bank][priv->pin] = priv;

  if (!g_eint_irq_attached[bank])
    {
      irqn = f1c_port_irqn(priv->port);
      ret = irq_attach(irqn, f1c_pio_eint_isr, (FAR void *)(uintptr_t)bank);
      if (ret < 0)
        {
          g_eint_dev[bank][priv->pin] = NULL;
          return ret;
        }

      up_enable_irq(irqn);
      g_eint_irq_attached[bank] = true;
    }

  return OK;
}

static int f1c_gpiodev_enable(FAR struct gpio_dev_s *dev, bool enable)
{
  FAR struct f1c_gpiodev_s *priv = (FAR struct f1c_gpiodev_s *)dev;
  int bank = f1c_port_irqbank(priv->port);
  uint32_t base;
  uint32_t val;

  if (bank < 0)
    {
      return -ENOTSUP;
    }

  base = f1c_eint_bank_base(bank);
  val  = getreg32(base + F1C_PIO_IRQ_CTL);
  if (enable)
    {
      val |= (1u << priv->pin);
    }
  else
    {
      val &= ~(1u << priv->pin);
    }

  putreg32(val, base + F1C_PIO_IRQ_CTL);
  return OK;
}

static int f1c_gpiodev_setpintype(FAR struct gpio_dev_s *dev,
                                  enum gpio_pintype_e pintype)
{
  FAR struct f1c_gpiodev_s *priv = (FAR struct f1c_gpiodev_s *)dev;
  int bank;
  uint32_t base;
  uint32_t val;
  uint32_t cfg;
  unsigned int shift;

  switch (pintype)
    {
      case GPIO_INPUT_PIN:
        f1c_gpio_config(priv->port, priv->pin, GPIO_MODE_INPUT,
                        GPIO_PULL_NONE, GPIO_DRV_1);
        break;

      case GPIO_INPUT_PIN_PULLUP:
        f1c_gpio_config(priv->port, priv->pin, GPIO_MODE_INPUT,
                        GPIO_PULL_UP, GPIO_DRV_1);
        break;

      case GPIO_INPUT_PIN_PULLDOWN:
        f1c_gpio_config(priv->port, priv->pin, GPIO_MODE_INPUT,
                        GPIO_PULL_DOWN, GPIO_DRV_1);
        break;

      case GPIO_INTERRUPT_PIN:
      case GPIO_INTERRUPT_BOTH_PIN:
      case GPIO_INTERRUPT_RISING_PIN:
      case GPIO_INTERRUPT_FALLING_PIN:
      case GPIO_INTERRUPT_HIGH_PIN:
      case GPIO_INTERRUPT_LOW_PIN:
        bank = f1c_port_irqbank(priv->port);
        if (bank < 0)
          {
            return -ENOTSUP;
          }

        f1c_gpio_config(priv->port, priv->pin, GPIO_MODE_EINT,
                        GPIO_PULL_UP, GPIO_DRV_1);

        if (pintype == GPIO_INTERRUPT_RISING_PIN ||
            pintype == GPIO_INTERRUPT_HIGH_PIN)
          {
            cfg = 0x0;
          }
        else if (pintype == GPIO_INTERRUPT_FALLING_PIN ||
                 pintype == GPIO_INTERRUPT_LOW_PIN)
          {
            cfg = 0x1;
          }
        else
          {
            cfg = F1C_PIO_EINT_BOTH;
          }

        base  = f1c_eint_bank_base(bank);
        shift = (priv->pin % 8) * 4;
        val   = getreg32(base + (priv->pin / 8) * 4);
        val  &= ~(0xfu << shift);
        val  |= (cfg << shift);
        putreg32(val, base + (priv->pin / 8) * 4);
        putreg32(1u << priv->pin, base + F1C_PIO_IRQ_STA);
        break;

      default:
        return -ENOTSUP;
    }

  priv->gpio.gp_pintype = pintype;
  return OK;
}

static const struct gpio_operations_s g_f1c_gpio_in_ops =
{
  .go_read       = f1c_gpiodev_read,
  .go_write      = NULL,
  .go_attach     = f1c_gpiodev_attach,
  .go_enable     = f1c_gpiodev_enable,
  .go_setpintype = f1c_gpiodev_setpintype,
};

int f1c_gpio_register_input(uint32_t port, uint8_t pin,
                            enum f1c_gpio_pull_e pull,
                            const char *name,
                            FAR struct gpio_dev_s **dev_out)
{
  FAR struct f1c_gpiodev_s *priv;
  int ret;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      return -ENOMEM;
    }

  f1c_gpio_config(port, pin, GPIO_MODE_INPUT, pull, GPIO_DRV_1);

  priv->port            = port;
  priv->pin             = pin;
  priv->irqbank         = -1;
  priv->gpio.gp_pintype = (pull == GPIO_PULL_UP) ? GPIO_INPUT_PIN_PULLUP :
                          (pull == GPIO_PULL_DOWN) ? GPIO_INPUT_PIN_PULLDOWN :
                          GPIO_INPUT_PIN;
  priv->gpio.gp_ops     = &g_f1c_gpio_in_ops;

  ret = gpio_pin_register_byname(&priv->gpio, name);
  if (ret < 0)
    {
      kmm_free(priv);
      return ret;
    }

  if (dev_out != NULL)
    {
      *dev_out = &priv->gpio;
    }

  return OK;
}
