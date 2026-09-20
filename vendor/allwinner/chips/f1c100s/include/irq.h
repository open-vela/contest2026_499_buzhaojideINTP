/****************************************************************************
 * vendor/allwinner/chips/f1c100s/include/irq.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_INCLUDE_IRQ_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_INCLUDE_IRQ_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* F1C100s Interrupt Sources */

#define F1C_IRQ_NMI          0
#define F1C_IRQ_UART0        1
#define F1C_IRQ_UART1        2
#define F1C_IRQ_UART2        3
#define F1C_IRQ_OWA          5
#define F1C_IRQ_CIR          6
#define F1C_IRQ_TWI0         7
#define F1C_IRQ_TWI1         8
#define F1C_IRQ_TWI2         9
#define F1C_IRQ_SPI0         10
#define F1C_IRQ_SPI1         11
#define F1C_IRQ_TIMER0       13
#define F1C_IRQ_TIMER1       14
#define F1C_IRQ_TIMER2       15
#define F1C_IRQ_WDOG         16
#define F1C_IRQ_RSB          17
#define F1C_IRQ_DMA          18
#define F1C_IRQ_TP           20
#define F1C_IRQ_AUDIO        21
#define F1C_IRQ_LRADC        22
#define F1C_IRQ_MMC0         23
#define F1C_IRQ_MMC1         24
#define F1C_IRQ_USB_OTG      26
#define F1C_IRQ_TVD          27
#define F1C_IRQ_TVE          28
#define F1C_IRQ_LCD          29
#define F1C_IRQ_DE_FE        30
#define F1C_IRQ_DE_BE        31
#define F1C_IRQ_CSI          32
#define F1C_IRQ_DE_INTERLACE 33
#define F1C_IRQ_VE           34
#define F1C_IRQ_DAUDIO       35
#define F1C_IRQ_PIOD         38
#define F1C_IRQ_PIOE         39
#define F1C_IRQ_PIOF         40

/* Total number of IRQs */
#define NR_IRQS              64

#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_INCLUDE_IRQ_H */
