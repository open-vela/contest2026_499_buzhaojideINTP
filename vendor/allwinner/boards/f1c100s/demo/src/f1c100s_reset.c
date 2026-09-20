/****************************************************************************
 * boards/f1c100s/demo/src/f1c100s_reset.c
 *
 * F1C100s board reset hooks.
 ****************************************************************************/

#include <nuttx/config.h>

#ifdef CONFIG_BOARDCTL_RESET
#  include <errno.h>
#  include <syslog.h>
#  include <sys/boardctl.h>

#  include "f1c100s_wdt.h"
#  ifdef CONFIG_F1C100S_BOOTKV
#    include "f1c100s_bootkv.h"
#  endif

extern int bootctl_reboot_bootloader(void);

int board_reset(int status)
{
#ifdef CONFIG_F1C100S_BOOTKV
  /* NSH's reset flag table uses index 3 for "bootloader".
   * BOARDIOC_SOFTRESETCAUSE_ENTER_BOOTLOADER is 3.
   */
  if (status == 3)
    {
      int ret = bootctl_reboot_bootloader();
      if (ret < 0)
        {
          syslog(LOG_ERR, "reset: bootloader request failed %d\n", ret);
          return ret;
        }
    }
  else
    {
      /* If we were in BOOTLOADER mode and a normal reboot is requested
       * (e.g. via fastboot reboot), clear the BOOTLOADER flag so BL boots AP.
       */
      struct f1c100s_bootkv_s kv;
      if (f1c100s_bootkv_read(&kv) == 0 &&
          kv.active_flag == F1C100S_BOOTKV_ACTIVE_BOOTLOADER)
        {
          kv.active_flag = F1C100S_BOOTKV_ACTIVE_CONFIRMED;
          f1c100s_bootkv_write(&kv);
        }
    }
#endif

  f1c100s_wdt_system_reset();
  return -EIO;
}
#endif
