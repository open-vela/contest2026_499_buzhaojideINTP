/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_audio.c
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
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/audio/audio.h>
#include <nuttx/queue.h>

#include "arm_internal.h"
#include "chip.h"
#include "hardware/f1c100s_audio.h"
#include "hardware/f1c100s_ccu.h"
#include "f1c100s_softreset.h"
#include "hardware/f1c100s_dma.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define AUDIO_CODEC_BASE 0x01c23c00

/* Register access helpers */
#define AUDIO_GETREG(o)     getreg32(AUDIO_CODEC_BASE + (o))
#define AUDIO_PUTREG(o,v)   putreg32((v), AUDIO_CODEC_BASE + (o))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct f1c100s_audio_dev_s
{
  struct audio_lowerhalf_s dev; /* Generic audio interface */

  /* State data */

  bool initialized;
  bool dac_running;
  bool adc_running;
  uint8_t mode;                 /* Current mode: AUDIO_TYPE_INPUT or AUDIO_TYPE_OUTPUT */
  uint32_t samplerate;
  uint8_t channels;
  uint8_t bps;

  /* DMA buffers */
  DMA_HANDLE dma_dac;           /* DMA handle for DAC playback */
  DMA_HANDLE dma_adc;           /* DMA handle for ADC recording */

  /* DAC (playback) buffer queues */
  struct dq_queue_s doneq;      /* Queue of completed buffers */
  struct dq_queue_s pendq;      /* Queue of pending buffers */

  /* ADC (recording) buffer queues */
  struct dq_queue_s adc_pendq;  /* Queue of pending ADC buffers */
  struct dq_queue_s adc_doneq;  /* Queue of completed ADC buffers */

  /* Interrupt handling */
  sem_t exclsem;                /* Mutual exclusion */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Audio Lower Half Interface */

static int f1c100s_audio_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                                 FAR struct audio_caps_s *caps);
static int f1c100s_audio_configure(FAR struct audio_lowerhalf_s *dev,
                                   FAR const struct audio_caps_s *caps);
static int f1c100s_audio_shutdown(FAR struct audio_lowerhalf_s *dev);
static int f1c100s_audio_start(FAR struct audio_lowerhalf_s *dev);
static int f1c100s_audio_stop(FAR struct audio_lowerhalf_s *dev);
static int f1c100s_audio_pause(FAR struct audio_lowerhalf_s *dev);
static int f1c100s_audio_resume(FAR struct audio_lowerhalf_s *dev);
static int f1c100s_audio_allocbuffer(FAR struct audio_lowerhalf_s *dev,
                                     FAR struct audio_buf_desc_s *apb);
static int f1c100s_audio_freebuffer(FAR struct audio_lowerhalf_s *dev,
                                    FAR struct audio_buf_desc_s *apb);
static int f1c100s_audio_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                       FAR struct ap_buffer_s *apb);
static int f1c100s_audio_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                                      FAR struct ap_buffer_s *apb);
static int f1c100s_audio_ioctl(FAR struct audio_lowerhalf_s *dev,
                               int cmd, unsigned long arg);
static int f1c100s_audio_reserve(FAR struct audio_lowerhalf_s *dev);
static int f1c100s_audio_release(FAR struct audio_lowerhalf_s *dev);

/* Hardware control */
static void f1c100s_audio_hwreset(FAR struct f1c100s_audio_dev_s *priv);
static int f1c100s_audio_interrupt(int irq, void *context, void *arg);
static void f1c100s_audio_start_dma(FAR struct f1c100s_audio_dev_s *priv,
                                     FAR struct ap_buffer_s *apb);
static void f1c100s_audio_start_adc_dma(FAR struct f1c100s_audio_dev_s *priv,
                                         FAR struct ap_buffer_s *apb);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct audio_ops_s g_audio_ops =
{
  .getcaps        = f1c100s_audio_getcaps,
  .configure      = f1c100s_audio_configure,
  .shutdown       = f1c100s_audio_shutdown,
  .start          = f1c100s_audio_start,
  .stop           = f1c100s_audio_stop,
  .pause          = f1c100s_audio_pause,
  .resume         = f1c100s_audio_resume,
  .allocbuffer    = f1c100s_audio_allocbuffer,
  .freebuffer     = f1c100s_audio_freebuffer,
  .enqueuebuffer  = f1c100s_audio_enqueuebuffer,
  .cancelbuffer   = f1c100s_audio_cancelbuffer,
  .ioctl          = f1c100s_audio_ioctl,
  .read           = NULL,
  .write          = NULL,
  .reserve        = f1c100s_audio_reserve,
  .release        = f1c100s_audio_release,
};

