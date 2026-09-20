/****************************************************************************
 * vendor/allwinner/chips/f1c100s/allwinner_f1c_irq.c
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
#include <nuttx/irq.h>
#include <nuttx/arch.h>

#include "chip.h"
#include "arm_internal.h"
#include "arm.h"  /* For PSR_* macros */

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define F1C_INTC_BASE        0x01c20400

#define INTC_VECTOR          (F1C_INTC_BASE + 0x00)
#define INTC_BASE_ADDR       (F1C_INTC_BASE + 0x04)
#define INTC_NMI_CTRL        (F1C_INTC_BASE + 0x0c)
#define INTC_PEND0           (F1C_INTC_BASE + 0x10)
#define INTC_PEND1           (F1C_INTC_BASE + 0x14)
#define INTC_EN0             (F1C_INTC_BASE + 0x20)
#define INTC_EN1             (F1C_INTC_BASE + 0x24)
#define INTC_MASK0           (F1C_INTC_BASE + 0x30)
#define INTC_MASK1           (F1C_INTC_BASE + 0x34)
#define INTC_RESP0           (F1C_INTC_BASE + 0x40)
#define INTC_RESP1           (F1C_INTC_BASE + 0x44)
#define INTC_FF0             (F1C_INTC_BASE + 0x50)
#define INTC_FF1             (F1C_INTC_BASE + 0x54)
#define INTC_PRIO0           (F1C_INTC_BASE + 0x60)
#define INTC_PRIO1           (F1C_INTC_BASE + 0x64)
#define INTC_PRIO2           (F1C_INTC_BASE + 0x68)
#define INTC_PRIO3           (F1C_INTC_BASE + 0x6c)

/* F1C100s has 64 interrupt sources (0-63) */
#define NR_IRQS              64

#ifdef CONFIG_16550_UART0_IRQ
#  define F1C100S_IRQ_UART0  CONFIG_16550_UART0_IRQ
#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_irqinitialize
 ****************************************************************************/

void up_irqinitialize(void)
{
  /* Disable all interrupts */

  putreg32(0, INTC_EN0);
  putreg32(0, INTC_EN1);

  /* Clear all pending interrupts */

  putreg32(0xffffffff, INTC_PEND0);
  putreg32(0xffffffff, INTC_PEND1);

  /* Unmask all interrupts (mask = 0 means unmasked) */

  putreg32(0, INTC_MASK0);
  putreg32(0, INTC_MASK1);

#ifndef CONFIG_SUPPRESS_INTERRUPTS
  /* Enable IRQ (but not FIQ) */

  up_irq_restore(PSR_MODE_SYS | PSR_F_BIT);
#endif
}

/****************************************************************************
 * Name: up_enable_irq
 *
 * Description:
 *   Enable the interrupt specified by 'irq'
 *
 ****************************************************************************/

void up_enable_irq(int irq)
{
  if (irq < 32)
    {
      uint32_t en = getreg32(INTC_EN0);
      en |= (1 << irq);
      putreg32(en, INTC_EN0);
    }
  else if (irq < 64)
    {
      uint32_t en = getreg32(INTC_EN1);
      en |= (1 << (irq - 32));
      putreg32(en, INTC_EN1);
    }
}

/****************************************************************************
 * Name: up_disable_irq
 *
 * Description:
 *   Disable the interrupt specified by 'irq'
 *
 ****************************************************************************/

void up_disable_irq(int irq)
{
  if (irq < 32)
    {
      uint32_t en = getreg32(INTC_EN0);
      en &= ~(1 << irq);
      putreg32(en, INTC_EN0);
    }
  else if (irq < 64)
    {
      uint32_t en = getreg32(INTC_EN1);
      en &= ~(1 << (irq - 32));
      putreg32(en, INTC_EN1);
    }
}

/****************************************************************************
 * Name: arm_ack_irq
 *
 * Description:
 *   Acknowledge the IRQ
 *
 ****************************************************************************/

void arm_ack_irq(int irq)
{
  /* Clear pending bit by writing 1 */

  if (irq < 32)
    {
      putreg32(1 << irq, INTC_PEND0);
    }
  else if (irq < 64)
    {
      putreg32(1 << (irq - 32), INTC_PEND1);
    }
}

/****************************************************************************
 * Name: up_get_pending_irq
 *
 * Description:
 *   Get the IRQ number of the current pending interrupt
 *
 ****************************************************************************/

int up_get_pending_irq(void)
{
  /* Use VECTOR register to get the pending IRQ number.
   * VECTOR = IRQ_number * 4, so IRQ = VECTOR / 4
   */

  uint32_t vector = getreg32(INTC_VECTOR);

  if (vector != 0)
    {
      return vector / 4;
    }

  return -1;  /* No pending IRQ */
}

/****************************************************************************
 * Name: arm_decodeirq
 *
 * Description:
 *   Called from the IRQ vector handler to decode and dispatch the IRQ.
 *
 ****************************************************************************/

uint32_t *arm_decodeirq(uint32_t *regs)
{
  int irq = up_get_pending_irq();

  if (irq >= 0)
    {
#ifdef F1C100S_IRQ_UART0
      /* DW-APB UART erratum: while USR.BUSY (bit 0) is set, IIR
       * reports 0x07 ("no interrupt pending") even though the INTC
       * line is asserted. The generic 16550 ISR takes that at face
       * value and never reads USR, so the INTC line never clears -
       * an IRQ storm that starves the timer tick and stalls boot
       * before nsh comes up. Reading USR here clears BUSY so the
       * 16550 ISR's next IIR read reflects the real interrupt cause.
       */

      if (irq == F1C100S_IRQ_UART0)
        {
          (void)getreg32(F1C_UART0_BASE + 0x7c);
        }
#endif

      /* Clear the pending bit in INTC before dispatching. */

      arm_ack_irq(irq);

      regs = arm_doirq(irq, regs);
    }

  return regs;
}
