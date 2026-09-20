/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_sdio.c
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

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/sdio.h>
#include <nuttx/kmalloc.h>
#include <nuttx/wqueue.h>

#ifdef CONFIG_F1C100S_SDIO_CARDDETECT
#include <fcntl.h>
#include <sys/ioctl.h>
#include <nuttx/ioexpander/gpio.h>
#endif

#include "arm_internal.h"
#include "chip.h"
#include "hardware/f1c100s_sdio.h"
#include "hardware/f1c100s_ccu.h"
#include "hardware/f1c100s_idmac.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SDC_GETREG(p,o)     getreg32((p)->base + (o))
#define SDC_PUTREG(p,o,v)   putreg32((v), (p)->base + (o))

/* GPIO base address */
#define GPIO_BASE           F1C_PIO_BASE

/* External function declarations */
void f1c100s_reset_sdc0(void);

#ifdef CONFIG_F1C100S_SDIO_DMA
/* IDMAC register access */

#define IDMAC_GETREG(p,o)   getreg32((p)->base + (o))
#define IDMAC_PUTREG(p,o,v) putreg32((v), (p)->base + (o))

/* Maximum bytes per DMA descriptor (8KB - must be multiple of 4) */

#define IDMAC_MAX_BUFFER    8192

/* DMA configuration */

#define F1C100S_SDIO_NDESC  16
#endif /* CONFIG_F1C100S_SDIO_DMA */

/* Timing constants */

#define SDIO_CMDTIMEOUT     MSEC2TICK(1000)
#define SDIO_LONGTIMEOUT    MSEC2TICK(2000)


/****************************************************************************
 * Private Types
 ****************************************************************************/

struct f1c100s_sdio_dev_s
{
  struct sdio_dev_s dev;        /* Standard SDIO device */
  uint32_t base;                /* SDC base address */
  int slotno;                   /* Slot number */
  bool initialized;             /* True if initialized */

  /* State data */

  uint32_t clk_rate;            /* Current clock rate */
  bool widebus;                 /* 4-bit bus */
  uint8_t *rxbuffer;            /* RX buffer pointer */
  const uint8_t *txbuffer;      /* TX buffer pointer */
  size_t remaining;             /* Bytes remaining to transfer */
  size_t xfered;                /* Bytes actually copied from/to FIFO */

  /* Wait/event support */

  sem_t waitsem;                /* Event wait semaphore */
  sdio_eventset_t waitevents;   /* Events to wait for */
  sdio_eventset_t wkupevents;   /* Events that woke us */
  struct wdog_s waitwdog;       /* Watchdog for timeouts */
  int cmdresult;                /* Result of last command (for recv_r*) */

  /* Card detect callback support */

  worker_t callback;            /* Registered media change callback */
  void *cbarg;                  /* Callback argument */
  sdio_eventset_t cbeventset;   /* Enabled callback events */

#ifdef CONFIG_F1C100S_SDIO_CARDDETECT
  /* Card detect GPIO support */

  int cd_fd;                    /* Card detect GPIO file descriptor */
  struct work_s cd_work;        /* Card detect work queue item */
  bool cd_present;              /* Current card present state */
#endif

#ifdef CONFIG_F1C100S_SDIO_DMA
  /* DMA support */