static struct f1c100s_audio_dev_s g_audiodev =
{
  .dev = { .ops = &g_audio_ops },
  .dac_running = false,
  .adc_running = false,
  .samplerate = 48000,
  .channels = 2,
  .bps = 16,
  .exclsem = SEM_INITIALIZER(1),
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void f1c100s_audio_hwreset(FAR struct f1c100s_audio_dev_s *priv)
{
  /* Reset DAC */
  AUDIO_PUTREG(F1C100S_DAC_DPC_OFFSET, 0);
  AUDIO_PUTREG(F1C100S_DAC_FIFOC_OFFSET, DAC_FIFOC_FIFO_FLUSH);

  /* Reset ADC */
  AUDIO_PUTREG(F1C100S_ADC_FIFOC_OFFSET, ADC_FIFOC_FIFO_FLUSH);

  /* Power-on performance tuning and calibration per F1C100s UM p.170-172 */
  AUDIO_PUTREG(F1C100S_ADDA_TUNE_OFFSET, ADDA_TUNE_MAGIC);
  AUDIO_PUTREG(F1C100S_BIAS_CAL_CTRL0_OFFSET, BIAS_CAL_CTRL0_MAGIC);
  AUDIO_PUTREG(F1C100S_BIAS_CAL_CTRL1_OFFSET, BIAS_CAL_CTRL1_MAGIC);

  /* Configure DAC mixer for playback per UM Section 4.3.8.8 (p.167-168):
   * - Enable Right & Left Analog DACs (DACAREN, DACALEN)
   * - Enable Right & Left Analog Mixers (RMIXEN, LMIXEN)
   * - Unmute Right & Left Headphone PA (RHPPAMUTE, LHPPAMUTE)
   * - Route DAC directly to HP PA (RHPIS=0, LHPIS=0)
   * - Unmute DAC into mixers (RMIXMUTE_RDAC, LMIXMUTE_LDAC)
   * - Enable Headphone PA (HPPAEN)
   * - Set initial headphone volume to -4dB (0x3b)
   * - Drive HPCOM direct (HPCOM_FC=3) with output protection (COMPTEN):
   *   this board's CN1 jack ties the headphone ground (HPCOM, Pin 3) to
   *   the chip's capless HP common-mode driver, confirmed against the
   *   schematic (HPCOM -> R17 -> HPCOMFB feedback loop). Leaving
   *   HPCOM_FC at its reset default (0 = floating) leaves that ground
   *   reference undriven -- audio would be silent or near-silent even
   *   though every other mixer bit is correct.
   */
  AUDIO_PUTREG(F1C100S_DAC_MIXER_CTRL_OFFSET,
               DAC_MIXER_DACAREN | DAC_MIXER_DACALEN |
               DAC_MIXER_RMIXEN | DAC_MIXER_LMIXEN |
               DAC_MIXER_RHPPAMUTE | DAC_MIXER_LHPPAMUTE |
               DAC_MIXER_RMIXMUTE_RDAC | DAC_MIXER_LMIXMUTE_LDAC |
               DAC_MIXER_HPPAEN | DAC_MIXER_HPVOL(0x3b) |
               DAC_MIXER_HPCOM_FC(3) | DAC_MIXER_COMPTEN);
}

/****************************************************************************
 * Name: f1c100s_audio_dma_callback
 ****************************************************************************/

static void f1c100s_audio_dma_callback(void *arg, int result)
{
  FAR struct f1c100s_audio_dev_s *priv = (FAR struct f1c100s_audio_dev_s *)arg;
  FAR struct ap_buffer_s *apb;
  FAR struct ap_buffer_s *next_apb;
  irqstate_t flags;
  int status = OK;
  bool running;

  /* Check DMA result */

  if (result != DMA_RESULT_OK)
    {
      auderr("ERROR: DMA transfer failed: %d\n", result);
      status = -EIO;
    }

  /* Critical section - protect queue access and running flag check */

  flags = enter_critical_section();

  /* Check running flag inside critical section to prevent race with stop() */

  running = priv->dac_running;
  if (!running)
    {
      leave_critical_section(flags);
      return;
    }

  /* Get completed buffer from pending queue */

  apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->pendq);

  /* Get next buffer while still in critical section */

  next_apb = (FAR struct ap_buffer_s *)dq_peek(&priv->pendq);

  leave_critical_section(flags);

  if (apb)
    {
      /* Notify upper layer - upper layer owns the buffer now */

      if (priv->dev.upper)
        {
          priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, status);
        }
    }

  /* Start next buffer if available and no error */

  if (status == OK && running && next_apb)
    {
      f1c100s_audio_start_dma(priv, next_apb);
    }
}

/****************************************************************************
 * Name: f1c100s_audio_start_dma
 ****************************************************************************/

static void f1c100s_audio_start_dma(FAR struct f1c100s_audio_dev_s *priv,
                                     FAR struct ap_buffer_s *apb)
{
  uint32_t src;
  uint32_t dst;
  uint32_t cfg;

  /* Source: audio buffer in memory */
  src = (uint32_t)apb->samp;

  /* Destination: DAC TX FIFO */
  dst = AUDIO_CODEC_BASE + F1C100S_DAC_TXDATA_OFFSET;

  /* DMA configuration: Memory to IO, 16-bit width */
  cfg = NDMA_CFG_SRC_DRQ_TYPE(DMA_DRQ_SDRAM) |
        NDMA_CFG_SRC_WIDTH_16 |
        NDMA_CFG_SRC_BURST_4 |
        NDMA_CFG_DST_DRQ_TYPE(AUDIO_DMA_CHAN_DAC) |
        NDMA_CFG_DST_MODE_IO |
        NDMA_CFG_DST_WIDTH_16 |
        NDMA_CFG_DST_BURST_4;

  /* Flush cache before DMA */
  up_clean_dcache((uintptr_t)apb->samp, (uintptr_t)apb->samp + apb->nbytes);

  /* Setup and start DMA transfer */
  f1c100s_dma_setup(priv->dma_dac, src, dst, apb->nbytes, cfg);
  f1c100s_dma_start(priv->dma_dac, f1c100s_audio_dma_callback, priv);
}

