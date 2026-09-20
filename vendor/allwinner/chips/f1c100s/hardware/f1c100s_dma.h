/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_dma.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_DMA_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_DMA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "chip.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* DMA Controller Base Address */
#define F1C100S_DMA_BASE            0x01C02000

/* DMA Register Offsets */
#define F1C100S_DMA_IRQ_EN          0x0000
#define F1C100S_DMA_IRQ_PEND        0x0004
#define F1C100S_DMA_AUTO_GATE       0x0008

/* NDMA (Normal DMA) Channels 0-3 */
#define F1C100S_NDMA_CFG(n)         (0x100 + (n) * 0x20)
#define F1C100S_NDMA_SRC(n)         (0x104 + (n) * 0x20)
#define F1C100S_NDMA_DST(n)         (0x108 + (n) * 0x20)
#define F1C100S_NDMA_BC(n)          (0x10C + (n) * 0x20)

/* DDMA (Dedicated DMA) Channels 0-3 */
#define F1C100S_DDMA_CFG(n)         (0x300 + (n) * 0x20)
#define F1C100S_DDMA_SRC(n)         (0x304 + (n) * 0x20)
#define F1C100S_DDMA_DST(n)         (0x308 + (n) * 0x20)
#define F1C100S_DDMA_BC(n)          (0x30C + (n) * 0x20)
#define F1C100S_DDMA_PARA(n)        (0x318 + (n) * 0x20)

/* Register Bit Definitions */

/* DMA IRQ Enable/Pending - F1C100s uses sun4i DMA controller */
#define DMA_IRQ_NDMA_HALF(n)        (1 << (((n) * 2) + 8))    /* Half done for NDMA n */
#define DMA_IRQ_NDMA_END(n)         (1 << (((n) * 2) + 9))    /* Full done for NDMA n */
#define DMA_IRQ_NDMA(n)             DMA_IRQ_NDMA_END(n)       /* Alias for end interrupt */
#define DMA_IRQ_DDMA_HALF(n)        (1 << (((n) * 2) + 24))   /* Half done for DDMA n */
#define DMA_IRQ_DDMA_END(n)         (1 << (((n) * 2) + 25))   /* Full done for DDMA n */
#define DMA_IRQ_DDMA(n)             DMA_IRQ_DDMA_END(n)       /* Alias for end interrupt */

/* NDMA Configuration (matches Linux sun4i-dma.c) */
#define NDMA_CFG_LOADING            (1 << 31)
#define NDMA_CFG_CONT_MODE          (1 << 30)
#define NDMA_CFG_WAIT_STATE_MASK    (0x7 << 27)
#define NDMA_CFG_WAIT_STATE(n)      (((n) & 0x7) << 27)
#define NDMA_CFG_DST_WIDTH_MASK     (0x3 << 25)
#define NDMA_CFG_DST_WIDTH_8        (0 << 25)
#define NDMA_CFG_DST_WIDTH_16       (1 << 25)
#define NDMA_CFG_DST_WIDTH_32       (2 << 25)
#define NDMA_CFG_DST_BURST_MASK     (0x3 << 23)
#define NDMA_CFG_DST_BURST_1        (0 << 23)
#define NDMA_CFG_DST_BURST_4        (1 << 23)
#define NDMA_CFG_DST_BURST_8        (2 << 23)
#define NDMA_CFG_DST_NON_SECURE     (1 << 22)
#define NDMA_CFG_DST_MODE_IO        (1 << 21) /* 0: Linear, 1: IO */
#define NDMA_CFG_DST_DRQ_TYPE_MASK  (0x1f << 16)
#define NDMA_CFG_DST_DRQ_TYPE(n)    (((n) & 0x1f) << 16)
#define NDMA_CFG_SRC_WIDTH_MASK     (0x3 << 9)
#define NDMA_CFG_SRC_WIDTH_8        (0 << 9)
#define NDMA_CFG_SRC_WIDTH_16       (1 << 9)
#define NDMA_CFG_SRC_WIDTH_32       (2 << 9)
#define NDMA_CFG_SRC_BURST_MASK     (0x3 << 7)
#define NDMA_CFG_SRC_BURST_1        (0 << 7)
#define NDMA_CFG_SRC_BURST_4        (1 << 7)
#define NDMA_CFG_SRC_BURST_8        (2 << 7)
#define NDMA_CFG_SRC_NON_SECURE     (1 << 6)
#define NDMA_CFG_SRC_MODE_IO        (1 << 5)  /* 0: Linear, 1: IO */
#define NDMA_CFG_SRC_DRQ_TYPE_MASK  (0x1f << 0)
#define NDMA_CFG_SRC_DRQ_TYPE(n)    (((n) & 0x1f) << 0)

/* DRQ Types (Source/Dest) */
#define DMA_DRQ_SRAM                0
#define DMA_DRQ_SDRAM               1
#define DMA_DRQ_OTG_EP1             17
#define DMA_DRQ_OTG_EP2             18
#define DMA_DRQ_OTG_EP3             19
#define DMA_DRQ_OTG_EP4             20
#define DMA_DRQ_OTG_EP5             21
#define DMA_DRQ_UART0               22
#define DMA_DRQ_UART1               23
#define DMA_DRQ_UART2               24  /* NOTE: Collides with SPI0 on NDMA */
#define DMA_DRQ_SPI0                24
#define DMA_DRQ_SPI1                25
#define DMA_DRQ_AUDIO_CODEC         26
#define DMA_DRQ_IR                  27

/* DMA Callback Function Prototype */
typedef void (*dma_callback_t)(void *arg, int result);