  struct f1c100s_idmac_desc_s *dmadesc; /* DMA descriptors */
  uint8_t dmadesc_buf[F1C100S_SDIO_NDESC * sizeof(struct f1c100s_idmac_desc_s)]
                       __attribute__((aligned(32))); /* DMA descriptor buffer */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Low-level helpers */

static void f1c100s_sdio_hwreset(FAR struct f1c100s_sdio_dev_s *priv);
static void f1c100s_sdio_setclk(FAR struct f1c100s_sdio_dev_s *priv,
                                 uint32_t clkdiv);
static int  f1c100s_sdio_waitcmd(FAR struct f1c100s_sdio_dev_s *priv);
static int  f1c100s_sdio_interrupt(int irq, void *context, void *arg);
#if 0 /* Disabled - using semaphore timeout instead */
static void f1c100s_sdio_timeout(wdparm_t arg);
#endif

/* SDIO Interface */

static void f1c100s_sdio_reset(FAR struct sdio_dev_s *dev);
static sdio_capset_t f1c100s_sdio_capabilities(FAR struct sdio_dev_s *dev);
static sdio_statset_t f1c100s_sdio_status(FAR struct sdio_dev_s *dev);
static void f1c100s_sdio_widebus(FAR struct sdio_dev_s *dev, bool wide);
static void f1c100s_sdio_clock(FAR struct sdio_dev_s *dev,
                                enum sdio_clock_e rate);
static int f1c100s_sdio_attach(FAR struct sdio_dev_s *dev);
static int f1c100s_sdio_sendcmd(FAR struct sdio_dev_s *dev,
                                 uint32_t cmd, uint32_t arg);
#ifdef CONFIG_SDIO_BLOCKSETUP
static void f1c100s_sdio_blocksetup(FAR struct sdio_dev_s *dev,
                                     unsigned int blocklen,
                                     unsigned int nblocks);
#endif
static int f1c100s_sdio_recvsetup(FAR struct sdio_dev_s *dev,
                                   FAR uint8_t *buffer, size_t nbytes);
static int f1c100s_sdio_sendsetup(FAR struct sdio_dev_s *dev,
                                   FAR const uint8_t *buffer, size_t nbytes);
static int f1c100s_sdio_cancel(FAR struct sdio_dev_s *dev);
static int f1c100s_sdio_waitresponse(FAR struct sdio_dev_s *dev,
                                       uint32_t cmd);
static int f1c100s_sdio_recv_r1(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                 FAR uint32_t *r1);
static int f1c100s_sdio_recv_r2(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                 FAR uint32_t r2[4]);
static int f1c100s_sdio_recv_r3(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                 FAR uint32_t *r3);
static int f1c100s_sdio_recv_r6(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                 FAR uint32_t *r6);
static int f1c100s_sdio_recv_r7(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                 FAR uint32_t *r7);
static void f1c100s_sdio_waitenable(FAR struct sdio_dev_s *dev,
                                     sdio_eventset_t eventset,
                                     uint32_t timeout);
static sdio_eventset_t f1c100s_sdio_eventwait(FAR struct sdio_dev_s *dev);
static void f1c100s_sdio_callbackenable(FAR struct sdio_dev_s *dev,
                                          sdio_eventset_t eventset);
static int f1c100s_sdio_registercallback(FAR struct sdio_dev_s *dev,
                                          worker_t callback, void *arg);
#ifdef CONFIG_F1C100S_SDIO_DMA
static int f1c100s_sdio_dmarecvsetup(FAR struct sdio_dev_s *dev,
                                      FAR uint8_t *buffer, size_t nbytes);
static int f1c100s_sdio_dmasendsetup(FAR struct sdio_dev_s *dev,
                                      FAR const uint8_t *buffer, size_t nbytes);
#endif
#ifndef CONFIG_F1C100S_SDIO_DMA
static void f1c100s_sdio_pio_drain_rx(FAR struct f1c100s_sdio_dev_s *priv);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct f1c100s_sdio_dev_s g_sdiodev0 =
{
  .base     = F1C100S_SDC0_BASE,
  .slotno   = 0,
  .waitsem  = SEM_INITIALIZER(0),
};

static const struct sdio_dev_s g_sdio_ops =
{
  .mutex             = NXMUTEX_INITIALIZER,
  .reset             = f1c100s_sdio_reset,
  .capabilities      = f1c100s_sdio_capabilities,
  .status            = f1c100s_sdio_status,
  .widebus           = f1c100s_sdio_widebus,
  .clock             = f1c100s_sdio_clock,
  .attach            = f1c100s_sdio_attach,
  .sendcmd           = f1c100s_sdio_sendcmd,
#ifdef CONFIG_SDIO_BLOCKSETUP
  .blocksetup        = f1c100s_sdio_blocksetup,
#endif
  .recvsetup         = f1c100s_sdio_recvsetup,
  .sendsetup         = f1c100s_sdio_sendsetup,
  .cancel            = f1c100s_sdio_cancel,
  .waitresponse      = f1c100s_sdio_waitresponse,
  .recv_r1           = f1c100s_sdio_recv_r1,
  .recv_r2           = f1c100s_sdio_recv_r2,
  .recv_r3           = f1c100s_sdio_recv_r3,
  .recv_r4           = NULL,
  .recv_r5           = NULL,
  .recv_r6           = f1c100s_sdio_recv_r6,
  .recv_r7           = f1c100s_sdio_recv_r7,
  .waitenable        = f1c100s_sdio_waitenable,
  .eventwait         = f1c100s_sdio_eventwait,
  .callbackenable    = f1c100s_sdio_callbackenable,
  .registercallback  = f1c100s_sdio_registercallback,
#ifdef CONFIG_F1C100S_SDIO_DMA
  .dmarecvsetup      = f1c100s_sdio_dmarecvsetup,
  .dmasendsetup      = f1c100s_sdio_dmasendsetup,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_sdio_hwsetup
 *
 * Description:
 *   Configure GPIO pins and clocks for SDIO.
 *   SDC0 uses PG0-PG5 with function 2:
 *     PG0=DAT1, PG1=DAT0, PG2=CLK, PG3=CMD, PG4=DAT3, PG5=DAT2
 *
 ****************************************************************************/

static void f1c100s_sdio_hwsetup(void)
{
  uint32_t regval;

  /* 1. Configure GPIO PF0-PF5 for SD card function (function 2)
   *    PF_CFG0 register is at GPIO_BASE + 0xB4 (0x01c208b4)
   *    Each pin uses 4 bits: [3:0]=PF0, [7:4]=PF1, etc.
   *    Matches Linux suniv-f1c100s.dtsi mmc0_pins (PF0-PF5 func 2)
   */

  regval = getreg32(GPIO_BASE + 0xB4);  /* PF_CFG0 */
  regval &= ~0x00FFFFFF;                 /* Clear PF0-PF5 function bits */
  regval |= 0x00222222;                  /* Set all to function 2 (MMC0) */
  putreg32(regval, GPIO_BASE + 0xB4);

  /* 2. Set pull-up on CMD (PF3) and DAT0-DAT3 (PF0,PF1,PF4,PF5)
   *    PF_PUL0 register is at GPIO_BASE + 0xD0 (0x01c208d0)
   *    Each pin uses 2 bits: 00=disable, 01=pull-up, 10=pull-down
   */

  regval = getreg32(GPIO_BASE + 0xD0);  /* PF_PUL0 */
  regval &= ~0x00000FFF;                 /* Clear PF0-PF5 pull bits */
  regval |= 0x00000555;                  /* Pull-up on PF0-PF5 (01 for each) */
  putreg32(regval, GPIO_BASE + 0xD0);

  /* 3. Set maximum drive strength (Level 3, 30mA) for PF0-PF5 (MMC0)
   *    PF_DRV0 register is at GPIO_BASE + 0xC4 (0x01c208c4)
   *    Matches Linux suniv-f1c100s.dtsi mmc0_pins (drive-strength = <30>)
   */

  regval = getreg32(GPIO_BASE + 0xC4);  /* PF_DRV0 */
  regval &= ~0x00000FFF;                 /* Clear PF0-PF5 drive bits */
  regval |= 0x00000FFF;                  /* Set to 30mA (11 for each) */
  putreg32(regval, GPIO_BASE + 0xC4);

  /* 4. Configure SD/MMC0 clock register (0x01c20088)
   *    Use OSC24M as source, divider = 1, sample/output phase = 180 deg
   */

  regval = CCU_SDMMC_CLK_ENABLE |
           CCU_SDMMC_CLK_SRC_OSC24M |
           CCU_SDMMC_SAMPLE_PHASE(2) |
           CCU_SDMMC_OUTPUT_PHASE(2) |
           CCU_SDMMC_CLK_RATIO_N(0) |
           CCU_SDMMC_CLK_RATIO_M(0);
  putreg32(regval, CCU_BASE + CCU_SDMMC0_CLK);
}

/****************************************************************************
 * Name: f1c100s_sdio_hwreset
 ****************************************************************************/

static void f1c100s_sdio_hwreset(FAR struct f1c100s_sdio_dev_s *priv)
{
  /* Software reset - reset controller, FIFO, and DMA */

  SDC_PUTREG(priv, SDC_GCTRL_OFFSET,
             SDC_GCTRL_SOFT_RST | SDC_GCTRL_FIFO_RST | SDC_GCTRL_DMA_RST);

  {
    int rstwait = 25000;

    while ((SDC_GETREG(priv, SDC_GCTRL_OFFSET) &
            (SDC_GCTRL_SOFT_RST | SDC_GCTRL_FIFO_RST | SDC_GCTRL_DMA_RST)) != 0 &&
           --rstwait > 0)
      {
        up_udelay(10);
      }
  }

  /* Clear reset bits, enable global interrupt. PIO must set
   * ACCESS_BY_AHB or CPU FIFO reads return nothing and CMD17
   * eventwait times out (SDIOWAIT_TIMEOUT=0x08).
   */

#ifndef CONFIG_F1C100S_SDIO_DMA
  SDC_PUTREG(priv, SDC_GCTRL_OFFSET,
             SDC_GCTRL_INT_EN | SDC_GCTRL_ACCESS_BY_AHB);
#else
  SDC_PUTREG(priv, SDC_GCTRL_OFFSET, SDC_GCTRL_INT_EN);
#endif

  /* Clear all interrupts */

  SDC_PUTREG(priv, SDC_RINT_OFFSET, 0xffffffff);

  /* Disable all interrupts initially */

  SDC_PUTREG(priv, SDC_IMASK_OFFSET, 0);

  /* Set timeout to maximum */

  SDC_PUTREG(priv, SDC_TMOUT_OFFSET, 0xffffffff);

  /* FIFO watermark. Real Linux sunxi-mmc.c default is 0x20070008:
   * burst=8 (bits 31:28), RX trigger=7 words (bits 23:16), TX
   * trigger=8 (bits 7:0). The previous (16 << 16) | 8 set RX trigger
   * to the FIFO's full depth (16 words) with no burst size at all --
   * RX_DATA_REQ then fires only once the FIFO is already full, so
   * there is zero headroom before the next incoming word overruns it.
   * Confirmed on real hardware: CMD17 failed with rintsts=0x820
   * (RX_DATA_REQ | FIFO_RUN_ERR) using the old value.
   */

  SDC_PUTREG(priv, SDC_FTRGL_OFFSET, 0x20070008);

  /* Stop IDMAC even in PIO so it cannot steal FIFO words. */

  SDC_PUTREG(priv, F1C100S_IDMAC_DMAC_OFFSET, 1);
  SDC_PUTREG(priv, F1C100S_IDMAC_IDST_OFFSET, 0xffffffff);
}

/****************************************************************************
 * Name: f1c100s_sdio_setclk
 ****************************************************************************/

static void f1c100s_sdio_setclk(FAR struct f1c100s_sdio_dev_s *priv,
                                 uint32_t clkdiv)
{
  uint32_t regval;
  int timeout;

  /* Set clock divider and enable card clock (matching Linux sunxi_mmc_oclk_onoff) */

  regval = SDC_GETREG(priv, SDC_CLKCR_OFFSET);
  regval = (regval & ~0xFFFF) | (clkdiv & 0xFFFF);
  regval |= SDC_CLKCR_CCLK_ENB;
  SDC_PUTREG(priv, SDC_CLKCR_OFFSET, regval);

  /* Send clock update command */

  SDC_PUTREG(priv, SDC_CMD_OFFSET,
             SDC_CMD_START | SDC_CMD_WAIT_PRE | SDC_CMD_UPCLK_ONLY);

  timeout = 50000;
  while ((SDC_GETREG(priv, SDC_CMD_OFFSET) & SDC_CMD_START) && --timeout > 0)
    {
      /* Wait for update complete */
    }

  if (timeout <= 0)
    {
      mcerr("ERROR: Clock update timeout\n");
    }
}

#ifndef CONFIG_F1C100S_SDIO_DMA
static void f1c100s_sdio_pio_drain_rx(FAR struct f1c100s_sdio_dev_s *priv)
{
  uint32_t status;
  uint32_t level;
  uint32_t data;
  size_t tocopy;

  if (priv->rxbuffer == NULL || priv->remaining == 0)
    {
      return;
    }

  status = SDC_GETREG(priv, SDC_STATUS_OFFSET);
  level = SDC_STATUS_FIFO_LEVEL(status);

  while (level > 0 && priv->remaining > 0)
    {
      data = SDC_GETREG(priv, SDC_FIFO_OFFSET);
      tocopy = priv->remaining > 4 ? 4 : priv->remaining;
      memcpy(priv->rxbuffer, &data, tocopy);
      priv->rxbuffer += tocopy;
      priv->remaining -= tocopy;
      priv->xfered += tocopy;
      level--;
    }
}
#endif

/****************************************************************************
 * Name: f1c100s_sdio_waitcmd
 ****************************************************************************/

static int f1c100s_sdio_waitcmd(FAR struct f1c100s_sdio_dev_s *priv)
{
  clock_t start = clock_systime_ticks();
  uint32_t rintsts;

  /* Poll RINT. PIO waitenable() keeps IMASK=0 so the MMC ISR cannot
   * W1C-steal CMD_DONE. Do not mask IRQs here: clock_systime_ticks()
   * then never advances and data commands hang.
   */

  while (1)
    {
      rintsts = SDC_GETREG(priv, SDC_RINT_OFFSET);

#ifndef CONFIG_F1C100S_SDIO_DMA
      f1c100s_sdio_pio_drain_rx(priv);
      if (priv->rxbuffer != NULL && priv->remaining == 0 &&
          priv->xfered > 0)
        {
          uint8_t *p = priv->rxbuffer - priv->xfered;
          mcinfo("PIO RX %zuB %02x %02x %02x %02x ... %02x %02x\n",
                 priv->xfered, p[0], p[1], p[2], p[3],
                 p[priv->xfered > 1 ? priv->xfered - 2 : 0],
                 p[priv->xfered > 0 ? priv->xfered - 1 : 0]);
          return OK;
        }
#endif

      if (rintsts & SDC_INT_ERROR_MASK)
        {
          ferr("ERROR: waitcmd rintsts=0x%08lx\n",
               (unsigned long)rintsts);
          SDC_PUTREG(priv, SDC_RINT_OFFSET, rintsts);

          if (rintsts & SDC_INT_RESP_TIMEOUT)
            {
              return -ETIMEDOUT;
            }

          return -EIO;
        }

      if (rintsts & SDC_INT_CMD_DONE)
        {
          SDC_PUTREG(priv, SDC_RINT_OFFSET, SDC_INT_CMD_DONE);
          return OK;
        }

#ifdef CONFIG_F1C100S_SDIO_DMA
      if ((rintsts & SDC_INT_DATA_OVER) ||
          (priv->wkupevents & SDIOWAIT_TRANSFERDONE))
        {
          return OK;
        }

      if (priv->wkupevents & SDIOWAIT_ERROR)
        {
          return -EIO;
        }
#endif

      if ((clock_systime_ticks() - start) > SDIO_CMDTIMEOUT)
        {
          /* CMD17 on this IP raises RX_DATA_REQ (0x20) without CMD_DONE.
           * If PIO already drained the block, treat that as success.
           */

          if (priv->rxbuffer != NULL && priv->remaining == 0 &&
              priv->xfered > 0)
            {
              return OK;
            }

#ifdef CONFIG_F1C100S_SDIO_DMA
          if (priv->wkupevents & SDIOWAIT_TRANSFERDONE)
            {
              return OK;
            }
#endif

          ferr("ERROR: waitcmd timeout rintsts=0x%08lx remaining=%zu wkupevents=0x%02x\n",
               (unsigned long)rintsts, priv->remaining, priv->wkupevents);
          return -ETIMEDOUT;
        }
    }
}

/****************************************************************************
 * Name: f1c100s_sdio_timeout
 ****************************************************************************/

#if 0 /* Disabled - using semaphore timeout instead */
static void f1c100s_sdio_timeout(wdparm_t arg)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)arg;

