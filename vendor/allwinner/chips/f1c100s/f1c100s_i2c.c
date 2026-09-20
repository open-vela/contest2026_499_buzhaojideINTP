/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_i2c.c
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

#include <nuttx/arch.h>
#include <nuttx/semaphore.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/mutex.h>
#include <nuttx/clock.h>
#include <nuttx/i2c/i2c_master.h>

#include <arch/board/board.h>

#include "arm_internal.h"
#include "chip.h"
#include "hardware/f1c100s_twi.h"
#include "hardware/f1c100s_gpio.h"
#include "hardware/f1c100s_ccu.h"
#include "f1c100s_softreset.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* I2C register access macros */

#define I2C_GETREG(p,o)     getreg32((p)->base + (o))
#define I2C_PUTREG(p,o,v)   putreg32((v), (p)->base + (o))

/* Timeout helpers */

#define I2C_TIMEOUT_TICKS   MSEC2TICK(I2C_TIMEOUT_MS)

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* I2C Device hardware configuration */

struct f1c100s_i2c_config_s
{
  uint32_t base;        /* I2C base address */
  uint8_t  bus;         /* I2C bus number */
  uint8_t  irq;         /* Interrupt number (for future use) */

  /* GPIO pins (for future use) */

  uint32_t scl_pin;
  uint32_t sda_pin;
};

/* I2C Device private data */

struct f1c100s_i2c_priv_s
{
  struct i2c_master_s dev;              /* Generic I2C device */
  FAR const struct f1c100s_i2c_config_s *config;  /* Port configuration */

  mutex_t lock;                         /* Mutual exclusion mutex */
  uint32_t frequency;                   /* Current I2C frequency */

  /* Interrupt support */
  sem_t wait;                           /* Wait for transfer completion */
  volatile uint8_t state;               /* Current state machine state */
  volatile int result;                  /* Transfer result */

