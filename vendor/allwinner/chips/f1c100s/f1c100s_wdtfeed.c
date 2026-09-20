/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_wdtfeed.c
 *
 * Spec §5: AP feeds the WDT from a periodic worker. BL stay-in-BL
 * loop also feeds so a hung wait still resets through awboot.
 ****************************************************************************/

#include <nuttx/config.h>

#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/compiler.h>
#include <nuttx/timers/watchdog.h>
#include <nuttx/wqueue.h>
#include <nuttx/clock.h>

#include "f1c100s_wdt.h"

#ifdef CONFIG_F1C100S_WDT

#ifndef CONFIG_F1C100S_WDT_TIMEOUT_MS
#  define CONFIG_F1C100S_WDT_TIMEOUT_MS 5000
#endif

#ifndef CONFIG_F1C100S_WDT_FEED_MS
#  define CONFIG_F1C100S_WDT_FEED_MS 2000
#endif

static int g_wdtfd = -1;
#ifdef CONFIG_SCHED_HPWORK
static struct work_s g_wdtwork;
#endif

int f1c100s_wdtfeed_keepalive(void)
{
  int ret;

  if (g_wdtfd < 0)
    {
      return -ENODEV;
    }

  ret = ioctl(g_wdtfd, WDIOC_KEEPALIVE, 0);
  return ret < 0 ? -errno : 0;
}

int f1c100s_wdtfeed_init(void)
{
  int timeout = CONFIG_F1C100S_WDT_TIMEOUT_MS;
  int ret;

  if (g_wdtfd >= 0)
    {
      return 0;
    }

  g_wdtfd = open("/dev/watchdog0", O_RDONLY);
  if (g_wdtfd < 0)
    {
      return -errno;
    }

  ret = ioctl(g_wdtfd, WDIOC_SETTIMEOUT, (unsigned long)timeout);
  if (ret < 0)
    {
      syslog(LOG_ERR, "wdt SETTIMEOUT failed\n");
    }

  ret = ioctl(g_wdtfd, WDIOC_START, 0);
  if (ret < 0)
    {
      syslog(LOG_ERR, "wdt START failed\n");
      close(g_wdtfd);
      g_wdtfd = -1;
      return -errno;
    }

  syslog(LOG_INFO, "wdt: timeout %d ms, feed %d ms\n",
         timeout, CONFIG_F1C100S_WDT_FEED_MS);
  return 0;
}

#ifdef CONFIG_SCHED_HPWORK
static void wdtfeed_worker(FAR void *arg)
{
  UNUSED(arg);
  f1c100s_wdtfeed_keepalive();
  work_queue(HPWORK, &g_wdtwork, wdtfeed_worker, NULL,
             MSEC2TICK(CONFIG_F1C100S_WDT_FEED_MS));
}

int f1c100s_wdtfeed_start_worker(void)
{
  int ret;

  ret = f1c100s_wdtfeed_init();
  if (ret < 0)
    {
      return ret;
    }

  return work_queue(HPWORK, &g_wdtwork, wdtfeed_worker, NULL,
                    MSEC2TICK(CONFIG_F1C100S_WDT_FEED_MS));
}
#endif

#endif /* CONFIG_F1C100S_WDT */
