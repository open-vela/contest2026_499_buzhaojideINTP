/****************************************************************************
 * vendor/allwinner/boards/f1c100s/demo/src/f1c100s_bringup.c
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

#include <stdio.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>
#include <debug.h>
#include <string.h>

#include <nuttx/board.h>
#include <nuttx/fs/fs.h>
#include <nuttx/audio/audio.h>
#include <nuttx/mtd/mtd.h>
#include <nuttx/spi/spi.h>

#include "arm_internal.h"
#include "f1c100s_demo.h"
#include "hardware/f1c100s_audio.h"
#include "hardware/f1c100s_spi.h"
#include "hardware/f1c100s_usb.h"

#ifdef CONFIG_FB_UPDATE
#include <nuttx/kthread.h>
#include <nuttx/video/fb.h>
#endif

#ifdef CONFIG_F1C100S_WDT
#include "f1c100s_wdt.h"
#endif

#ifdef CONFIG_F1C100S_BOOTKV
#include "f1c100s_bootkv.h"
#include "bootctl.h"
#endif

#include "f1c100s_st7789.h"
#include "f1c100s_crashlog.h"

#include "f1c100s_partitions.h"

#ifdef CONFIG_F1C100S_SDIO
#include <nuttx/sdio.h>
#include <nuttx/mmcsd.h>
#if defined(CONFIG_MBR_PARTITION) || defined(CONFIG_GPT_PARTITION)
#include <nuttx/fs/partition.h>
#endif
#include "hardware/f1c100s_sdio.h"
#endif

#ifdef CONFIG_F1C100S_FB
#include "hardware/f1c100s_de.h"
#endif

#ifdef CONFIG_F1C100S_PWM
#include <nuttx/timers/pwm.h>
FAR struct pwm_lowerhalf_s *f1c100s_pwm_initialize(int channel);
#endif

#ifdef CONFIG_F1C100S_ONESHOT
#include <nuttx/timers/oneshot.h>
FAR struct oneshot_lowerhalf_s *f1c100s_oneshot_initialize(void);
#endif

#ifdef CONFIG_INPUT_BUTTONS
#include <nuttx/input/buttons.h>
#endif

#ifdef CONFIG_F1C100S_LRADC
int f1c100s_adcbuttons_initialize(void);
#endif

#ifdef CONFIG_CDCACM
#include <nuttx/usb/cdcacm.h>
#endif

#ifdef CONFIG_USBMSC
#include <nuttx/usb/usbmsc.h>
#endif

#ifdef CONFIG_USBADB
#include <nuttx/usb/adb.h>
#endif

#ifdef CONFIG_RNDIS
#include <nuttx/usb/rndis.h>
#endif

#ifdef CONFIG_NET_CDCECM
#include <nuttx/usb/cdcecm.h>
#endif

#ifdef CONFIG_USBDEV_COMPOSITE
#include <nuttx/usb/composite.h>
#endif

#ifdef CONFIG_I2C_DRIVER
#include <nuttx/i2c/i2c_master.h>
#endif

#if defined(CONFIG_F1C100S_I2C0) || defined(CONFIG_F1C100S_I2C1) || \
    defined(CONFIG_F1C100S_I2C2)
FAR struct i2c_master_s *f1c100s_i2cbus_initialize(int bus);
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_MTD
static int f1c100s_register_mtdpart(FAR struct mtd_dev_s *parent,
                                    FAR const char *name,
                                    off_t off_bytes, off_t size_bytes)
{
  struct mtd_geometry_s geo;
  FAR struct mtd_dev_s *part;
  int ret;

  memset(&geo, 0, sizeof(geo));
  ret = MTD_IOCTL(parent, MTDIOC_GEOMETRY, (unsigned long)(uintptr_t)&geo);
  if (ret < 0)
    {
      return ret;
    }

  if (geo.blocksize == 0 ||
      (off_bytes % geo.blocksize) != 0 ||
      (size_bytes % geo.blocksize) != 0)
    {
      syslog(LOG_ERR, "mtd part %s: geometry mismatch\n", name);
      return -EINVAL;
    }

  part = mtd_partition(parent, off_bytes / (off_t)geo.blocksize,
                       size_bytes / (off_t)geo.blocksize);
  if (part == NULL)
    {
      return -ENOMEM;
    }

  ret = register_mtddriver(name, part, 0666, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "register %s failed: %d\n", name, ret);
    }

  return ret;
}
#endif

#ifdef CONFIG_USBDEV_COMPOSITE

/* Matches F1C100S_NENDPOINTS in f1c100s_usbdev.c (chip-private, not
 * exposed via a header): EP0 + 5 configurable endpoints.
 */

