/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_spi.c
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
#include <assert.h>
#include <errno.h>
#include <debug.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/mutex.h>
#include <nuttx/spi/spi.h>
#ifdef CONFIG_SPI_CMDDATA
#include <fcntl.h>
#include <sys/ioctl.h>
#include <nuttx/ioexpander/gpio.h>
#endif

#include <arch/board/board.h>

#include "arm_internal.h"
#include "chip.h"
#include "hardware/f1c100s_spi.h"
#include "hardware/f1c100s_ccu.h"
#include "f1c100s_softreset.h"
#include "hardware/f1c100s_dma.h"

/* GPIO configuration - use internal functions */
#define F1C_PIO_BASE         0x01c20800
#define GPIOC_BASE           (F1C_PIO_BASE + 0x48)
#define GPIOE_BASE            (F1C_PIO_BASE + 0x90)
#define GPIO_CFG0            0x00
#define GPIO_CFG1            0x04

/* GPIO port definitions */
#define GPIO_PORTA           0
#define GPIO_PORTB           1
#define GPIO_PORTC           2
#define GPIO_PORTD           3
#define GPIO_PORTE           4
#define GPIO_PORTF           5
#define GPIO_PIN(port, pin)  (((port) << 5) | (pin))

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* SPI register access macros */

#define SPI_GETREG(p,o)     getreg32((p)->config->base + (o))
#define SPI_PUTREG(p,o,v)   putreg32((v), (p)->config->base + (o))
#define SPI_GETREG8(p,o)    getreg8((p)->config->base + (o))
#define SPI_PUTREG8(p,o,v)  putreg8((v), (p)->config->base + (o))

/* Default SPI configuration */

#define SPI_DEFAULT_FREQUENCY 1000000  /* 1 MHz */

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* SPI Device hardware configuration */

struct f1c100s_spiconfig_s
{
  uint32_t base;        /* SPI base address */
  uint8_t  bus;         /* SPI bus number */
  uint8_t  irq;         /* Interrupt number (for future use) */

  /* GPIO pins (for future use) */

  uint32_t clk_pin;
  uint32_t mosi_pin;
  uint32_t miso_pin;
  uint32_t cs_pin;
};

/* Timeout hits are silent unless CONFIG_DEBUG_SPI_ERROR. nsh: mw <addr>. */

volatile uint32_t g_f1c_spi_xch_to;
volatile uint32_t g_f1c_spi_rx_to;
volatile uint32_t g_f1c_spi_chunks;
volatile uint32_t g_f1c_spi_abort_off;

/* SPI Device private data */

struct f1c100s_spidev_s
{
  struct spi_dev_s spidev;           /* Externally visible part */
  FAR const struct f1c100s_spiconfig_s *config;  /* Port configuration */

  mutex_t lock;                      /* Mutual exclusion mutex */
  uint32_t frequency;                /* Requested clock frequency */
  uint32_t actual;                   /* Actual clock frequency */
  uint8_t nbits;                     /* Width of word in bits (8) */
  uint8_t mode;                      /* SPI mode */
  bool initialized;                  /* True if initialized */

