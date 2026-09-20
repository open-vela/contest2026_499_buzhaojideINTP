/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_softreset.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_F1C100S_SOFTRESET_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_F1C100S_SOFTRESET_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: f1c100s_softreset
 *
 * Description:
 *   Perform a soft reset on a peripheral.
 *
 * Input Parameters:
 *   reset_reg - Reset register offset (e.g., CCU_BUS_SOFT_RST0)
 *   reset_bit - Bit position in the register
 *
 ****************************************************************************/

void f1c100s_softreset(uint32_t reset_reg, uint8_t reset_bit);

/****************************************************************************
 * Name: f1c100s_reset_uart0
 *
 * Description:
 *   Reset UART0 peripheral.
 *
 ****************************************************************************/

void f1c100s_reset_uart0(void);

/****************************************************************************
 * Name: f1c100s_reset_spi0
 *
 * Description:
 *   Reset SPI0 peripheral.
 *
 ****************************************************************************/

void f1c100s_reset_spi0(void);

/****************************************************************************
 * Name: f1c100s_reset_spi1
 *
 * Description:
 *   Reset SPI1 peripheral.
 *
 ****************************************************************************/

void f1c100s_reset_spi1(void);


/****************************************************************************
 * Name: f1c100s_reset_i2c0
 *
 * Description:
 *   Reset I2C0 peripheral.
 *
 ****************************************************************************/

void f1c100s_reset_i2c0(void);

/****************************************************************************
 * Name: f1c100s_reset_i2c1
 *
 * Description:
 *   Reset I2C1 peripheral.
 *
 ****************************************************************************/

void f1c100s_reset_i2c1(void);

/****************************************************************************
 * Name: f1c100s_reset_i2c2
 *
 * Description:
 *   Reset I2C2 peripheral.
 *
 ****************************************************************************/

void f1c100s_reset_i2c2(void);


/****************************************************************************
 * Name: f1c100s_reset_gpio
 *
 * Description:
 *   Reset GPIO peripheral.
 *
 ****************************************************************************/

void f1c100s_reset_gpio(void);

/****************************************************************************
 * Name: f1c100s_reset_audio
 *
 * Description:
 *   Reset Audio Codec peripheral.
 *
 ****************************************************************************/

void f1c100s_reset_audio(void);
void f1c100s_reset_sdc0(void);

#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_F1C100S_SOFTRESET_H */