#define F1C100S_COMPOSITE_NENDPOINTS 4

/* Compile-time endpoint budget check. Each composited function's real
 * endpoint count (CDCACM_NUM_EPS=3, USBADB_NUM_EPS=2, RNDIS_NUM_EPS=3;
 * fastboot reuses the ADB driver so it costs the same 2) is 0 unless
 * this board actually selects that function, so this sums only what a
 * given .config really composites -- catches an over-budget selection
 * (e.g. ADB+RNDIS needs 5, only EP1-EP3 work) at
 * build time instead of a runtime -ENOSPC (or worse, silently wrong
 * endpoint numbers) discovered on real hardware.
 */

#ifdef CONFIG_F1C100S_USB_CDCACM
#  define F1C100S_USB_EP_CDCACM   CDCACM_NUM_EPS
#else
#  define F1C100S_USB_EP_CDCACM   0
#endif

#ifdef CONFIG_F1C100S_USB_ADB
#  define F1C100S_USB_EP_ADB      USBADB_NUM_EPS
#else
#  define F1C100S_USB_EP_ADB      0
#endif

#ifdef CONFIG_F1C100S_USB_RNDIS
  /* RNDIS_NUM_EPS is private to drivers/usbdev/rndis.c (not exposed via
   * include/nuttx/usb/rndis.h the way CDCACM_NUM_EPS/USBADB_NUM_EPS are),
   * so this mirrors its value (1 interrupt IN + 1 bulk IN + 1 bulk OUT)
   * literally. If that driver's endpoint count ever changes, this needs
   * updating by hand -- there's no way to pull it in automatically.
   */
#  define F1C100S_USB_EP_RNDIS    3
#else
#  define F1C100S_USB_EP_RNDIS    0
#endif

#ifdef CONFIG_F1C100S_USB_FASTBOOT
#  define F1C100S_USB_EP_FASTBOOT USBADB_NUM_EPS
#else
#  define F1C100S_USB_EP_FASTBOOT 0
#endif

#define F1C100S_USB_EP_TOTAL \
  (F1C100S_USB_EP_CDCACM + F1C100S_USB_EP_ADB + \
   F1C100S_USB_EP_RNDIS + F1C100S_USB_EP_FASTBOOT)

#if defined(CONFIG_USBDEV_COMPOSITE) && \
    (F1C100S_USB_EP_TOTAL > (F1C100S_COMPOSITE_NENDPOINTS - 1))
#  error "F1C100s USB composite: selected functions need more endpoints than \
the hardware has (3 usable: EP1-EP3). Deselect one of \
CONFIG_F1C100S_USB_CDCACM / _ADB / _RNDIS / _FASTBOOT."
#endif