  /* Transfer context */
  FAR struct i2c_msg_s *msgs;           /* Current message array */
  int msg_count;                        /* Number of messages */
  int msg_idx;                          /* Current message index */
  FAR uint8_t *buffer;                  /* Current buffer pointer */
  int bytes_remaining;                  /* Bytes left in current message */
  bool is_read;                         /* Current operation is read */
  bool initialized;                     /* True if initialized */
  uint32_t base;                        /* Cached base address */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* I2C operations */

static int i2c_transfer(FAR struct i2c_master_s *dev,
                        FAR struct i2c_msg_s *msgs, int count);
#ifdef CONFIG_I2C_RESET
static int i2c_reset(FAR struct i2c_master_s *dev);
#endif

/* Initialization */

static int i2c_hw_initialize(FAR struct f1c100s_i2c_priv_s *priv);
static void i2c_set_frequency(FAR struct f1c100s_i2c_priv_s *priv,
                               uint32_t frequency);

/* Interrupt handling */

static int f1c100s_i2c_interrupt(int irq, void *context, void *arg);
static void i2c_next_message(FAR struct f1c100s_i2c_priv_s *priv);

/* Low-level helpers (for polling mode, currently unused) */

#if 0
static int  i2c_start(FAR struct f1c100s_i2c_priv_s *priv);
static int  i2c_send_address(FAR struct f1c100s_i2c_priv_s *priv,
                              uint8_t addr, bool read);
static int  i2c_write_byte(FAR struct f1c100s_i2c_priv_s *priv,
                            uint8_t data);
static int  i2c_read_byte(FAR struct f1c100s_i2c_priv_s *priv,
                           FAR uint8_t *data, bool last);
static int  i2c_wait_status(FAR struct f1c100s_i2c_priv_s *priv,
                             uint8_t expected_status);
#endif
static void i2c_stop(FAR struct f1c100s_i2c_priv_s *priv);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* I2C0 device configuration */

static const struct f1c100s_i2c_config_s g_i2c0_config =
{
  .base     = F1C100S_I2C0_BASE,
  .bus      = 0,
  .irq      = 7,  /* F1C_IRQ_TWI0 */
#ifdef CONFIG_F1C100S_I2C0_PE
  .scl_pin  = GPIO_PIN(GPIO_PORTE, 11), /* PE11: SCK (func 3) */
  .sda_pin  = GPIO_PIN(GPIO_PORTE, 12), /* PE12: SDA (func 3) */
#else
  .scl_pin  = GPIO_PIN(GPIO_PORTD, 12), /* PD12: SCK (func 3) */
  .sda_pin  = GPIO_PIN(GPIO_PORTD, 0),  /* PD0:  SDA (func 3) */
#endif
};

/* I2C0 device private data */

static struct f1c100s_i2c_priv_s g_i2c0_priv =
{
  .config = &g_i2c0_config,
  .lock   = NXMUTEX_INITIALIZER,
  .wait   = SEM_INITIALIZER(0),
  .state  = I2C_STATE_IDLE,
};

#ifdef CONFIG_F1C100S_I2C1
/* I2C1 device configuration */

static const struct f1c100s_i2c_config_s g_i2c1_config =
{
  .base     = F1C100S_I2C1_BASE,
  .bus      = 1,
  .irq      = 8,  /* F1C_IRQ_TWI1 */
  .scl_pin  = GPIO_PIN(GPIO_PORTD, 5),  /* PD5 */
  .sda_pin  = GPIO_PIN(GPIO_PORTD, 6),  /* PD6 */
};

/* I2C1 device private data */

static struct f1c100s_i2c_priv_s g_i2c1_priv =
{
  .config = &g_i2c1_config,
  .lock   = NXMUTEX_INITIALIZER,
  .wait   = SEM_INITIALIZER(0),
  .state  = I2C_STATE_IDLE,
};
#endif

#ifdef CONFIG_F1C100S_I2C2
/* WARNING: On F1C100s, PE0/PE1 function 5 is UART0 console (PE0=RX, PE1=TX),
 * while function 4 is I2C2. Enabling I2C2 on PE0/PE1 will conflict with the
 * default UART0 console! (Issue I7)
 */

/* I2C2 device configuration */

static const struct f1c100s_i2c_config_s g_i2c2_config =
{
  .base     = F1C100S_I2C2_BASE,
  .bus      = 2,
  .irq      = 9,  /* F1C_IRQ_TWI2 */
  .scl_pin  = GPIO_PIN(GPIO_PORTE, 0),  /* PE0 */
  .sda_pin  = GPIO_PIN(GPIO_PORTE, 1),  /* PE1 */
};

/* I2C2 device private data */

static struct f1c100s_i2c_priv_s g_i2c2_priv =
{
  .config = &g_i2c2_config,
  .lock   = NXMUTEX_INITIALIZER,
  .wait   = SEM_INITIALIZER(0),
  .state  = I2C_STATE_IDLE,
};
#endif

/* I2C operations vtable */

static const struct i2c_ops_s g_i2c_ops =
{
#ifdef CONFIG_F1C100S_I2C_INTERRUPTS
  .transfer = i2c_transfer,
#else
  .transfer = i2c_transfer_polling,
#endif
#ifdef CONFIG_I2C_RESET
  .reset    = i2c_reset,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: i2c_next_message
 *
 * Description:
 *   Move to next message or complete transfer
 *
 ****************************************************************************/

static void i2c_next_message(FAR struct f1c100s_i2c_priv_s *priv)
{
  priv->msg_idx++;

  if (priv->msg_idx >= priv->msg_count)
    {
      /* All messages done */
      priv->state = I2C_STATE_DONE;
      priv->result = OK;
      nxsem_post(&priv->wait);
    }
  else
    {
      /* Start next message */
      FAR struct i2c_msg_s *msg = &priv->msgs[priv->msg_idx];

      priv->buffer = msg->buffer;
      priv->bytes_remaining = msg->length;
      priv->is_read = (msg->flags & I2C_M_READ) != 0;

      /* Send (repeated) START if needed */
      if (!(msg->flags & I2C_M_NOSTART))
        {
          uint32_t cntr = I2C_GETREG(priv, I2C_CNTR_OFFSET);
          cntr |= I2C_CNTR_M_STA;
          I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);
          priv->state = I2C_STATE_START;
        }
      else
        {
          /* I2C_M_NOSTART: continue sending/receiving data without address */

          if (priv->is_read)
            {
              /* For read, set ACK and wait for data */

              uint32_t cntr = I2C_GETREG(priv, I2C_CNTR_OFFSET);
              if (priv->bytes_remaining > 1)
                {
                  cntr |= I2C_CNTR_ACK;
                }
              else
                {
                  cntr &= ~I2C_CNTR_ACK;
                }
              I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);
              priv->state = I2C_STATE_DATA_RECV;
            }
          else if (priv->bytes_remaining > 0)
            {
              /* For write, send first data byte */

              I2C_PUTREG(priv, I2C_DATA_OFFSET, *priv->buffer++);
              priv->bytes_remaining--;
              priv->state = I2C_STATE_DATA_SEND;
            }
          else
            {
              i2c_next_message(priv);
            }
        }
    }
}

/****************************************************************************
 * Name: f1c100s_i2c_interrupt
 *
 * Description:
 *   I2C interrupt handler
 *
 ****************************************************************************/

