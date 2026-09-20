/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_wdt.c
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

#include <stdint.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/timers/watchdog.h>
#include <nuttx/irq.h>

#include "arm_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define F1C_WDT_BASE      0x01c20ca0

#define WDT_IRQ_EN        (F1C_WDT_BASE + 0x00)
#define WDT_IRQ_STA       (F1C_WDT_BASE + 0x04)
#define WDT_CTRL          (F1C_WDT_BASE + 0x10)
#define WDT_CFG           (F1C_WDT_BASE + 0x14)
#define WDT_MODE          (F1C_WDT_BASE + 0x18)

/* WDT_CTRL bits */

#define WDT_CTRL_KEY      (0xa57 << 1)
#define WDT_CTRL_RESTART  (1 << 0)

/* WDT_CFG bits */

#define WDT_CFG_RESET     (1 << 0)
#define WDT_CFG_IRQ       (2 << 0)

/* WDT_MODE bits */

#define WDT_MODE_EN       (1 << 0)
#define WDT_MODE_INTV(n)  ((n) << 4)  /* Interval: 0=0.5s, 1=1s, ... 11=16s */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct f1c100s_wdt_s
{
  struct watchdog_lowerhalf_s lower;
  uint32_t timeout;
  bool     started;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int f1c100s_wdt_start(struct watchdog_lowerhalf_s *lower);
static int f1c100s_wdt_stop(struct watchdog_lowerhalf_s *lower);
static int f1c100s_wdt_keepalive(struct watchdog_lowerhalf_s *lower);
static int f1c100s_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                  struct watchdog_status_s *status);
static int f1c100s_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                                   uint32_t timeout);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct watchdog_ops_s g_wdt_ops =
{
  .start      = f1c100s_wdt_start,
  .stop       = f1c100s_wdt_stop,
  .keepalive  = f1c100s_wdt_keepalive,
  .getstatus  = f1c100s_wdt_getstatus,
  .settimeout = f1c100s_wdt_settimeout,
};

static struct f1c100s_wdt_s g_wdt =
{
  .lower =
    {
      .ops = &g_wdt_ops,
    },
  .timeout = 10000,  /* Default 10 seconds */
  .started = false,
};

/* Timeout table: index -> milliseconds */

static const uint32_t g_timeout_table[] =
{
  500,    /* 0: 0.5s */
  1000,   /* 1: 1s */
  2000,   /* 2: 2s */
  3000,   /* 3: 3s */
  4000,   /* 4: 4s */
  5000,   /* 5: 5s */
  6000,   /* 6: 6s */
  8000,   /* 7: 8s */
  10000,  /* 8: 10s */
  12000,  /* 9: 12s */
  14000,  /* 10: 14s */
  16000,  /* 11: 16s */
};

#define TIMEOUT_TABLE_SIZE (sizeof(g_timeout_table) / sizeof(g_timeout_table[0]))

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int f1c100s_wdt_start(struct watchdog_lowerhalf_s *lower)
{
  struct f1c100s_wdt_s *priv = (struct f1c100s_wdt_s *)lower;
  uint32_t intv = 8;  /* Default 10s */
  uint32_t i;

  /* Find closest timeout interval */

  for (i = 0; i < TIMEOUT_TABLE_SIZE; i++)
    {
      if (g_timeout_table[i] >= priv->timeout)
        {
          intv = i;
          break;
        }
    }

  if (i >= TIMEOUT_TABLE_SIZE)
    {
      intv = TIMEOUT_TABLE_SIZE - 1;
    }

  /* Configure for system reset */

  putreg32(WDT_CFG_RESET, WDT_CFG);

  /* Set interval and enable */

  putreg32(WDT_MODE_INTV(intv) | WDT_MODE_EN, WDT_MODE);

  /* Restart counter */

  putreg32(WDT_CTRL_KEY | WDT_CTRL_RESTART, WDT_CTRL);

  priv->started = true;
  return OK;
}

static int f1c100s_wdt_stop(struct watchdog_lowerhalf_s *lower)
{
  struct f1c100s_wdt_s *priv = (struct f1c100s_wdt_s *)lower;

  /* Disable watchdog */

  putreg32(0, WDT_MODE);
  priv->started = false;
  return OK;
}

void f1c100s_wdt_force_stop(void)
{
  putreg32(0, WDT_MODE);
  g_wdt.started = false;
}

void f1c100s_wdt_system_reset(void)
{
  up_irq_save();

  /* 0 = the shortest supported interval (about 0.5 s). */
  putreg32(WDT_CFG_RESET, WDT_CFG);
  putreg32(WDT_MODE_INTV(0) | WDT_MODE_EN, WDT_MODE);
  putreg32(WDT_CTRL_KEY | WDT_CTRL_RESTART, WDT_CTRL);

  for (;;)
    {
      /* The watchdog owns the reset path; do not return to callers. */
    }
}

static int f1c100s_wdt_keepalive(struct watchdog_lowerhalf_s *lower)
{
  /* Restart counter */

  putreg32(WDT_CTRL_KEY | WDT_CTRL_RESTART, WDT_CTRL);
  return OK;
}

static int f1c100s_wdt_getstatus(struct watchdog_lowerhalf_s *lower,
                                  struct watchdog_status_s *status)
{
  struct f1c100s_wdt_s *priv = (struct f1c100s_wdt_s *)lower;

  status->flags = 0;
  if (priv->started)
    {
      status->flags |= WDFLAGS_ACTIVE;
    }

  status->timeout = priv->timeout;
  status->timeleft = priv->timeout;  /* Cannot read remaining time */
  return OK;
}

static int f1c100s_wdt_settimeout(struct watchdog_lowerhalf_s *lower,
                                   uint32_t timeout)
{
  struct f1c100s_wdt_s *priv = (struct f1c100s_wdt_s *)lower;

  if (timeout < 500 || timeout > 16000)
    {
      return -ERANGE;
    }

  priv->timeout = timeout;

  /* If running, restart with new timeout */

  if (priv->started)
    {
      f1c100s_wdt_start(lower);
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_wdt_initialize
 ****************************************************************************/

int f1c100s_wdt_initialize(const char *devpath)
{
  return watchdog_register(devpath, &g_wdt.lower) != NULL ? OK : -ENODEV;
}