static int f1c100s_composite_initialize(void)
{
  struct composite_devdesc_s dev[3];
  int ifnobase = 0;
  int strbase = COMPOSITE_NSTRIDS;

  /* EP0 is reserved. epno written here IS the USB address and the
   * MUSB physical INDEX (allocep: eplist[epno].epphy = i, no remap).
   * Each function's get_composite_devdesc() leaves epno[] zeroed.
   */

  int epbase = 1;
  int dev_idx = 0;
  FAR void *handle;

#ifdef CONFIG_F1C100S_USB_CDCACM
  cdcacm_get_composite_devdesc(&dev[dev_idx]);
  dev[dev_idx].devinfo.ifnobase = ifnobase;
  dev[dev_idx].devinfo.strbase = strbase;
  dev[dev_idx].devinfo.epno[CDCACM_EP_INTIN_IDX]   = epbase;
  dev[dev_idx].devinfo.epno[CDCACM_EP_BULKIN_IDX]  = epbase + 1;
  dev[dev_idx].devinfo.epno[CDCACM_EP_BULKOUT_IDX] = epbase + 2;
  dev[dev_idx].minor = 0;
  ifnobase += dev[dev_idx].devinfo.ninterfaces;
  strbase += dev[dev_idx].devinfo.nstrings;
  epbase += dev[dev_idx].devinfo.nendpoints;
  dev_idx++;
#endif

#ifdef CONFIG_F1C100S_USB_RNDIS
  usbdev_rndis_get_composite_devdesc(&dev[dev_idx]);
  dev[dev_idx].devinfo.ifnobase = ifnobase;
  dev[dev_idx].devinfo.strbase = strbase;
  dev[dev_idx].devinfo.epno[RNDIS_EP_INTIN_IDX]   = epbase;
  dev[dev_idx].devinfo.epno[RNDIS_EP_BULKIN_IDX]  = epbase + 1;
  dev[dev_idx].devinfo.epno[RNDIS_EP_BULKOUT_IDX] = epbase + 2;
  dev[dev_idx].minor = 0;
  ifnobase += dev[dev_idx].devinfo.ninterfaces;
  strbase += dev[dev_idx].devinfo.nstrings;
  epbase += dev[dev_idx].devinfo.nendpoints;
  dev_idx++;
#endif

#ifdef CONFIG_F1C100S_USB_ADB
  usbdev_adb_get_composite_devdesc(&dev[dev_idx]);
  dev[dev_idx].devinfo.ifnobase = ifnobase;
  dev[dev_idx].devinfo.strbase = strbase;
  dev[dev_idx].devinfo.epno[USBADB_EP_BULKIN_IDX]  = epbase;
  dev[dev_idx].devinfo.epno[USBADB_EP_BULKOUT_IDX] = epbase + 1;
  dev[dev_idx].minor = 0;
  ifnobase += dev[dev_idx].devinfo.ninterfaces;
  strbase += dev[dev_idx].devinfo.nstrings;
  epbase += dev[dev_idx].devinfo.nendpoints;
  dev_idx++;
#endif

#ifdef CONFIG_F1C100S_USB_FASTBOOT
  usbdev_adb_get_composite_devdesc(&dev[dev_idx]);
  dev[dev_idx].devinfo.ifnobase = ifnobase;
  dev[dev_idx].devinfo.strbase = strbase;
  dev[dev_idx].devinfo.epno[USBADB_EP_BULKIN_IDX]  = epbase;
  dev[dev_idx].devinfo.epno[USBADB_EP_BULKOUT_IDX] = epbase + 1;
  dev[dev_idx].minor = 0;
  ifnobase += dev[dev_idx].devinfo.ninterfaces;
  strbase += dev[dev_idx].devinfo.nstrings;
  epbase += dev[dev_idx].devinfo.nendpoints;
  dev_idx++;
#endif

  if (dev_idx == 0)
    {
      uwarn("No USB functions configured for composite\n");
      return -ENODEV;
    }

  if (epbase > F1C100S_COMPOSITE_NENDPOINTS)
    {
      uerr("ERROR: composite needs %d endpoints, hardware has %d\n",
           epbase - 1, F1C100S_COMPOSITE_NENDPOINTS - 1);
      return -ENOSPC;
    }

  handle = composite_initialize(composite_getdevdescs(), dev, dev_idx);
  if (handle == NULL)
    {
      uerr("ERROR: composite_initialize failed\n");
      return -ENODEV;
    }

  uinfo("USB Composite device initialized with %d functions\n",
        dev_idx);
  return OK;
}
#endif /* CONFIG_USBDEV_COMPOSITE */

#ifdef CONFIG_FB_UPDATE
static int f1c100s_fb_autoupdate(int argc, FAR char **argv)
{
  int fd0 = -1;
  int fd1 = -1;
  struct fb_area_s a0;
  struct fb_area_s a1;

  UNUSED(argc);
  UNUSED(argv);

  memset(&a0, 0, sizeof(a0));
  memset(&a1, 0, sizeof(a1));
#ifdef CONFIG_LCD_ST7789
  a0.w = CONFIG_LCD_ST7789_XRES;
  a0.h = CONFIG_LCD_ST7789_YRES;
  fd0 = open("/dev/fb0", O_RDWR);
#endif
#ifdef CONFIG_F1C100S_FB
  a1.w = CONFIG_F1C100S_FB_WIDTH;
  a1.h = CONFIG_F1C100S_FB_HEIGHT;
  fd1 = open("/dev/fb1", O_RDWR);
  if (fd1 < 0)
    {
      fd1 = open("/dev/fb0", O_RDWR);
    }
#endif

  syslog(LOG_INFO, "fbupd: fb0=%d fb1=%d (periodic FBIO_UPDATE)\n", fd0, fd1);

  for (; ; )
    {
      if (fd0 >= 0)
        {
          ioctl(fd0, FBIO_UPDATE, (unsigned long)&a0);
        }

      if (fd1 >= 0 && fd1 != fd0)
        {
          ioctl(fd1, FBIO_UPDATE, (unsigned long)&a1);
        }

      usleep(200000);
    }

  return 0;
}
#endif