/****************************************************************************
 * Name: f1c100s_audio_adc_dma_callback
 ****************************************************************************/

static void f1c100s_audio_adc_dma_callback(void *arg, int result)
{
  FAR struct f1c100s_audio_dev_s *priv = (FAR struct f1c100s_audio_dev_s *)arg;
  FAR struct ap_buffer_s *apb;
  FAR struct ap_buffer_s *next_apb;
  irqstate_t flags;
  int status = OK;
  bool running;

  if (result != DMA_RESULT_OK)
    {
      auderr("ERROR: ADC DMA transfer failed: %d\n", result);
      status = -EIO;
    }

  /* Critical section - protect queue access and running flag check */

  flags = enter_critical_section();

  /* Check running flag inside critical section to prevent race with stop() */

  running = priv->adc_running;

  if (!running)
    {
      leave_critical_section(flags);
      return;
    }

  /* Get completed buffer from ADC pending queue */

  apb = (FAR struct ap_buffer_s *)dq_remfirst(&priv->adc_pendq);

  /* Get next buffer while still in critical section */

  next_apb = (FAR struct ap_buffer_s *)dq_peek(&priv->adc_pendq);

  leave_critical_section(flags);

  if (apb)
    {
      /* Cache was already invalidated before DMA started in
       * f1c100s_audio_start_adc_dma(), so no need to invalidate here.
       * This avoids corrupting adjacent memory allocations.
       */

      /* Set actual bytes received */

      apb->nbytes = apb->nmaxbytes;

      /* Notify upper layer - upper layer owns the buffer now */

      if (priv->dev.upper)
        {
          priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, status);
        }
    }

  /* Start next buffer if available and no error */

  if (status == OK && running && next_apb)
    {
      f1c100s_audio_start_adc_dma(priv, next_apb);
    }
}

/****************************************************************************
 * Name: f1c100s_audio_start_adc_dma
 ****************************************************************************/

static void f1c100s_audio_start_adc_dma(FAR struct f1c100s_audio_dev_s *priv,
                                         FAR struct ap_buffer_s *apb)
{
  uint32_t src;
  uint32_t dst;
  uint32_t cfg;

  /* Validate buffer */

  if (!apb || !apb->samp || apb->nmaxbytes == 0)
    {
      return;
    }

  audinfo("Start ADC DMA: %p, %d bytes\n", apb->samp, apb->nmaxbytes);

  /* Invalidate cache BEFORE DMA transfer starts.
   * This ensures any stale data in cache won't overwrite DMA-written data.
   * Using static buffers avoids heap corruption issues.
   */

  up_invalidate_dcache((uintptr_t)apb->samp,
                       (uintptr_t)apb->samp + apb->nmaxbytes);

  /* Source: ADC RX FIFO */

  src = AUDIO_CODEC_BASE + F1C100S_ADC_RXDATA_OFFSET;

  /* Destination: audio buffer in memory */

  dst = (uint32_t)apb->samp;

  /* DMA configuration: IO to Memory, 16-bit width */

  cfg = NDMA_CFG_SRC_DRQ_TYPE(AUDIO_DMA_CHAN_ADC) |
        NDMA_CFG_SRC_MODE_IO |
        NDMA_CFG_SRC_WIDTH_16 |
        NDMA_CFG_SRC_BURST_4 |
        NDMA_CFG_DST_DRQ_TYPE(DMA_DRQ_SDRAM) |
        NDMA_CFG_DST_WIDTH_16 |
        NDMA_CFG_DST_BURST_4;

  /* Setup and start DMA transfer */

  f1c100s_dma_setup(priv->dma_adc, src, dst, apb->nmaxbytes, cfg);
  f1c100s_dma_start(priv->dma_adc, f1c100s_audio_adc_dma_callback, priv);
}

/****************************************************************************
 * Name: f1c100s_audio_interrupt
 ****************************************************************************/

static int f1c100s_audio_interrupt(int irq, void *context, void *arg)
{
  uint32_t dac_fifos;
  uint32_t adc_fifos;
  uint32_t fifoc;

  UNUSED(irq);
  UNUSED(context);
  UNUSED(arg);

  /* Read FIFO status */
  dac_fifos = AUDIO_GETREG(F1C100S_DAC_FIFOS_OFFSET);
  adc_fifos = AUDIO_GETREG(F1C100S_ADC_FIFOS_OFFSET);

  /* Handle DAC FIFO underrun */
  if (dac_fifos & DAC_FIFOS_TXE_INT)
    {
      audwarn("WARN: DAC FIFO underrun\n");

      /* Clear interrupt by writing 1 to pending bit */
      AUDIO_PUTREG(F1C100S_DAC_FIFOS_OFFSET, DAC_FIFOS_TXE_INT);

      /* Flush FIFO to clear underrun */
      fifoc = AUDIO_GETREG(F1C100S_DAC_FIFOC_OFFSET);
      fifoc |= DAC_FIFOC_FIFO_FLUSH;
      AUDIO_PUTREG(F1C100S_DAC_FIFOC_OFFSET, fifoc);
    }

  /* Handle ADC data available - clear interrupt
   * Note: ADC recording uses DMA mode, so we just clear the interrupt here.
   * The actual data transfer is handled by DMA callback.
   */
  if (adc_fifos & ADC_FIFOS_RXA_INT)
    {
      AUDIO_PUTREG(F1C100S_ADC_FIFOS_OFFSET, ADC_FIFOS_RXA_INT);
    }

  return OK;
}