  /* DMA and interrupt support */

#ifdef CONFIG_F1C100S_SPI_DMA
  DMA_HANDLE dma_rx;                 /* DMA channel for RX */
  DMA_HANDLE dma_tx;                 /* DMA channel for TX */
  sem_t dma_wait;                    /* Semaphore for DMA completion */
  volatile int dma_pending;          /* Number of pending DMA transfers */
  volatile int dma_result;           /* DMA result code */
#endif

#ifdef CONFIG_F1C100S_SPI_INTERRUPTS
  sem_t waitsem;                     /* Wait for transfer complete */
  volatile bool ready;               /* Transfer complete flag */
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* SPI operations */

static int      spi_lock(FAR struct spi_dev_s *dev, bool lock);
static void     spi_select(FAR struct spi_dev_s *dev, uint32_t devid,
                           bool selected);
static uint32_t spi_setfrequency(FAR struct spi_dev_s *dev,
                                  uint32_t frequency);
static void     spi_setmode(FAR struct spi_dev_s *dev, enum spi_mode_e mode);
static void     spi_setbits(FAR struct spi_dev_s *dev, int nbits);
static uint8_t  spi_status(FAR struct spi_dev_s *dev, uint32_t devid);
#ifdef CONFIG_SPI_CMDDATA
static int      spi_cmddata(FAR struct spi_dev_s *dev, uint32_t devid,
                            bool cmd);
#endif
static uint32_t spi_send(FAR struct spi_dev_s *dev, uint32_t wd);
static void     spi_exchange(FAR struct spi_dev_s *dev,
                             FAR const void *txbuffer,
                             FAR void *rxbuffer, size_t nwords);
#ifndef CONFIG_SPI_EXCHANGE
static void     spi_sndblock(FAR struct spi_dev_s *dev,
                             FAR const void *txbuffer, size_t nwords);
static void     spi_recvblock(FAR struct spi_dev_s *dev,
                              FAR void *rxbuffer, size_t nwords);
#endif

#ifdef CONFIG_F1C100S_SPI_DMA
static void     spi_dma_callback(void *arg, int result);
static void     spi_dma_exchange(FAR struct f1c100s_spidev_s *priv,
                                 FAR const void *txbuffer,
                                 FAR void *rxbuffer, size_t nwords);
#endif

/* Initialization */

static void     spi_hw_initialize(FAR struct f1c100s_spidev_s *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* SPI0 device configuration */

static const struct f1c100s_spiconfig_s g_spi0config =
{
  .base      = F1C100S_SPI0_BASE,
  .bus       = 0,
  .irq       = F1C_IRQ_SPI0,
  .clk_pin   = GPIO_PIN(GPIO_PORTC, 0),  /* PC0 */
  .mosi_pin  = GPIO_PIN(GPIO_PORTC, 1),  /* PC1 */
  .miso_pin  = GPIO_PIN(GPIO_PORTC, 2),  /* PC2 */
  .cs_pin    = GPIO_PIN(GPIO_PORTC, 3),  /* PC3 */
};

/* SPI0 device private data */

static struct f1c100s_spidev_s g_spi0dev =
{
  .config = &g_spi0config,
  .lock   = NXMUTEX_INITIALIZER,
};

#ifdef CONFIG_F1C100S_SPI1
/* SPI1 device configuration */

static const struct f1c100s_spiconfig_s g_spi1config =
{
  .base      = F1C100S_SPI1_BASE,
  .bus       = 1,
  .irq       = F1C_IRQ_SPI1,
  /* Fields unused (spi_gpio_init() below is the sole source of truth
   * for the signal-to-pin mapping; nothing bit-bangs these
   * individually). Left as GPIO_PORTA placeholders on purpose -- do
   * NOT hand-fill these with the real PE7/PE8/PE9 numbers, that's
   * exactly the kind of stale duplicate comment that caused the
   * original ST7789 pin bug (see spi_gpio_init()'s comment).
   */
  .clk_pin   = GPIO_PIN(GPIO_PORTA, 0),
  .mosi_pin  = GPIO_PIN(GPIO_PORTA, 0),
  .miso_pin  = GPIO_PIN(GPIO_PORTA, 0),
  .cs_pin    = GPIO_PIN(GPIO_PORTA, 0),
};

/* SPI1 device private data */

static struct f1c100s_spidev_s g_spi1dev =
{
  .config = &g_spi1config,
  .lock   = NXMUTEX_INITIALIZER,
};
#endif

/* SPI operations vtable */

static const struct spi_ops_s g_spi_ops =
{
  .lock              = spi_lock,
  .select            = spi_select,
  .setfrequency      = spi_setfrequency,
  .setmode           = spi_setmode,
  .setbits           = spi_setbits,
#ifdef CONFIG_SPI_HWFEATURES
  .hwfeatures        = NULL,  /* Not supported yet */
#endif
  .status            = spi_status,
#ifdef CONFIG_SPI_CMDDATA
  .cmddata           = spi_cmddata,
#endif
  .send              = spi_send,
#ifdef CONFIG_SPI_EXCHANGE
  .exchange          = spi_exchange,
#else
  .sndblock          = spi_sndblock,
  .recvblock         = spi_recvblock,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: spi_lock
 *
 * Description:
 *   Lock or unlock the SPI device
 *
 ****************************************************************************/

static int spi_lock(FAR struct spi_dev_s *dev, bool lock)
{
  FAR struct f1c100s_spidev_s *priv = (FAR struct f1c100s_spidev_s *)dev;
  int ret;

  if (lock)
    {
      ret = nxmutex_lock(&priv->lock);
    }
  else
    {
      ret = nxmutex_unlock(&priv->lock);
    }

  return ret;
}

/****************************************************************************
 * Name: spi_select
 *
 * Description:
 *   Select or deselect the SPI device
 *
 ****************************************************************************/

static void spi_select(FAR struct spi_dev_s *dev, uint32_t devid,
                       bool selected)
{
  FAR struct f1c100s_spidev_s *priv = (FAR struct f1c100s_spidev_s *)dev;
  uint32_t regval;

  spiinfo("devid: %d CS: %s\n", (int)devid, selected ? "assert" : "de-assert");

  /* Control CS via TCR register CS_MANUAL and SS_LEVEL bits */

  regval = SPI_GETREG(priv, SPI_TCR_OFFSET);
  regval |= SPI_TCR_CS_MANUAL;

  if (selected)
    {
      /* Reset FIFOs at start of transaction */

      SPI_PUTREG(priv, SPI_FCR_OFFSET, SPI_FCR_RF_RST | SPI_FCR_TF_RST);
      SPI_PUTREG(priv, SPI_FCR_OFFSET, 0);

      /* Assert CS (active low: drive CS low) */

      regval &= ~SPI_TCR_SS_LEVEL;
    }
  else
    {
      /* Deassert CS (inactive high: drive CS high) */

      regval |= SPI_TCR_SS_LEVEL;
    }

  SPI_PUTREG(priv, SPI_TCR_OFFSET, regval);
}

/****************************************************************************
 * Name: spi_setfrequency
 *
 * Description:
 *   Set the SPI frequency
 *
 ****************************************************************************/

static uint32_t spi_setfrequency(FAR struct spi_dev_s *dev,
                                  uint32_t frequency)
{
  FAR struct f1c100s_spidev_s *priv = (FAR struct f1c100s_spidev_s *)dev;
  uint32_t ahb_freq;
  uint32_t actual;
  uint32_t regval;

  /* Has the frequency changed? */

  if (frequency == priv->frequency)
    {
      return priv->actual;
    }

  /* Get AHB clock frequency */

  ahb_freq = f1c100s_get_ahb_freq();

  /* Calculate clock divider
   * SPI_CLK = AHB_CLK / (2^(cdr1+1) * (cdr2+1))
   * We want: cdr1 >= 0, cdr2 >= 0
   */

  if (frequency > ahb_freq)
    {
      frequency = ahb_freq;
    }

  /* Setup clock divider per Linux spi-sun6i.c:
   * CDR2: SPI_CLK = MOD_CLK / (2 * (cdr2 + 1)), select with DRS (bit 12) set
   * CDR1: SPI_CLK = MOD_CLK / (1 << div), DRS = 0
   * Try CDR2 first, fallback to CDR1 if divider exceeds 256.
   */

  uint32_t div_cdr1 = (ahb_freq + frequency - 1) / frequency;
  uint32_t div_cdr2 = (div_cdr1 + 1) / 2;

  if (div_cdr2 <= 256)
    {
      if (div_cdr2 == 0)
        {
          div_cdr2 = 1;
        }

      regval = SPI_CCR_CDR2(div_cdr2 - 1) | SPI_CCR_DRS;
      actual = ahb_freq / (2 * div_cdr2);
    }
  else
    {
      uint32_t div_pow = 0;
      while ((1 << div_pow) < div_cdr1 && div_pow < 15)
        {
          div_pow++;
        }

      regval = SPI_CCR_CDR1(div_pow);
      actual = ahb_freq / (1 << div_pow);
    }

  /* Set clock divider */

  SPI_PUTREG(priv, SPI_CCR_OFFSET, regval);

  /* Save the frequency setting */

  priv->frequency = frequency;
  priv->actual    = actual;

  spiinfo("Frequency %lu->%lu\n", (unsigned long)frequency, (unsigned long)actual);
  return actual;
}

/****************************************************************************
 * Name: spi_setmode
 *
 * Description:
 *   Set the SPI mode
 *
 ****************************************************************************/

static void spi_setmode(FAR struct spi_dev_s *dev, enum spi_mode_e mode)
{
  FAR struct f1c100s_spidev_s *priv = (FAR struct f1c100s_spidev_s *)dev;
  uint32_t regval;

  spiinfo("mode=%d\n", mode);

  /* Has the mode changed? */

  if (mode == priv->mode)
    {
      return;
    }

  /* Read current TCR */

  regval = SPI_GETREG(priv, SPI_TCR_OFFSET);

  /* Clear mode bits */

  regval &= ~(SPI_TCR_CPOL | SPI_TCR_CPHA);

  /* Set new mode */

  switch (mode)
    {
      case SPIDEV_MODE0: /* CPOL=0 CPHA=0 */
        break;

      case SPIDEV_MODE1: /* CPOL=0 CPHA=1 */
        regval |= SPI_TCR_CPHA;
        break;

      case SPIDEV_MODE2: /* CPOL=1 CPHA=0 */
        regval |= SPI_TCR_CPOL;
        break;

      case SPIDEV_MODE3: /* CPOL=1 CPHA=1 */
        regval |= (SPI_TCR_CPOL | SPI_TCR_CPHA);
        break;

      default:
        return;
    }

  /* Write TCR */

  SPI_PUTREG(priv, SPI_TCR_OFFSET, regval);

  /* Save mode */

  priv->mode = mode;
}

/****************************************************************************
 * Name: spi_setbits
 *
 * Description:
 *   Set the number of bits per word
 *
 ****************************************************************************/

static void spi_setbits(FAR struct spi_dev_s *dev, int nbits)
{
  FAR struct f1c100s_spidev_s *priv = (FAR struct f1c100s_spidev_s *)dev;

  spiinfo("nbits=%d\n", nbits);

  /* The SPI1 FIFO hardware is byte-wide regardless of nbits; 16-bit
   * "words" are packed/unpacked as two FIFO byte transfers in
   * spi_exchange() (MSB first, per SPI_SNDBLOCK's documented uint16_t
   * packing). This matters concretely: drivers/lcd/st7789.c calls
   * SPI_SETBITS(16) then SPI_SNDBLOCK(buf, pixel_count) for RGB565
   * pixel data -- silently rejecting 16 here (as this used to) makes
   * the caller believe nwords means pixels while spi_exchange() still
   * counted raw bytes, transmitting only half of every row.
   */

  if (nbits != 8 && nbits != 16)
    {
      spierr("ERROR: Only 8/16-bit transfers supported\n");
      return;
    }

  priv->nbits = nbits;
}

/****************************************************************************
 * Name: spi_status
 *
 * Description:
 *   Get SPI device status
 *
 ****************************************************************************/

static uint8_t spi_status(FAR struct spi_dev_s *dev, uint32_t devid)
{
  /* Return that device is present and not write-protected */

  return SPI_STATUS_PRESENT;
}

#ifdef CONFIG_SPI_CMDDATA
/****************************************************************************
 * Name: spi_cmddata
 *
 * Description:
 *   Set the SPI D/C line for a 4-wire SPI LCD (ST7789 on SPIDEV_DISPLAY(0),
 *   SPI1 only). cmd=true selects command mode (DC low); the board already
 *   registered the DC pin as /dev/lcd_dc (f1c100s_st7789.c's
 *   board_lcd_initialize()), so this just opens it once and toggles it.
 *
 ****************************************************************************/

static int spi_cmddata(FAR struct spi_dev_s *dev, uint32_t devid, bool cmd)
{
  static int fd = -1;

  if (devid != SPIDEV_DISPLAY(0))
    {
      return -ENODEV;
    }

  if (fd < 0)
    {
      fd = open("/dev/lcd_dc", O_WRONLY);
      if (fd < 0)
        {
          return -ENODEV;
        }
    }

  /* cmd true -> DC low (command); cmd false -> DC high (data) */

  return ioctl(fd, GPIOC_WRITE, (unsigned long)!cmd);
}
#endif

/****************************************************************************
 * Name: spi_send
 *
 * Description:
 *   Exchange one word on SPI
 *
 ****************************************************************************/

static uint32_t spi_send(FAR struct spi_dev_s *dev, uint32_t wd)
{
  FAR struct f1c100s_spidev_s *priv = (FAR struct f1c100s_spidev_s *)dev;
  uint32_t regval;
  uint32_t rxdata;
  int timeout;

  /* Set transfer count to 1 byte BEFORE filling TX FIFO */

  SPI_PUTREG(priv, SPI_MBC_OFFSET, 1);  /* Total burst count */
  SPI_PUTREG(priv, SPI_MTC_OFFSET, 1);  /* TX count */
  SPI_PUTREG(priv, SPI_BCC_OFFSET, 1);  /* Single mode TX count */

  /* Write data to TX FIFO (8-bit write per Linux sun6i_spi.c writeb) */

  SPI_PUTREG8(priv, SPI_TXD_OFFSET, (uint8_t)(wd & 0xff));

  /* Trigger exchange - read current TCR and set XCH bit */

  regval = SPI_GETREG(priv, SPI_TCR_OFFSET);
  regval |= SPI_TCR_XCH;
  SPI_PUTREG(priv, SPI_TCR_OFFSET, regval);

  /* Wait for exchange to complete (XCH bit clears) with timeout */

  timeout = 10000;
  while ((SPI_GETREG(priv, SPI_TCR_OFFSET) & SPI_TCR_XCH) != 0)
    {
      if (--timeout <= 0)
        {
          g_f1c_spi_xch_to++;
          return 0xff;
        }
    }

  /* Wait for RX FIFO to receive byte */

  timeout = 10000;
  while ((SPI_GETREG(priv, SPI_FSR_OFFSET) & 0xff) == 0)
    {
      if (--timeout <= 0)
        {
          g_f1c_spi_rx_to++;
          return 0xff;
        }
    }

  /* Read received data from RX FIFO (8-bit read per Linux readb) */

  rxdata = SPI_GETREG8(priv, SPI_RXD_OFFSET);

  spiinfo("TX: 0x%02x RX: 0x%02x\n", (unsigned)(wd & 0xff), (unsigned)rxdata);

  return rxdata;
}

/****************************************************************************
 * Name: spi_exchange
 *
 * Description:
 *   Exchange a block of data on SPI
 *
 ****************************************************************************/

static void spi_exchange(FAR struct spi_dev_s *dev,
                         FAR const void *txbuffer,
                         FAR void *rxbuffer, size_t nwords)
{
  FAR struct f1c100s_spidev_s *priv = (FAR struct f1c100s_spidev_s *)dev;
  FAR const uint8_t *src = (FAR const uint8_t *)txbuffer;
  FAR uint8_t *dest = (FAR uint8_t *)rxbuffer;
  uint32_t regval;
  size_t i;

  /* SPI_SNDBLOCK's contract: nwords counts 8-bit words when nbits<=8,
   * 16-bit words when nbits>8 (include/nuttx/spi/spi.h). The FIFO
   * hardware itself is byte-wide either way -- in 16-bit mode we push
   * two FIFO bytes per logical word, MSB first (matches how
   * drivers/lcd/st7789.c sends RGB565 pixels: SPI_SETBITS(16) then
   * SPI_SNDBLOCK(pixelbuf, pixel_count)). Below, "chunk"/"offset" stay
   * in BYTES throughout; bpw is how many FIFO bytes one caller-visible
   * word costs.
   */

  size_t bpw = (priv->nbits > 8) ? 2 : 1;
  size_t nbytes = nwords * bpw;

  spiinfo("nwords=%d nbits=%d\n", (int)nwords, priv->nbits);

  if (nwords == 0)
    {
      return;
    }

#ifdef CONFIG_F1C100S_SPI_DMA
  /* Use DMA for transfers larger than threshold. NOTE: spi_dma_exchange()
   * has not been updated for 16-bit words -- it still assumes nwords is
   * a byte count, same bug as this function used to have. Nothing in
   * the current board configs enables both CONFIG_F1C100S_SPI_DMA and
   * a 16-bit SPI consumer, so this is a latent issue, not an active
   * one; fix spi_dma_exchange() the same way before relying on DMA for
   * 16-bit transfers (e.g. ST7789 with CONFIG_F1C100S_SPI_DMA=y).
   */

  if (priv->dma_tx && priv->dma_rx &&
      nwords >= CONFIG_F1C100S_SPI_DMA_THRESHOLD && txbuffer &&
      priv->nbits <= 8)
    {
      spi_dma_exchange(priv, txbuffer, rxbuffer, nwords);
      return;
    }
#endif

  /* PIO mode transfer */

  /* Note: Do NOT reset FIFOs here - this function may be called
   * in the middle of a multi-part SPI transaction (e.g., after
   * sending command+address via SPI_SEND, then data via SPI_SNDBLOCK).
   * Resetting FIFOs would disrupt the ongoing transaction.
   */

  /* Process in chunks of FIFO size (bytes) */

  size_t offset = 0;

  while (offset < nbytes)
    {
      size_t chunk = nbytes - offset;
      if (chunk > SPI_FIFO_SIZE)
        {
          chunk = SPI_FIFO_SIZE;
        }

      /* Keep 16-bit words whole within a chunk boundary so the
       * MSB/LSB pairing below never splits across FIFO bursts.
       */

      if (bpw == 2)
        {
          chunk &= ~((size_t)1);
        }

      /* Set burst count for this chunk BEFORE filling TX FIFO */
      /* This tells QEMU how many bytes to expect */

      SPI_PUTREG(priv, SPI_MBC_OFFSET, chunk);
      SPI_PUTREG(priv, SPI_MTC_OFFSET, chunk);
      SPI_PUTREG(priv, SPI_BCC_OFFSET, chunk);

      /* Fill TX FIFO (8-bit writes per Linux sun6i_spi.c writeb).
       * In 16-bit mode, src/dest are reinterpreted as native uint16_t
       * words and each word is split MSB-then-LSB onto the wire.
       */

      for (i = 0; i < chunk; i++)
        {
          uint8_t txbyte = 0xff;

          if (src)
            {
              if (bpw == 2)
                {
                  size_t widx = (offset + i) / 2;
                  uint16_t word = ((FAR const uint16_t *)src)[widx];
                  txbyte = ((offset + i) & 1) ? (uint8_t)word
                                               : (uint8_t)(word >> 8);
                }
              else
                {
                  txbyte = src[offset + i];
                }
            }

          SPI_PUTREG8(priv, SPI_TXD_OFFSET, txbyte);
        }

      /* Start transfer */

      regval = SPI_GETREG(priv, SPI_TCR_OFFSET);
      regval |= SPI_TCR_XCH;
      SPI_PUTREG(priv, SPI_TCR_OFFSET, regval);

      /* Wait for transfer complete with timeout */

      int timeout = 10000;
      while ((SPI_GETREG(priv, SPI_TCR_OFFSET) & SPI_TCR_XCH) != 0)
        {
          if (--timeout <= 0)
            {
              g_f1c_spi_xch_to++;
              g_f1c_spi_abort_off = (uint32_t)offset;
              SPI_PUTREG(priv, SPI_FCR_OFFSET,
                         SPI_FCR_RF_RST | SPI_FCR_TF_RST);
              return;
            }
        }

      /* Wait for RX FIFO to receive all chunk bytes */

      timeout = 10000;
      while ((SPI_GETREG(priv, SPI_FSR_OFFSET) & 0xff) < chunk)
        {
          if (--timeout <= 0)
            {
              g_f1c_spi_rx_to++;
              g_f1c_spi_abort_off = (uint32_t)offset;
              SPI_PUTREG(priv, SPI_FCR_OFFSET,
                         SPI_FCR_RF_RST | SPI_FCR_TF_RST);
              return;
            }
        }

      /* Read RX FIFO (8-bit reads per Linux sun6i_spi.c readb) */

      for (i = 0; i < chunk; i++)
        {
          uint8_t rx = SPI_GETREG8(priv, SPI_RXD_OFFSET);

          if (dest)
            {
              if (bpw == 2)
                {
                  size_t widx = (offset + i) / 2;

                  if ((offset + i) & 1)
                    {
                      ((FAR uint16_t *)dest)[widx] =
                        (uint16_t)(((FAR uint16_t *)dest)[widx] | rx);
                    }
                  else
                    {
                      ((FAR uint16_t *)dest)[widx] =
                        (uint16_t)((uint16_t)rx << 8);
                    }
                }
              else
                {
                  dest[offset + i] = rx;
                }
            }
        }

      offset += chunk;
      g_f1c_spi_chunks++;
    }
}

#ifndef CONFIG_SPI_EXCHANGE
/****************************************************************************
 * Name: spi_sndblock
 *
 * Description:
 *   Send a block of data on SPI
 *
 ****************************************************************************/

static void spi_sndblock(FAR struct spi_dev_s *dev,
                         FAR const void *txbuffer, size_t nwords)
{
  spi_exchange(dev, txbuffer, NULL, nwords);
}

/****************************************************************************
 * Name: spi_recvblock
 *
 * Description:
 *   Receive a block of data from SPI
 *
 ****************************************************************************/

static void spi_recvblock(FAR struct spi_dev_s *dev,
                          FAR void *rxbuffer, size_t nwords)
{
  spi_exchange(dev, NULL, rxbuffer, nwords);
}
#endif

#ifdef CONFIG_F1C100S_SPI_DMA
/****************************************************************************
 * Name: spi_dma_callback
 ****************************************************************************/

static void spi_dma_callback(void *arg, int result)
{
  FAR struct f1c100s_spidev_s *priv = (FAR struct f1c100s_spidev_s *)arg;

  /* Save error result if any */

  if (result != DMA_RESULT_OK)
    {
      priv->dma_result = result;
    }

  /* Decrement pending count and signal when all DMA complete */

  if (--priv->dma_pending <= 0)
    {
      nxsem_post(&priv->dma_wait);
    }
}

/****************************************************************************
 * Name: spi_dma_exchange
 ****************************************************************************/

static void spi_dma_exchange(FAR struct f1c100s_spidev_s *priv,
                             FAR const void *txbuffer,
                             FAR void *rxbuffer, size_t nwords)
{
  uint32_t tx_cfg;
  uint32_t rx_cfg;
  uint32_t regval;
  uint8_t drq_type;
  int ret;

  /* Determine DRQ type based on SPI bus */

  drq_type = (priv->config->bus == 0) ? DMA_DRQ_SPI0 : DMA_DRQ_SPI1;

  /* Initialize DMA state */

  priv->dma_result = DMA_RESULT_OK;
  priv->dma_pending = rxbuffer ? 2 : 1;  /* TX + RX or TX only */

  /* Reset FIFOs */

  SPI_PUTREG(priv, SPI_FCR_OFFSET, SPI_FCR_RF_RST | SPI_FCR_TF_RST);
  up_udelay(1);

  /* Enable DMA request in FIFO control register */

  regval = (1 << 8) | (1 << 24);  /* TX_DRQ_EN | RX_DRQ_EN */
  SPI_PUTREG(priv, SPI_FCR_OFFSET, regval);

  /* Set burst count */

  SPI_PUTREG(priv, SPI_MBC_OFFSET, nwords);
  SPI_PUTREG(priv, SPI_MTC_OFFSET, nwords);
  SPI_PUTREG(priv, SPI_BCC_OFFSET, nwords);

  /* Configure TX DMA: Memory -> SPI TX FIFO */

  tx_cfg = NDMA_CFG_SRC_DRQ_TYPE(DMA_DRQ_SDRAM) |
           NDMA_CFG_SRC_WIDTH_8 |
           NDMA_CFG_SRC_BURST_4 |
           NDMA_CFG_DST_DRQ_TYPE(drq_type) |
           NDMA_CFG_DST_WIDTH_8 |
           NDMA_CFG_DST_BURST_4 |
           NDMA_CFG_DST_MODE_IO;

  /* Configure RX DMA: SPI RX FIFO -> Memory */

  rx_cfg = NDMA_CFG_SRC_DRQ_TYPE(drq_type) |
           NDMA_CFG_SRC_WIDTH_8 |
           NDMA_CFG_SRC_BURST_4 |
           NDMA_CFG_SRC_MODE_IO |
           NDMA_CFG_DST_DRQ_TYPE(DMA_DRQ_SDRAM) |
           NDMA_CFG_DST_WIDTH_8 |
           NDMA_CFG_DST_BURST_4;

  /* Setup and start RX DMA first */

  if (rxbuffer)
    {
      f1c100s_dma_setup(priv->dma_rx,
                        priv->config->base + SPI_RXD_OFFSET,
                        (uint32_t)rxbuffer, nwords, rx_cfg);
      f1c100s_dma_start(priv->dma_rx, spi_dma_callback, priv);
    }

  /* Setup and start TX DMA */

  f1c100s_dma_setup(priv->dma_tx,
                    (uint32_t)txbuffer,
                    priv->config->base + SPI_TXD_OFFSET,
                    nwords, tx_cfg);
  f1c100s_dma_start(priv->dma_tx, spi_dma_callback, priv);

  /* Start SPI transfer */

  regval = SPI_GETREG(priv, SPI_TCR_OFFSET);
  regval |= SPI_TCR_XCH;
  SPI_PUTREG(priv, SPI_TCR_OFFSET, regval);

  /* Wait for DMA completion with timeout (1 second) */

  ret = nxsem_tickwait(&priv->dma_wait, MSEC2TICK(1000));
  if (ret < 0)
    {
      spierr("ERROR: SPI DMA timeout\n");
      f1c100s_dma_stop(priv->dma_tx);
      if (rxbuffer)
        {
          f1c100s_dma_stop(priv->dma_rx);
        }
    }
  else if (priv->dma_result != DMA_RESULT_OK)
    {
      spierr("ERROR: SPI DMA failed: %d\n", priv->dma_result);
    }

  /* Wait for SPI transfer complete with timeout */

  int timeout = 10000;
  while ((SPI_GETREG(priv, SPI_TCR_OFFSET) & SPI_TCR_XCH) != 0)
    {
      if (--timeout <= 0)
        {
          spierr("ERROR: SPI XCH timeout\n");
          break;
        }
    }

  /* Disable DMA request */

  SPI_PUTREG(priv, SPI_FCR_OFFSET, 0);
}
#endif

/****************************************************************************
 * Name: spi_hw_initialize
 *
 * Description:
 *   Initialize SPI hardware
 *
 ****************************************************************************/

static void spi_gpio_init(int bus)
{
  uint32_t val;

  /* SPI0: PC0=CLK, PC1=MOSI, PC2=MISO, PC3=CS (Function 2) */

  if (bus == 0)
    {
      /* Configure PC0-PC3 as SPI0 function (mode 2) */

      val = getreg32(GPIOC_BASE + GPIO_CFG0);
      val &= ~0x0000ffff;  /* Clear PC0-PC3 config */
      val |= 0x00002222;   /* Set PC0-PC3 to function 2 (SPI0) */
      putreg32(val, GPIOC_BASE + GPIO_CFG0);
    }
#ifdef CONFIG_F1C100S_SPI1
  else if (bus == 1)
    {
      /* SPI1: PE7=CS, PE8=MOSI, PE9=CLK (Function 4).
       *
       * PA0-3 Function 6 is ALSO a legitimate SPI1 mux option on this
       * silicon (f1c100s_ds_v1_0.pdf Table 4-2) -- that is not the bug.
       * The bug was using it: on this board's schematic (SCH_F1C200S),
       * PA0-3 are wired to the RGB panel's resistive touchscreen
       * (X1/X2/Y1/Y2), not to the ST7789 header. Verified pin-by-pin
       * against the schematic net labels and the datasheet's package
       * pin-number table (Pin 42/41/40 = PE7/PE8/PE9). Do not revert to
       * PA0-3 without re-checking the schematic.
       */

      val = getreg32(GPIOE_BASE + GPIO_CFG0);
      val &= ~(0xfu << 28);   /* Clear PE7 config */
      val |= (0x4u << 28);    /* PE7 = function 4 (SPI1_CS) */
      putreg32(val, GPIOE_BASE + GPIO_CFG0);

      val = getreg32(GPIOE_BASE + GPIO_CFG1);
      val &= ~0x000000ff;     /* Clear PE8/PE9 config */
      val |= 0x00000044;      /* PE8/PE9 = function 4 (MOSI/CLK) */
      putreg32(val, GPIOE_BASE + GPIO_CFG1);
    }
#endif
}

static void spi_hw_initialize(FAR struct f1c100s_spidev_s *priv)
{
  uint32_t regval;

  /* Enable SPI clock and deassert reset */

  if (priv->config->bus == 0)
    {
      /* Enable SPI0 clock gate */

      f1c100s_clk_enable(CCU_BUS_CLK_GATING0, 20);  /* SPI0 */

      /* Deassert SPI0 reset */

      f1c100s_reset_spi0();

      /* Configure GPIO pins for SPI0 */

      spi_gpio_init(0);
    }
#ifdef CONFIG_F1C100S_SPI1
  else if (priv->config->bus == 1)
    {
      /* Enable SPI1 clock gate */

      f1c100s_clk_enable(CCU_BUS_CLK_GATING0, 21);  /* SPI1 */
      f1c100s_reset_spi1();

      /* Configure GPIO pins for SPI1 */

      spi_gpio_init(1);
    }
#endif

  /* Soft reset. GCR_SRST is self-clearing; wait for it. Writing EN
   * while SRST is still held drops the enable on a warm re-init
   * (AP re-inits SPI1 after BL already used it).
   */

  {
    int timeout;

    regval = SPI_GETREG(priv, SPI_GCR_OFFSET);
    SPI_PUTREG(priv, SPI_GCR_OFFSET, regval | SPI_GCR_SRST);
    timeout = 10000;
    while ((SPI_GETREG(priv, SPI_GCR_OFFSET) & SPI_GCR_SRST) != 0)
      {
        if (--timeout <= 0)
          {
            spierr("ERROR: SPI GCR SRST stuck\n");
            break;
          }
      }
  }

  /* Configure as master mode */

  regval = SPI_GCR_EN | SPI_GCR_MODE | SPI_GCR_TP_EN;
  SPI_PUTREG(priv, SPI_GCR_OFFSET, regval);

  /* Reset FIFOs */

  regval = SPI_FCR_RF_RST | SPI_FCR_TF_RST;
  SPI_PUTREG(priv, SPI_FCR_OFFSET, regval);
  up_udelay(10);
  SPI_PUTREG(priv, SPI_FCR_OFFSET, 0);

  /* Configure TCR - default to mode 0, CS deasserted */

  regval = SPI_TCR_CS_MANUAL | SPI_TCR_SS_LEVEL;  /* Manual CS (bit 6), CS high (deasserted) */
  SPI_PUTREG(priv, SPI_TCR_OFFSET, regval);

  /* Set default frequency */

  spi_setfrequency(&priv->spidev, SPI_DEFAULT_FREQUENCY);

  /* Set default mode */

  spi_setmode(&priv->spidev, SPIDEV_MODE0);

  /* Set default bits */

  priv->nbits = 8;

#ifdef CONFIG_F1C100S_SPI_DMA
  /* Initialize DMA semaphore */
  nxsem_init(&priv->dma_wait, 0, 0);

  /* Allocate DMA channels */
  priv->dma_rx = f1c100s_dma_channel_alloc();
  priv->dma_tx = f1c100s_dma_channel_alloc();

  if (priv->dma_rx == NULL || priv->dma_tx == NULL)
    {
      spierr("ERROR: Failed to allocate DMA channels\n");
      if (priv->dma_rx) f1c100s_dma_channel_free(priv->dma_rx);
      if (priv->dma_tx) f1c100s_dma_channel_free(priv->dma_tx);
      priv->dma_rx = NULL;
      priv->dma_tx = NULL;
    }
  else
    {
      spiinfo("DMA channels allocated: RX=%p TX=%p\n", priv->dma_rx, priv->dma_tx);
    }
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

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

FAR struct spi_dev_s *f1c100s_spibus_initialize(int bus)
{
  FAR struct f1c100s_spidev_s *priv = NULL;

  spiinfo("bus=%d\n", bus);

  /* Select device */

  if (bus == 0)
    {
      priv = &g_spi0dev;
    }
#ifdef CONFIG_F1C100S_SPI1
  else if (bus == 1)
    {
      priv = &g_spi1dev;
    }
#endif
  else
    {
      spierr("ERROR: Invalid bus: %d\n", bus);
      return NULL;
    }

  /* Check if already initialized */

  if (priv->initialized)
    {
      spiinfo("SPI%d already initialized\n", bus);
      return (FAR struct spi_dev_s *)priv;
    }

  /* Initialize hardware */

  spi_hw_initialize(priv);

  /* Set up SPI operations */

  priv->spidev.ops = &g_spi_ops;
  priv->initialized = true;

  spiinfo("SPI%d initialized\n", bus);
  return (FAR struct spi_dev_s *)priv;
}