static int f1c100s_i2c_interrupt(int irq, void *context, void *arg)
{
  FAR struct f1c100s_i2c_priv_s *priv = (FAR struct f1c100s_i2c_priv_s *)arg;
  uint32_t status;
  uint32_t cntr;

  /* Read status */
  status = I2C_GETREG(priv, I2C_STAT_OFFSET);

  /* Handle based on status */
  switch (status)
    {
      case I2C_STAT_TX_START:
      case I2C_STAT_TX_RESTART:
        /* START transmitted, send address */
        {
          FAR struct i2c_msg_s *msg = &priv->msgs[priv->msg_idx];
          uint8_t byte = (msg->addr << 1) | (priv->is_read ? 1 : 0);
          I2C_PUTREG(priv, I2C_DATA_OFFSET, byte);
          priv->state = I2C_STATE_ADDR_SEND;
        }
        break;

      case I2C_STAT_TX_AW_ACK:
        /* Address+write ACKed, start sending data */
        if (priv->bytes_remaining > 0)
          {
            I2C_PUTREG(priv, I2C_DATA_OFFSET, *priv->buffer++);
            priv->bytes_remaining--;
            priv->state = I2C_STATE_DATA_SEND;
          }
        else
          {
            /* No data, move to next message */
            i2c_next_message(priv);
          }
        break;

      case I2C_STAT_TXD_ACK:
        /* Data byte ACKed */
        if (priv->bytes_remaining > 0)
          {
            /* Send next byte */
            I2C_PUTREG(priv, I2C_DATA_OFFSET, *priv->buffer++);
            priv->bytes_remaining--;
          }
        else
          {
            /* This message done */
            i2c_next_message(priv);
          }
        break;

      case I2C_STAT_TX_AR_ACK:
        /* Address+read ACKed, prepare to receive */
        cntr = I2C_GETREG(priv, I2C_CNTR_OFFSET);
        if (priv->bytes_remaining > 1)
          {
            cntr |= I2C_CNTR_ACK;  /* ACK for non-last bytes */
          }
        else
          {
            cntr &= ~I2C_CNTR_ACK; /* NACK for last byte */
          }
        I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);
        priv->state = I2C_STATE_DATA_RECV;
        break;

      case I2C_STAT_RXD_ACK:
      case I2C_STAT_RXD_NAK:
        /* Data byte received */
        *priv->buffer++ = I2C_GETREG(priv, I2C_DATA_OFFSET);
        priv->bytes_remaining--;

        if (priv->bytes_remaining > 0)
          {
            /* Prepare for next byte */
            cntr = I2C_GETREG(priv, I2C_CNTR_OFFSET);
            if (priv->bytes_remaining > 1)
              {
                cntr |= I2C_CNTR_ACK;
              }
            else
              {
                cntr &= ~I2C_CNTR_ACK;
              }
            I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);
          }
        else
          {
            /* This message done */
            i2c_next_message(priv);
          }
        break;

      case I2C_STAT_BUS_ERROR:
      case I2C_STAT_LOST_ARB:
      case I2C_STAT_TX_AW_NAK:
      case I2C_STAT_TX_AR_NAK:
      case I2C_STAT_TXD_NAK:
        /* Error occurred */
        priv->state  = I2C_STATE_ERROR;
        priv->result = -EIO;

        cntr  = I2C_GETREG(priv, I2C_CNTR_OFFSET);
        cntr |= (I2C_CNTR_STOP | I2C_CNTR_INT_FLAG); /* STOP + W1C-clear flag */
        cntr &= ~(I2C_CNTR_INT_EN | I2C_CNTR_START);
        I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);

        nxsem_post(&priv->wait);
        return OK;

      default:
        /* Unexpected status - treat as error */
        i2cerr("Unexpected status: 0x%02lx\n", (unsigned long)status);
        priv->state  = I2C_STATE_ERROR;
        priv->result = -EIO;

        cntr  = I2C_GETREG(priv, I2C_CNTR_OFFSET);
        cntr |= (I2C_CNTR_STOP | I2C_CNTR_INT_FLAG); /* STOP + W1C-clear flag */
        cntr &= ~(I2C_CNTR_INT_EN | I2C_CNTR_START);
        I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);

        nxsem_post(&priv->wait);
        return OK;
    }

  /* Clear INT_FLAG to advance the TWI engine to the next state.
   * On real silicon INT_FLAG is WRITE-1-TO-CLEAR; writing 0 leaves it set,
   * so the IRQ stays asserted and the ISR re-fires back-to-back forever
   * (interrupt storm that freezes the whole system - reproduced on real
   * hardware when this masking was removed: Ctrl-C and every later NSH
   * command stopped getting any response at all).
   *
   * START/STOP are self-clear-to-0 bits: the GETREG above still reads
   * back whatever was last written (hardware does not clear them on its
   * own), so without explicitly masking them here the PUTREG writes the
   * stale 1 straight back and re-triggers START/STOP. Matches
   * t113_i2c.c's f1c100s_i2c_interrupt() tail, which carries the same
   * comment after hitting this exact failure mode there first.
   */

  cntr  = I2C_GETREG(priv, I2C_CNTR_OFFSET);
  cntr |= I2C_CNTR_INT_FLAG;
  cntr &= ~(I2C_CNTR_START | I2C_CNTR_STOP);
  I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);

  return OK;
}

