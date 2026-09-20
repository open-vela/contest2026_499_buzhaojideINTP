/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_sdio.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_SDIO_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_SDIO_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/sdio.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SD/MMC Controller Base Address */

#define F1C100S_SDC0_BASE     0x01c0f000
#define F1C100S_SDC1_BASE     0x01c10000
#define F1C100S_SDC2_BASE     0x01c11000

/* SDC Register Offsets */

#define SDC_GCTRL_OFFSET      0x00  /* Global Control Register */
#define SDC_CLKCR_OFFSET      0x04  /* Clock Control Register */
#define SDC_TMOUT_OFFSET      0x08  /* Timeout Register */
#define SDC_WIDTH_OFFSET      0x0C  /* Bus Width Register */
#define SDC_BLKSZ_OFFSET      0x10  /* Block Size Register */
#define SDC_BCNTR_OFFSET      0x14  /* Byte Count Register */
#define SDC_CMD_OFFSET        0x18  /* Command Register */
#define SDC_CARG_OFFSET       0x1C  /* Command Argument Register */
#define SDC_RESP0_OFFSET      0x20  /* Response Register 0 */
#define SDC_RESP1_OFFSET      0x24  /* Response Register 1 */
#define SDC_RESP2_OFFSET      0x28  /* Response Register 2 */
#define SDC_RESP3_OFFSET      0x2C  /* Response Register 3 */
#define SDC_IMASK_OFFSET      0x30  /* Interrupt Mask Register */
#define SDC_MINT_OFFSET       0x34  /* Masked Interrupt Status */
#define SDC_RINT_OFFSET       0x38  /* Raw Interrupt Status */
#define SDC_STATUS_OFFSET     0x3C  /* Status Register */
#define SDC_FTRGL_OFFSET      0x40  /* FIFO Threshold Watermark */
#define SDC_FUNS_OFFSET       0x44  /* Function Select Register */
#define SDC_DBGC_OFFSET       0x50  /* Debug Control Register */
#define SDC_A12A_OFFSET       0x58  /* Auto Command 12 Argument */
#define SDC_NTSR_OFFSET       0x5C  /* New Timing Set Register */
#define SDC_HWRST_OFFSET      0x78  /* Hardware Reset Register */
#define SDC_DMAC_OFFSET       0x80  /* Bus Mode Control Register */
#define SDC_DLBA_OFFSET       0x84  /* Descriptor List Base Address */
#define SDC_IDST_OFFSET       0x88  /* IDMAC Status Register */
#define SDC_IDIE_OFFSET       0x8C  /* IDMAC Interrupt Enable */
#define SDC_FIFO_OFFSET       0x100 /* FIFO Access Address */

/* SDC_GCTRL - Global Control Register */

#define SDC_GCTRL_SOFT_RST    (1 << 0)  /* Software Reset */
#define SDC_GCTRL_FIFO_RST    (1 << 1)  /* FIFO Reset */
#define SDC_GCTRL_DMA_RST     (1 << 2)  /* DMA Reset */
#define SDC_GCTRL_INT_EN      (1 << 4)  /* Global Interrupt Enable */
#define SDC_GCTRL_DMA_EN      (1 << 5)  /* DMA Enable */
#define SDC_GCTRL_DDR_MODE    (1 << 10) /* DDR Mode */
#define SDC_GCTRL_ACCESS_BY_AHB (1U << 31) /* CPU/AHB FIFO access (PIO) */

/* SDC_CLKCR - Clock Control Register */

#define SDC_CLKCR_CCLK_ENB    (1 << 16) /* Card Clock Enable */
#define SDC_CLKCR_CCLK_CTRL   (1 << 17) /* Card Clock Control */

/* SDC_CMD - Command Register */

#define SDC_CMD_START         (1 << 31) /* Start Command */
#define SDC_CMD_UPCLK_ONLY    (1 << 21) /* Update Clock Only */
#define SDC_CMD_SEND_INIT     (1 << 15) /* Send Initialization */
#define SDC_CMD_STOP_ABT      (1 << 14) /* Stop/Abort Command */
#define SDC_CMD_WAIT_PRE      (1 << 13) /* Wait for Previous Data Transfer */
#define SDC_CMD_AUTO_STOP     (1 << 12) /* Send Stop Command Automatically */
#define SDC_CMD_SEQMOD        (1 << 11) /* Sequential Mode */
#define SDC_CMD_WRITE         (1 << 10) /* Write to Card */
#define SDC_CMD_DATA_TRANS    (1 << 9)  /* Data Transfer Command */
#define SDC_CMD_CHK_RESP_CRC  (1 << 8)  /* Check Response CRC */
#define SDC_CMD_LONG_RESP     (1 << 7)  /* Long Response */
#define SDC_CMD_RESP_RCV      (1 << 6)  /* Response Receive */

/* Response Types */

