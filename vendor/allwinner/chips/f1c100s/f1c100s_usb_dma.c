/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_usb_dma.c
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

#ifdef CONFIG_F1C100S_USB_DMA

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/semaphore.h>

#include "f1c100s_usb_dma.h"
#include "hardware/f1c100s_usb.h"
#include "hardware/f1c100s_dma.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define USB_DMA_NCHANNELS  5  /* EP1-EP5 */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct usb_dma_chan_s
{
  DMA_HANDLE handle;           /* DMA channel handle */
  uint8_t epno;                /* Endpoint number */
  int dir;                     /* Transfer direction */
  uint8_t *buffer;             /* Data buffer */
  size_t len;                  /* Transfer length */
  usb_dma_callback_t callback; /* Completion callback */
  void *arg;                   /* Callback argument */
  sem_t waitsem;               /* Synchronous wait semaphore */
  volatile bool done;          /* Transfer complete flag */
  volatile int result;         /* Transfer result */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct usb_dma_chan_s g_usb_dma[USB_DMA_NCHANNELS];
static bool g_usb_dma_initialized = false;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: usb_dma_callback
 ****************************************************************************/

static void usb_dma_callback(void *arg, int result)
{
  struct usb_dma_chan_s *chan = (struct usb_dma_chan_s *)arg;
  irqstate_t flags;
  uint16_t csr;
  size_t residual;

  /* Check for incomplete transfer */

  if (result == OK && chan->handle != NULL)
    {
      residual = f1c100s_dma_residual(chan->handle);
      if (residual > 0)
        {
          uerr("USB DMA incomplete: %zu bytes remaining\n", residual);
          result = -EIO;
        }
    }

  /* Handle cache for RX (invalidate after transfer) */

  if (chan->dir == USB_DMA_DIR_RX)
    {
      up_invalidate_dcache((uintptr_t)chan->buffer,
                           (uintptr_t)chan->buffer + chan->len);

      /* Clear RXPKTRDY after DMA complete */

      flags = enter_critical_section();
      USB_PUTREG8(chan->epno, USB_EP_IDX_OFFSET);
      csr = USB_GETREG16(USB_RXCSR_OFFSET);
      csr &= ~USB_RXCSR_RXPKTRDY;
      USB_PUTREG16(csr, USB_RXCSR_OFFSET);
      leave_critical_section(flags);
    }
  else
    {
      /* Set TXPKTRDY after TX DMA complete */

      flags = enter_critical_section();
      USB_PUTREG8(chan->epno, USB_EP_IDX_OFFSET);
      csr = USB_GETREG16(USB_TXCSR_OFFSET);
      csr |= USB_TXCSR_TXPKTRDY;
      USB_PUTREG16(csr, USB_TXCSR_OFFSET);
      leave_critical_section(flags);
    }

  /* Store result and signal completion */

  chan->result = result;
  chan->done = true;

  /* Call user callback if provided */

  if (chan->callback)
    {
      chan->callback(chan->arg, result);
    }

  /* Signal waiting thread */

  nxsem_post(&chan->waitsem);

  /* Free DMA channel */

  if (chan->handle)
    {
      f1c100s_dma_channel_free(chan->handle);
      chan->handle = NULL;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_usb_dma_init
 ****************************************************************************/

int f1c100s_usb_dma_init(void)
{
  int i;

  if (g_usb_dma_initialized)
    {
      return OK;
    }

  for (i = 0; i < USB_DMA_NCHANNELS; i++)
    {
      memset(&g_usb_dma[i], 0, sizeof(struct usb_dma_chan_s));
      nxsem_init(&g_usb_dma[i].waitsem, 0, 0);
    }

  g_usb_dma_initialized = true;
  return OK;
}

/****************************************************************************
 * Name: f1c100s_usb_dma_start
 ****************************************************************************/

int f1c100s_usb_dma_start(uint8_t epno, int dir, uint8_t *buffer, size_t len,
                          usb_dma_callback_t callback, void *arg)
{
  struct usb_dma_chan_s *chan;
  irqstate_t flags;
  uint32_t src;
  uint32_t dst;
  uint32_t cfg;
  uint16_t csr;
  uint8_t drq;
  int idx;
  int ret;

  /* Check if USB DMA is initialized */

  if (!g_usb_dma_initialized)
    {
      return -ENODEV;
    }

  /* Check 4-byte alignment for 32-bit DMA */

  if ((uintptr_t)buffer & 0x3)
    {
      uinfo("USB DMA: Buffer not aligned, use PIO instead\n");
      return -EINVAL;
    }

  /* Validate endpoint number (1-5) */

  if (epno < 1 || epno > USB_DMA_NCHANNELS)
    {
      return -EINVAL;
    }

  idx = epno - 1;
  chan = &g_usb_dma[idx];

  /* Check if channel is busy */

  if (chan->handle != NULL)
    {
      return -EBUSY;
    }

  /* Allocate DMA channel */

  chan->handle = f1c100s_dma_channel_alloc();
  if (chan->handle == NULL)
    {
      return -EBUSY;
    }

  /* Store transfer parameters */

  chan->epno = epno;
  chan->dir = dir;
  chan->buffer = buffer;
  chan->len = len;
  chan->callback = callback;
  chan->arg = arg;
  chan->done = false;
  chan->result = -EINPROGRESS;

  /* DRQ type for USB endpoints */

  drq = DMA_DRQ_OTG_EP1 + (epno - 1);

  /* Configure DMA based on direction */

  if (dir == USB_DMA_DIR_TX)
    {
      /* TX: Memory -> USB FIFO */

      src = (uint32_t)buffer;
      dst = F1C100S_USB_BASE + USB_FIFO_OFFSET(epno);

      cfg = NDMA_CFG_SRC_DRQ_TYPE(DMA_DRQ_SDRAM) |
            NDMA_CFG_SRC_WIDTH_32 |
            NDMA_CFG_SRC_BURST_4 |
            NDMA_CFG_DST_DRQ_TYPE(drq) |
            NDMA_CFG_DST_MODE_IO |
            NDMA_CFG_DST_WIDTH_32 |
            NDMA_CFG_DST_BURST_4;

      /* Clean cache before TX */

      up_clean_dcache((uintptr_t)buffer, (uintptr_t)buffer + len);

      /* Enable DMA request on TX endpoint (Mode 0) */

      flags = enter_critical_section();
      USB_PUTREG8(epno, USB_EP_IDX_OFFSET);
      csr = USB_GETREG16(USB_TXCSR_OFFSET);
      csr |= USB_TXCSR_DMAREQENAB;
      csr &= ~USB_TXCSR_DMAREQMODE;
      USB_PUTREG16(csr, USB_TXCSR_OFFSET);
      leave_critical_section(flags);
    }
  else
    {
      /* RX: USB FIFO -> Memory */

      src = F1C100S_USB_BASE + USB_FIFO_OFFSET(epno);
      dst = (uint32_t)buffer;

      cfg = NDMA_CFG_SRC_DRQ_TYPE(drq) |
            NDMA_CFG_SRC_MODE_IO |
            NDMA_CFG_SRC_WIDTH_32 |
            NDMA_CFG_SRC_BURST_4 |
            NDMA_CFG_DST_DRQ_TYPE(DMA_DRQ_SDRAM) |
            NDMA_CFG_DST_WIDTH_32 |
            NDMA_CFG_DST_BURST_4;

      /* Enable DMA request on RX endpoint (Mode 0) */

      flags = enter_critical_section();
      USB_PUTREG8(epno, USB_EP_IDX_OFFSET);
      csr = USB_GETREG16(USB_RXCSR_OFFSET);
      csr |= USB_RXCSR_DMAREQENAB;
      csr &= ~USB_RXCSR_DMAREQMODE;
      USB_PUTREG16(csr, USB_RXCSR_OFFSET);
      leave_critical_section(flags);
    }

  /* Setup and start DMA */

  ret = f1c100s_dma_setup(chan->handle, src, dst, len, cfg);
  if (ret < 0)
    {
      f1c100s_dma_channel_free(chan->handle);
      chan->handle = NULL;
      return ret;
    }

  ret = f1c100s_dma_start(chan->handle, usb_dma_callback, chan);
  if (ret < 0)
    {
      f1c100s_dma_channel_free(chan->handle);
      chan->handle = NULL;
      return ret;
    }

  return OK;
}

/****************************************************************************
 * Name: f1c100s_usb_dma_cancel
 ****************************************************************************/

int f1c100s_usb_dma_cancel(uint8_t epno)
{
  struct usb_dma_chan_s *chan;
  irqstate_t flags;
  uint16_t csr;
  int idx;

  if (epno < 1 || epno > USB_DMA_NCHANNELS)
    {
      return -EINVAL;
    }

  idx = epno - 1;
  chan = &g_usb_dma[idx];

  if (chan->handle == NULL)
    {
      return OK;
    }

  /* Stop DMA */

  f1c100s_dma_stop(chan->handle);

  /* Clear USB DMA request enable */

  flags = enter_critical_section();
  USB_PUTREG8(epno, USB_EP_IDX_OFFSET);

  if (chan->dir == USB_DMA_DIR_TX)
    {
      csr = USB_GETREG16(USB_TXCSR_OFFSET);
      csr &= ~USB_TXCSR_DMAREQENAB;
      USB_PUTREG16(csr, USB_TXCSR_OFFSET);
    }
  else
    {
      csr = USB_GETREG16(USB_RXCSR_OFFSET);
      csr &= ~USB_RXCSR_DMAREQENAB;
      USB_PUTREG16(csr, USB_RXCSR_OFFSET);
    }

  leave_critical_section(flags);

  /* Free channel */

  f1c100s_dma_channel_free(chan->handle);
  chan->handle = NULL;
  chan->done = true;
  chan->result = -ECANCELED;

  return OK;
}

/****************************************************************************
 * Name: f1c100s_usb_dma_wait
 ****************************************************************************/

int f1c100s_usb_dma_wait(uint8_t epno, uint32_t timeout_ms)
{
  struct usb_dma_chan_s *chan;
  int ret;
  int idx;

  if (epno < 1 || epno > USB_DMA_NCHANNELS)
    {
      return -EINVAL;
    }

  idx = epno - 1;
  chan = &g_usb_dma[idx];

  /* Always wait on semaphore - callback will post it */

  if (timeout_ms > 0)
    {
      ret = nxsem_tickwait(&chan->waitsem, MSEC2TICK(timeout_ms));
    }
  else
    {
      ret = nxsem_wait(&chan->waitsem);
    }

  if (ret < 0)
    {
      f1c100s_usb_dma_cancel(epno);
      return ret;
    }

  return chan->result == OK ? (int)chan->len : chan->result;
}

#endif /* CONFIG_F1C100S_USB_DMA */