/****************************************************************************
 * Name: i2c_wait_status
 *
 * Description:
 *   Wait for expected I2C status (for polling mode)
 *
 ****************************************************************************/

#ifndef CONFIG_F1C100S_I2C_INTERRUPTS
static int i2c_wait_status(FAR struct f1c100s_i2c_priv_s *priv,
                            uint8_t expected_status)
{
  clock_t start = clock_systime_ticks();
  uint8_t status;
  uint32_t cntr;

  /* Wait for interrupt flag */

  while (!(I2C_GETREG(priv, I2C_CNTR_OFFSET) & I2C_CNTR_INT_FLAG))
    {
      if ((clock_systime_ticks() - start) > I2C_TIMEOUT_TICKS)
        {
          i2cerr("ERROR: Timeout waiting for interrupt flag\n");
          return -ETIMEDOUT;
        }
    }

  /* Read status */

  status = I2C_GETREG(priv, I2C_STAT_OFFSET);

  /* Clear interrupt flag (Allwinner sun6i/suniv: write 1 to clear) */

  cntr = I2C_GETREG(priv, I2C_CNTR_OFFSET);
  cntr |= I2C_CNTR_INT_FLAG;
  I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);

  /* Check status */

  if (status != expected_status)
    {
      i2cerr("ERROR: Unexpected status: 0x%02x (expected 0x%02x)\n",
             status, expected_status);
      return -EIO;
    }

  return OK;
}
#endif /* !CONFIG_F1C100S_I2C_INTERRUPTS */

/****************************************************************************
 * Name: i2c_start
 *
 * Description:
 *   Send START condition (for polling mode)
 *
 ****************************************************************************/

#ifndef CONFIG_F1C100S_I2C_INTERRUPTS
static int i2c_start(FAR struct f1c100s_i2c_priv_s *priv)
{
  uint32_t cntr;

  /* Send START */

  cntr = I2C_GETREG(priv, I2C_CNTR_OFFSET);
  cntr |= I2C_CNTR_M_STA;
  I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);

  /* Wait for START complete */

  return i2c_wait_status(priv, I2C_STAT_TX_START);
}
#endif /* !CONFIG_F1C100S_I2C_INTERRUPTS */

/****************************************************************************
 * Name: i2c_stop
 *
 * Description:
 *   Send STOP condition
 *
 ****************************************************************************/

static void i2c_stop(FAR struct f1c100s_i2c_priv_s *priv)
{
  uint32_t cntr;

  /* Send STOP */

  cntr = I2C_GETREG(priv, I2C_CNTR_OFFSET);
  cntr |= (I2C_CNTR_STOP | I2C_CNTR_INT_FLAG);
  cntr &= ~I2C_CNTR_START;
  I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);

  /* Wait a bit for STOP to complete */

  up_udelay(10);
}

/****************************************************************************
 * Name: i2c_send_address
 *
 * Description:
 *   Send slave address (for polling mode)
 *
 ****************************************************************************/

#ifndef CONFIG_F1C100S_I2C_INTERRUPTS
static int i2c_send_address(FAR struct f1c100s_i2c_priv_s *priv,
                             uint8_t addr, bool read)
{
  uint8_t byte;
  uint8_t expected_status;

  /* Prepare address byte */

  byte = (addr << 1) | (read ? 1 : 0);

  /* Write address */

  I2C_PUTREG(priv, I2C_DATA_OFFSET, byte);

  /* Wait for ACK */

  expected_status = read ? I2C_STAT_TX_AR_ACK : I2C_STAT_TX_AW_ACK;
  return i2c_wait_status(priv, expected_status);
}

/****************************************************************************
 * Name: i2c_write_byte
 *
 * Description:
 *   Write a byte to I2C bus (for polling mode)
 *
 ****************************************************************************/

static int i2c_write_byte(FAR struct f1c100s_i2c_priv_s *priv,
                           uint8_t data)
{
  /* Write data */

  I2C_PUTREG(priv, I2C_DATA_OFFSET, data);

  /* Wait for ACK */

  return i2c_wait_status(priv, I2C_STAT_TXD_ACK);
}

