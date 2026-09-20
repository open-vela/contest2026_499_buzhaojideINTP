/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_gpio.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_GPIO_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_GPIO_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* GPIO Port Base Address */

#define GPIO_BASE             0x01c20800

/* Per-port base addresses (PA-PF, 0x24 bytes apart) */

#define GPIOA_BASE            (GPIO_BASE + 0x00)
#define GPIOB_BASE            (GPIO_BASE + 0x24)
#define GPIOC_BASE            (GPIO_BASE + 0x48)
#define GPIOD_BASE            (GPIO_BASE + 0x6c)
#define GPIOE_BASE            (GPIO_BASE + 0x90)
#define GPIOF_BASE            (GPIO_BASE + 0xb4)

/* Common per-port register offsets */

#define GPIOx_CFG0_OFFSET     0x00  /* Pin 0-7 function config */
#define GPIOx_CFG1_OFFSET     0x04  /* Pin 8-15 function config */
#define GPIOx_CFG2_OFFSET     0x08  /* Pin 16-23 function config */
#define GPIOx_PUL0_OFFSET     0x1c  /* Pin 0-15 pull-up/down config */

/* GPIO Pin Encoding
 *
 * 16-bit Encoding:
 *   PPPP PPPP NNNN NNNN
 *
 * Where:
 *   P = Port (0-5 for PA-PF)
 *   N = Pin number (0-31)
 */

#define GPIO_PORT_SHIFT       8
#define GPIO_PIN_SHIFT        0
#define GPIO_PORT_MASK        (0xFF << GPIO_PORT_SHIFT)
#define GPIO_PIN_MASK         (0xFF << GPIO_PIN_SHIFT)

/* GPIO Ports */

#define GPIO_PORTA            (0 << GPIO_PORT_SHIFT)
#define GPIO_PORTB            (1 << GPIO_PORT_SHIFT)
#define GPIO_PORTC            (2 << GPIO_PORT_SHIFT)
#define GPIO_PORTD            (3 << GPIO_PORT_SHIFT)
#define GPIO_PORTE            (4 << GPIO_PORT_SHIFT)
#define GPIO_PORTF            (5 << GPIO_PORT_SHIFT)

/* Pin Configuration Macros */

#define GPIO_PIN(port, pin)   ((port) | (pin))

/* Extract port and pin from pinset */

#define GPIO_GET_PORT(pinset) (((pinset) & GPIO_PORT_MASK) >> GPIO_PORT_SHIFT)
#define GPIO_GET_PIN(pinset)  (((pinset) & GPIO_PIN_MASK) >> GPIO_PIN_SHIFT)

/* GPIO Function Modes */

enum f1c_gpio_mode_e
{
  GPIO_MODE_INPUT   = 0,
  GPIO_MODE_OUTPUT  = 1,
  GPIO_MODE_AF2     = 2,
  GPIO_MODE_AF3     = 3,
  GPIO_MODE_AF4     = 4,
  GPIO_MODE_AF5     = 5,
  GPIO_MODE_AF6     = 6,
  GPIO_MODE_EINT    = 6,  /* External interrupt */
  GPIO_MODE_DISABLE = 7
};

enum f1c_gpio_pull_e
{
  GPIO_PULL_NONE     = 0,
  GPIO_PULL_UP       = 1,
  GPIO_PULL_DOWN     = 2,
  GPIO_PULL_RESERVED = 3
};

enum f1c_gpio_drv_e
{
  GPIO_DRV_0 = 0,  /* Level 0 */
  GPIO_DRV_1 = 1,  /* Level 1 */
  GPIO_DRV_2 = 2,  /* Level 2 */
  GPIO_DRV_3 = 3   /* Level 3 */
};

/* GPIO Drive Strength */

#define GPIO_DRV_LEVEL0       0  /* Weakest */
#define GPIO_DRV_LEVEL1       1
#define GPIO_DRV_LEVEL2       2
#define GPIO_DRV_LEVEL3       3  /* Strongest */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

struct gpio_dev_s;

#ifdef __cplusplus
extern "C"
{
#endif

/* f1c_gpio_config/write/read: the functions actually implemented in
 * f1c100s_gpio.c and used throughout this tree today. Port is a raw
 * PIO block base address (GPIOx_BASE), not the packed pinset the
 * f1c100s_gpio_config/write/read prototypes below describe.
 */

void f1c_gpio_config(uint32_t port, uint8_t pin,
                     enum f1c_gpio_mode_e mode,
                     enum f1c_gpio_pull_e pull,
                     enum f1c_gpio_drv_e drv);
void f1c_gpio_write(uint32_t port, uint8_t pin, bool value);
bool f1c_gpio_read(uint32_t port, uint8_t pin);

/****************************************************************************
 * Name: f1c_gpio_register_output
 *
 * Description:
 *   Configure a pin as a push-pull output and register it as a named
 *   /dev/gpio character device (gpio_pin_register_byname). Drivers that
 *   need to toggle a control pin (LCD DC/RST, etc.) go through this --
 *   open() the resulting /dev/<name> once and ioctl(GPIOC_WRITE, ...)
 *   from then on -- instead of calling f1c_gpio_write() directly.
 *
 * Input Parameters:
 *   port - GPIO port base address (GPIOx_BASE)
 *   pin  - Pin number (0-31)
 *   drv  - Drive strength
 *   name - Device name; registers as /dev/<name>
 *
 * Returned Value:
 *   OK on success; a negated errno value on failure.
 *
 ****************************************************************************/

int f1c_gpio_register_output(uint32_t port, uint8_t pin,
                             enum f1c_gpio_drv_e drv,
                             const char *name);

/****************************************************************************
 * Name: f1c_gpio_register_input
 *
 * Description:
 *   Configure a pin as a GPIO input (optional pull) and register it as
 *   /dev/<name>. Consumers open() and ioctl(GPIOC_READ). Interrupt pin
 *   types (GPIOC_SETPINTYPE + go_attach/go_enable) are implemented via
 *   the suniv PIO EINT banks (PD/PE/PF, function 6).
 *
 *   dev_out may be NULL. When non-NULL it receives the gpio_dev_s so
 *   kernel callers can use go_attach without going through GPIOC_REGISTER
 *   (that ioctl is the userspace signal path).
 ****************************************************************************/

int f1c_gpio_register_input(uint32_t port, uint8_t pin,
                            enum f1c_gpio_pull_e pull,
                            const char *name,
                            FAR struct gpio_dev_s **dev_out);

/* f1c100s_gpio_config/write/read: declared here for a planned packed-
 * pinset API (GPIO_PIN(port, pin) style, matching other ARM boards'
 * <chip>_configgpio() convention) but never implemented anywhere in
 * this tree -- f1c_gpio_config/write/read above are the real ones.
 * Left declared rather than removed since it's pre-existing and out
 * of scope for the /dev/gpio work that touched this header.
 */

void f1c100s_gpio_config(uint32_t pinset, uint32_t mode,
                         uint32_t pull, uint32_t drv);
void f1c100s_gpio_write(uint32_t pinset, bool value);
bool f1c100s_gpio_read(uint32_t pinset);

#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_GPIO_H */