static int f1c100s_audio_getcaps(FAR struct audio_lowerhalf_s *dev, int type,
                                 FAR struct audio_caps_s *caps)
{

  /* Validate the structure */

  UNUSED(dev);

  if (caps == NULL || caps->ac_len < sizeof(struct audio_caps_s))
    {
      return -EINVAL;
    }

  caps->ac_format.b[0] = 0;
  caps->ac_format.b[1] = 0;
  caps->ac_controls.w = 0;

  switch (type)
    {
      case AUDIO_TYPE_QUERY:
        /* Report supported device types */
        caps->ac_controls.b[0] = AUDIO_TYPE_OUTPUT | AUDIO_TYPE_INPUT |
                                  AUDIO_TYPE_FEATURE;
        /* Report supported formats for nxrecorder/nxplayer device detection */
        caps->ac_format.hw = (1 << (AUDIO_FMT_PCM - 1));
        break;

      case AUDIO_TYPE_OUTPUT:
        /* Report supported output formats */
        caps->ac_format.hw = (1 << (AUDIO_FMT_PCM - 1));
        caps->ac_channels = (1 << 4) | 2; /* Min 1, Max 2 channels */
        caps->ac_controls.w = AUDIO_SAMP_RATE_8K | AUDIO_SAMP_RATE_16K |
                               AUDIO_SAMP_RATE_32K | AUDIO_SAMP_RATE_48K |
                               AUDIO_SAMP_RATE_96K | AUDIO_SAMP_RATE_192K;
        break;

      case AUDIO_TYPE_INPUT:
        /* Report supported input formats */
        caps->ac_format.hw = (1 << (AUDIO_FMT_PCM - 1));
        caps->ac_channels = (1 << 4) | 2; /* Min 1, Max 2 channels */
        caps->ac_controls.w = AUDIO_SAMP_RATE_8K | AUDIO_SAMP_RATE_16K |
                               AUDIO_SAMP_RATE_32K | AUDIO_SAMP_RATE_48K;
        break;

      case AUDIO_TYPE_FEATURE:
        /* Report supported features */
        caps->ac_controls.b[0] = AUDIO_FU_VOLUME | AUDIO_FU_MUTE;
        break;

      default:
        caps->ac_controls.b[0] = 0;
        break;
    }

  return caps->ac_len;
}

static uint32_t f1c100s_get_samplerate_cfg(uint32_t samplerate)
{
  switch (samplerate)
    {
      case 8000:   return DAC_FIFOC_FS_8K;
      case 12000:  return DAC_FIFOC_FS_12K;
      case 16000:  return DAC_FIFOC_FS_16K;
      case 24000:  return DAC_FIFOC_FS_24K;
      case 32000:  return DAC_FIFOC_FS_32K;
      case 48000:  return DAC_FIFOC_FS_48K;
      case 96000:  return DAC_FIFOC_FS_96K;
      case 192000: return DAC_FIFOC_FS_192K;
      default:     return DAC_FIFOC_FS_48K;
    }
}

