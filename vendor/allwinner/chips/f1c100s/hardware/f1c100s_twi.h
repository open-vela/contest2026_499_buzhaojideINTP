/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_twi.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_TWI_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_TWI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/i2c/i2c_master.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* I2C Controller Base Addresses */

#define F1C100S_I2C0_BASE     0x01c27000
#define F1C100S_I2C1_BASE     0x01c27400
#define F1C100S_I2C2_BASE     0x01c27800

/* I2C Register Offsets */

#define I2C_ADDR_OFFSET       0x00   /* Slave Address Register */
#define I2C_XADDR_OFFSET      0x04   /* Extended Slave Address Register */
#define I2C_DATA_OFFSET       0x08   /* Data Register */
#define I2C_CNTR_OFFSET       0x0C   /* Control Register */
#define I2C_STAT_OFFSET       0x10   /* Status Register */
#define I2C_CCR_OFFSET        0x14   /* Clock Control Register */
#define I2C_SRST_OFFSET       0x18   /* Software Reset Register */
#define I2C_EFR_OFFSET        0x1C   /* Enhanced Feature Register */
#define I2C_LCR_OFFSET        0x20   /* Line Control Register */

/* I2C_ADDR - Slave Address Register */

#define I2C_ADDR_MASK         0x7F   /* 7-bit address */

/* I2C_CNTR - Control Register */

#define I2C_CNTR_INT_EN       (1 << 7)  /* Interrupt Enable */
#define I2C_CNTR_BUS_EN       (1 << 6)  /* Bus Enable */
#define I2C_CNTR_START        (1 << 5)  /* START Condition */
#define I2C_CNTR_STOP         (1 << 4)  /* STOP Condition */
#define I2C_CNTR_INT_FLAG     (1 << 3)  /* Interrupt Flag */
#define I2C_CNTR_ACK          (1 << 2)  /* Assert ACK */

/* I2C_STAT - Status Register */

#define I2C_STAT_BUS_ERROR    0x00   /* Bus error */
#define I2C_STAT_TX_START     0x08   /* START transmitted */
#define I2C_STAT_TX_RESTART   0x10   /* Repeated START transmitted */
#define I2C_STAT_TX_AW_ACK    0x18   /* Address+W transmitted, ACK received */
#define I2C_STAT_TX_AW_NAK    0x20   /* Address+W transmitted, NACK received */
#define I2C_STAT_TXD_ACK      0x28   /* Data transmitted, ACK received */
#define I2C_STAT_TXD_NAK      0x30   /* Data transmitted, NACK received */
#define I2C_STAT_LOST_ARB     0x38   /* Arbitration lost */
#define I2C_STAT_TX_AR_ACK    0x40   /* Address+R transmitted, ACK received */
#define I2C_STAT_TX_AR_NAK    0x48   /* Address+R transmitted, NACK received */
#define I2C_STAT_RXD_ACK      0x50   /* Data received, ACK returned */
#define I2C_STAT_RXD_NAK      0x58   /* Data received, NACK returned */
#define I2C_STAT_IDLE         0xF8   /* Idle */

/* I2C_CCR - Clock Control Register */

#define I2C_CCR_CLK_M_SHIFT   3
#define I2C_CCR_CLK_M_MASK    (0xF << I2C_CCR_CLK_M_SHIFT)
#define I2C_CCR_CLK_N_SHIFT   0
#define I2C_CCR_CLK_N_MASK    (0x7 << I2C_CCR_CLK_N_SHIFT)

/* I2C_SRST - Software Reset Register */

#define I2C_SRST_RESET        (1 << 0)

/* Additional Control Register Aliases for clarity */

#define I2C_CNTR_M_STA        I2C_CNTR_START  /* Master START (alias) */
#define I2C_CNTR_M_STP        I2C_CNTR_STOP   /* Master STOP (alias) */

/* I2C State Machine States */

enum f1c100s_i2c_state_e
{
  I2C_STATE_IDLE = 0,       /* Idle state */
  I2C_STATE_START,          /* Sending START condition */
  I2C_STATE_ADDR_SEND,      /* Sending address */
  I2C_STATE_DATA_SEND,      /* Sending data */
  I2C_STATE_DATA_RECV,      /* Receiving data */
  I2C_STATE_STOP,           /* Sending STOP condition */
  I2C_STATE_DONE,           /* Transfer complete */
  I2C_STATE_ERROR           /* Error occurred */
};

/* I2C Configuration */

#define I2C_TIMEOUT_MS        1000   /* Transfer timeout in ms */
#define I2C_MAX_RETRIES       3      /* Maximum retry count */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: f1c100s_i2cbus_initialize
 *
 * Description:
 *   Initialize the selected I2C bus
 *
 * Input Parameters:
 *   bus - I2C bus number (0, 1, or 2)
 *
 * Returned Value:
 *   Valid I2C device structure reference on success; NULL on failure
 *
 ****************************************************************************/

FAR struct i2c_master_s *f1c100s_i2cbus_initialize(int bus);

/****************************************************************************
 * Name: f1c100s_i2cbus_uninitialize
 *
 * Description:
 *   Uninitialize an I2C bus
 *
 * Input Parameters:
 *   dev - I2C device structure
 *
 * Returned Value:
 *   OK on success; A negated errno value on failure
 *
 ****************************************************************************/

int f1c100s_i2cbus_uninitialize(FAR struct i2c_master_s *dev);

#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_TWI_H */