#if defined(CONFIG_F1C100S_SDIO) && (defined(CONFIG_MBR_PARTITION) || defined(CONFIG_GPT_PARTITION))
static void f1c100s_partition_handler(FAR struct partition_s *part, FAR void *arg)
{
  char devname[32];
  int ret;

  snprintf(devname, sizeof(devname), "/dev/mmcsd0p%d", (int)part->index + 1);
  ret = register_blockpartition(devname, 0666, "/dev/mmcsd0",
                                part->firstblock, part->nblocks);
  syslog(LOG_INFO, "Registered partition %s (start=%jd, count=%jd): %d\n",
         devname, (intmax_t)part->firstblock, (intmax_t)part->nblocks, ret);

  if (part->name[0] != '\0' && strcmp(part->name, devname + 5) != 0)
    {
      char named_dev[128];
      snprintf(named_dev, sizeof(named_dev), "/dev/%s", part->name);
      register_blockpartition(named_dev, 0666, "/dev/mmcsd0",
                              part->firstblock, part->nblocks);
    }
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_bringup
 *
 * Description:
 *   Perform board-specific initialization. Called from board_late_initialize
 *   or board_app_initialize.
 *
 ****************************************************************************/

int f1c100s_bringup(void)
{
  int ret = OK;
  syslog(LOG_INFO, "bringup\n");
  f1c100s_crashlog_print();

#ifdef CONFIG_F1C100S_ST7789
  ret = f1c100s_st7789_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "st7789 init failed: %d\n", ret);
    }
  else
    {
      f1c100s_boot_status("Running");
    }
#endif

  /* /proc and /tmp are mounted by etc/init.d/rc.sysinit (runs once
   * per boot via CONFIG_ETC_ROMFS, guarded by the same
   * CONFIG_FS_PROCFS/CONFIG_FS_TMPFS). Mounting them here too used to
   * make rc.sysinit's mount fail with ENOTDIR ("target exists and is
   * a special node") on every boot, since the first mount already
   * replaced the pseudo-dir inode.
   */

#ifdef CONFIG_F1C100S_WDT
  ret = f1c100s_wdt_initialize("/dev/watchdog0");
  if (ret < 0)
    {
      ferr("ERROR: WDT init failed: %d\n", ret);
    }
  else
    {
#ifndef CONFIG_BOARDCTL_BOOT_IMAGE
      ret = f1c100s_wdtfeed_start_worker();
      if (ret < 0)
        {
          syslog(LOG_ERR, "wdt feed worker: %d\n", ret);
        }
#else
      ret = f1c100s_wdtfeed_init();
      if (ret < 0)
        {
          syslog(LOG_ERR, "wdt feed init: %d\n", ret);
        }
#endif
    }
#endif

#ifdef CONFIG_F1C100S_FB
  /* ST7789 (SPI1, separate hardware path) always owns /dev/fb0 via
   * fb_register(0, 0) above. Independent peripherals, independent GPIO
   * banks (PA0-3 vs PD0-21) -- no resource conflict, so give the RGB
   * LCD /dev/fb1.
   */
#  ifdef CONFIG_F1C100S_ST7789
  ret = f1c100s_fb_initialize(1);
#  else
  ret = f1c100s_fb_initialize(0);