static int f1c100s_audio_configure(FAR struct audio_lowerhalf_s *dev,
                                   FAR const struct audio_caps_s *caps)
{
  FAR struct f1c100s_audio_dev_s *priv = (FAR struct f1c100s_audio_dev_s *)dev;
  int ret = OK;


  switch (caps->ac_type)
    {
      case AUDIO_TYPE_OUTPUT:
        /* Configure DAC output format */
        priv->mode = AUDIO_TYPE_OUTPUT;
        if (caps->ac_subtype == AUDIO_FMT_PCM)
          {
            uint32_t fifoc = AUDIO_GETREG(F1C100S_DAC_FIFOC_OFFSET);

            /* Clear sample rate bits */
            fifoc &= ~DAC_FIFOC_FS_MASK;

            /* Set new sample rate */
            fifoc |= f1c100s_get_samplerate_cfg(priv->samplerate);

            /* Set FIFO mode and sample bits */
            fifoc &= ~DAC_FIFOC_FIFO_MODE_MASK;
            if (priv->bps == 16)
              {
                fifoc |= DAC_FIFOC_FIFO_MODE_16_15_0;
                fifoc &= ~DAC_FIFOC_TX_SAMPLE_BITS;
              }
            else /* 24-bit */
              {
                fifoc |= DAC_FIFOC_FIFO_MODE_24_31_8;
                fifoc |= DAC_FIFOC_TX_SAMPLE_BITS;
              }

            /* Set mono/stereo */
            if (priv->channels == 1)
              {
                fifoc |= DAC_FIFOC_MONO_EN;
              }
            else
              {
                fifoc &= ~DAC_FIFOC_MONO_EN;
              }

            /* Set trigger level and enable DMA request */
            fifoc &= ~DAC_FIFOC_TX_TRI_LEVEL_MASK;
            fifoc |= DAC_FIFOC_TX_TRI_LEVEL(64);
            fifoc |= DAC_FIFOC_DAC_DRQ_EN;

            AUDIO_PUTREG(F1C100S_DAC_FIFOC_OFFSET, fifoc);
          }
        break;

      case AUDIO_TYPE_INPUT:
        /* Configure ADC input format */
        priv->mode = AUDIO_TYPE_INPUT;
        if (caps->ac_subtype == AUDIO_FMT_PCM)
          {
            uint32_t fifoc = AUDIO_GETREG(F1C100S_ADC_FIFOC_OFFSET);

            /* Clear and set sample rate */
            fifoc &= ~ADC_FIFOC_FS_MASK;
            fifoc |= f1c100s_get_samplerate_cfg(priv->samplerate);

            /* Set sample bits */
            if (priv->bps == 16)
              {
                fifoc &= ~ADC_FIFOC_RX_SAMPLE_BITS;
              }
            else
              {
                fifoc |= ADC_FIFOC_RX_SAMPLE_BITS;
              }

            /* Set mono/stereo */
            if (priv->channels == 1)
              {
                fifoc |= ADC_FIFOC_MONO_EN;
              }
            else
              {
                fifoc &= ~ADC_FIFOC_MONO_EN;
              }

            /* Set trigger level and enable DMA request */
            fifoc &= ~ADC_FIFOC_RX_TRI_LEVEL_MASK;
            fifoc |= ADC_FIFOC_RX_TRI_LEVEL(16);
            fifoc |= ADC_FIFOC_ADC_DRQ_EN;

            AUDIO_PUTREG(F1C100S_ADC_FIFOC_OFFSET, fifoc);

            /* Enable ADC analog and input mixer per F1C100s UM 4.3.8.9 (p.169).
             *
             * LINEIN_EN dropped: this board's schematic shows LINL/LINR
             * unconnected (no line-in jack), so leaving that mixer input
             * live just invites floating-pin noise into the ADC.
             *
             * MICBOOST(4) added: this is a full-register write, not a
             * read-modify-write, so it zeroes MICBOOST's bits[2:0] to
             * 0dB regardless of the POR default. UM 4.3.8.9 documents
             * the POR default as 0x4 = 33dB (0dB at 000, 24-42dB at
             * 001-111 in 3dB steps) -- restore that default explicitly
             * instead of silently dropping to 0dB, or MIC input is
             * effectively inaudible.
             */
            AUDIO_PUTREG(F1C100S_ADC_MIXER_CTRL_OFFSET,
                         ADC_MIXER_ADCEN | ADC_MIXER_MIC1_EN |
                         ADC_MIXER_MICBOOST(4));
          }
        break;

      case AUDIO_TYPE_FEATURE:
        /* Configure features (volume, mute) */
        switch (caps->ac_format.hw)
          {
            case AUDIO_FU_VOLUME:
              {
                uint16_t volume = caps->ac_controls.hw[0];
                uint32_t dpc = AUDIO_GETREG(F1C100S_DAC_DPC_OFFSET);

                /* Map 0-1000 to 0-63 hardware volume */
                uint8_t hwvol = (volume * 63) / 1000;

                dpc &= ~DAC_DPC_DVOL_MASK;
                dpc |= DAC_DPC_DVOL(hwvol);

                AUDIO_PUTREG(F1C100S_DAC_DPC_OFFSET, dpc);
              }
              break;

            default:
              break;
          }
        break;

      default:
        ret = -ENOTTY;
        break;
    }

  return ret;
}

static int f1c100s_audio_shutdown(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct f1c100s_audio_dev_s *priv = (FAR struct f1c100s_audio_dev_s *)dev;


  /* Disable DAC and ADC */
  uint32_t val = AUDIO_GETREG(F1C100S_DAC_DPC_OFFSET);
  val &= ~DAC_DPC_EN_DA;
  AUDIO_PUTREG(F1C100S_DAC_DPC_OFFSET, val);

  val = AUDIO_GETREG(F1C100S_ADC_FIFOC_OFFSET);
  val &= ~ADC_FIFOC_EN_AD;
  AUDIO_PUTREG(F1C100S_ADC_FIFOC_OFFSET, val);

  priv->dac_running = false;
  priv->adc_running = false;

  return OK;
}

