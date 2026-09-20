/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_usb_dma.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_F1C100S_USB_DMA_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_F1C100S_USB_DMA_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stddef.h>
#include "chip.h"

#ifdef CONFIG_F1C100S_USB_DMA

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define USB_DMA_DIR_RX  0  /* Device to Host (IN) */
#define USB_DMA_DIR_TX  1  /* Host to Device (OUT) */

#ifndef CONFIG_F1C100S_USB_DMA_THRESHOLD
#define CONFIG_F1C100S_USB_DMA_THRESHOLD 64
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifndef __ASSEMBLY__

/* DMA transfer completion callback */

typedef void (*usb_dma_callback_t)(void *arg, int result);

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: f1c100s_usb_dma_init
 *
 * Description:
 *   Initialize the USB DMA subsystem.
 *
 * Returned Value:
 *   OK on success, negative errno on failure.
 *
 ****************************************************************************/

int f1c100s_usb_dma_init(void);

/****************************************************************************
 * Name: f1c100s_usb_dma_start
 *
 * Description:
 *   Start a DMA transfer for a USB endpoint.
 *
 * Input Parameters:
 *   epno     - Endpoint number (1-5)
 *   dir      - Transfer direction (USB_DMA_DIR_RX or USB_DMA_DIR_TX)
 *   buffer   - Data buffer (must be DMA-capable memory)
 *   len      - Transfer length in bytes
 *   callback - Completion callback (NULL for synchronous)
 *   arg      - Callback argument
 *
 * Returned Value:
 *   OK on success, -EBUSY if no DMA channel available, negative errno on error.
 *
 ****************************************************************************/

int f1c100s_usb_dma_start(uint8_t epno, int dir, uint8_t *buffer, size_t len,
                          usb_dma_callback_t callback, void *arg);

/****************************************************************************
 * Name: f1c100s_usb_dma_cancel
 *
 * Description:
 *   Cancel a pending DMA transfer.
 *
 * Input Parameters:
 *   epno - Endpoint number
 *
 * Returned Value:
 *   OK on success, negative errno on failure.
 *
 ****************************************************************************/

int f1c100s_usb_dma_cancel(uint8_t epno);

/****************************************************************************
 * Name: f1c100s_usb_dma_wait
 *
 * Description:
 *   Wait for a DMA transfer to complete (synchronous interface).
 *
 * Input Parameters:
 *   epno       - Endpoint number
 *   timeout_ms - Timeout in milliseconds (0 = wait forever)
 *
 * Returned Value:
 *   Number of bytes transferred on success, negative errno on failure.
 *
 ****************************************************************************/

int f1c100s_usb_dma_wait(uint8_t epno, uint32_t timeout_ms);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* CONFIG_F1C100S_USB_DMA */
#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_F1C100S_USB_DMA_H */
