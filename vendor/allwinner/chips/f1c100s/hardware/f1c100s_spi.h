/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_spi.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_SPI_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_SPI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/spi/spi.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SPI Controller Base Addresses */

#define F1C100S_SPI0_BASE     0x01c05000
#define F1C100S_SPI1_BASE     0x01c06000

/* SPI Register Offsets */

#define SPI_GCR_OFFSET        0x04  /* Global Control Register */
#define SPI_TCR_OFFSET        0x08  /* Transfer Control Register */
#define SPI_IER_OFFSET        0x10  /* Interrupt Control Register */
#define SPI_ISR_OFFSET        0x14  /* Interrupt Status Register */
#define SPI_FCR_OFFSET        0x18  /* FIFO Control Register */
#define SPI_FSR_OFFSET        0x1c  /* FIFO Status Register */
#define SPI_WCR_OFFSET        0x20  /* Wait Clock Counter Register */
#define SPI_CCR_OFFSET        0x24  /* Clock Rate Control Register */
#define SPI_MBC_OFFSET        0x30  /* Burst Counter Register */
#define SPI_MTC_OFFSET        0x34  /* Transmit Counter Register */
#define SPI_BCC_OFFSET        0x38  /* Burst Control Register */
#define SPI_TXD_OFFSET        0x200 /* TX Data Register */
#define SPI_RXD_OFFSET        0x300 /* RX Data Register */

/* SPI_GCR - Global Control Register bits */

#define SPI_GCR_EN            (1 << 0)   /* SPI Module Enable */
#define SPI_GCR_MODE          (1 << 1)   /* SPI Function Mode (1=Master) */
#define SPI_GCR_TP_EN         (1 << 7)   /* Transmit Pause Enable */
#define SPI_GCR_SRST          (1 << 31)  /* Soft Reset */

/* SPI_TCR - Transfer Control Register bits */

#define SPI_TCR_CPHA          (1 << 0)   /* SPI Clock/Data Phase Control */
#define SPI_TCR_CPOL          (1 << 1)   /* SPI Clock Polarity Control */
#define SPI_TCR_SPOL          (1 << 2)   /* SPI Chip Select Polarity */
#define SPI_TCR_SSCTL         (1 << 3)   /* SPI Chip Select Control */
#define SPI_TCR_SS_MASK       (3 << 4)   /* SPI Chip Select */
#define SPI_TCR_CS_MANUAL     (1 << 6)   /* SPI Manual CS Control (matches Linux SUN6I_TFR_CTL_CS_MANUAL) */
#define SPI_TCR_SS_OWNER      (1 << 6)   /* SS Owner: 1=Software manual, 0=Hardware */
#define SPI_TCR_SS_LEVEL      (1 << 7)   /* SPI Chip Select Level (1=High, 0=Low) */
#define SPI_TCR_DHB           (1 << 8)   /* Discard Hash Burst */
#define SPI_TCR_DDB           (1 << 9)   /* Dummy Burst Type */
#define SPI_TCR_RPSM          (1 << 10)  /* Rapids Mode Select */
#define SPI_TCR_SDC           (1 << 11)  /* Master Sample Data Control */
#define SPI_TCR_FBS           (1 << 12)  /* First Transmit Bit Select */
#define SPI_TCR_XCH           (1 << 31)  /* Exchange Burst */

/* SPI_CCR - Clock Rate Control Register bits (matches Linux spi-sun6i.c) */

#define SPI_CCR_CDR2_MASK     0xff
#define SPI_CCR_CDR2(n)       (((n) & SPI_CCR_CDR2_MASK) << 0)
#define SPI_CCR_CDR1_MASK     0xf
#define SPI_CCR_CDR1(n)       (((n) & SPI_CCR_CDR1_MASK) << 8)
#define SPI_CCR_DRS           (1 << 12)  /* Divide Rate Select: 1=CDR2, 0=CDR1 */

/* SPI_FCR - FIFO Control Register bits */

#define SPI_FCR_RF_RST        (1 << 15)  /* RX FIFO Reset */
#define SPI_FCR_TF_RST        (1 << 31)  /* TX FIFO Reset */

/* SPI_FSR - FIFO Status Register bits */

#define SPI_FSR_RX_CNT_MASK   (0xFF << 0)  /* RX FIFO Counter */
#define SPI_FSR_RB_CNT_MASK   (0x7 << 12)  /* RX FIFO Buffer Counter */
#define SPI_FSR_RB_WR         (1 << 15)    /* RX FIFO Buffer Write Enable */
#define SPI_FSR_RF_FULL       (1 << 16)    /* RX FIFO Full */
#define SPI_FSR_RF_EMPTY      (1 << 17)    /* RX FIFO Empty */
#define SPI_FSR_TX_CNT_MASK   (0xFF << 16) /* TX FIFO Counter */
#define SPI_FSR_TB_CNT_MASK   (0x7 << 28)  /* TX FIFO Buffer Counter */
#define SPI_FSR_TB_WR         (1 << 31)    /* TX FIFO Buffer Write Enable */

/* SPI Configuration */

#define SPI_FIFO_SIZE         64    /* FIFO depth in bytes */
#define SPI_MAX_FREQUENCY     24000000  /* 24 MHz max */

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
 * Name: f1c100s_spibus_initialize
 *
 * Description:
 *   Initialize the selected SPI bus
 *
 * Input Parameters:
 *   bus - SPI bus number (0 or 1)
 *
 * Returned Value:
 *   Valid SPI device structure reference on success; NULL on failure
 *
 ****************************************************************************/

struct spi_dev_s *f1c100s_spibus_initialize(int bus);

#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_SPI_H */