static int f1c100s_audio_start(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct f1c100s_audio_dev_s *priv = (FAR struct f1c100s_audio_dev_s *)dev;
  FAR struct ap_buffer_s *apb;
  uint32_t val;

  if (priv->mode == AUDIO_TYPE_OUTPUT)
    {
      /* Enable DAC for playback */

      val = AUDIO_GETREG(F1C100S_DAC_DPC_OFFSET);
      val |= DAC_DPC_EN_DA;
      AUDIO_PUTREG(F1C100S_DAC_DPC_OFFSET, val);

      /* Flush DAC FIFO */

      val = AUDIO_GETREG(F1C100S_DAC_FIFOC_OFFSET);
      val |= DAC_FIFOC_FIFO_FLUSH;
      AUDIO_PUTREG(F1C100S_DAC_FIFOC_OFFSET, val);

      priv->dac_running = true;
    }
  else if (priv->mode == AUDIO_TYPE_INPUT)
    {
      /* Enable ADC for recording */

      val = AUDIO_GETREG(F1C100S_ADC_FIFOC_OFFSET);
      val |= ADC_FIFOC_EN_AD;
      AUDIO_PUTREG(F1C100S_ADC_FIFOC_OFFSET, val);

      priv->adc_running = true;

      /* Start first ADC DMA transfer if buffers are pending */

      apb = (FAR struct ap_buffer_s *)dq_peek(&priv->adc_pendq);
      if (apb && priv->dma_adc)
        {
          f1c100s_audio_start_adc_dma(priv, apb);
        }
    }

  return OK;
}

static int f1c100s_audio_stop(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct f1c100s_audio_dev_s *priv = (FAR struct f1c100s_audio_dev_s *)dev;
  uint32_t val;
  irqstate_t flags;

  /* IMPORTANT: Stop DMA first to prevent any new interrupts */

  if (priv->dma_dac)
    {
      f1c100s_dma_stop(priv->dma_dac);
    }

  if (priv->dma_adc)
    {
      f1c100s_dma_stop(priv->dma_adc);
    }

  /* Now mark as not running - any pending callbacks will see this and exit */

  flags = enter_critical_section();
  priv->dac_running = false;
  priv->adc_running = false;

  /* Disable DAC */

  val = AUDIO_GETREG(F1C100S_DAC_DPC_OFFSET);
  val &= ~DAC_DPC_EN_DA;
  AUDIO_PUTREG(F1C100S_DAC_DPC_OFFSET, val);

  /* Disable ADC */

  val = AUDIO_GETREG(F1C100S_ADC_FIFOC_OFFSET);
  val &= ~ADC_FIFOC_EN_AD;
  AUDIO_PUTREG(F1C100S_ADC_FIFOC_OFFSET, val);

  /* Clear queues - DO NOT return buffers here!
   * Buffers may have already been returned by DMA callbacks and
   * re-enqueued by upper layer. Returning them again causes double-free.
   * Upper layer will handle buffer cleanup on AUDIO_CALLBACK_COMPLETE.
   */

  dq_init(&priv->pendq);
  dq_init(&priv->adc_pendq);

  leave_critical_section(flags);

  /* Notify upper layer that stop is complete */

  if (priv->dev.upper)
    {
      priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_COMPLETE, NULL, OK);
    }

  return OK;
}

static int f1c100s_audio_pause(FAR struct audio_lowerhalf_s *dev)
{
  return f1c100s_audio_stop(dev);
}

static int f1c100s_audio_resume(FAR struct audio_lowerhalf_s *dev)
{
  return f1c100s_audio_start(dev);
}

/* Static buffers for DMA - avoid heap allocation issues */
static uint8_t g_audio_buffer1[8192 + 128] __attribute__((aligned(32)));
static uint8_t g_audio_buffer2[8192 + 128] __attribute__((aligned(32)));
static bool g_buffer1_used = false;
static bool g_buffer2_used = false;

static int f1c100s_audio_allocbuffer(FAR struct audio_lowerhalf_s *dev,
                                     FAR struct audio_buf_desc_s *bufdesc)
{
  FAR struct ap_buffer_s *apb;
  FAR uint8_t *samp;

  DEBUGASSERT(bufdesc->u.pbuffer != NULL);

  /* Use static buffers to avoid heap corruption issues with DMA */

  apb = kumm_zalloc(sizeof(struct ap_buffer_s));
  if (apb == NULL)
    {
      return -ENOMEM;
    }

  /* Assign a static buffer */

  if (!g_buffer1_used)
    {
      samp = g_audio_buffer1 + 64;  /* Skip front padding */
      g_buffer1_used = true;
      auderr("allocbuffer: using buffer1, samp=%p (buffer1=%p)\n",
             samp, g_audio_buffer1);
    }
  else if (!g_buffer2_used)
    {
      samp = g_audio_buffer2 + 64;  /* Skip front padding */
      g_buffer2_used = true;
      auderr("allocbuffer: using buffer2, samp=%p (buffer2=%p)\n",
             samp, g_audio_buffer2);
    }
  else
    {
      kumm_free(apb);
      return -ENOMEM;
    }

  /* Initialize the buffer */

  apb->i.channels = 1;
  apb->crefs      = 1;
  apb->nmaxbytes  = bufdesc->numbytes;
  apb->nbytes     = 0;
  apb->flags      = 0;
  apb->samp       = samp;

  nxmutex_init(&apb->lock);

  *bufdesc->u.pbuffer = apb;
  return sizeof(struct audio_buf_desc_s);
}