/****************************************************************************
 * Name: i2c_read_byte
 *
 * Description:
 *   Read a byte from I2C bus (for polling mode)
 *
 ****************************************************************************/

static int i2c_read_byte(FAR struct f1c100s_i2c_priv_s *priv,
                          FAR uint8_t *data, bool last)
{
  uint32_t cntr;
  int ret;

  /* Set/clear ACK bit for next byte */

  cntr = I2C_GETREG(priv, I2C_CNTR_OFFSET);
  if (last)
    {
      cntr &= ~I2C_CNTR_ACK;  /* NACK for last byte */
    }
  else
    {
      cntr |= I2C_CNTR_ACK;   /* ACK for non-last byte */
    }

  I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);

  /* Wait for data */

  ret = i2c_wait_status(priv, last ? I2C_STAT_RXD_NAK : I2C_STAT_RXD_ACK);
  if (ret < 0)
    {
      return ret;
    }

  /* Read data */

  *data = I2C_GETREG(priv, I2C_DATA_OFFSET);
  return OK;
}

/****************************************************************************
 * Name: i2c_transfer_polling
 *
 * Description:
 *   Perform I2C transfer using polling mode
 *
 ****************************************************************************/

static int i2c_transfer_polling(FAR struct i2c_master_s *dev,
                                 FAR struct i2c_msg_s *msgs, int count)
{
  FAR struct f1c100s_i2c_priv_s *priv = (FAR struct f1c100s_i2c_priv_s *)dev;
  FAR struct i2c_msg_s *msg;
  int ret = OK;
  int i;
  int j;

  DEBUGASSERT(dev != NULL && msgs != NULL && count > 0);

  i2cinfo("Transfer (polling): count=%d\n", count);

  /* Lock the bus */

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  /* Process each message */

  for (i = 0; i < count; i++)
    {
      msg = &msgs[i];

      /* Send START (or repeated START) if not suppressed */

      if (!(msg->flags & I2C_M_NOSTART))
        {
          ret = i2c_start(priv);
          if (ret < 0)
            {
              i2cerr("ERROR: START failed: %d\n", ret);
              goto errout;
            }

          /* Send address */

          ret = i2c_send_address(priv, msg->addr,
                                  (msg->flags & I2C_M_READ) != 0);
          if (ret < 0)
            {
              i2cerr("ERROR: Address failed: %d\n", ret);
              goto errout;
            }
        }

      /* Transfer data */

      if (msg->flags & I2C_M_READ)
        {
          /* Read data */

          for (j = 0; j < msg->length; j++)
            {
              ret = i2c_read_byte(priv, &msg->buffer[j],
                                   j == msg->length - 1);
              if (ret < 0)
                {
                  i2cerr("ERROR: Read failed: %d\n", ret);
                  goto errout;
                }
            }
        }
      else
        {
          /* Write data */

          for (j = 0; j < msg->length; j++)
            {
              ret = i2c_write_byte(priv, msg->buffer[j]);
              if (ret < 0)
                {
                  i2cerr("ERROR: Write failed: %d\n", ret);
                  goto errout;
                }
            }
        }
    }

errout:
  /* Send STOP if not suppressed */

  if (!(msgs[count - 1].flags & I2C_M_NOSTOP))
    {
      i2c_stop(priv);
    }

  /* Unlock the bus */

  nxmutex_unlock(&priv->lock);

  return ret;
}
#endif /* !CONFIG_F1C100S_I2C_INTERRUPTS */

/****************************************************************************
 * Name: i2c_transfer
 *
 * Description:
 *   Perform I2C transfer (interrupt mode)
 *
 ****************************************************************************/