  mcinfo("Timeout\n");

  /* Wakeup with timeout event */
  priv->wkupevents |= SDIOWAIT_TIMEOUT;
  nxsem_post(&priv->waitsem);
}
#endif

/****************************************************************************
 * Name: f1c100s_sdio_interrupt
 ****************************************************************************/

static int f1c100s_sdio_interrupt(int irq, void *context, void *arg)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)arg;
  uint32_t rintsts;
  sdio_eventset_t wkupevents = 0;

  /* Read interrupt status */
  rintsts = SDC_GETREG(priv, SDC_RINT_OFFSET);

#ifdef CONFIG_F1C100S_SDIO_DMA
  /* Read and clear IDMAC status (W1C) */
  uint32_t idst = IDMAC_GETREG(priv, F1C100S_IDMAC_IDST_OFFSET);
  if (idst != 0)
    {
      IDMAC_PUTREG(priv, F1C100S_IDMAC_IDST_OFFSET, idst);

      if (idst & (IDMAC_IDST_FBE | IDMAC_IDST_AIS))
        {
          ferr("ERROR: IDMAC bus error idst=0x%08lx\n", (unsigned long)idst);
          wkupevents |= SDIOWAIT_ERROR;
        }

      if (priv->rxbuffer && (idst & IDMAC_IDST_RI))
        {
          wkupevents |= SDIOWAIT_TRANSFERDONE;
        }
    }
#endif

  /* Command done is polled by waitcmd(); do not touch SDC_INT_CMD_DONE here */

  /* Data transfer done */
  if (rintsts & SDC_INT_DATA_OVER)
    {
      wkupevents |= SDIOWAIT_TRANSFERDONE;
      SDC_PUTREG(priv, SDC_RINT_OFFSET, SDC_INT_DATA_OVER);
    }

  /* Response timeout */
  if (rintsts & SDC_INT_RESP_TIMEOUT)
    {
      wkupevents |= SDIOWAIT_RESPONSEDONE;
      SDC_PUTREG(priv, SDC_RINT_OFFSET, SDC_INT_RESP_TIMEOUT);
    }

  /* Other errors */
  if (rintsts & SDC_INT_ERROR_MASK)
    {
#ifdef CONFIG_F1C100S_SDIO_DMA
      ferr("ERROR: SDC ISR error rintsts=0x%08lx idst=0x%08lx\n",
           (unsigned long)rintsts, (unsigned long)idst);
#else
      ferr("ERROR: SDC ISR error rintsts=0x%08lx\n", (unsigned long)rintsts);
#endif
      wkupevents |= SDIOWAIT_ERROR;
      SDC_PUTREG(priv, SDC_RINT_OFFSET, rintsts & SDC_INT_ERROR_MASK);
    }

  /* Wakeup waiting thread if any events occurred */
  if (wkupevents != 0)
    {
      priv->wkupevents |= wkupevents;
      nxsem_post(&priv->waitsem);
    }

  return OK;
}

/****************************************************************************
 * SDIO Interface Implementation
 ****************************************************************************/

static void f1c100s_sdio_reset(FAR struct sdio_dev_s *dev)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  mcinfo("SDIO reset\n");

  /* Perform hardware reset */

  f1c100s_sdio_hwreset(priv);

  /* Reset state */

  priv->clk_rate = 0;
  priv->widebus = false;
  priv->rxbuffer = NULL;
  priv->txbuffer = NULL;
  priv->remaining = 0;
}

static sdio_capset_t f1c100s_sdio_capabilities(FAR struct sdio_dev_s *dev)
{
  sdio_capset_t caps = 0;

  UNUSED(dev);

  /* Report capabilities based on Kconfig */

#ifdef CONFIG_F1C100S_SDIO_WIDTH_D1_ONLY
  caps |= SDIO_CAPS_1BIT_ONLY;
#endif

#ifdef CONFIG_F1C100S_SDIO_DMA
  caps |= SDIO_CAPS_DMASUPPORTED | SDIO_CAPS_DMABEFOREWRITE;
#endif

  return caps;
}

static sdio_statset_t f1c100s_sdio_status(FAR struct sdio_dev_s *dev)
{
#ifdef CONFIG_F1C100S_SDIO_CARDDETECT
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  /* Return cached card present state from GPIO detection */

  return priv->cd_present ? SDIO_STATUS_PRESENT : 0;
#else
  /* No card detect - assume card is always present */

  UNUSED(dev);
  return SDIO_STATUS_PRESENT;
#endif
}

static void f1c100s_sdio_widebus(FAR struct sdio_dev_s *dev, bool wide)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;
  uint32_t regval;

  mcinfo("Wide bus: %d\n", wide);

  /* Set bus width */

  regval = SDC_GETREG(priv, SDC_WIDTH_OFFSET);
  regval &= ~0x3;
  regval |= wide ? 0x1 : 0x0;  /* 0=1bit, 1=4bit */
  SDC_PUTREG(priv, SDC_WIDTH_OFFSET, regval);

  priv->widebus = wide;
}

