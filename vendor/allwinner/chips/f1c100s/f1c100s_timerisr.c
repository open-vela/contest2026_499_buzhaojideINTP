/****************************************************************************
 * vendor/allwinner/chips/f1c100s/allwinner_f1c_oneshot.c
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
#include <time.h>
#include <debug.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <arch/board/board.h>
#include <arch/irq.h>

#include "chip.h"
#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define F1C_TMR_BASE         0x01c20c00

#define TMR_IRQ_EN           (F1C_TMR_BASE + 0x00)
#define TMR_IRQ_STA          (F1C_TMR_BASE + 0x04)
#define TMR0_CTRL            (F1C_TMR_BASE + 0x10)
#define TMR0_INTV            (F1C_TMR_BASE + 0x14)
#define TMR0_CUR             (F1C_TMR_BASE + 0x18)

/* Timer Control Register bits */
#define TMR_CTRL_EN          (1 << 0)
#define TMR_CTRL_RELOAD      (1 << 1)
#define TMR_CTRL_SRC_32K     (0 << 2)   /* 32kHz OSC - clk0 */
#define TMR_CTRL_SRC_24M     (1 << 2)   /* 24MHz OSC - clk1 */
#define TMR_CTRL_PRESCALE_1  (0 << 4)
#define TMR_CTRL_MODE_CONT   (0 << 7)   /* Continuous mode */

/* Timer IRQ */
#define F1C_IRQ_TMR0         13          /* Timer 0 interrupt */

/* Timer frequency (24MHz / prescaler) */
#define TIMER_FREQ           24000000

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c_timer_interrupt
 *
 * Description:
 *   The timer ISR will perform a variety of services for various portions
 *   of the system.
 *
 ****************************************************************************/

static int f1c_timer_interrupt(int irq, void *context, void *arg)
{
  /* Clear timer interrupt */

  putreg32(1 << 0, TMR_IRQ_STA);

  /* Process timer interrupt */

  nxsched_process_timer();

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_timer_initialize
 *
 * Description:
 *   This function is called during start-up to initialize
 *   the timer interrupt.
 *
 ****************************************************************************/

void up_timer_initialize(void)
{
  uint32_t reload_value;

  /* Calculate reload value for 10ms tick (100Hz) at 24MHz */

  reload_value = 240000;  /* 24MHz / 100Hz = 240000 */

  /* Disable timer first */

  putreg32(0, TMR0_CTRL);

  /* Set interval value */

  putreg32(reload_value, TMR0_INTV);

  /* Clear any pending interrupt */

  putreg32(1 << 0, TMR_IRQ_STA);

  /* Enable timer interrupt */

  putreg32(1 << 0, TMR_IRQ_EN);

  /* Attach and enable the timer interrupt */

  irq_attach(F1C_IRQ_TMR0, f1c_timer_interrupt, NULL);
  up_enable_irq(F1C_IRQ_TMR0);

  /* Start the timer: enable, reload, 24MHz source, continuous mode */

  putreg32(TMR_CTRL_EN | TMR_CTRL_RELOAD | TMR_CTRL_SRC_24M |
           TMR_CTRL_PRESCALE_1 | TMR_CTRL_MODE_CONT, TMR0_CTRL);
}