#ifdef CONFIG_F1C100S_I2C_INTERRUPTS
static int i2c_transfer(FAR struct i2c_master_s *dev,
                         FAR struct i2c_msg_s *msgs, int count)
{
  FAR struct f1c100s_i2c_priv_s *priv = (FAR struct f1c100s_i2c_priv_s *)dev;
  FAR struct i2c_msg_s *msg;
  uint32_t cntr;
  int ret = OK;

  DEBUGASSERT(dev != NULL && msgs != NULL && count > 0);

  i2cinfo("Transfer: count=%d\n", count);

  /* Lock the bus */

  ret = nxmutex_lock(&priv->lock);
  if (ret < 0)
    {
      return ret;
    }

  /* Setup transfer context */

  priv->msgs = msgs;
  priv->msg_count = count;
  priv->msg_idx = 0;
  priv->state = I2C_STATE_START;
  priv->result = OK;

  /* Initialize first message context */

  msg = &msgs[0];
  priv->buffer = msg->buffer;
  priv->bytes_remaining = msg->length;
  priv->is_read = (msg->flags & I2C_M_READ) != 0;

  /* Clear any pending interrupt flag before enabling interrupts */

  cntr = I2C_GETREG(priv, I2C_CNTR_OFFSET);
  cntr |= I2C_CNTR_INT_FLAG;
  I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);

  cntr &= ~I2C_CNTR_INT_FLAG;

  /* Enable interrupts */

  cntr |= I2C_CNTR_INT_EN;
  I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);

  /* Trigger START condition */

  cntr |= I2C_CNTR_M_STA;
  I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);

  /* Wait for transfer completion */

  ret = nxsem_tickwait(&priv->wait, MSEC2TICK(CONFIG_F1C100S_I2C_TIMEOUT_MS));
  if (ret < 0)
    {
      if (ret == -ETIMEDOUT)
        {
          /* Timeout is expected when no device responds */
          priv->result = -ETIMEDOUT;
        }
      else
        {
          i2cerr("ERROR: Wait interrupted: %d\n", ret);
          priv->result = ret;
        }
    }

  /* Disable interrupts */

  cntr = I2C_GETREG(priv, I2C_CNTR_OFFSET);
  cntr &= ~(I2C_CNTR_INT_EN | I2C_CNTR_INT_FLAG);
  I2C_PUTREG(priv, I2C_CNTR_OFFSET, cntr);

  if (ret < 0 || priv->result < 0)
    {
      /* On timeout or error, the controller may still be mid-transaction.
       * Soft-reset it to flush any pending state, then re-enable the bus.
       * This prevents stale interrupts or a locked controller from corrupting
       * the next transfer's state machine.
       */

      I2C_PUTREG(priv, I2C_SRST_OFFSET, I2C_SRST_RESET);
      up_udelay(10);
      i2c_set_frequency(priv, priv->frequency > 0 ? priv->frequency : 100000);
      I2C_PUTREG(priv, I2C_CNTR_OFFSET, I2C_CNTR_BUS_EN);
    }

  /* Only send STOP if not suppressed by I2C_M_NOSTOP flag on last message */

  if (!(msgs[count - 1].flags & I2C_M_NOSTOP) && ret == OK && priv->result == OK)
    {
      i2c_stop(priv);
    }

  /* Unlock the bus */

  nxmutex_unlock(&priv->lock);

  /* Return result from interrupt handler */

  return priv->result;
}
#endif /* CONFIG_F1C100S_I2C_INTERRUPTS */

/****************************************************************************
 * Name: i2c_reset
 *
 * Description:
 *   Reset I2C bus
 *
 ****************************************************************************/

#ifdef CONFIG_I2C_RESET
static int i2c_reset(FAR struct i2c_master_s *dev)
{
  FAR struct f1c100s_i2c_priv_s *priv = (FAR struct f1c100s_i2c_priv_s *)dev;

  /* Perform soft reset */

  I2C_PUTREG(priv, I2C_SRST_OFFSET, I2C_SRST_RESET);
  up_udelay(10);

  /* Re-initialize hardware */

  i2c_hw_initialize(priv);

  return OK;
}
#endif

/****************************************************************************
 * Name: i2c_set_frequency
 *
 * Description:
 *   Set I2C bus frequency
 *
 ****************************************************************************/

static void i2c_set_frequency(FAR struct f1c100s_i2c_priv_s *priv,
                               uint32_t frequency)
{
  uint32_t apb_freq;
  uint32_t clk_m;
  uint32_t clk_n;
  uint32_t ccr;

  /* Get APB frequency */

  apb_freq = f1c100s_get_apb_freq();

  /* Calculate clock divider
   * I2C_CLK = APB_CLK / (10 * (clk_m + 1) * 2^clk_n)
   * Aim for 100kHz (standard) or 400kHz (fast)
   */

  /* Simple calculation - can be optimized */

  clk_n = 0;
  while (frequency < (apb_freq / (10 * 16 * (1 << clk_n))) && clk_n < 7)
    {
      clk_n++;
    }

  clk_m = (apb_freq / (10 * frequency * (1 << clk_n))) - 1;
  if (clk_m > 15)
    {
      clk_m = 15;
    }

  /* Write clock control register */

  ccr = (clk_m << I2C_CCR_CLK_M_SHIFT) | (clk_n << I2C_CCR_CLK_N_SHIFT);
  I2C_PUTREG(priv, I2C_CCR_OFFSET, ccr);

  priv->frequency = frequency;

  i2cinfo("Frequency: %lu Hz (M=%lu, N=%lu)\n",
          (unsigned long)frequency, (unsigned long)clk_m, (unsigned long)clk_n);
}