static void f1c100s_sdio_clock(FAR struct sdio_dev_s *dev,
                                enum sdio_clock_e rate)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;
  uint32_t regval;
  uint32_t clkdiv;
  uint32_t n = 0;
  uint32_t m = 0;
  uint32_t sample_phase = 0;
  uint32_t output_phase = 0;

  mcinfo("Clock rate: %d\n", rate);

  switch (rate)
    {
      case CLOCK_SDIO_DISABLED:
        /* Disable SD/MMC clock */

        regval = getreg32(CCU_BASE + CCU_SDMMC0_CLK);
        regval &= ~CCU_SDMMC_CLK_ENABLE;
        putreg32(regval, CCU_BASE + CCU_SDMMC0_CLK);
        priv->clk_rate = 0;
        return;

      case CLOCK_IDMODE:
        /* Initial ID mode: ~400kHz
         * 24MHz / (2+1) / (15+1) = 500kHz
         * Linux delays: out 180 (2), sample 180 (2)
         */

        n = 2;
        m = 15;
        sample_phase = 2;
        output_phase = 2;
        break;

      case CLOCK_MMC_TRANSFER:
      case CLOCK_SD_TRANSFER_1BIT:
      case CLOCK_SD_TRANSFER_4BIT:
        /* Fast transfer clock:
         * 24MHz / (2^0) / (1+1) = 12MHz (Rock solid, 1.11MB/s throughput)
         * Sample phase 1 (75 deg), Output phase 2 (180 deg)
         */

        n = 0;
        m = 1;
        sample_phase = 1;
        output_phase = 2;
        break;

      default:
        return;
    }

  /* Configure SD/MMC clock register */

  regval = CCU_SDMMC_CLK_ENABLE |
           CCU_SDMMC_CLK_SRC_OSC24M |
           CCU_SDMMC_SAMPLE_PHASE(sample_phase) |
           CCU_SDMMC_OUTPUT_PHASE(output_phase) |
           CCU_SDMMC_CLK_RATIO_N(n) |
           CCU_SDMMC_CLK_RATIO_M(m);
  putreg32(regval, CCU_BASE + CCU_SDMMC0_CLK);

  /* Also update the internal card clock divider */

  clkdiv = 0;  /* No additional division in SDC */
  f1c100s_sdio_setclk(priv, clkdiv);

  /* Let the card settle after a rate change (ID 400kHz → transfer). */

  up_udelay(1000);

  priv->clk_rate = rate;
}

static int f1c100s_sdio_attach(FAR struct sdio_dev_s *dev)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;
  int ret;

  mcinfo("SDIO attach: attaching IRQ\n");

  /* Attach interrupt handler */
  ret = irq_attach(F1C_IRQ_MMC0, f1c100s_sdio_interrupt, priv);
  if (ret < 0)
    {
      mcerr("ERROR: Failed to attach IRQ: %d\n", ret);
      return ret;
    }

  mcinfo("SDIO attach: enabling IRQ\n");

  /* Enable interrupt */
  up_enable_irq(F1C_IRQ_MMC0);

  mcinfo("SDIO attach: done\n");

  return OK;
}

#ifdef CONFIG_SDIO_BLOCKSETUP
static void f1c100s_sdio_blocksetup(FAR struct sdio_dev_s *dev,
                                     unsigned int blocklen,
                                     unsigned int nblocks)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  mcinfo("SDIO: blocksetup blocklen=%u nblocks=%u\n", blocklen, nblocks);

  /* Set block size */

  SDC_PUTREG(priv, SDC_BLKSZ_OFFSET, blocklen);

  /* Set byte count */

  SDC_PUTREG(priv, SDC_BCNTR_OFFSET, blocklen * nblocks);
}
#endif

static int f1c100s_sdio_sendcmd(FAR struct sdio_dev_s *dev,
                                 uint32_t cmd, uint32_t arg)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;
  uint32_t regval;
  uint32_t cmdidx;

  cmdidx = (cmd & MMCSD_CMDIDX_MASK) >> MMCSD_CMDIDX_SHIFT;

  mcinfo("SDIO: CMD%lu arg=0x%08lx\n", (unsigned long)cmdidx, (unsigned long)arg);

  /* Reset command result for this new command */

  priv->cmdresult = OK;

  /* Do NOT touch remaining/rxbuffer/txbuffer here. A NODATAXFR command
   * (e.g. CMD55/APP_CMD) is routinely sent right after
   * SDIO_RECVSETUP() has already armed the buffer for the ACMD that
   * follows (mmcsd_get_scr() does exactly this) -- clearing them here
   * stomped that setup and made the SCR/ACMD data phase read nothing.
   * T113's driver never touches these fields in sendcmd(); only
   * recvsetup/sendsetup/eventwait manage them.
   */

  /* Clear interrupts */

  SDC_PUTREG(priv, SDC_RINT_OFFSET, 0xffffffff);

  /* Set command argument */

  SDC_PUTREG(priv, SDC_CARG_OFFSET, arg);

  /* Build command register value */

  regval = SDC_CMD_START | cmdidx;

  /* Set response type */

  if ((cmd & MMCSD_RESPONSE_MASK) != MMCSD_NO_RESPONSE)
    {
      regval |= SDC_CMD_RESP_RCV;

      if ((cmd & MMCSD_RESPONSE_MASK) == MMCSD_R2_RESPONSE)
        {
          regval |= SDC_CMD_LONG_RESP | SDC_CMD_CHK_RESP_CRC;
        }
      else if ((cmd & MMCSD_RESPONSE_MASK) == MMCSD_R3_RESPONSE)
        {
          /* R3 has no CRC */
        }
      else
        {
          regval |= SDC_CMD_CHK_RESP_CRC;
        }
    }

  /* Check for data transfer */

  if ((cmd & MMCSD_DATAXFR_MASK) != MMCSD_NODATAXFR)
    {
      regval |= SDC_CMD_DATA_TRANS;

      if ((cmd & MMCSD_DATAXFR_MASK) & MMCSD_WRXFR)
        {
          regval |= SDC_CMD_WRITE;
        }

      /* WAIT_PRE (wait for the previous data transfer to finish)
       * belongs only on data commands, matching T113/Linux sunxi-mmc.
       * Setting it on every command can make a non-data command block
       * forever if the card is holding DAT0 busy.
       */

      regval |= SDC_CMD_WAIT_PRE;
    }

  if ((cmd & MMCSD_STOPXFR) != 0 || cmdidx == MMCSD_CMDIDX12)
    {
      /* Stop current data transfer in progress immediately */

      regval |= SDC_CMD_STOP_ABT;
      regval &= ~SDC_CMD_WAIT_PRE;
    }
  else
    {
      /* Wait for card to release DAT0 busy from previous write operation */

      int timeout = 50000;
      while ((SDC_GETREG(priv, SDC_STATUS_OFFSET) & SDC_STATUS_CARD_DATA_BUSY) &&
             --timeout > 0)
        {
          up_udelay(10);
        }

      if (timeout <= 0)
        {
          ferr("ERROR: card busy timeout before CMD%lu\n", (unsigned long)cmdidx);
        }
    }

  /* Send the command. Do not busy-wait for START to clear here -- the
   * controller clears it once the command is issued; T113/Linux only
   * poll START on the clock-update path, not after a regular command.
   */

  SDC_PUTREG(priv, SDC_CMD_OFFSET, regval);

  return OK;
}

static int f1c100s_sdio_recvsetup(FAR struct sdio_dev_s *dev,
                                   FAR uint8_t *buffer, size_t nbytes)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  mcinfo("SDIO: recvsetup %zu bytes\n", nbytes);

  DEBUGASSERT(buffer != NULL && nbytes > 0);

  /* Save buffer info */

  priv->rxbuffer = buffer;
  priv->txbuffer = NULL;
  priv->remaining = nbytes;
  priv->xfered = 0;

#ifndef CONFIG_F1C100S_SDIO_DMA
  {
    uint32_t gctrl = SDC_GETREG(priv, SDC_GCTRL_OFFSET);
    int rstwait = 25000;

    gctrl = (gctrl & ~SDC_GCTRL_DMA_EN) | SDC_GCTRL_ACCESS_BY_AHB |
            SDC_GCTRL_FIFO_RST;
    SDC_PUTREG(priv, SDC_GCTRL_OFFSET, gctrl);
    while ((SDC_GETREG(priv, SDC_GCTRL_OFFSET) & SDC_GCTRL_FIFO_RST) != 0 &&
           --rstwait > 0)
      {
        up_udelay(10);
      }

    gctrl = SDC_GETREG(priv, SDC_GCTRL_OFFSET);
    gctrl = (gctrl & ~SDC_GCTRL_DMA_EN) | SDC_GCTRL_ACCESS_BY_AHB;
    SDC_PUTREG(priv, SDC_GCTRL_OFFSET, gctrl);
  }
#endif

  /* BLKSZ must be the transfer length (T113: hardcoded 512 hangs
   * ACMD51 8-byte SCR). CMD17 is 512 so this matches FAT reads.
   * For multi-block reads/writes, BLKSZ must remain 512.
   */

  size_t rx_blksz = (nbytes >= 512) ? 512 : nbytes;
  SDC_PUTREG(priv, SDC_BLKSZ_OFFSET, rx_blksz);
  SDC_PUTREG(priv, SDC_BCNTR_OFFSET, nbytes);

  return OK;
}