/* DMA Result Codes */
#define DMA_RESULT_OK       0
#define DMA_RESULT_ERROR   -1
#define DMA_RESULT_TIMEOUT -2

/* DMA Channel Handle */
typedef void *DMA_HANDLE;

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* DMA Transfer Descriptor for software chaining */

struct dma_xfer_desc_s
{
  uint32_t src;                    /* Source address */
  uint32_t dst;                    /* Destination address */
  size_t   nbytes;                 /* Transfer size */
  uint32_t cfg;                    /* Configuration */
  struct dma_xfer_desc_s *next;    /* Next descriptor (NULL = end) */
};

/* Scatter-Gather entry */

struct dma_sg_entry_s
{
  uint32_t addr;                   /* Buffer address */
  size_t   len;                    /* Buffer length */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifndef __ASSEMBLY__

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: f1c100s_dma_init
 *
 * Description:
 *   Initialize the DMA subsystem.
 *
 ****************************************************************************/

void f1c100s_dma_init(void);

/****************************************************************************
 * Name: f1c100s_dma_channel_alloc
 *
 * Description:
 *   Allocate a DMA channel.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   On success, a non-NULL handle to the allocated channel.
 *   NULL on failure (no channels available).
 *
 ****************************************************************************/

DMA_HANDLE f1c100s_dma_channel_alloc(void);

/****************************************************************************
 * Name: f1c100s_dma_channel_free
 *
 * Description:
 *   Free a previously allocated DMA channel.
 *
 * Input Parameters:
 *   handle - The DMA channel handle to free.
 *
 ****************************************************************************/

void f1c100s_dma_channel_free(DMA_HANDLE handle);

/****************************************************************************
 * Name: f1c100s_dma_setup
 *
 * Description:
 *   Configure a DMA transfer.
 *
 * Input Parameters:
 *   handle - DMA channel handle
 *   src    - Source address
 *   dst    - Destination address
 *   nbytes - Number of bytes to transfer
 *   cfg    - Configuration word (NDMA_CFG_*)
 *
 ****************************************************************************/

void f1c100s_dma_setup(DMA_HANDLE handle, uint32_t src, uint32_t dst,
                       size_t nbytes, uint32_t cfg);

/****************************************************************************
 * Name: f1c100s_dma_start
 *
 * Description:
 *   Start a DMA transfer.
 *
 * Input Parameters:
 *   handle   - DMA channel handle
 *   callback - Function to call on completion
 *   arg      - Argument to pass to callback
 *
 ****************************************************************************/

void f1c100s_dma_start(DMA_HANDLE handle, dma_callback_t callback, void *arg);

/****************************************************************************
 * Name: f1c100s_dma_stop
 *
 * Description:
 *   Stop a DMA transfer.
 *
 * Input Parameters:
 *   handle - DMA channel handle
 *
 ****************************************************************************/

void f1c100s_dma_stop(DMA_HANDLE handle);

/****************************************************************************
 * Name: f1c100s_dma_wait
 *
 * Description:
 *   Wait for DMA transfer to complete.
 *
 * Input Parameters:
 *   handle  - DMA channel handle
 *   timeout - Timeout in milliseconds (0 = wait forever)
 *
 * Returned Value:
 *   OK on success, negative errno on failure or timeout.
 *
 ****************************************************************************/

int f1c100s_dma_wait(DMA_HANDLE handle, uint32_t timeout);

/****************************************************************************
 * Name: f1c100s_dma_busy
 *
 * Description:
 *   Check if DMA channel is busy.
 *
 * Returned Value:
 *   true if busy, false if idle.
 *
 ****************************************************************************/

bool f1c100s_dma_busy(DMA_HANDLE handle);

/****************************************************************************
 * Name: f1c100s_dma_residual
 *
 * Description:
 *   Get remaining bytes to transfer.
 *
 * Returned Value:
 *   Number of bytes remaining, or 0 if complete.
 *
 ****************************************************************************/

size_t f1c100s_dma_residual(DMA_HANDLE handle);

/****************************************************************************
 * Name: f1c100s_dma_capable
 *
 * Description:
 *   Check if a memory region is suitable for DMA.
 *
 * Returned Value:
 *   true if the region can be used for DMA, false otherwise.
 *
 ****************************************************************************/

bool f1c100s_dma_capable(uintptr_t addr, size_t len);

/****************************************************************************
 * Name: f1c100s_dma_setup_chain
 *
 * Description:
 *   Configure a chained DMA transfer using software descriptor list.
 *
 * Input Parameters:
 *   handle - DMA channel handle
 *   desc   - Pointer to first transfer descriptor
 *
 ****************************************************************************/

void f1c100s_dma_setup_chain(DMA_HANDLE handle, struct dma_xfer_desc_s *desc);

/****************************************************************************
 * Name: f1c100s_dma_sg_setup
 *
 * Description:
 *   Configure a scatter-gather DMA transfer.
 *
 * Input Parameters:
 *   handle  - DMA channel handle
 *   sg_src  - Source scatter-gather list
 *   sg_dst  - Destination scatter-gather list
 *   nentries - Number of entries in each list
 *   cfg     - Configuration word
 *   descs   - Pre-allocated descriptor array (nentries elements)
 *
 * Note:
 *   Caller must provide pre-allocated descriptor array.
 *   Source and destination lists must have matching total lengths.
 *
 ****************************************************************************/

void f1c100s_dma_sg_setup(DMA_HANDLE handle,
                          struct dma_sg_entry_s *sg_src,
                          struct dma_sg_entry_s *sg_dst,
                          int nentries, uint32_t cfg,
                          struct dma_xfer_desc_s *descs);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_DMA_H */