/****************************************************************************
 * Name: i2c_hw_initialize
 *
 * Description:
 *   Initialize I2C hardware with timeout and error detection
 *
 * Return Value:
 *   OK on success, negative errno on failure
 *
 ****************************************************************************/

static int i2c_hw_initialize(FAR struct f1c100s_i2c_priv_s *priv)
{
  uint32_t regval;
  irqstate_t flags;
  int timeout;
#ifdef CONFIG_F1C100S_I2C_INTERRUPTS
  int ret;
#endif

  /* Cache base address for faster access */

  priv->base = priv->config->base;

  /* Enable I2C clock and deassert reset */

  if (priv->config->bus == 0)
    {
      /* Enable I2C0 clock (Bus Clock Gate 2, Bit 16) */

      f1c100s_clk_enable(CCU_BUS_CLK_GATING2, 16);

      /* Deassert I2C0 reset */

      f1c100s_reset_i2c0();

#ifdef CONFIG_F1C100S_I2C0_PE
      /* Configure GPIO PE11 (SCK, func 3) and PE12 (SDA, func 3) per F1C100s UM 4.2.3.17 (p.127-128).
       * PIO config registers are shared across peripherals (e.g. the
       * framebuffer driver also configures Port D), so RMW is
       * critical-section protected.
       */

      flags = enter_critical_section();
      regval = getreg32(GPIOE_BASE + GPIOx_CFG1_OFFSET);
      regval &= ~((0xf << 12) | (0xf << 16));
      regval |= (3 << 12) | (3 << 16);
      putreg32(regval, GPIOE_BASE + GPIOx_CFG1_OFFSET);
      leave_critical_section(flags);

      /* Enable pull-ups on PE11 and PE12 */

      flags = enter_critical_section();
      regval = getreg32(GPIOE_BASE + GPIOx_PUL0_OFFSET);
      regval &= ~((0x3 << 22) | (0x3 << 24));
      regval |= (1 << 22) | (1 << 24);
      putreg32(regval, GPIOE_BASE + GPIOx_PUL0_OFFSET);
      leave_critical_section(flags);
#else
      /* Configure GPIO PD0 (SDA, func 3) and PD12 (SCK, func 3) per Linux dtsi */

      flags = enter_critical_section();
      regval = getreg32(GPIOD_BASE + GPIOx_CFG0_OFFSET);
      regval &= ~(0xf << 0);
      regval |= (3 << 0);
      putreg32(regval, GPIOD_BASE + GPIOx_CFG0_OFFSET);

      regval = getreg32(GPIOD_BASE + GPIOx_CFG1_OFFSET);
      regval &= ~(0xf << 16);
      regval |= (3 << 16);
      putreg32(regval, GPIOD_BASE + GPIOx_CFG1_OFFSET);

      /* Enable pull-ups on PD0 and PD12 */

      regval = getreg32(GPIOD_BASE + GPIOx_PUL0_OFFSET);
      regval &= ~((0x3 << 0) | (0x3 << 24));
      regval |= (1 << 0) | (1 << 24);
      putreg32(regval, GPIOD_BASE + GPIOx_PUL0_OFFSET);
      leave_critical_section(flags);
#endif
    }
#ifdef CONFIG_F1C100S_I2C1
  else if (priv->config->bus == 1)
    {
      /* Enable I2C1 clock (Bus Clock Gate 2, Bit 17) */

      f1c100s_clk_enable(CCU_BUS_CLK_GATING2, 17);

      /* Deassert I2C1 reset */

      f1c100s_reset_i2c1();
    }
#endif
#ifdef CONFIG_F1C100S_I2C2
  else if (priv->config->bus == 2)
    {
      /* Enable I2C2 clock (Bus Clock Gate 2, Bit 18) */

      f1c100s_clk_enable(CCU_BUS_CLK_GATING2, 18);

      /* Deassert I2C2 reset */

      f1c100s_reset_i2c2();
    }
#endif

  /* Soft reset with timeout */

  i2cinfo("I2C%d: base=0x%08lx, SRST addr=0x%08lx\n",
          priv->config->bus, (unsigned long)priv->base,
          (unsigned long)(priv->base + I2C_SRST_OFFSET));

  I2C_PUTREG(priv, I2C_SRST_OFFSET, I2C_SRST_RESET);

  /* Wait for soft reset to complete (check if register is accessible) */

  timeout = 1000;
  while (timeout-- > 0)
    {
      regval = I2C_GETREG(priv, I2C_SRST_OFFSET);
      if ((regval & I2C_SRST_RESET) == 0)
        {
          break;
        }

      up_udelay(10);
    }

  if (timeout <= 0)
    {
      i2cerr("ERROR: I2C%d soft reset timeout (SRST=0x%02lx)\n",
             priv->config->bus, (unsigned long)regval);
      return -ETIMEDOUT;
    }

  /* Set frequency to 100kHz (standard mode) */

  i2c_set_frequency(priv, 100000);

  /* Enable I2C bus */

  I2C_PUTREG(priv, I2C_CNTR_OFFSET, I2C_CNTR_BUS_EN);

  /* Verify hardware is responding by reading back control register */

  regval = I2C_GETREG(priv, I2C_CNTR_OFFSET);
  if ((regval & I2C_CNTR_BUS_EN) == 0)
    {
      i2cerr("ERROR: I2C%d hardware not responding (CNTR=0x%02lx)\n",
             priv->config->bus, (unsigned long)regval);
      return -EIO;
    }

#ifdef CONFIG_F1C100S_I2C_INTERRUPTS
  /* Attach and enable interrupt (only in interrupt mode) */

  ret = irq_attach(priv->config->irq, f1c100s_i2c_interrupt, priv);
  if (ret < 0)
    {
      i2cerr("ERROR: Failed to attach IRQ %d: %d\n", priv->config->irq, ret);
      return ret;
    }

  up_enable_irq(priv->config->irq);

  i2cinfo("I2C%d initialized successfully (IRQ %d)\n",
          priv->config->bus, priv->config->irq);
#else
  i2cinfo("I2C%d initialized successfully (polling mode)\n",
          priv->config->bus);
#endif

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_i2cbus_initialize
 *
 * Description:
 *   Initialize the selected I2C bus
 *
 ****************************************************************************/

FAR struct i2c_master_s *f1c100s_i2cbus_initialize(int bus)
{
  FAR struct f1c100s_i2c_priv_s *priv = NULL;
  int ret;

  i2cinfo("bus=%d\n", bus);

  /* Select device */

  if (bus == 0)
    {
      priv = &g_i2c0_priv;
    }
#ifdef CONFIG_F1C100S_I2C1
  else if (bus == 1)
    {
      priv = &g_i2c1_priv;
    }
#endif
#ifdef CONFIG_F1C100S_I2C2
  else if (bus == 2)
    {
      priv = &g_i2c2_priv;
    }
#endif
  else
    {
      i2cerr("ERROR: Invalid bus: %d\n", bus);
      return NULL;
    }

  /* Check if already initialized */

  if (priv->initialized)
    {
      i2cinfo("I2C%d already initialized\n", bus);
      return (FAR struct i2c_master_s *)priv;
    }

  /* Initialize hardware with error handling */

  ret = i2c_hw_initialize(priv);
  if (ret < 0)
    {
      i2cerr("ERROR: I2C%d hardware init failed: %d\n", bus, ret);
      return NULL;
    }

  /* Set up I2C operations */

  priv->dev.ops = &g_i2c_ops;
  priv->initialized = true;

  i2cinfo("I2C%d initialized\n", bus);
  return (FAR struct i2c_master_s *)priv;
}

/****************************************************************************
 * Name: f1c100s_i2cbus_uninitialize
 *
 * Description:
 *   Uninitialize an I2C bus
 *
 ****************************************************************************/

int f1c100s_i2cbus_uninitialize(FAR struct i2c_master_s *dev)
{
  FAR struct f1c100s_i2c_priv_s *priv = (FAR struct f1c100s_i2c_priv_s *)dev;

  DEBUGASSERT(dev != NULL);

#ifdef CONFIG_F1C100S_I2C_INTERRUPTS
  /* Disable and detach interrupt (only in interrupt mode) */

  up_disable_irq(priv->config->irq);
  irq_detach(priv->config->irq);
#endif

  /* Disable I2C bus */

  I2C_PUTREG(priv, I2C_CNTR_OFFSET, 0);

  /* Clear initialized flag */

  priv->initialized = false;

  /* Disable clock gating for this I2C bus */

  switch (priv->config->bus)
    {
#ifdef CONFIG_F1C100S_I2C0
      case 0:
        f1c100s_clk_disable(CCU_BUS_CLK_GATING2, 16);  /* I2C0 */
        break;
#endif
#ifdef CONFIG_F1C100S_I2C1
      case 1:
        f1c100s_clk_disable(CCU_BUS_CLK_GATING2, 17);  /* I2C1 */
        break;
#endif
#ifdef CONFIG_F1C100S_I2C2
      case 2:
        f1c100s_clk_disable(CCU_BUS_CLK_GATING2, 18);  /* I2C2 */
        break;
#endif
      default:
        break;
    }

  return OK;
}