static int f1c100s_sdio_sendsetup(FAR struct sdio_dev_s *dev,
                                   FAR const uint8_t *buffer, size_t nbytes)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  mcinfo("TX setup: %zu bytes\n", nbytes);

  DEBUGASSERT(buffer != NULL && nbytes > 0);

  /* Save buffer info */

  priv->txbuffer = buffer;
  priv->rxbuffer = NULL;
  priv->remaining = nbytes;
  priv->xfered = 0;

#ifndef CONFIG_F1C100S_SDIO_DMA
  {
    uint32_t gctrl = SDC_GETREG(priv, SDC_GCTRL_OFFSET);
    gctrl = (gctrl & ~SDC_GCTRL_DMA_EN) | SDC_GCTRL_ACCESS_BY_AHB;
    SDC_PUTREG(priv, SDC_GCTRL_OFFSET, gctrl);
  }
#endif

  size_t tx_blksz = (nbytes >= 512) ? 512 : nbytes;
  SDC_PUTREG(priv, SDC_BLKSZ_OFFSET, tx_blksz);
  SDC_PUTREG(priv, SDC_BCNTR_OFFSET, nbytes);

  return OK;
}

static int f1c100s_sdio_cancel(FAR struct sdio_dev_s *dev)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;
#ifdef CONFIG_F1C100S_SDIO_DMA
  uint32_t regval;
#endif

  mcinfo("Cancel transfer\n");

#ifdef CONFIG_F1C100S_SDIO_DMA
  /* Disable IDMAC and interrupts */

  IDMAC_PUTREG(priv, F1C100S_IDMAC_IDIE_OFFSET, 0);
  IDMAC_PUTREG(priv, F1C100S_IDMAC_IDST_OFFSET, 0x337);
  IDMAC_PUTREG(priv, F1C100S_IDMAC_DMAC_OFFSET, 0);

  /* Reset DMA and FIFO, disable DMA in GCTRL */

  regval = SDC_GETREG(priv, SDC_GCTRL_OFFSET);
  regval |= SDC_GCTRL_DMA_RST;
  SDC_PUTREG(priv, SDC_GCTRL_OFFSET, regval);
  regval &= ~SDC_GCTRL_DMA_EN;
  SDC_PUTREG(priv, SDC_GCTRL_OFFSET, regval);
  regval |= SDC_GCTRL_FIFO_RST;
  SDC_PUTREG(priv, SDC_GCTRL_OFFSET, regval);
#endif

  /* Reset hardware */

  f1c100s_sdio_hwreset(priv);

  /* Clear buffers */

  priv->rxbuffer = NULL;
  priv->txbuffer = NULL;
  priv->remaining = 0;

  return OK;
}

static int f1c100s_sdio_waitresponse(FAR struct sdio_dev_s *dev,
                                       uint32_t cmd)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  priv->cmdresult = f1c100s_sdio_waitcmd(priv);
  return priv->cmdresult;
}

static int f1c100s_sdio_recv_r1(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                 FAR uint32_t *r1)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  /* Read response and return command result from waitresponse */

  *r1 = SDC_GETREG(priv, SDC_RESP0_OFFSET);
  mcinfo("SDIO: R1=0x%08lx (cmdres=%d)\n", (unsigned long)*r1, priv->cmdresult);
  return priv->cmdresult;
}

static int f1c100s_sdio_recv_r2(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                 FAR uint32_t r2[4])
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  /* Just read the registers - error handling is done by waitresponse */

  r2[0] = SDC_GETREG(priv, SDC_RESP0_OFFSET);
  r2[1] = SDC_GETREG(priv, SDC_RESP1_OFFSET);
  r2[2] = SDC_GETREG(priv, SDC_RESP2_OFFSET);
  r2[3] = SDC_GETREG(priv, SDC_RESP3_OFFSET);
  return OK;
}

static int f1c100s_sdio_recv_r3(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                 FAR uint32_t *r3)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  /* Return saved command result from waitresponse */

  *r3 = SDC_GETREG(priv, SDC_RESP0_OFFSET);
  return priv->cmdresult;
}

static int f1c100s_sdio_recv_r6(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                 FAR uint32_t *r6)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  /* Read response and return command result from waitresponse */

  *r6 = SDC_GETREG(priv, SDC_RESP0_OFFSET);
  return priv->cmdresult;
}

static int f1c100s_sdio_recv_r7(FAR struct sdio_dev_s *dev, uint32_t cmd,
                                 FAR uint32_t *r7)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  /* Read response and return command result from waitresponse */

  *r7 = SDC_GETREG(priv, SDC_RESP0_OFFSET);
  return priv->cmdresult;
}

static void f1c100s_sdio_waitenable(FAR struct sdio_dev_s *dev,
                                     sdio_eventset_t eventset,
                                     uint32_t timeout)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;
  uint32_t imask = 0;

  mcinfo("SDIO: waitenable events=0x%02x timeout=%lu\n", eventset, (unsigned long)timeout);

  priv->waitevents = eventset;
  priv->wkupevents = 0;

#ifndef CONFIG_F1C100S_SDIO_DMA
  /* PIO: keep IMASK=0 so the live MMC ISR cannot W1C-steal RINT bits
   * from the poll loops (T113 same rule).
   */

  UNUSED(imask);
  UNUSED(timeout);
  SDC_PUTREG(priv, SDC_IMASK_OFFSET, 0);
  return;
#else
  /* DMA mode: reset semaphore to drain any stale post from previous transfer */

  nxsem_reset(&priv->waitsem, 0);

  /* Enable interrupts for requested events.
   * Command done is polled by waitcmd(); do not enable SDC_INT_CMD_DONE
   * in IMASK to avoid unhandled ISR interrupt spinning.
   */

  if (eventset & SDIOWAIT_TRANSFERDONE)
    {
      imask |= SDC_INT_DATA_OVER;
    }

  if (eventset & SDIOWAIT_RESPONSEDONE)
    {
      imask |= SDC_INT_RESP_TIMEOUT;
    }

  if (eventset & SDIOWAIT_ERROR)
    {
      imask |= SDC_INT_ERROR_MASK;
    }

  /* Enable interrupts in hardware */
  SDC_PUTREG(priv, SDC_IMASK_OFFSET, imask);

  mcinfo("SDIO: waitenable imask=0x%08lx\n", (unsigned long)imask);

  /* Skip watchdog timer - use semaphore timeout instead */
#if 0
  /* Setup watchdog for timeout */
  if (timeout > 0)
    {
      wd_start(&priv->waitwdog, timeout, f1c100s_sdio_timeout, (wdparm_t)priv);
    }
#endif
#endif /* CONFIG_F1C100S_SDIO_DMA */
}

static sdio_eventset_t f1c100s_sdio_eventwait(FAR struct sdio_dev_s *dev)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;
  sdio_eventset_t wkupevents;
#ifndef CONFIG_F1C100S_SDIO_DMA
  uint32_t rintsts;
  clock_t start;
#endif

  mcinfo("eventwait rem=%zu xfered=%zu wait=0x%x rx=%p blksz=%lu bcntr=%lu\n",
         priv->remaining, priv->xfered, (unsigned)priv->waitevents,
         priv->rxbuffer,
         (unsigned long)SDC_GETREG(priv, SDC_BLKSZ_OFFSET),
         (unsigned long)SDC_GETREG(priv, SDC_BCNTR_OFFSET));

