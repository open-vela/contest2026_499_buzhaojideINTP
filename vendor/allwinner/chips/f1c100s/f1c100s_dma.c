/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_dma.c
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
#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/semaphore.h>
#include <syslog.h>

#include <stdint.h>
#include <errno.h>
#include <debug.h>

#include "arm_internal.h"
#include <nuttx/cache.h>
#include "chip.h"
#include "hardware/f1c100s_dma.h"
#include "hardware/f1c100s_ccu.h"
#include "f1c100s_irq.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NDMA_CHANNELS 4
#define DDMA_CHANNELS 4
#define DMA_CHANNELS  (NDMA_CHANNELS + DDMA_CHANNELS)

#define DMA_GETREG(o)     getreg32(F1C100S_DMA_BASE + (o))
#define DMA_PUTREG(o,v)   putreg32((v), F1C100S_DMA_BASE + (o))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct f1c100s_dma_chan_s
{
  uint8_t  chan;        /* Channel number (0-7) */
  bool     inuse;       /* Channel in use? */
  bool     ndma;        /* Is NDMA? (0-3) */
  volatile bool done;   /* Transfer complete flag */
  dma_callback_t callback; /* Callback function */
  void    *arg;         /* Callback argument */
  uintptr_t dst;        /* Destination address for cache invalidation */
  size_t   nbytes;      /* Transfer size for cache invalidation */
  struct dma_xfer_desc_s *chain; /* Current descriptor chain */
  sem_t    waitsem;     /* Semaphore for synchronous wait */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct f1c100s_dma_chan_s g_dma_chan[DMA_CHANNELS];
static sem_t g_dma_sem = SEM_INITIALIZER(1);

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void f1c100s_dma_start_xfer(struct f1c100s_dma_chan_s *chan);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_dma_start_xfer
 ****************************************************************************/

static void f1c100s_dma_start_xfer(struct f1c100s_dma_chan_s *chan)
{
  struct dma_xfer_desc_s *desc = chan->chain;
  uint32_t regval;

  if (!desc)
    {
      return;
    }

  /* Store destination info for cache invalidation */

  chan->dst = desc->dst;
  chan->nbytes = desc->nbytes;

  /* Clean source cache */

  up_clean_dcache(desc->src, desc->src + desc->nbytes);

  /* Setup transfer */

  if (chan->ndma)
    {
      DMA_PUTREG(F1C100S_NDMA_SRC(chan->chan), desc->src);
      DMA_PUTREG(F1C100S_NDMA_DST(chan->chan), desc->dst);
      DMA_PUTREG(F1C100S_NDMA_BC(chan->chan), desc->nbytes);
      DMA_PUTREG(F1C100S_NDMA_CFG(chan->chan), desc->cfg);

      /* Start */

      regval = DMA_GETREG(F1C100S_NDMA_CFG(chan->chan));
      regval |= NDMA_CFG_LOADING;
      DMA_PUTREG(F1C100S_NDMA_CFG(chan->chan), regval);
    }
  else
    {
      DMA_PUTREG(F1C100S_DDMA_SRC(chan->chan), desc->src);
      DMA_PUTREG(F1C100S_DDMA_DST(chan->chan), desc->dst);
      DMA_PUTREG(F1C100S_DDMA_BC(chan->chan), desc->nbytes);
      DMA_PUTREG(F1C100S_DDMA_CFG(chan->chan), desc->cfg);

      regval = DMA_GETREG(F1C100S_DDMA_CFG(chan->chan));
      regval |= NDMA_CFG_LOADING;
      DMA_PUTREG(F1C100S_DDMA_CFG(chan->chan), regval);
    }
}

/****************************************************************************
 * Name: f1c100s_dma_interrupt
 ****************************************************************************/

static int f1c100s_dma_interrupt(int irq, void *context, void *arg)
{
  uint32_t status;
  uint32_t pending;
  int i;

  /* Read interrupt pending register */
  status = DMA_GETREG(F1C100S_DMA_IRQ_PEND);

  /* Clear all pending interrupts */
  DMA_PUTREG(F1C100S_DMA_IRQ_PEND, status);

  /* Check each channel */
  for (i = 0; i < DMA_CHANNELS; i++)
    {
      struct f1c100s_dma_chan_s *chan = &g_dma_chan[i];

      if (chan->inuse)
        {
          pending = 0;
          if (chan->ndma)
            {
              if (status & DMA_IRQ_NDMA(chan->chan))
                {
                  pending = 1;
                }
            }
          else
            {
              if (status & DMA_IRQ_DDMA(chan->chan))
                {
                  pending = 1;
                }
            }

          if (pending)
            {
              /* NOTE: Cache invalidation is now handled by the caller
               * (e.g., audio driver) before DMA starts. Doing it here
               * after DMA completion can corrupt adjacent memory if
               * the buffer is not cache-line aligned.
               */
#if 0
              /* Invalidate destination cache after DMA completion */

              if (chan->dst && chan->nbytes > 0)
                {
                  syslog(LOG_ERR, "DMA: invalidate cache dst=0x%08lx end=0x%08lx nbytes=%lu\n",
                         (unsigned long)chan->dst,
                         (unsigned long)(chan->dst + chan->nbytes),
                         (unsigned long)chan->nbytes);
                  up_invalidate_dcache(chan->dst, chan->dst + chan->nbytes);
                }
#endif

              /* Check for next descriptor in chain */

              if (chan->chain && chan->chain->next)
                {
                  chan->chain = chan->chain->next;
                  f1c100s_dma_start_xfer(chan);
                }
              else
                {
                  /* Chain complete, mark done and post semaphore */

                  chan->chain = NULL;
                  chan->done = true;
                  nxsem_post(&chan->waitsem);

                  if (chan->callback)
                    {
                      chan->callback(chan->arg, DMA_RESULT_OK);
                    }
                }
            }
        }
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_dma_init
 ****************************************************************************/

void f1c100s_dma_init(void)
{
  int i;

  /* Enable DMA clock */
  f1c100s_clk_enable(CCU_BUS_CLK_GATING0, 6);

  /* Initialize channel structures */
  for (i = 0; i < DMA_CHANNELS; i++)
    {
      g_dma_chan[i].chan = i;
      g_dma_chan[i].inuse = false;
      g_dma_chan[i].ndma = (i < NDMA_CHANNELS);
      g_dma_chan[i].done = false;
      g_dma_chan[i].callback = NULL;
      g_dma_chan[i].arg = NULL;
      nxsem_init(&g_dma_chan[i].waitsem, 0, 0);
    }

  /* Disable all interrupts */
  DMA_PUTREG(F1C100S_DMA_IRQ_EN, 0);

  /* Clear pending interrupts */
  DMA_PUTREG(F1C100S_DMA_IRQ_PEND, 0xffffffff);

  /* Auto-gate enable */
  DMA_PUTREG(F1C100S_DMA_AUTO_GATE, 1);

  /* Note: irq_attach and up_enable_irq are called in f1c100s_dma_start()
   * because this function is called before up_irqinitialize() which
   * would clear any interrupt configuration done here.
   */
}

/****************************************************************************
 * Name: f1c100s_dma_channel_alloc
 ****************************************************************************/

DMA_HANDLE f1c100s_dma_channel_alloc(void)
{
  struct f1c100s_dma_chan_s *chan = NULL;
  int i;
  int ret;

  ret = nxsem_wait(&g_dma_sem);
  if (ret < 0)
    {
      return NULL;
    }

  /* Find a free channel (prefer NDMA for SPI) */
  for (i = 0; i < NDMA_CHANNELS; i++)
    {
      if (!g_dma_chan[i].inuse)
        {
          chan = &g_dma_chan[i];
          chan->inuse = true;
          break;
        }
    }

  /* If no NDMA, try DDMA */
  if (!chan)
    {
      for (i = NDMA_CHANNELS; i < DMA_CHANNELS; i++)
        {
          if (!g_dma_chan[i].inuse)
            {
              chan = &g_dma_chan[i];
              chan->inuse = true;
              break;
            }
        }
    }

  nxsem_post(&g_dma_sem);
  return (DMA_HANDLE)chan;
}

/****************************************************************************
 * Name: f1c100s_dma_channel_free
 ****************************************************************************/

void f1c100s_dma_channel_free(DMA_HANDLE handle)
{
  struct f1c100s_dma_chan_s *chan = (struct f1c100s_dma_chan_s *)handle;

  if (chan)
    {
      f1c100s_dma_stop(handle);

      nxsem_wait(&g_dma_sem);
      chan->inuse = false;
      chan->callback = NULL;
      chan->arg = NULL;
      nxsem_post(&g_dma_sem);
    }
}

/****************************************************************************
 * Name: f1c100s_dma_setup
 ****************************************************************************/

void f1c100s_dma_setup(DMA_HANDLE handle, uint32_t src, uint32_t dst,
                       size_t nbytes, uint32_t cfg)
{
  struct f1c100s_dma_chan_s *chan = (struct f1c100s_dma_chan_s *)handle;

  if (!chan)
    {
      return;
    }

  /* Store destination info for cache invalidation after DMA completion */

  chan->dst = dst;
  chan->nbytes = nbytes;

  /* Clean source cache before DMA transfer */

  up_clean_dcache(src, src + nbytes);

  if (chan->ndma)
    {
      DMA_PUTREG(F1C100S_NDMA_SRC(chan->chan), src);
      DMA_PUTREG(F1C100S_NDMA_DST(chan->chan), dst);
      DMA_PUTREG(F1C100S_NDMA_BC(chan->chan), nbytes);
      DMA_PUTREG(F1C100S_NDMA_CFG(chan->chan), cfg);
    }
  else
    {
      /* DDMA setup (simplified, assuming similar structure for now) */
      DMA_PUTREG(F1C100S_DDMA_SRC(chan->chan), src);
      DMA_PUTREG(F1C100S_DDMA_DST(chan->chan), dst);
      DMA_PUTREG(F1C100S_DDMA_BC(chan->chan), nbytes);
      DMA_PUTREG(F1C100S_DDMA_CFG(chan->chan), cfg);
    }
}

/****************************************************************************
 * Name: f1c100s_dma_start
 ****************************************************************************/

void f1c100s_dma_start(DMA_HANDLE handle, dma_callback_t callback, void *arg)
{
  struct f1c100s_dma_chan_s *chan = (struct f1c100s_dma_chan_s *)handle;
  uint32_t regval;
  uint32_t irqen;

  if (!chan)
    {
      return;
    }

  /* Reset done flag and drain semaphore */

  chan->done = false;
  while (nxsem_trywait(&chan->waitsem) == OK);

  chan->callback = callback;
  chan->arg = arg;

  /* Enable interrupt for this channel */

  irqen = DMA_GETREG(F1C100S_DMA_IRQ_EN);
  if (chan->ndma)
    {
      irqen |= DMA_IRQ_NDMA(chan->chan);
    }
  else
    {
      irqen |= DMA_IRQ_DDMA(chan->chan);
    }
  DMA_PUTREG(F1C100S_DMA_IRQ_EN, irqen);

  /* Enable DMA interrupt in interrupt controller */

  up_enable_irq(F1C_IRQ_DMA);

  /* Attach interrupt handler (only once) */

  {
    static bool irq_attached = false;
    if (!irq_attached)
      {
        irq_attach(F1C_IRQ_DMA, f1c100s_dma_interrupt, NULL);
        irq_attached = true;
      }
  }

  /* If chain is set, use chain transfer */

  if (chan->chain)
    {
      f1c100s_dma_start_xfer(chan);
      return;
    }

  /* Start DMA (non-chain mode) */

  if (chan->ndma)
    {
      regval = DMA_GETREG(F1C100S_NDMA_CFG(chan->chan));
      regval |= NDMA_CFG_LOADING;
      DMA_PUTREG(F1C100S_NDMA_CFG(chan->chan), regval);
    }
  else
    {
      regval = DMA_GETREG(F1C100S_DDMA_CFG(chan->chan));
      regval |= NDMA_CFG_LOADING;
      DMA_PUTREG(F1C100S_DDMA_CFG(chan->chan), regval);
    }
}

/****************************************************************************
 * Name: f1c100s_dma_stop
 ****************************************************************************/

void f1c100s_dma_stop(DMA_HANDLE handle)
{
  struct f1c100s_dma_chan_s *chan = (struct f1c100s_dma_chan_s *)handle;
  uint32_t regval;
  uint32_t irqen;

  if (!chan)
    {
      return;
    }

  /* Disable interrupt */
  irqen = DMA_GETREG(F1C100S_DMA_IRQ_EN);
  if (chan->ndma)
    {
      irqen &= ~DMA_IRQ_NDMA(chan->chan);
    }
  else
    {
      irqen &= ~DMA_IRQ_DDMA(chan->chan);
    }
  DMA_PUTREG(F1C100S_DMA_IRQ_EN, irqen);

  /* Stop DMA */
  if (chan->ndma)
    {
      regval = DMA_GETREG(F1C100S_NDMA_CFG(chan->chan));
      regval &= ~NDMA_CFG_LOADING;
      DMA_PUTREG(F1C100S_NDMA_CFG(chan->chan), regval);
    }
  else
    {
      regval = DMA_GETREG(F1C100S_DDMA_CFG(chan->chan));
      regval &= ~NDMA_CFG_LOADING;
      DMA_PUTREG(F1C100S_DDMA_CFG(chan->chan), regval);
    }

  /* Clear chain */

  chan->chain = NULL;
}

/****************************************************************************
 * Name: f1c100s_dma_setup_chain
 ****************************************************************************/

void f1c100s_dma_setup_chain(DMA_HANDLE handle, struct dma_xfer_desc_s *desc)
{
  struct f1c100s_dma_chan_s *chan = (struct f1c100s_dma_chan_s *)handle;

  if (!chan || !desc)
    {
      return;
    }

  chan->chain = desc;
}

/****************************************************************************
 * Name: f1c100s_dma_sg_setup
 ****************************************************************************/

void f1c100s_dma_sg_setup(DMA_HANDLE handle,
                          struct dma_sg_entry_s *sg_src,
                          struct dma_sg_entry_s *sg_dst,
                          int nentries, uint32_t cfg,
                          struct dma_xfer_desc_s *descs)
{
  struct f1c100s_dma_chan_s *chan = (struct f1c100s_dma_chan_s *)handle;
  int i;

  if (!chan || !sg_src || !sg_dst || !descs || nentries <= 0)
    {
      return;
    }

  /* Build descriptor chain from scatter-gather lists */

  for (i = 0; i < nentries; i++)
    {
      descs[i].src = sg_src[i].addr;
      descs[i].dst = sg_dst[i].addr;
      descs[i].nbytes = sg_src[i].len;
      descs[i].cfg = cfg;
      descs[i].next = (i < nentries - 1) ? &descs[i + 1] : NULL;
    }

  /* Set up the chain */

  chan->chain = descs;
}

/****************************************************************************
 * Name: f1c100s_dma_wait
 ****************************************************************************/

int f1c100s_dma_wait(DMA_HANDLE handle, uint32_t timeout)
{
  struct f1c100s_dma_chan_s *chan = (struct f1c100s_dma_chan_s *)handle;
  int ret;

  if (!chan)
    {
      return -EINVAL;
    }

  if (chan->done)
    {
      return OK;
    }

  if (timeout > 0)
    {
      ret = nxsem_tickwait(&chan->waitsem, MSEC2TICK(timeout));
    }
  else
    {
      ret = nxsem_wait(&chan->waitsem);
    }

  return ret;
}

/****************************************************************************
 * Name: f1c100s_dma_busy
 ****************************************************************************/

bool f1c100s_dma_busy(DMA_HANDLE handle)
{
  struct f1c100s_dma_chan_s *chan = (struct f1c100s_dma_chan_s *)handle;

  if (!chan)
    {
      return false;
    }

  if (chan->ndma)
    {
      return (DMA_GETREG(F1C100S_NDMA_CFG(chan->chan)) & NDMA_CFG_LOADING) != 0;
    }
  else
    {
      return (DMA_GETREG(F1C100S_DDMA_CFG(chan->chan)) & NDMA_CFG_LOADING) != 0;
    }
}

/****************************************************************************
 * Name: f1c100s_dma_residual
 ****************************************************************************/

size_t f1c100s_dma_residual(DMA_HANDLE handle)
{
  struct f1c100s_dma_chan_s *chan = (struct f1c100s_dma_chan_s *)handle;

  if (!chan)
    {
      return 0;
    }

  if (chan->ndma)
    {
      return DMA_GETREG(F1C100S_NDMA_BC(chan->chan));
    }
  else
    {
      return DMA_GETREG(F1C100S_DDMA_BC(chan->chan));
    }
}

/****************************************************************************
 * Name: f1c100s_dma_capable
 ****************************************************************************/

bool f1c100s_dma_capable(uintptr_t addr, size_t len)
{
  /* F1C100s DRAM range: 0x80000000 - 0x81FFFFFF (32MB) */

  const uintptr_t dram_start = 0x80000000;
  const uintptr_t dram_end   = 0x82000000;

  /* Check if address is in DRAM range */

  if (addr < dram_start || addr >= dram_end)
    {
      return false;
    }

  /* Check length without overflow */

  if (len > (dram_end - addr))
    {
      return false;
    }

  /* Check 4-byte alignment */

  if ((addr & 0x3) != 0)
    {
      return false;
    }

  return true;
}