static int f1c100s_audio_freebuffer(FAR struct audio_lowerhalf_s *dev,
                                    FAR struct audio_buf_desc_s *bufdesc)
{
  FAR struct ap_buffer_s *apb = bufdesc->u.buffer;

  if (apb)
    {
      /* Mark static buffer as unused */

      if (apb->samp == g_audio_buffer1 + 64)
        {
          g_buffer1_used = false;
        }
      else if (apb->samp == g_audio_buffer2 + 64)
        {
          g_buffer2_used = false;
        }

      /* Free the apb structure */

      nxmutex_destroy(&apb->lock);
      kumm_free(apb);
    }
  return OK;
}

static void f1c100s_audio_pio_transfer(FAR struct f1c100s_audio_dev_s *priv,
                                        FAR struct ap_buffer_s *apb)
{
  FAR uint16_t *src = (FAR uint16_t *)apb->samp;
  uint32_t samples = apb->nbytes / sizeof(uint16_t);
  uint32_t fifos;
  uint32_t i;
  int timeout;


  for (i = 0; i < samples; i++)
    {
      /* Wait for FIFO space (TXE_CNT > 0) with timeout */

      timeout = 10000;
      do
        {
          fifos = AUDIO_GETREG(F1C100S_DAC_FIFOS_OFFSET);
          if (--timeout <= 0)
            {
              auderr("ERROR: FIFO wait timeout at sample %lu\n",
                     (unsigned long)i);
              return;
            }
        }
      while ((fifos & DAC_FIFOS_TXE_CNT_MASK) == 0);

      /* Write sample to TX FIFO */

      AUDIO_PUTREG(F1C100S_DAC_TXDATA_OFFSET, src[i]);
    }
}

/****************************************************************************
 * Name: f1c100s_audio_pio_record
 ****************************************************************************/

static void f1c100s_audio_pio_record(FAR struct f1c100s_audio_dev_s *priv,
                                      FAR struct ap_buffer_s *apb)
{
  FAR uint16_t *dst = (FAR uint16_t *)apb->samp;
  uint32_t samples = apb->nmaxbytes / sizeof(uint16_t);
  uint32_t fifos;
  uint32_t i;
  int timeout;


  for (i = 0; i < samples; i++)
    {
      /* Wait for FIFO data (RXA_CNT > 0) with timeout */

      timeout = 10000;
      do
        {
          fifos = AUDIO_GETREG(F1C100S_ADC_FIFOS_OFFSET);
          if (--timeout <= 0)
            {
              auderr("ERROR: ADC FIFO wait timeout at sample %lu\n",
                     (unsigned long)i);
              apb->nbytes = i * sizeof(uint16_t);
              return;
            }
        }
      while ((fifos & ADC_FIFOS_RXA_CNT_MASK) == 0);

      /* Read sample from RX FIFO */

      dst[i] = (uint16_t)AUDIO_GETREG(F1C100S_ADC_RXDATA_OFFSET);
    }

  apb->nbytes = samples * sizeof(uint16_t);
}

static int f1c100s_audio_enqueuebuffer(FAR struct audio_lowerhalf_s *dev,
                                       FAR struct ap_buffer_s *apb)
{
  FAR struct f1c100s_audio_dev_s *priv = (FAR struct f1c100s_audio_dev_s *)dev;
  irqstate_t flags;
  bool start_dma = false;

  audinfo("Enqueue buffer: %p, nbytes=%d\n", apb, apb->nbytes);

  /* Check if this is a recording buffer based on configured mode */

  if (priv->mode == AUDIO_TYPE_INPUT)
    {
      /* Recording mode - add to ADC queue */

      if (priv->dma_adc)
        {
          /* Critical section - protect queue access */

          flags = enter_critical_section();
          start_dma = (dq_count(&priv->adc_pendq) == 0) && priv->adc_running;
          dq_addlast(&apb->dq_entry, &priv->adc_pendq);
          leave_critical_section(flags);

          /* Start DMA if this is the first buffer and ADC is running */

          if (start_dma)
            {
              f1c100s_audio_start_adc_dma(priv, apb);
            }
        }
      else
        {
          /* PIO mode fallback */

          f1c100s_audio_pio_record(priv, apb);

          /* Notify completion immediately */

          if (priv->dev.upper)
            {
              priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
            }
        }
    }
  else
    {
      /* Playback mode - add to DAC queue */

      if (priv->dma_dac)
        {
          /* DMA mode - critical section for queue access */

          flags = enter_critical_section();
          start_dma = (dq_count(&priv->pendq) == 0) && priv->dac_running;
          dq_addlast(&apb->dq_entry, &priv->pendq);
          leave_critical_section(flags);

          if (start_dma)
            {
              f1c100s_audio_start_dma(priv, apb);
            }
        }
      else
        {
          /* PIO mode fallback */

          f1c100s_audio_pio_transfer(priv, apb);

          /* Notify completion immediately */

          if (priv->dev.upper)
            {
              priv->dev.upper(priv->dev.priv, AUDIO_CALLBACK_DEQUEUE, apb, OK);
            }
        }
    }

  return OK;
}

static int f1c100s_audio_cancelbuffer(FAR struct audio_lowerhalf_s *dev,
                                      FAR struct ap_buffer_s *apb)
{
  return OK;
}

static int f1c100s_audio_ioctl(FAR struct audio_lowerhalf_s *dev,
                               int cmd, unsigned long arg)
{
  return -ENOTTY;
}