#ifndef CONFIG_F1C100S_SDIO_DMA
  start = clock_systime_ticks();

  if (priv->remaining == 0 &&
      (priv->waitevents & SDIOWAIT_TRANSFERDONE) &&
      priv->rxbuffer != NULL && priv->xfered == 0)
    {
      uint32_t n = SDC_GETREG(priv, SDC_BCNTR_OFFSET);
      if (n > 0 && n <= 4096)
        {
          priv->remaining = n;
        }
    }

  if (priv->remaining == 0)
    {
      /* waitcmd may already have drained the whole CMD17 block by
       * the time CMD_DONE is seen. Do not sit on DATA_OVER then —
       * that timed out with SDIOWAIT_TIMEOUT (0x08).
       */

      if ((priv->waitevents & SDIOWAIT_TRANSFERDONE) &&
          priv->xfered > 0)
        {
          uint8_t *p = priv->rxbuffer - priv->xfered;
          mcinfo("PIO sector %02x %02x %02x %02x %02x %02x %02x %02x "
                 "%02x %02x %02x %02x %02x %02x %02x %02x tail %02x %02x\n",
                 p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                 p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15],
                 priv->xfered >= 512 ? p[510] : 0,
                 priv->xfered >= 512 ? p[511] : 0);
          rintsts = SDC_GETREG(priv, SDC_RINT_OFFSET);
          if (rintsts & SDC_INT_DATA_OVER)
            {
              SDC_PUTREG(priv, SDC_RINT_OFFSET, SDC_INT_DATA_OVER);
            }

          priv->wkupevents |= SDIOWAIT_TRANSFERDONE;
          goto done;
        }

      while (1)
        {
          rintsts = SDC_GETREG(priv, SDC_RINT_OFFSET);

          if (rintsts & SDC_INT_ERROR_MASK)
            {
              mcerr("ERROR: cmd wait rintsts=0x%08lx\n",
                    (unsigned long)rintsts);
              SDC_PUTREG(priv, SDC_RINT_OFFSET, rintsts);
              priv->wkupevents |= SDIOWAIT_ERROR;
              goto done;
            }

          if ((priv->waitevents & SDIOWAIT_CMDDONE) &&
              (rintsts & SDC_INT_CMD_DONE))
            {
              SDC_PUTREG(priv, SDC_RINT_OFFSET, SDC_INT_CMD_DONE);
              priv->wkupevents |= SDIOWAIT_CMDDONE;
              goto done;
            }

          if ((priv->waitevents & SDIOWAIT_TRANSFERDONE) &&
              (rintsts & SDC_INT_DATA_OVER))
            {
              SDC_PUTREG(priv, SDC_RINT_OFFSET, SDC_INT_DATA_OVER);
              priv->wkupevents |= SDIOWAIT_TRANSFERDONE;
              goto done;
            }

          if ((clock_systime_ticks() - start) > SDIO_CMDTIMEOUT)
            {
              mcerr("ERROR: cmd wait timeout rintsts=0x%08lx\n",
                    (unsigned long)rintsts);
              priv->wkupevents |= SDIOWAIT_TIMEOUT;
              goto done;
            }
        }
    }

  /* Non-DMA data: poll FIFO */

  while (priv->remaining > 0)
    {
      rintsts = SDC_GETREG(priv, SDC_RINT_OFFSET);
      uint32_t status = SDC_GETREG(priv, SDC_STATUS_OFFSET);

      /* Check for errors */

      if (rintsts & SDC_INT_ERROR_MASK)
        {
          mcerr("ERROR: eventwait error rintsts=0x%08lx\n",
                (unsigned long)rintsts);
          SDC_PUTREG(priv, SDC_RINT_OFFSET, rintsts);
          priv->wkupevents |= SDIOWAIT_ERROR;
          goto done;
        }

      if (priv->rxbuffer != NULL)
        {
          f1c100s_sdio_pio_drain_rx(priv);
        }

      /* Write data to FIFO if needed (for TX) */

      else if (priv->txbuffer != NULL)
        {
          /* Check if FIFO has space (FIFO_FULL bit is 0) */

          if (!(status & (1 << 3)))
            {
              uint32_t data = 0;
              size_t tocopy = priv->remaining > 4 ? 4 : priv->remaining;

              memcpy(&data, priv->txbuffer, tocopy);
              SDC_PUTREG(priv, SDC_FIFO_OFFSET, data);
              priv->txbuffer += tocopy;
              priv->remaining -= tocopy;
            }
        }

      /* Check for data transfer complete */

      if (rintsts & SDC_INT_DATA_OVER)
        {
          SDC_PUTREG(priv, SDC_RINT_OFFSET, SDC_INT_DATA_OVER);
          priv->wkupevents |= SDIOWAIT_TRANSFERDONE;
          goto done;
        }

      /* Check timeout */

      if ((clock_systime_ticks() - start) > MSEC2TICK(5000))
        {
          ferr("ERROR: eventwait timeout remaining=%zu xfered=%zu "
               "rintsts=0x%08lx status=0x%08lx gctrl=0x%08lx "
               "blksz=0x%08lx bcntr=0x%08lx\n",
               priv->remaining, priv->xfered,
               (unsigned long)rintsts,
               (unsigned long)status,
               (unsigned long)SDC_GETREG(priv, SDC_GCTRL_OFFSET),
               (unsigned long)SDC_GETREG(priv, SDC_BLKSZ_OFFSET),
               (unsigned long)SDC_GETREG(priv, SDC_BCNTR_OFFSET));
          priv->wkupevents |= SDIOWAIT_TIMEOUT;
          goto done;
        }
    }

  /* All data read, wait for DATA_OVER */

  while (1)
    {
      rintsts = SDC_GETREG(priv, SDC_RINT_OFFSET);

      if (rintsts & SDC_INT_DATA_OVER)
        {
          SDC_PUTREG(priv, SDC_RINT_OFFSET, SDC_INT_DATA_OVER);
          priv->wkupevents |= SDIOWAIT_TRANSFERDONE;
          break;
        }

      if (rintsts & SDC_INT_ERROR_MASK)
        {
          SDC_PUTREG(priv, SDC_RINT_OFFSET, rintsts);
          priv->wkupevents |= SDIOWAIT_ERROR;
          break;
        }

      if ((clock_systime_ticks() - start) > MSEC2TICK(5000))
        {
          priv->wkupevents |= SDIOWAIT_TIMEOUT;
          break;
        }
    }

done:
#else
  /* DMA mode: wait for interrupt */

  int ret = nxsem_tickwait(&priv->waitsem, MSEC2TICK(5000));
  if (ret == -ETIMEDOUT)
    {
      ferr("ERROR: Event wait timeout (safety)\n");
      priv->wkupevents |= SDIOWAIT_TIMEOUT;
    }

  /* If woken with wkupevents == 0, check IDST, RINT and descriptor OWN directly */

  if (priv->wkupevents == 0)
    {
      uint32_t idst = IDMAC_GETREG(priv, F1C100S_IDMAC_IDST_OFFSET);
      uint32_t rint = SDC_GETREG(priv, SDC_RINT_OFFSET);

      if (rint & SDC_INT_DATA_OVER)
        {
          SDC_PUTREG(priv, SDC_RINT_OFFSET, SDC_INT_DATA_OVER);
          priv->wkupevents |= SDIOWAIT_TRANSFERDONE;
        }
      else if (priv->rxbuffer && (idst & IDMAC_IDST_RI))
        {
          IDMAC_PUTREG(priv, F1C100S_IDMAC_IDST_OFFSET, idst);
          priv->wkupevents |= SDIOWAIT_TRANSFERDONE;
        }
    }

  mcinfo("eventwait wake: ret=%d wkup=0x%02x idst=0x%08lx des0=0x%08lx des1=0x%08lx rint=0x%08lx status=0x%08lx\n",
         ret, priv->wkupevents,
         (unsigned long)IDMAC_GETREG(priv, F1C100S_IDMAC_IDST_OFFSET),
         (unsigned long)priv->dmadesc[0].des0,
         (unsigned long)priv->dmadesc[0].des1,
         (unsigned long)SDC_GETREG(priv, SDC_RINT_OFFSET),
         (unsigned long)SDC_GETREG(priv, SDC_STATUS_OFFSET));

  if (priv->rxbuffer != NULL)
    {
      uint8_t *p = priv->rxbuffer;
      mcinfo("rxbuf: %02x %02x %02x %02x %02x %02x %02x %02x ... %02x %02x %02x %02x\n",
             p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
             p[508], p[509], p[510], p[511]);
    }

  /* Quiesce interrupts for this transfer */

  IDMAC_PUTREG(priv, F1C100S_IDMAC_IDIE_OFFSET, 0);
  IDMAC_PUTREG(priv, F1C100S_IDMAC_IDST_OFFSET, 0x337);

  /* Ensure RX FIFO has drained before cache invalidation */

  if ((priv->wkupevents & SDIOWAIT_TRANSFERDONE) && priv->rxbuffer != NULL)
    {
      int guard = 1000000;
      while (guard-- > 0 &&
             !(SDC_GETREG(priv, SDC_STATUS_OFFSET) & SDC_STATUS_FIFO_EMPTY))
        {
        }

      up_invalidate_dcache((uintptr_t)priv->rxbuffer,
                           (uintptr_t)priv->rxbuffer + priv->remaining);
    }
#endif

  /* Get wakeup events */

  wkupevents = priv->wkupevents;
  priv->wkupevents = 0;
  priv->waitevents = 0;

  /* Clear RX/TX buffer pointers */

  priv->rxbuffer = NULL;
  priv->txbuffer = NULL;

  /* Disable all interrupts */

  SDC_PUTREG(priv, SDC_IMASK_OFFSET, 0);

  mcinfo("SDIO: eventwait done events=0x%02x\n", wkupevents);

  return wkupevents;
}

static void f1c100s_sdio_callbackenable(FAR struct sdio_dev_s *dev,
                                          sdio_eventset_t eventset)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  mcinfo("Callback enable: events=0x%02x\n", eventset);

  /* Save the enabled events */

  priv->cbeventset = eventset;
}

static int f1c100s_sdio_registercallback(FAR struct sdio_dev_s *dev,
                                          worker_t callback, void *arg)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  /* Save the callback info */

  priv->callback = callback;
  priv->cbarg = arg;

  mcinfo("Callback registered: %p arg=%p\n", callback, arg);
  return OK;
}

