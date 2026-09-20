/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_uart.c
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
#include <nuttx/serial/uart_16550.h>

#include "chip.h"
#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if defined(CONFIG_UART1_SERIAL_CONSOLE)
#  define CONSOLE_BASE  F1C_UART1_BASE
#else
#  define CONSOLE_BASE  F1C_UART0_BASE
#endif

#define UART_THR  (CONSOLE_BASE + 0x00)
#define UART_DLL  (CONSOLE_BASE + 0x00)
#define UART_DLH  (CONSOLE_BASE + 0x04)
#define UART_IER  (CONSOLE_BASE + 0x04)
#define UART_FCR  (CONSOLE_BASE + 0x08)
#define UART_LCR  (CONSOLE_BASE + 0x0c)
#define UART_MCR  (CONSOLE_BASE + 0x10)
#define UART_LSR  (CONSOLE_BASE + 0x14)
#define UART_LSR_THRE  (1 << 5)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void arm_lowputc(char ch)
{
  uint32_t timeout = 1000000;
  while ((getreg32(UART_LSR) & UART_LSR_THRE) == 0 && --timeout > 0);
  if (timeout > 0)
    {
      putreg32(ch, UART_THR);
    }
}

void f1c_early_puts(const char *str)
{
  while (*str)
    {
      if (*str == '\n')
        {
          arm_lowputc('\r');
        }
      arm_lowputc(*str++);
    }
}

void f1c_early_puthex(uint32_t val)
{
  const char hex[] = "0123456789abcdef";
  for (int i = 28; i >= 0; i -= 4)
    {
      arm_lowputc(hex[(val >> i) & 0xf]);
    }
}

void arm_earlyserialinit(void)
{
#ifndef CONFIG_SUPPRESS_UART_CONFIG
  /* Configure GPIO for console UART */

#if defined(CONFIG_UART1_SERIAL_CONSOLE)
  /* UART1: PA2=TX, PA3=RX, Function 5 */

  uint32_t pa_cfg0 = F1C_PIO_BASE + 0x00;
  uint32_t val = getreg32(pa_cfg0);
  val &= ~((0x7 << 8) | (0x7 << 12));
  val |= ((5 << 8) | (5 << 12));
  putreg32(val, pa_cfg0);
#else
  /* UART0: PE0=RX, PE1=TX, Function 5 */

  uint32_t pe_cfg0 = F1C_PIO_BASE + 0x90;
  uint32_t val = getreg32(pe_cfg0);
  val &= ~((0x7 << 0) | (0x7 << 4));
  val |= ((5 << 0) | (5 << 4));
  putreg32(val, pe_cfg0);
#endif

  /* Initialize UART for early console */

  putreg32(0x00, UART_IER);           /* Disable interrupts */
  putreg32(0x07, UART_FCR);           /* Enable and reset FIFOs */
  putreg32(0x00, UART_MCR);           /* Clear modem control */

  /* DW 8250: do not touch LCR while USR.BUSY (bit 0). */
  while (getreg32(CONSOLE_BASE + 0x7c) & 1)
    {
    }

  /* Set baud rate divisor using 16550 config */

#if defined(CONFIG_16550_UART0_CLOCK) && defined(CONFIG_16550_UART0_BAUD)
  uint32_t divisor = CONFIG_16550_UART0_CLOCK / 16 / CONFIG_16550_UART0_BAUD;
#else
  uint32_t divisor = 100000000 / 16 / 115200;  /* Default: 100MHz APB, 115200 */
#endif
  uint32_t lcr = getreg32(UART_LCR);
  putreg32(lcr | 0x80, UART_LCR);     /* DLAB = 1 */
  putreg32(divisor & 0xff, UART_DLL);
  putreg32((divisor >> 8) & 0xff, UART_DLH);
  putreg32(0x03, UART_LCR);           /* 8N1, DLAB = 0 */
#endif /* !CONFIG_SUPPRESS_UART_CONFIG */

#ifdef CONFIG_16550_UART
  /* Call 16550 early serial init to set isconsole flag */
  u16550_earlyserialinit();
#endif
}

void arm_serialinit(void)
{
#ifdef CONFIG_16550_UART
  u16550_serialinit();
#endif
}
