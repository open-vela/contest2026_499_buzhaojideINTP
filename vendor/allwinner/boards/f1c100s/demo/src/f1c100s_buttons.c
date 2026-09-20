/****************************************************************************
 * vendor/allwinner/boards/f1c100s/demo/src/f1c100s_buttons.c
 *
 * SCH_F1C200S小电脑.pdf: SW1/SW2/SW3 are independent GPIOs with 47k
 * pull-ups to 3.3V, press to GND (active-low). Not an LRADC divider.
 *
 *   SW1 L  = PE2
 *   SW2 CK = PE3
 *   SW3 R  = PE4
 *
 * Pins are registered as /dev/gpio_key_* via f1c_gpio_register_input().
 * board_buttons() reads them with ioctl(GPIOC_READ). IRQs go through
 * the same gpio_dev go_attach/go_enable (PIO EINT), not raw PIO pokes.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <errno.h>
#include <syslog.h>
#include <debug.h>

#include <nuttx/board.h>
#include <nuttx/irq.h>
#include <nuttx/ioexpander/gpio.h>
#include <arch/board/board.h>

#include "hardware/f1c100s_gpio.h"

#ifdef CONFIG_ARCH_BUTTONS

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* PE4 is SW3 only. ST7789 DC is PE5, RST is PE10. */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct f1c_board_btn_s
{
  FAR struct gpio_dev_s *gpio;
  xcpt_t                 handler;
  FAR void              *arg;
  uint32_t               port;
  uint8_t                pin;
  bool                   skip;
  FAR const char        *name;
  FAR const char        *devname;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct f1c_board_btn_s g_btns[NUM_BUTTONS] =
{
  { NULL, NULL, NULL, GPIOE_BASE, 2, false, "L",  "gpio_key_l"  },
  { NULL, NULL, NULL, GPIOE_BASE, 3, false, "CK", "gpio_key_ck" },
  { NULL, NULL, NULL, GPIOE_BASE, 4, false, "R",  "gpio_key_r"  },
};

static uint32_t g_nbuttons;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int f1c_btn_gpio_isr(FAR struct gpio_dev_s *dev, uint8_t pin)
{
  int id;

  for (id = 0; id < NUM_BUTTONS; id++)
    {
      if (g_btns[id].gpio == dev && g_btns[id].handler != NULL)
        {
          return g_btns[id].handler(0, NULL, g_btns[id].arg);
        }
    }

  UNUSED(pin);
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

uint32_t board_button_initialize(void)
{
  int i;
  int ret;

  g_nbuttons = 0;

  for (i = 0; i < NUM_BUTTONS; i++)
    {
      if (g_btns[i].skip)
        {
          syslog(LOG_WARNING,
                 "button %s (PE%u) skipped: pin shared with ST7789 DC\n",
                 g_btns[i].name, g_btns[i].pin);
          continue;
        }

      ret = f1c_gpio_register_input(g_btns[i].port, g_btns[i].pin,
                                    GPIO_PULL_UP, g_btns[i].devname,
                                    &g_btns[i].gpio);
      if (ret < 0)
        {
          syslog(LOG_ERR, "button %s register %s failed: %d\n",
                 g_btns[i].name, g_btns[i].devname, ret);
          g_btns[i].gpio = NULL;
          continue;
        }

      g_nbuttons++;
      syslog(LOG_INFO, "button %s on PE%u as /dev/%s (active-low)\n",
             g_btns[i].name, g_btns[i].pin, g_btns[i].devname);
    }

  return g_nbuttons;
}

uint32_t board_buttons(void)
{
  uint32_t set = 0;
  int i;
  bool value;

  for (i = 0; i < NUM_BUTTONS; i++)
    {
      if (g_btns[i].gpio == NULL || g_btns[i].gpio->gp_ops->go_read == NULL)
        {
          continue;
        }

      if (g_btns[i].gpio->gp_ops->go_read(g_btns[i].gpio, &value) < 0)
        {
          continue;
        }

      /* External 47k pull-up: high = released, low = pressed. */

      if (!value)
        {
          set |= (1u << i);
        }
    }

  return set;
}

#endif /* CONFIG_ARCH_BUTTONS */

#ifdef CONFIG_ARCH_IRQBUTTONS
int board_button_irq(int id, xcpt_t irqhandler, FAR void *arg)
{
  FAR struct f1c_board_btn_s *btn;
  int ret;

  if (id < 0 || id >= NUM_BUTTONS)
    {
      return -EINVAL;
    }

  btn = &g_btns[id];
  if (btn->gpio == NULL || btn->gpio->gp_ops == NULL)
    {
      return -ENODEV;
    }

  if (btn->gpio->gp_ops->go_setpintype == NULL ||
      btn->gpio->gp_ops->go_attach == NULL ||
      btn->gpio->gp_ops->go_enable == NULL)
    {
      return -ENOSYS;
    }

  if (irqhandler == NULL)
    {
      btn->gpio->gp_ops->go_enable(btn->gpio, false);
      btn->gpio->gp_ops->go_attach(btn->gpio, NULL);
      btn->handler = NULL;
      btn->arg     = NULL;
      return OK;
    }

  ret = btn->gpio->gp_ops->go_setpintype(btn->gpio, GPIO_INTERRUPT_BOTH_PIN);
  if (ret < 0)
    {
      return ret;
    }

  btn->handler = irqhandler;
  btn->arg     = arg;

  ret = btn->gpio->gp_ops->go_attach(btn->gpio, f1c_btn_gpio_isr);
  if (ret < 0)
    {
      btn->handler = NULL;
      return ret;
    }

  return btn->gpio->gp_ops->go_enable(btn->gpio, true);
}
#endif /* CONFIG_ARCH_IRQBUTTONS */