/****************************************************************************
 * Name: f1c100s_sdio_dmasetup
 *
 * Description:
 *   Setup DMA descriptors for a transfer
 *
 ****************************************************************************/

#ifdef CONFIG_F1C100S_SDIO_DMA
static void f1c100s_sdio_dmasetup(FAR struct f1c100s_sdio_dev_s *priv,
                                   FAR uint8_t *buffer, size_t nbytes)
{
  FAR struct f1c100s_idmac_desc_s *desc = priv->dmadesc;
  uintptr_t bufaddr = (uintptr_t)buffer;
  size_t remaining = nbytes;
  int i = 0;

  /* Build descriptor chain */

  while (remaining > 0 && i < F1C100S_SDIO_NDESC)
    {
      size_t chunk = remaining > IDMAC_MAX_BUFFER ? IDMAC_MAX_BUFFER : remaining;

      desc[i].des0 = IDMAC_DES0_OWN | IDMAC_DES0_CH | IDMAC_DES0_DIC;
      desc[i].des1 = (chunk == IDMAC_MAX_BUFFER) ? 0 : chunk;
      desc[i].des2 = bufaddr;
      desc[i].des3 = (uintptr_t)&desc[i + 1];

      /* Mark first descriptor */

      if (i == 0)
        {
          desc[i].des0 |= IDMAC_DES0_FD;
        }

      bufaddr += chunk;
      remaining -= chunk;
      i++;
    }

  /* Mark last descriptor */

  if (i > 0)
    {
      desc[i - 1].des0 |= (IDMAC_DES0_LD | IDMAC_DES0_ER);
      desc[i - 1].des0 &= ~IDMAC_DES0_DIC;
      desc[i - 1].des3 = 0;  /* End of chain */
    }

  /* Flush cache for descriptors */

  up_clean_dcache((uintptr_t)desc,
                  (uintptr_t)desc + i * sizeof(struct f1c100s_idmac_desc_s));

  /* Flush cache for buffer (write) or invalidate (read) done in caller */
}
#endif

/****************************************************************************
 * Name: f1c100s_sdio_dmarecvsetup
 ****************************************************************************/

#ifdef CONFIG_F1C100S_SDIO_DMA
static int f1c100s_sdio_dmarecvsetup(FAR struct sdio_dev_s *dev,
                                      FAR uint8_t *buffer, size_t nbytes)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;
  uint32_t regval;

  mcinfo("DMA RX setup: %zu bytes\n", nbytes);

  DEBUGASSERT(buffer != NULL && nbytes > 0);

  /* Save buffer info for cache invalidation in interrupt */

  priv->rxbuffer = buffer;
  priv->remaining = nbytes;

  /* Setup DMA descriptors */

  f1c100s_sdio_dmasetup(priv, buffer, nbytes);

  /* Enable DMA and reset DMA interface in global control.
   * Clear ACCESS_BY_AHB so FIFO is routed to IDMAC.
   */

  regval = SDC_GETREG(priv, SDC_GCTRL_OFFSET);
  regval &= ~SDC_GCTRL_ACCESS_BY_AHB;
  regval |= SDC_GCTRL_DMA_EN | SDC_GCTRL_DMA_RST;
  SDC_PUTREG(priv, SDC_GCTRL_OFFSET, regval);
  for (int t = 10000; t > 0 &&
       (SDC_GETREG(priv, SDC_GCTRL_OFFSET) & SDC_GCTRL_DMA_RST); t--);

  /* Soft reset IDMAC; wait self-clear */

  IDMAC_PUTREG(priv, F1C100S_IDMAC_DMAC_OFFSET, IDMAC_DMAC_SWR);
  for (int t = 10000; t > 0 &&
       (IDMAC_GETREG(priv, F1C100S_IDMAC_DMAC_OFFSET) & IDMAC_DMAC_SWR); t--);

  /* Set descriptor list base address */

  IDMAC_PUTREG(priv, F1C100S_IDMAC_DLBA_OFFSET, (uint32_t)priv->dmadesc);

  /* Arm IDMAC receive interrupt (RI) */

  IDMAC_PUTREG(priv, F1C100S_IDMAC_IDIE_OFFSET, IDMAC_IDIE_RI);

  /* Clear stale IDMAC status */

  IDMAC_PUTREG(priv, F1C100S_IDMAC_IDST_OFFSET, 0x337);

  /* Enable IDMAC with fixed burst */

  IDMAC_PUTREG(priv, F1C100S_IDMAC_DMAC_OFFSET,
               IDMAC_DMAC_FB | IDMAC_DMAC_IDMAC_EN);

  /* Set block size and byte count */

  SDC_PUTREG(priv, SDC_BLKSZ_OFFSET, 512);
  SDC_PUTREG(priv, SDC_BCNTR_OFFSET, nbytes);

  return OK;
}

/****************************************************************************
 * Name: f1c100s_sdio_dmasendsetup
 ****************************************************************************/

static int f1c100s_sdio_dmasendsetup(FAR struct sdio_dev_s *dev,
                                      FAR const uint8_t *buffer, size_t nbytes)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;
  uint32_t regval;

  mcinfo("DMA TX setup: %zu bytes\n", nbytes);

  DEBUGASSERT(buffer != NULL && nbytes > 0);

  /* Save buffer info */

  priv->txbuffer = buffer;
  priv->remaining = nbytes;

  /* Flush cache for transmit buffer */

  up_clean_dcache((uintptr_t)buffer, (uintptr_t)buffer + nbytes);

  /* Setup DMA descriptors (cast away const - DMA won't modify TX buffer) */

  f1c100s_sdio_dmasetup(priv, (FAR uint8_t *)buffer, nbytes);

  /* Enable DMA and reset DMA interface in global control.
   * Clear ACCESS_BY_AHB so FIFO is routed to IDMAC.
   */

  regval = SDC_GETREG(priv, SDC_GCTRL_OFFSET);
  regval &= ~SDC_GCTRL_ACCESS_BY_AHB;
  regval |= SDC_GCTRL_DMA_EN | SDC_GCTRL_DMA_RST;
  SDC_PUTREG(priv, SDC_GCTRL_OFFSET, regval);
  for (int t = 10000; t > 0 &&
       (SDC_GETREG(priv, SDC_GCTRL_OFFSET) & SDC_GCTRL_DMA_RST); t--);

  /* Soft reset IDMAC; wait self-clear */

  IDMAC_PUTREG(priv, F1C100S_IDMAC_DMAC_OFFSET, IDMAC_DMAC_SWR);
  for (int t = 10000; t > 0 &&
       (IDMAC_GETREG(priv, F1C100S_IDMAC_DMAC_OFFSET) & IDMAC_DMAC_SWR); t--);

  /* Set descriptor list base address */

  IDMAC_PUTREG(priv, F1C100S_IDMAC_DLBA_OFFSET, (uint32_t)priv->dmadesc);

  /* In TX mode, IDMAC moves data into FIFO. Completion is signaled by
   * SDC_INT_DATA_OVER when the controller has finished sending the data
   * over the bus. Do not arm TI interrupt to avoid early completion.
   */

  IDMAC_PUTREG(priv, F1C100S_IDMAC_IDIE_OFFSET, 0);

  /* Clear stale IDMAC status */

  IDMAC_PUTREG(priv, F1C100S_IDMAC_IDST_OFFSET, 0x337);

  /* Enable IDMAC with fixed burst */

  IDMAC_PUTREG(priv, F1C100S_IDMAC_DMAC_OFFSET,
               IDMAC_DMAC_FB | IDMAC_DMAC_IDMAC_EN);

  /* Set block size and byte count */

  SDC_PUTREG(priv, SDC_BLKSZ_OFFSET, 512);
  SDC_PUTREG(priv, SDC_BCNTR_OFFSET, nbytes);

  return OK;
}
#endif /* CONFIG_F1C100S_SDIO_DMA */

/****************************************************************************
 * Card Detect Functions
 ****************************************************************************/

#ifdef CONFIG_F1C100S_SDIO_CARDDETECT

/****************************************************************************
 * Name: f1c100s_sdio_cd_worker
 *
 * Description:
 *   Work queue handler for card detect events
 *
 ****************************************************************************/

static void f1c100s_sdio_cd_worker(FAR void *arg)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)arg;
  bool value;
  bool present;
  int ret;

  /* Read current GPIO state */

  ret = ioctl(priv->cd_fd, GPIOC_READ, (unsigned long)&value);
  if (ret < 0)
    {
      mcerr("ERROR: Failed to read CD GPIO: %d\n", ret);
      return;
    }

  /* Determine card present state (consider inversion) */

