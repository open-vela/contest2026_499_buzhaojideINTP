/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_idmac.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_IDMAC_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_IDMAC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* IDMAC Register Offsets (relative to SD/MMC controller base) */

#define F1C100S_IDMAC_DMAC_OFFSET     0x80  /* Bus Mode Control Register */
#define F1C100S_IDMAC_DLBA_OFFSET     0x84  /* Descriptor List Base Address */
#define F1C100S_IDMAC_IDST_OFFSET     0x88  /* IDMAC Status Register */
#define F1C100S_IDMAC_IDIE_OFFSET     0x8C  /* IDMAC Interrupt Enable */

/* DMAC - Bus Mode Control Register */

#define IDMAC_DMAC_SWR                (1 << 0)  /* Software Reset */
#define IDMAC_DMAC_FB                 (1 << 1)  /* Fixed Burst */
#define IDMAC_DMAC_DSL_SHIFT          (2)       /* Descriptor Skip Length */
#define IDMAC_DMAC_DSL_MASK           (0x1f << IDMAC_DMAC_DSL_SHIFT)
#define IDMAC_DMAC_IDMAC_EN           (1 << 7)  /* IDMAC Enable */
#define IDMAC_DMAC_PBL_SHIFT          (8)       /* Programmable Burst Length */
#define IDMAC_DMAC_PBL_MASK           (0xff << IDMAC_DMAC_PBL_SHIFT)
#define IDMAC_DMAC_PBL_1              (1 << 8)
#define IDMAC_DMAC_PBL_4              (4 << 8)
#define IDMAC_DMAC_PBL_8              (8 << 8)

/* IDST - IDMAC Status Register */

#define IDMAC_IDST_TI                 (1 << 0)  /* Transmit Interrupt */
#define IDMAC_IDST_RI                 (1 << 1)  /* Receive Interrupt */
#define IDMAC_IDST_FBE                (1 << 2)  /* Fatal Bus Error */
#define IDMAC_IDST_DU                 (1 << 4)  /* Descriptor Unavailable */
#define IDMAC_IDST_CES                (1 << 5)  /* Card Error Summary */
#define IDMAC_IDST_NIS                (1 << 8)  /* Normal Interrupt Summary */
#define IDMAC_IDST_AIS                (1 << 9)  /* Abnormal Interrupt Summary */
#define IDMAC_IDST_EB_SHIFT           (10)      /* Error Bits */
#define IDMAC_IDST_EB_MASK            (0x7 << IDMAC_IDST_EB_SHIFT)

/* IDIE - IDMAC Interrupt Enable Register */

#define IDMAC_IDIE_TI                 (1 << 0)  /* Transmit Interrupt Enable */
#define IDMAC_IDIE_RI                 (1 << 1)  /* Receive Interrupt Enable */
#define IDMAC_IDIE_FBE                (1 << 2)  /* Fatal Bus Error Enable */
#define IDMAC_IDIE_DU                 (1 << 4)  /* Descriptor Unavailable Enable */
#define IDMAC_IDIE_CES                (1 << 5)  /* Card Error Summary Enable */
#define IDMAC_IDIE_NIS                (1 << 8)  /* Normal Interrupt Summary Enable */
#define IDMAC_IDIE_AIS                (1 << 9)  /* Abnormal Interrupt Summary Enable */

/* DMA Descriptor Control Bits (DES0) */

#define IDMAC_DES0_DIC                (1 << 1)  /* Disable Interrupt on Completion */
#define IDMAC_DES0_LD                 (1 << 2)  /* Last Descriptor */
#define IDMAC_DES0_FD                 (1 << 3)  /* First Descriptor */
#define IDMAC_DES0_CH                 (1 << 4)  /* Chain Mode */
#define IDMAC_DES0_ER                 (1 << 5)  /* End of Ring */
#define IDMAC_DES0_CES                (1 << 30) /* Card Error Summary */
#define IDMAC_DES0_OWN                (1 << 31) /* Descriptor Ownership (1=DMA, 0=CPU) */

/* DMA Descriptor Buffer Size Bits (DES1) */

#define IDMAC_DES1_BS1_SHIFT          (0)       /* Buffer 1 Size */
#define IDMAC_DES1_BS1_MASK           (0x1fff << IDMAC_DES1_BS1_SHIFT)
#define IDMAC_DES1_BS2_SHIFT          (13)      /* Buffer 2 Size */
#define IDMAC_DES1_BS2_MASK           (0x1fff << IDMAC_DES1_BS2_SHIFT)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* IDMAC Descriptor Structure */

struct f1c100s_idmac_desc_s
{
  volatile uint32_t des0;  /* Control and Status */
  volatile uint32_t des1;  /* Buffer Size */
  volatile uint32_t des2;  /* Buffer Address */
  volatile uint32_t des3;  /* Next Descriptor Address */
};

#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_IDMAC_H */
