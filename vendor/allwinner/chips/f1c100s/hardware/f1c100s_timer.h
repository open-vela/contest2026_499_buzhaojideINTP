/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_timer.h
 *
 * F1C100s Timer Hardware Definitions
 ****************************************************************************/

#ifndef __F1C100S_TIMER_H
#define __F1C100S_TIMER_H

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Timer Base Address */

#define F1C100S_TMR_BASE         0x01c20c00

/* Register Offsets */

#define TMR_IRQ_EN_OFFSET        0x00  /* Timer IRQ Enable */
#define TMR_IRQ_STA_OFFSET       0x04  /* Timer IRQ Status */
#define TMR0_CTRL_OFFSET         0x10  /* Timer 0 Control */
#define TMR0_INTV_OFFSET         0x14  /* Timer 0 Interval Value */
#define TMR0_CUR_OFFSET          0x18  /* Timer 0 Current Value */
#define TMR1_CTRL_OFFSET         0x20  /* Timer 1 Control */
#define TMR1_INTV_OFFSET         0x24  /* Timer 1 Interval Value */
#define TMR1_CUR_OFFSET          0x28  /* Timer 1 Current Value */

/* Register Addresses */

#define F1C100S_TMR_IRQ_EN       (F1C100S_TMR_BASE + TMR_IRQ_EN_OFFSET)
#define F1C100S_TMR_IRQ_STA      (F1C100S_TMR_BASE + TMR_IRQ_STA_OFFSET)
#define F1C100S_TMR0_CTRL        (F1C100S_TMR_BASE + TMR0_CTRL_OFFSET)
#define F1C100S_TMR0_INTV        (F1C100S_TMR_BASE + TMR0_INTV_OFFSET)
#define F1C100S_TMR0_CUR         (F1C100S_TMR_BASE + TMR0_CUR_OFFSET)
#define F1C100S_TMR1_CTRL        (F1C100S_TMR_BASE + TMR1_CTRL_OFFSET)
#define F1C100S_TMR1_INTV        (F1C100S_TMR_BASE + TMR1_INTV_OFFSET)
#define F1C100S_TMR1_CUR         (F1C100S_TMR_BASE + TMR1_CUR_OFFSET)

/* Timer IRQ Enable/Status Register Bits */

#define TMR_IRQ_TMR0             (1 << 0)  /* Timer 0 IRQ */
#define TMR_IRQ_TMR1             (1 << 1)  /* Timer 1 IRQ */

/* Timer Control Register Bits */

#define TMR_CTRL_EN              (1 << 0)  /* Timer Enable */
#define TMR_CTRL_RELOAD          (1 << 1)  /* Reload Interval Value */
#define TMR_CTRL_SRC_32K         (0 << 2)  /* Clock Source: 32KHz */
#define TMR_CTRL_SRC_24M         (1 << 2)  /* Clock Source: 24MHz OSC */
#define TMR_CTRL_PRESCALE_1      (0 << 4)  /* Prescaler: /1 */
#define TMR_CTRL_PRESCALE_2      (1 << 4)  /* Prescaler: /2 */
#define TMR_CTRL_PRESCALE_4      (2 << 4)  /* Prescaler: /4 */
#define TMR_CTRL_PRESCALE_8      (3 << 4)  /* Prescaler: /8 */
#define TMR_CTRL_PRESCALE_16     (4 << 4)  /* Prescaler: /16 */
#define TMR_CTRL_PRESCALE_32     (5 << 4)  /* Prescaler: /32 */
#define TMR_CTRL_PRESCALE_64     (6 << 4)  /* Prescaler: /64 */
#define TMR_CTRL_PRESCALE_128    (7 << 4)  /* Prescaler: /128 */
#define TMR_CTRL_MODE_CONT       (0 << 7)  /* Mode: Continuous */
#define TMR_CTRL_MODE_SINGLE     (1 << 7)  /* Mode: Single */

/* Timer IRQ Numbers */

#define F1C100S_IRQ_TMR0         13
#define F1C100S_IRQ_TMR1         14

/* Timer Frequency */

#define TIMER_FREQ               24000000

#endif /* __F1C100S_TIMER_H */