#ifdef CONFIG_F1C100S_SDIO_CD_INVERTED
  present = value;   /* High = inserted */
#else
  present = !value;  /* Low = inserted (normal) */
#endif

  mcinfo("Card detect: GPIO=%d present=%d (was %d)\n",
         value, present, priv->cd_present);

  /* Check if state changed */

  if (present != priv->cd_present)
    {
      priv->cd_present = present;

      /* Call registered callback if event is enabled */

      if (priv->callback)
        {
          if (present && (priv->cbeventset & SDIOMEDIA_INSERTED))
            {
              mcinfo("Card inserted, calling callback\n");
              priv->callback(priv->cbarg);
            }
          else if (!present && (priv->cbeventset & SDIOMEDIA_EJECTED))
            {
              mcinfo("Card ejected, calling callback\n");
              priv->callback(priv->cbarg);
            }
        }
    }
}

/****************************************************************************
 * Name: f1c100s_sdio_cd_interrupt
 *
 * Description:
 *   GPIO interrupt handler for card detect
 *
 ****************************************************************************/

static int f1c100s_sdio_cd_interrupt(FAR struct gpio_dev_s *dev, uint8_t pin)
{
  FAR struct f1c100s_sdio_dev_s *priv = &g_sdiodev0;

  /* Schedule work to handle the card detect event */

  work_queue(HPWORK, &priv->cd_work, f1c100s_sdio_cd_worker, priv, 0);
  return OK;
}

/****************************************************************************
 * Name: f1c100s_sdio_cd_init
 *
 * Description:
 *   Initialize card detect GPIO
 *
 ****************************************************************************/

static int f1c100s_sdio_cd_init(FAR struct f1c100s_sdio_dev_s *priv)
{
  bool value;
  int ret;

  /* Open the GPIO device */

  priv->cd_fd = open(CONFIG_F1C100S_SDIO_CD_DEVPATH, O_RDWR);
  if (priv->cd_fd < 0)
    {
      ferr("ERROR: Failed to open CD GPIO %s: %d\n",
            CONFIG_F1C100S_SDIO_CD_DEVPATH, errno);
      return -errno;
    }

  /* Set pin type to interrupt on both edges */

  ret = ioctl(priv->cd_fd, GPIOC_SETPINTYPE,
              (unsigned long)GPIO_INTERRUPT_BOTH_PIN);
  if (ret < 0)
    {
      ferr("ERROR: Failed to set CD GPIO pin type: %d\n", ret);
      close(priv->cd_fd);
      priv->cd_fd = -1;
      return ret;
    }

  /* Register interrupt callback */

  ret = ioctl(priv->cd_fd, GPIOC_REGISTER,
              (unsigned long)f1c100s_sdio_cd_interrupt);
  if (ret < 0)
    {
      ferr("ERROR: Failed to register CD GPIO callback: %d\n", ret);
      close(priv->cd_fd);
      priv->cd_fd = -1;
      return ret;
    }

  /* Read initial state */

  ret = ioctl(priv->cd_fd, GPIOC_READ, (unsigned long)&value);
  if (ret < 0)
    {
      mcerr("ERROR: Failed to read initial CD GPIO state: %d\n", ret);
      close(priv->cd_fd);
      priv->cd_fd = -1;
      return ret;
    }

#ifdef CONFIG_F1C100S_SDIO_CD_INVERTED
  priv->cd_present = value;
#else
  priv->cd_present = !value;
#endif

  mcinfo("Card detect initialized: %s, present=%d\n",
         CONFIG_F1C100S_SDIO_CD_DEVPATH, priv->cd_present);

  return OK;
}

#endif /* CONFIG_F1C100S_SDIO_CARDDETECT */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct sdio_dev_s *f1c100s_sdio_initialize(int slotno)
{
  FAR struct f1c100s_sdio_dev_s *priv;
#ifdef CONFIG_F1C100S_SDIO_CARDDETECT
  int ret;
#endif

  mcinfo("SDIO init: step 1 - entry\n");

  if (slotno != 0)
    {
      mcerr("ERROR: Invalid slot: %d\n", slotno);
      return NULL;
    }

  priv = &g_sdiodev0;

  /* Check if already initialized */

  if (priv->initialized)
    {
      mcinfo("SDIO already initialized\n");
      return (FAR struct sdio_dev_s *)priv;
    }

  mcinfo("SDIO init: step 2 - copy ops\n");

  /* Copy ops structure */

  memcpy(&priv->dev, &g_sdio_ops, sizeof(struct sdio_dev_s));

  mcinfo("SDIO init: step 3 - hwsetup\n");

  /* Configure GPIO pins and SD/MMC clock */

  f1c100s_sdio_hwsetup();

  mcinfo("SDIO init: step 4 - clk enable\n");

  /* Enable SDC0 bus clock gating */

  f1c100s_clk_enable(CCU_BUS_CLK_GATING0, 8);

  mcinfo("SDIO init: step 5 - reset\n");

  /* Soft reset SDC0 */

  f1c100s_reset_sdc0();

  /* Wait for reset to complete */

  up_udelay(100);

  mcinfo("SDIO init: step 6 - sem protocol\n");

  /* Set semaphore protocol to priority inheritance
   * Note: waitsem is already initialized via SEM_INITIALIZER(0) in static data
   */

  nxsem_set_protocol(&priv->waitsem, SEM_PRIO_NONE);

#ifdef CONFIG_F1C100S_SDIO_DMA
  mcinfo("SDIO init: step 7 - dma desc\n");

  /* Allocate DMA descriptors from aligned buffer */

  priv->dmadesc = (struct f1c100s_idmac_desc_s *)priv->dmadesc_buf;
#endif

  /* Initialize watchdog timer - clear func to mark as inactive */

  priv->waitwdog.func = NULL;

  mcinfo("SDIO init: step 8 - hwreset\n");

  /* Initialize hardware */

  f1c100s_sdio_hwreset(priv);

#ifdef CONFIG_F1C100S_SDIO_CARDDETECT
  mcinfo("SDIO init: step 9 - card detect\n");

  /* Initialize card detect GPIO */

  priv->cd_fd = -1;
  priv->cd_present = false;  /* Assume no card until CD init succeeds */

  ret = f1c100s_sdio_cd_init(priv);
  if (ret < 0)
    {
      /* GPIO device not ready or failed to open - this is an error
       * when card detect is enabled. Report it but continue init.
       */

      ferr("ERROR: Card detect GPIO init failed: %d\n", ret);
      ferr("ERROR: Hot-plug will not work. Check GPIO device path: %s\n",
            CONFIG_F1C100S_SDIO_CD_DEVPATH);

      /* Set present to false so mmcsd_slotinitialize will skip probe */

      priv->cd_present = false;
    }
#endif

  mcinfo("SDIO init: step 10 - done\n");

  priv->initialized = true;

  mcinfo("SD/MMC driver initialized (interrupt + DMA mode)\n");

  return (FAR struct sdio_dev_s *)priv;
}

/****************************************************************************
 * Name: f1c100s_sdio_uninitialize
 *
 * Description:
 *   Uninitialize the SDIO driver
 *
 ****************************************************************************/

int f1c100s_sdio_uninitialize(FAR struct sdio_dev_s *dev)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  if (priv == NULL || !priv->initialized)
    {
      return -EINVAL;
    }

  /* Disable and detach interrupt */

  up_disable_irq(F1C_IRQ_MMC0);
  irq_detach(F1C_IRQ_MMC0);

  /* Cancel any pending watchdog */

  wd_cancel(&priv->waitwdog);

  /* Destroy semaphore */

  nxsem_destroy(&priv->waitsem);

  /* Reset hardware */

  f1c100s_sdio_hwreset(priv);

  /* Clear initialized flag */

  priv->initialized = false;

#ifdef CONFIG_F1C100S_SDIO_CARDDETECT
  /* Close card detect GPIO */

  if (priv->cd_fd >= 0)
    {
      close(priv->cd_fd);
      priv->cd_fd = -1;
    }
#endif

  mcinfo("SDIO uninitialized\n");

  return OK;
}

#ifdef CONFIG_F1C100S_SDIO_CARDDETECT
/****************************************************************************
 * Name: f1c100s_sdio_carddetect_init
 *
 * Description:
 *   Initialize card detect GPIO. Call this after GPIO device is ready.
 *
 ****************************************************************************/

int f1c100s_sdio_carddetect_init(FAR struct sdio_dev_s *dev)
{
  FAR struct f1c100s_sdio_dev_s *priv = (FAR struct f1c100s_sdio_dev_s *)dev;

  if (priv == NULL || !priv->initialized)
    {
      return -EINVAL;
    }

  return f1c100s_sdio_cd_init(priv);
}
#endif