#  endif
  if (ret < 0)
    {
      gerr("ERROR: FB init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_F1C100S_LRADC
  ret = f1c100s_adcbuttons_initialize();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: ADC buttons init failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_INPUT_BUTTONS_LOWER
  ret = btn_lower_initialize("/dev/buttons");
  if (ret < 0)
    {
      syslog(LOG_ERR, "btn_lower_initialize failed: %d\n", ret);
    }
#endif

#ifdef CONFIG_F1C100S_PWM
  FAR struct pwm_lowerhalf_s *pwm;

#ifdef CONFIG_F1C100S_PWM0
  pwm = f1c100s_pwm_initialize(0);
  if (pwm)
    {
      ret = pwm_register("/dev/pwm0", pwm);
      if (ret < 0)
        {
          syslog(LOG_ERR, "PWM0 register failed: %d\n", ret);
        }
    }
#endif

#ifdef CONFIG_F1C100S_PWM1
  pwm = f1c100s_pwm_initialize(1);
  if (pwm)
    {
      ret = pwm_register("/dev/pwm1", pwm);
      if (ret < 0)
        {
          syslog(LOG_ERR, "PWM1 register failed: %d\n", ret);
        }

#if defined(CONFIG_F1C100S_FB) && defined(CONFIG_F1C100S_FB_BACKLIGHT_PWM)
      /* Auto-start PWM1 backlight for 40-pin RGB LCD (PE6 -> AP3019AKTR).
       * Set standard 50 kHz switching / dimming frequency and configured duty.
       */

      struct pwm_info_s info;
      info.frequency = 50000;
      info.duty = ((uint32_t)CONFIG_F1C100S_FB_BACKLIGHT_DUTY << 16) / 100;
      if (pwm->ops->setup(pwm) == OK)
        {
          pwm->ops->start(pwm, &info);
          syslog(LOG_INFO, "RGB LCD Backlight enabled on PWM1 (%d%% duty)\n",
                 CONFIG_F1C100S_FB_BACKLIGHT_DUTY);
        }
#endif
    }
#endif
#endif /* CONFIG_F1C100S_PWM */

#ifdef CONFIG_I2C
#ifdef CONFIG_I2C_DRIVER
#ifdef CONFIG_F1C100S_I2C0
  FAR struct i2c_master_s *i2c0 = f1c100s_i2cbus_initialize(0);
  if (i2c0 != NULL)
    {
      ret = i2c_register(i2c0, 0);
      if (ret < 0)
        {
          i2cerr("I2C0: Register failed: %d\n", ret);
        }

#ifdef CONFIG_MTD_AT24XX
      FAR struct mtd_dev_s *at24 = at24c_initialize(i2c0);
      if (at24 != NULL)
        {
          ret = ftl_initialize(0, at24);
          if (ret < 0)
            {
              syslog(LOG_ERR, "ERROR: FTL init failed: %d\n", ret);
            }
        }
#endif
    }
#endif

#ifdef CONFIG_F1C100S_I2C1
  ret = i2c_register(f1c100s_i2cbus_initialize(1), 1);
#endif

#ifdef CONFIG_F1C100S_I2C2
  ret = i2c_register(f1c100s_i2cbus_initialize(2), 2);
#endif
#endif /* CONFIG_I2C_DRIVER */
#endif

#ifdef CONFIG_SPI
#ifdef CONFIG_MTD_W25
  {
    FAR struct spi_dev_s *spi;
    FAR struct mtd_dev_s *mtd;

    spi = f1c100s_spibus_initialize(0);
    if (spi != NULL)
      {
        mtd = w25_initialize(spi);
        if (mtd != NULL)
          {
            ret = register_mtddriver("/dev/mtd0", mtd, 0, NULL);
            if (ret < 0)
              {
                uerr("ERROR: register_mtddriver(/dev/mtd0) failed: %d\n", ret);
              }
            else
              {
                uinfo("W25 Flash registered as /dev/mtd0\n");
              }

#ifdef CONFIG_F1C100S_BOOTKV
            {
              int kvret = f1c100s_bootkv_bind(mtd, F1C100S_PART_KV_OFFSET);
              if (kvret < 0)
                {
                  syslog(LOG_ERR, "bootkv bind failed: %d\n", kvret);
                }

#ifndef CONFIG_BOARDCTL_BOOT_IMAGE
              /* AP path only (BL selects BOARDCTL_BOOT_IMAGE). Mark this
               * boot successful now that the KV is bound, so BL's
               * fail_count resets. Same "run every time" contract as
               * openvela bootctl_success().
               */

              if (kvret == 0)
                {
                  int bcret = bootctl_success();
                  if (bcret < 0 && bcret != -ENOENT)
                    {
                      syslog(LOG_ERR, "bootctl_success: %d\n", bcret);
                    }
                }
#endif
            }
#endif

            /* Named partitions double as fastboot flash targets:
             *   fastboot flash resv <small test image>  (safe: 40 KiB 0xFF)
             *   fastboot flash ap   nuttx.bin
             *   fastboot flash bl   nuttx-bl.bin
             * Do not flash SPL (offset 0). resv is the write/readback
             * target for proving fastboot actually programs NOR.
             */

            f1c100s_register_mtdpart(mtd, "/dev/resv",
                                     F1C100S_PART_RESV_OFFSET,
                                     F1C100S_PART_RESV_SIZE);
            f1c100s_register_mtdpart(mtd, "/dev/bl",
                                     F1C100S_PART_BL_OFFSET,
                                     F1C100S_PART_BL_SIZE);
            f1c100s_register_mtdpart(mtd, "/dev/ap",
                                     F1C100S_PART_AP_OFFSET,
                                     F1C100S_PART_AP_SIZE);
            f1c100s_register_mtdpart(mtd, "/dev/data",
                                     F1C100S_PART_DATA_OFFSET,
                                     F1C100S_PART_DATA_SIZE);
            f1c100s_register_mtdpart(mtd, "/dev/kv",
                                     F1C100S_PART_KV_OFFSET,
                                     F1C100S_PART_KV_SIZE);
          }
        else
          {
            uerr("ERROR: w25_initialize failed\n");
          }
      }
    else
      {
        uerr("ERROR: f1c100s_spibus_initialize(0) failed\n");
      }
  }
#endif
#endif

#ifdef CONFIG_F1C100S_SDIO
  FAR struct sdio_dev_s *sdio = f1c100s_sdio_initialize(0);
  if (sdio != NULL)
    {
      ret = mmcsd_slotinitialize(0, sdio);
      if (ret < 0)
        {
          mcerr("ERROR: mmcsd_slotinitialize failed: %d\n", ret);
        }
      else
        {
#if defined(CONFIG_MBR_PARTITION) || defined(CONFIG_GPT_PARTITION)
          int prc = parse_block_partition("/dev/mmcsd0", f1c100s_partition_handler, NULL);
          syslog(LOG_INFO, "parse_block_partition(/dev/mmcsd0) = %d\n", prc);
#endif
        }
    }
#endif

#ifdef CONFIG_AUDIO
  FAR struct audio_lowerhalf_s *audio = f1c100s_audio_initialize();
  if (audio != NULL)
    {
      ret = audio_register("pcm0", audio);
    }
#endif

#ifdef CONFIG_F1C100S_USBDEV
  /* Initialize USB Device controller (MUSB) */

  ret = f1c100s_usbdev_initialize();
  if (ret < 0)
    {
      uerr("ERROR: USB Device init failed: %d\n", ret);
    }
  else
    {
      uinfo("USB Device initialized\n");

#ifdef CONFIG_USBDEV_COMPOSITE
      ret = f1c100s_composite_initialize();
#else
#if defined(CONFIG_F1C100S_USB_CDCACM) && defined(CONFIG_CDCACM)
      ret = cdcacm_initialize(0, NULL);
      if (ret < 0)
        {
          uerr("ERROR: cdcacm_initialize failed: %d\n", ret);
        }
#endif
#endif /* CONFIG_USBDEV_COMPOSITE */
    }
#endif /* CONFIG_F1C100S_USBDEV */

#if defined(CONFIG_F1C100S_USB_MSC) && defined(CONFIG_USBMSC) && \
    !defined(CONFIG_USBDEV_COMPOSITE)
  {
    FAR void *msc_handle;
    ret = usbmsc_configure(1, &msc_handle);
    if (ret >= 0)
      {
        ret = usbmsc_bindlun(msc_handle, "/dev/mmcsd0", 0, 0, 0, false);
        if (ret >= 0)
          {
            ret = usbmsc_exportluns(msc_handle);
          }
      }
  }
#endif

#if defined(CONFIG_F1C100S_USB_ADB) && defined(CONFIG_USBADB) && \
    !defined(CONFIG_USBDEV_COMPOSITE)
  usbdev_adb_initialize();
#endif

#if (defined(CONFIG_F1C100S_USB_RNDIS) || defined(CONFIG_F1C100S_USB_NET)) && \
    !defined(CONFIG_USBDEV_COMPOSITE)
  {
    /* Ensure /data LittleFS is mounted to read /data/net_mode */
#ifdef CONFIG_FS_LITTLEFS
    mkdir("/data", 0777);
    mount("/dev/data", "/data", "littlefs", 0, "autoformat");
#endif

    char mode[16] = {0};
    int fd = open("/data/net_mode", O_RDONLY);
    if (fd >= 0)
      {
        int n = read(fd, mode, sizeof(mode) - 1);
        if (n > 0)
          {
            mode[n] = '\0';
          }
        close(fd);
      }
    else
      {
        /* Create default file if missing */
        fd = open("/data/net_mode", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd >= 0)
          {
            write(fd, "rndis\n", 6);
            close(fd);
          }
        strncpy(mode, "rndis", sizeof(mode) - 1);
      }

    /* If mode starts with "ecm", use CDC-ECM (for macOS / Linux) */
    if (strncmp(mode, "ecm", 3) == 0)
      {
#ifdef CONFIG_NET_CDCECM
        syslog(LOG_INFO, "[USB] Activating CDC-ECM Ethernet Gadget (macOS / Linux mode)\n");
        ret = cdcecm_initialize(0, NULL);
        if (ret < 0)
          {
            syslog(LOG_ERR, "ERROR: cdcecm_initialize failed: %d\n", ret);
          }
#elif defined(CONFIG_RNDIS)
        uint8_t mac[6] = {0xa0, 0xf1, 0xc1, 0x00, 0x00, 0x01};
        ret = usbdev_rndis_initialize(mac);
#endif
      }
    else
      {
#ifdef CONFIG_RNDIS
        syslog(LOG_INFO, "[USB] Activating RNDIS Ethernet Gadget (Windows / Linux mode)\n");
        uint8_t mac[6] = {0xa0, 0xf1, 0xc1, 0x00, 0x00, 0x01};
        ret = usbdev_rndis_initialize(mac);
        if (ret < 0)
          {
            syslog(LOG_ERR, "ERROR: usbdev_rndis_initialize failed: %d\n", ret);
          }
#elif defined(CONFIG_NET_CDCECM)
        syslog(LOG_INFO, "[USB] Activating CDC-ECM Ethernet Gadget (macOS / Linux mode)\n");
        ret = cdcecm_initialize(0, NULL);
#endif
      }
  }
#endif


#if defined(CONFIG_F1C100S_USB_FASTBOOT) && defined(CONFIG_USBFASTBOOT) && \
    !defined(CONFIG_USBDEV_COMPOSITE)
  if (usbdev_adb_initialize() == NULL)
    {
      uerr("ERROR: fastboot gadget init failed\n");
    }
  else
    {
      uinfo("fastboot gadget registered (/dev/fastboot)\n");
    }
#endif

#ifdef CONFIG_F1C100S_ONESHOT
  /* Initialize Oneshot Timer */
  {
    FAR struct oneshot_lowerhalf_s *oneshot = f1c100s_oneshot_initialize();
    if (oneshot)
      {
        ret = oneshot_register("/dev/timer0", oneshot);
        if (ret < 0)
          {
            syslog(LOG_ERR, "Oneshot register failed: %d\n", ret);
          }
      }
  }
#endif

#ifdef CONFIG_FB_UPDATE
  /* Linux fbdev/fbtft: write() to /dev/fb* becomes visible without an
   * extra ioctl. NuttX with CONFIG_FB_UPDATE only memcpy's; ST7789
   * needs FBIO_UPDATE (SPI) and RGB needs a D-cache clean. Do that
   * here so dd/cat work like the other project. Do not patch nuttx
   * drivers/video/fb.c.
   */

    {
      int fbupd;

      fbupd = kthread_create("fbupd", 50, 2048,
                             f1c100s_fb_autoupdate, NULL);
      if (fbupd < 0)
        {
          syslog(LOG_ERR, "fbupd kthread failed: %d\n", fbupd);
        }
    }
#endif

  return ret;
}