#define SDC_RESP_NONE         0
#define SDC_RESP_R1           (SDC_CMD_RESP_RCV)
#define SDC_RESP_R1B          (SDC_CMD_RESP_RCV | SDC_CMD_CHK_RESP_CRC)
#define SDC_RESP_R2           (SDC_CMD_RESP_RCV | SDC_CMD_LONG_RESP | SDC_CMD_CHK_RESP_CRC)
#define SDC_RESP_R3           (SDC_CMD_RESP_RCV)
#define SDC_RESP_R6           (SDC_CMD_RESP_RCV | SDC_CMD_CHK_RESP_CRC)
#define SDC_RESP_R7           (SDC_CMD_RESP_RCV | SDC_CMD_CHK_RESP_CRC)

/* SDC_RINT/IMASK - Interrupt Status/Mask (matches Linux sunxi-mmc.c) */

#define SDC_INT_RESP_ERR      (1 << 1)  /* Response Error */
#define SDC_INT_CMD_DONE      (1 << 2)  /* Command Done */
#define SDC_INT_DATA_OVER     (1 << 3)  /* Data Transfer Over */
#define SDC_INT_TX_DATA_REQ   (1 << 4)  /* TX Data Request */
#define SDC_INT_RX_DATA_REQ   (1 << 5)  /* RX Data Request */
#define SDC_INT_RESP_CRC_ERR  (1 << 6)  /* Response CRC Error */
#define SDC_INT_DATA_CRC_ERR  (1 << 7)  /* Data CRC Error */
#define SDC_INT_RESP_TIMEOUT  (1 << 8)  /* Response Timeout */
#define SDC_INT_DATA_TIMEOUT  (1 << 9)  /* Data Timeout */
#define SDC_INT_VOLT_CHG_DONE (1 << 10) /* Voltage Change Done */
#define SDC_INT_FIFO_RUN_ERR  (1 << 11) /* FIFO Underrun/Overrun Error */
#define SDC_INT_HW_LOCKED     (1 << 12) /* Hardware Locked */
#define SDC_INT_START_BIT_ERR (1 << 13) /* Start Bit Error */
#define SDC_INT_AUTO_CMD_DONE (1 << 14) /* Auto Command Done */
#define SDC_INT_END_BIT_ERR   (1 << 15) /* End Bit Error */
#define SDC_INT_SDIO          (1 << 16) /* SDIO Interrupt */
#define SDC_INT_CARD_INSERT   (1 << 30) /* Card Inserted */
#define SDC_INT_CARD_REMOVE   (1 << 31) /* Card Removed */

/* Common status register bits */

#define SDC_STATUS_FIFO_EMPTY     (1 << 2)
#define SDC_STATUS_FIFO_FULL      (1 << 3)
#define SDC_STATUS_CARD_PRESENT   (1 << 8)
#define SDC_STATUS_CARD_DATA_BUSY (1 << 9)  /* Card Data Busy (DAT0 low) */
#define SDC_STATUS_DATA_FSM_BUSY  (1 << 10) /* Data FSM Busy */
#define SDC_STATUS_FIFO_LEVEL(s)  (((s) >> 17) & 0x1f)

#define SDC_INT_ERROR_MASK    (SDC_INT_RESP_ERR | SDC_INT_RESP_CRC_ERR | \
                               SDC_INT_DATA_CRC_ERR | SDC_INT_RESP_TIMEOUT | \
                               SDC_INT_DATA_TIMEOUT | SDC_INT_FIFO_RUN_ERR | \
                               SDC_INT_HW_LOCKED | SDC_INT_START_BIT_ERR | \
                               SDC_INT_END_BIT_ERR)

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: f1c100s_sdio_initialize
 *
 * Description:
 *   Initialize the SDIO slot
 *
 * Input Parameters:
 *   slotno - Slot number (0, 1, or 2)
 *
 * Returned Value:
 *   Valid SDIO device structure reference on success; NULL on failure
 *
 ****************************************************************************/

FAR struct sdio_dev_s *f1c100s_sdio_initialize(int slotno);

/****************************************************************************
 * Name: f1c100s_sdio_uninitialize
 *
 * Description:
 *   Uninitialize the SDIO driver
 *
 * Input Parameters:
 *   dev - SDIO device structure reference
 *
 * Returned Value:
 *   OK on success; negative errno on failure
 *
 ****************************************************************************/

int f1c100s_sdio_uninitialize(FAR struct sdio_dev_s *dev);

#ifdef CONFIG_F1C100S_SDIO_CARDDETECT
/****************************************************************************
 * Name: f1c100s_sdio_carddetect_init
 *
 * Description:
 *   Initialize card detect GPIO. Call this after GPIO device is ready.
 *
 * Input Parameters:
 *   dev - SDIO device structure reference
 *
 * Returned Value:
 *   OK on success; negative errno on failure
 *
 ****************************************************************************/

int f1c100s_sdio_carddetect_init(FAR struct sdio_dev_s *dev);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_SDIO_H */
