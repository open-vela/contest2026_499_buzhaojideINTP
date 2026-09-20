/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_bootctl.c
 *
 * Maps openvela bootctl_* onto the single-slot KV record.
 *
 *   active()   -> "ap" if ap_size != 0, else NULL
 *   update()   -> mark AP untrusted (about to rewrite the image)
 *   done()     -> image written, pending first successful boot
 *   success()  -> fail_count = 0, AP confirmed (spec §2)
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <string.h>
#include <syslog.h>

#include "f1c100s_bootkv.h"
#include "bootctl.h"

#ifdef CONFIG_F1C100S_BOOTKV

int bootctl_reboot_bootloader(void)
{
  struct f1c100s_bootkv_s kv;
  int ret = f1c100s_bootkv_read(&kv);

  if (ret != 0)
    {
      return ret == F1C100S_BOOTKV_BLANK ? -ENOENT : ret;
    }

  kv.active_flag = F1C100S_BOOTKV_ACTIVE_BOOTLOADER;
  return f1c100s_bootkv_write(&kv);
}

const char *bootctl_active(void)
{
  struct f1c100s_bootkv_s kv;
  int ret;

  ret = f1c100s_bootkv_read(&kv);
  if (ret != 0 || kv.ap_size == 0)
    {
      return NULL;
    }

  return BOOTCTL_SLOT_AP;
}

int bootctl_update(void)
{
  struct f1c100s_bootkv_s kv;
  int ret;

  ret = f1c100s_bootkv_read(&kv);
  if (ret < 0 && ret != -EIO)
    {
      return ret;
    }

  if (ret != 0)
    {
      memset(&kv, 0, sizeof(kv));
    }

  kv.active_flag = F1C100S_BOOTKV_ACTIVE_NONE;
  return f1c100s_bootkv_write(&kv);
}

int bootctl_done(void)
{
  struct f1c100s_bootkv_s kv;
  int ret;

  ret = f1c100s_bootkv_read(&kv);
  if (ret < 0 && ret != -EIO)
    {
      return ret;
    }

  if (ret != 0)
    {
      return -ENOENT;
    }

  kv.active_flag = F1C100S_BOOTKV_ACTIVE_PENDING;
  return f1c100s_bootkv_write(&kv);
}

int bootctl_success(void)
{
  struct f1c100s_bootkv_s kv;
  int ret;

  ret = f1c100s_bootkv_read(&kv);
  if (ret != 0)
    {
      if (ret == F1C100S_BOOTKV_BLANK || ret == -EIO)
        {
          memset(&kv, 0, sizeof(kv));
          kv.seq = 1;
        }
      else
        {
          return ret;
        }
    }

  if (kv.fail_count == 0 &&
      kv.active_flag == F1C100S_BOOTKV_ACTIVE_CONFIRMED)
    {
      return 0;
    }

  kv.fail_count  = 0;
  kv.active_flag = F1C100S_BOOTKV_ACTIVE_CONFIRMED;
  ret = f1c100s_bootkv_write(&kv);
  if (ret == 0)
    {
      syslog(LOG_INFO, "bootctl: success, fail_count cleared\n");
    }

  return ret;
}

#endif /* CONFIG_F1C100S_BOOTKV */
