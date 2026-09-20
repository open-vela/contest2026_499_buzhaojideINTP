/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_oneshot.c
 *
 * F1C100s Oneshot Timer Driver using Timer1
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/spinlock.h>
#include <nuttx/kmalloc.h>
#include <nuttx/timers/oneshot.h>

#include "arm_internal.h"
#include "hardware/f1c100s_timer.h"
#include "f1c100s_oneshot.h"

#ifdef CONFIG_F1C100S_ONESHOT

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct f1c100s_oneshot_lowerhalf_s
{
  struct oneshot_lowerhalf_s lh;
  uint32_t freq;
  volatile bool running;
  oneshot_callback_t callback;
  void *arg;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int f1c100s_oneshot_max_delay(struct oneshot_lowerhalf_s *lower,
                                      struct timespec *ts);
static int f1c100s_oneshot_start(struct oneshot_lowerhalf_s *lower,
                                  oneshot_callback_t callback, void *arg,
                                  const struct timespec *ts);
static int f1c100s_oneshot_cancel(struct oneshot_lowerhalf_s *lower,
                                   struct timespec *ts);
static int f1c100s_oneshot_current(struct oneshot_lowerhalf_s *lower,
                                    struct timespec *ts);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct oneshot_operations_s g_oneshot_ops =
{
  .max_delay = f1c100s_oneshot_max_delay,
  .start     = f1c100s_oneshot_start,
  .cancel    = f1c100s_oneshot_cancel,
  .current   = f1c100s_oneshot_current,
};

static struct f1c100s_oneshot_lowerhalf_s *g_oneshot_priv;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int f1c100s_oneshot_interrupt(int irq, void *context, void *arg)
{
  struct f1c100s_oneshot_lowerhalf_s *priv =
    (struct f1c100s_oneshot_lowerhalf_s *)arg;
  oneshot_callback_t callback;
  void *cbarg;

  putreg32(TMR_IRQ_TMR1, F1C100S_TMR_IRQ_STA);
  putreg32(0, F1C100S_TMR1_CTRL);

  priv->running = false;

  callback = priv->callback;
  cbarg = priv->arg;
  priv->callback = NULL;
  priv->arg = NULL;

  if (callback)
    {
      callback(&priv->lh, cbarg);
    }

  return OK;
}

static int f1c100s_oneshot_max_delay(struct oneshot_lowerhalf_s *lower,
                                      struct timespec *ts)
{
  ts->tv_sec = 178;
  ts->tv_nsec = 0;
  return OK;
}

static int f1c100s_oneshot_start(struct oneshot_lowerhalf_s *lower,
                                  oneshot_callback_t callback, void *arg,
                                  const struct timespec *ts)
{
  struct f1c100s_oneshot_lowerhalf_s *priv =
    (struct f1c100s_oneshot_lowerhalf_s *)lower;
  uint64_t usec;
  uint32_t ticks;
  irqstate_t flags;

  flags = enter_critical_section();

  usec = (uint64_t)ts->tv_sec * 1000000 + ts->tv_nsec / 1000;
  ticks = (uint32_t)((usec * TIMER_FREQ) / 1000000);
  if (ticks < 1) ticks = 1;

  priv->callback = callback;
  priv->arg = arg;
  priv->running = true;

  putreg32(0, F1C100S_TMR1_CTRL);
  putreg32(ticks, F1C100S_TMR1_INTV);
  putreg32(TMR_IRQ_TMR1, F1C100S_TMR_IRQ_STA);

  uint32_t irq_en = getreg32(F1C100S_TMR_IRQ_EN);
  putreg32(irq_en | TMR_IRQ_TMR1, F1C100S_TMR_IRQ_EN);

  putreg32(TMR_CTRL_EN | TMR_CTRL_RELOAD | TMR_CTRL_SRC_24M |
           TMR_CTRL_PRESCALE_1 | TMR_CTRL_MODE_SINGLE, F1C100S_TMR1_CTRL);

  leave_critical_section(flags);
  return OK;
}

static int f1c100s_oneshot_cancel(struct oneshot_lowerhalf_s *lower,
                                   struct timespec *ts)
{
  struct f1c100s_oneshot_lowerhalf_s *priv =
    (struct f1c100s_oneshot_lowerhalf_s *)lower;
  irqstate_t flags;
  uint32_t cur;
  uint64_t usec;

  flags = enter_critical_section();

  if (!priv->running)
    {
      ts->tv_sec = 0;
      ts->tv_nsec = 0;
      leave_critical_section(flags);
      return OK;
    }

  putreg32(0, F1C100S_TMR1_CTRL);
  cur = getreg32(F1C100S_TMR1_CUR);

  usec = ((uint64_t)cur * 1000000) / TIMER_FREQ;
  ts->tv_sec = usec / 1000000;
  ts->tv_nsec = (usec % 1000000) * 1000;

  priv->running = false;
  priv->callback = NULL;
  priv->arg = NULL;

  leave_critical_section(flags);
  return OK;
}

static int f1c100s_oneshot_current(struct oneshot_lowerhalf_s *lower,
                                    struct timespec *ts)
{
  uint32_t cur = getreg32(F1C100S_TMR1_CUR);
  uint64_t usec = ((uint64_t)cur * 1000000) / TIMER_FREQ;
  ts->tv_sec = usec / 1000000;
  ts->tv_nsec = (usec % 1000000) * 1000;
  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

struct oneshot_lowerhalf_s *f1c100s_oneshot_initialize(void)
{
  struct f1c100s_oneshot_lowerhalf_s *priv;

  priv = kmm_zalloc(sizeof(struct f1c100s_oneshot_lowerhalf_s));
  if (priv == NULL)
    {
      return NULL;
    }

  priv->lh.ops = &g_oneshot_ops;
  priv->freq = TIMER_FREQ;
  priv->running = false;
  priv->callback = NULL;
  priv->arg = NULL;

  irq_attach(F1C100S_IRQ_TMR1, f1c100s_oneshot_interrupt, priv);
  up_enable_irq(F1C100S_IRQ_TMR1);

  g_oneshot_priv = priv;
  return &priv->lh;
}

#endif /* CONFIG_F1C100S_ONESHOT */