static int f1c100s_audio_reserve(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct f1c100s_audio_dev_s *priv = (FAR struct f1c100s_audio_dev_s *)dev;
  return nxsem_wait_uninterruptible(&priv->exclsem);
}

static int f1c100s_audio_release(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct f1c100s_audio_dev_s *priv = (FAR struct f1c100s_audio_dev_s *)dev;
  return nxsem_post(&priv->exclsem);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_audio_initialize
 ****************************************************************************/

FAR struct audio_lowerhalf_s *f1c100s_audio_initialize(void)
{
  FAR struct f1c100s_audio_dev_s *priv = &g_audiodev;
  irqstate_t flags;
  int timeout;
  int ret;

  /* Initialize queues */

  dq_init(&priv->doneq);
  dq_init(&priv->pendq);
  dq_init(&priv->adc_doneq);
  dq_init(&priv->adc_pendq);

  /* Check if already initialized */

  if (priv->initialized)
    {
      audinfo("Audio already initialized\n");
      return &priv->dev;
    }

  /* Enable PLL_AUDIO (CCU 0x008) per F1C100s UM 3.2.5.2 (p.49).
   * Default reset N=86, M=21 yields 24.5714MHz audio base clock.
   * CCU registers are shared across peripherals, so the RMW is
   * critical-section protected like every other CCU RMW in this file.
   */

  flags = enter_critical_section();
  putreg32(getreg32(F1C100S_CCU_PLL_AUDIO_CTRL) | CCU_PLL_AUDIO_ENABLE,
           F1C100S_CCU_PLL_AUDIO_CTRL);
  leave_critical_section(flags);

  /* Wait for PLL_AUDIO to lock (bit 28) with timeout */

  for (timeout = 0x10000; timeout > 0; timeout--)
    {
      if (getreg32(F1C100S_CCU_PLL_AUDIO_CTRL) & CCU_PLL_AUDIO_LOCK)
        {
          break;
        }
    }

  /* Enable Audio Codec module clock (CCU 0x140 bit 31, AC_DIG_CLK) per UM 3.2.5.26 (p.65) */

  flags = enter_critical_section();
  putreg32(getreg32(F1C100S_CCU_CODEC_CLK) | CCU_CODEC_CLK_ENABLE,
           F1C100S_CCU_CODEC_CLK);
  leave_critical_section(flags);

  /* Enable Audio Codec APB bus clock (BUS_CLK_GATE2, bit 0) */

  f1c100s_clk_enable(CCU_BUS_CLK_GATING2, 0);

  /* Reset Audio Codec */

  f1c100s_reset_audio();

  /* Wait for reset to complete */

  up_mdelay(10);

  /* Hardware reset and mixer configuration */

  f1c100s_audio_hwreset(priv);

  /* Allocate DMA channels */
  priv->dma_dac = f1c100s_dma_channel_alloc();
  priv->dma_adc = f1c100s_dma_channel_alloc();

  if (!priv->dma_dac || !priv->dma_adc)
    {
      auderr("ERROR: Failed to allocate DMA channels\n");
      if (priv->dma_dac) f1c100s_dma_channel_free(priv->dma_dac);
      if (priv->dma_adc) f1c100s_dma_channel_free(priv->dma_adc);
      priv->dma_dac = NULL;
      priv->dma_adc = NULL;
      audwarn("WARN: Falling back to PIO mode\n");
    }
  else
    {
      audinfo("DMA channels allocated: DAC=%p ADC=%p\n", priv->dma_dac, priv->dma_adc);
    }

  /* Attach interrupt handler */

  ret = irq_attach(F1C_IRQ_AUDIO, f1c100s_audio_interrupt, priv);
  if (ret < 0)
    {
      auderr("ERROR: Failed to attach IRQ: %d\n", ret);
      if (priv->dma_dac) f1c100s_dma_channel_free(priv->dma_dac);
      if (priv->dma_adc) f1c100s_dma_channel_free(priv->dma_adc);
      return NULL;
    }

  /* Enable interrupt */

  up_enable_irq(F1C_IRQ_AUDIO);

  priv->initialized = true;

  return &priv->dev;
}

/****************************************************************************
 * Name: f1c100s_audio_uninitialize
 ****************************************************************************/

int f1c100s_audio_uninitialize(FAR struct audio_lowerhalf_s *dev)
{
  FAR struct f1c100s_audio_dev_s *priv = (FAR struct f1c100s_audio_dev_s *)dev;

  if (priv == NULL || !priv->initialized)
    {
      return -EINVAL;
    }

  /* Disable and detach interrupt */

  up_disable_irq(F1C_IRQ_AUDIO);
  irq_detach(F1C_IRQ_AUDIO);

  /* Free DMA channels */

  if (priv->dma_dac)
    {
      f1c100s_dma_channel_free(priv->dma_dac);
      priv->dma_dac = NULL;
    }

  if (priv->dma_adc)
    {
      f1c100s_dma_channel_free(priv->dma_adc);
      priv->dma_adc = NULL;
    }

  /* Reset hardware */

  f1c100s_audio_hwreset(priv);

  /* Clear initialized flag */

  priv->initialized = false;

  audinfo("Audio uninitialized\n");

  return OK;
}
