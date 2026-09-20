/****************************************************************************
 * vendor/allwinner/boards/f1c100s/demo/src/f1c100s_boot_image.c
 *
 * BOARDIOC_BOOT_IMAGE handler for the F1C100s NuttX-BL (spec §2).
 *
 * miniboot_main() only calls boardctl(BOARDIOC_BOOT_IMAGE). All boot
 * policy lives here: read KV, decide jump-AP vs stay-in-BL, load the
 * AP image from SPI-NOR, CRC it, bump fail_count, jump.
 *
 * BL and AP are both linked at 0x80000000 (same ld.script, same awboot
 * KERNEL_LOAD_ADDR). The live BL cannot be overwritten in place, so
 * the AP is read into a high DRAM staging buffer and a 7-instruction
 * trampoline in SRAM copies it down with MMU/caches already off
 * (f1c100s_head.S clears CR_M; CONFIG_ARM_DCACHE_DISABLE=y).
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <assert.h>

#include <sched.h>
#include <unistd.h>

#include <nuttx/compiler.h>
#include <nuttx/wdog.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/crc32.h>
#include <nuttx/board.h>

#ifdef CONFIG_SYSTEM_FASTBOOTD
int fastbootd_main(int argc, FAR char *argv[]);
#endif
#ifdef CONFIG_EXAMPLES_FB
int fb_main(int argc, FAR char *argv[]);
static FAR char * const g_bl_fb_argv[] =
{
  (FAR char *)"fb", (FAR char *)"/dev/fb0", NULL
};
#endif

static struct wdog_s g_bl_wait_wd;
static volatile bool g_bl_wait_expired;

static void bl_wait_expired(wdparm_t arg)
{
  UNUSED(arg);
  g_bl_wait_expired = true;
}

#include "f1c100s_bootkv.h"
#include "f1c100s_partitions.h"
#include "f1c100s_st7789.h"

#ifdef CONFIG_F1C100S_WDT
#include "f1c100s_wdt.h"
#endif

#ifndef CONFIG_F1C100S_BL_WAIT_SEC
#  define CONFIG_F1C100S_BL_WAIT_SEC 15
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define F1C100S_AP_LOAD_ADDR    0x80000000u
#define F1C100S_AP_STAGING      0x81000000u
#define F1C100S_TRAMPOLINE_ADDR 0x00007c00u

/* Staging is the top 16 MiB of DRAM. AP partition is ~3.0 MiB, so a
 * full-size image easily fits below 0x82000000. The BL image itself
 * lives in the first 640 KiB; head.S's page table (unused while MMU
 * is off) sits at 0x80100000. Staging starts at 16 MiB to stay clear
 * of both.
 */

#define F1C100S_AP_STAGING_SIZE 0x01000000u

#ifndef CONFIG_F1C100S_BOOT_FAIL_LIMIT
#  define CONFIG_F1C100S_BOOT_FAIL_LIMIT 3
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef void (*f1c100s_tramp_t)(uint32_t src, uint32_t dst,
                                uint32_t size, uint32_t entry);

struct f1c100s_egon_hdr_s
{
  uint32_t branch;
  char     magic[8];
  uint32_t csum;
  uint32_t length;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* ARM LE encodings. AAPCS: r0=src r1=dst r2=size r3=entry.
 *
 *   cmp   r2, #0
 *   beq   2f
 * 1: ldr  r4, [r0], #4
 *    str  r4, [r1], #4
 *    subs r2, r2, #4
 *    bne  1b
 * 2: mov  pc, r3
 */

static const uint32_t g_trampoline[] =
{
  0xe3520000,  /* cmp  r2, #0      */
  0x0a000003,  /* beq  2f          */
  0xe4904004,  /* ldr  r4, [r0],#4 */
  0xe4814004,  /* str  r4, [r1],#4 */
  0xe2522004,  /* subs r2, r2, #4  */
  0x1afffffb,  /* bne  1b          */
  0xe1a0f003   /* mov  pc, r3      */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int boot_load_and_jump(FAR struct f1c100s_bootkv_s *kv, int kvret);
static void noreturn_function boot_stay_in_bl(FAR struct f1c100s_bootkv_s *kv,
                                             int kvret);

/* Probe AP partition header to determine whether a valid eGON image exists
 * and inspect its self-describing length.
 */

static int boot_probe_ap(FAR uint32_t *ap_size_out)
{
  struct f1c100s_egon_hdr_s hdr;
  int ret;

  ret = f1c100s_bootkv_flash_read(F1C100S_PART_AP_OFFSET, &hdr, sizeof(hdr));
  if (ret < 0)
    {
      syslog(LOG_ERR, "boot: failed to read AP header at 0x%08x: %d\n",
             (unsigned)F1C100S_PART_AP_OFFSET, ret);
      return ret;
    }

  if (memcmp(hdr.magic, "eGON.NTX", 8) != 0 &&
      memcmp(hdr.magic, "eGON.BT0", 8) != 0)
    {
      syslog(LOG_WARNING, "boot: AP missing eGON.NTX magic\n");
      return -ENOEXEC;
    }

  if (hdr.length >= 1024 && hdr.length <= F1C100S_PART_AP_SIZE &&
      hdr.length <= F1C100S_AP_STAGING_SIZE)
    {
      *ap_size_out = hdr.length;
    }
  else
    {
      *ap_size_out = 0;
    }

  return 0;
}

static void noreturn_function boot_stay_in_bl(FAR struct f1c100s_bootkv_s *kv,
                                             int kvret)
{
  syslog(LOG_WARNING, "boot: staying in BL, waiting %d s for fastboot\n",
         CONFIG_F1C100S_BL_WAIT_SEC);
  f1c100s_boot_status("Waiting for fastboot...");

#ifdef CONFIG_EXAMPLES_FB
  /* The BL entry point is miniboot_main(), so rcS is not reached here.
   * Launch the existing fb example only on the KV-selected BL path. */
  {
    int pid = task_create("fb", 100, 2048, fb_main, g_bl_fb_argv);
    if (pid < 0)
      {
        syslog(LOG_ERR, "boot: fb task_create failed %d\n", pid);
      }
  }
#endif

#ifdef CONFIG_SYSTEM_FASTBOOTD
  {
    int pid = task_create("fastbootd",
                          CONFIG_SYSTEM_FASTBOOTD_PRIORITY,
                          CONFIG_SYSTEM_FASTBOOTD_STACKSIZE,
                          fastbootd_main, NULL);
    if (pid < 0)
      {
        syslog(LOG_ERR, "boot: fastbootd task_create failed %d\n", pid);
      }
    else
      {
        syslog(LOG_INFO, "boot: fastbootd pid %d\n", pid);
      }
  }
#endif

  /* If this was an explicit reboot-to-bootloader request, consume it now
   * so a future power cycle or reset will boot normally rather than sticking.
   */

  if (kvret == 0 && kv->active_flag == F1C100S_BOOTKV_ACTIVE_BOOTLOADER)
    {
      kv->active_flag = F1C100S_BOOTKV_ACTIVE_CONFIRMED;
      (void)f1c100s_bootkv_write(kv);
    }

  /* Wait for a host; feed WDT so a stuck BL still resets through awboot.
   * On timeout, attempt auto-healing AP boot.
   */

  g_bl_wait_expired = false;
  wd_start(&g_bl_wait_wd, SEC2TICK(CONFIG_F1C100S_BL_WAIT_SEC),
           bl_wait_expired, 0);

  while (!g_bl_wait_expired)
    {
#ifdef CONFIG_F1C100S_WDT
      f1c100s_wdtfeed_keepalive();
#endif
      usleep(200000);
    }

  syslog(LOG_WARNING,
         "boot: fastboot wait timed out, attempting auto-healing AP boot\n");
  f1c100s_boot_status("Booting AP");

  /* Reset fail_count to give timeout fallback a fresh boot attempt */

  kv->fail_count = 0;
  (void)boot_load_and_jump(kv, kvret);

  /* If we reach here, no bootable AP exists at all. Keep fastboot running. */

  syslog(LOG_ERR, "boot: AP not bootable, staying in fastboot loop\n");
  f1c100s_boot_status("No valid AP, fastboot ready");

  for (; ; )
    {
#ifdef CONFIG_F1C100S_WDT
      f1c100s_wdtfeed_keepalive();
#endif
      usleep(200000);
    }
}

static int boot_load_and_jump(FAR struct f1c100s_bootkv_s *kv, int kvret)
{
  FAR uint8_t *staging = (FAR uint8_t *)(uintptr_t)F1C100S_AP_STAGING;
  uint32_t hdr_len = 0;
  uint32_t target_size = 0;
  uint32_t actual_crc;
  uint32_t copy;
  irqstate_t flags;
  int ret;
  f1c100s_tramp_t tramp;
  bool kv_valid = (kvret == 0);
  bool need_kv_heal = false;

  /* Step 1: Probe AP header */

  ret = boot_probe_ap(&hdr_len);
  if (ret < 0)
    {
      syslog(LOG_ERR, "boot: AP header invalid or missing eGON magic\n");
      return ret;
    }

  /* Step 2: Determine load size.
   * Prefer self-describing length stamped into eGON header.
   */

  if (hdr_len > 0)
    {
      target_size = hdr_len;
    }
  else if (kv_valid && kv->ap_size > 0 &&
           kv->ap_size <= F1C100S_PART_AP_SIZE &&
           kv->ap_size <= F1C100S_AP_STAGING_SIZE)
    {
      target_size = kv->ap_size;
    }
  else
    {
      syslog(LOG_ERR, "boot: neither AP header nor KV has valid size\n");
      return -EINVAL;
    }

  syslog(LOG_INFO, "boot: loading AP %u bytes from 0x%08x\n",
         (unsigned)target_size, (unsigned)F1C100S_PART_AP_OFFSET);

  /* Step 3: Read AP into DRAM staging area */

  ret = f1c100s_bootkv_flash_read(F1C100S_PART_AP_OFFSET,
                                  staging, target_size);
  if (ret < 0)
    {
      syslog(LOG_ERR, "boot: AP flash read failed %d\n", ret);
      return ret;
    }

  /* Step 4: Verify eGON magic in staging buffer */

  if (memcmp(staging + 4, "eGON.NTX", 8) != 0 &&
      memcmp(staging + 4, "eGON.BT0", 8) != 0)
    {
      syslog(LOG_ERR, "boot: AP staging missing eGON magic\n");
      return -ENOEXEC;
    }

  /* Step 5: Compute actual IEEE 802.3 CRC-32 of the image */

  actual_crc = crc32(staging, target_size);

  /* Step 6: Smart Auto-Healing & KV Synchronization
   * If KV is blank, read-corrupted, or size/CRC does not match:
   * this is a firmware re-flash / update / initial install.
   * Auto-heal and sync KV immediately so the board never gets bricked!
   */

  if (!kv_valid)
    {
      syslog(LOG_WARNING,
             "boot: KV blank/unusable (%d), auto-initializing for AP\n",
             kvret);
      memset(kv, 0, sizeof(*kv));
      kv->seq = 1;
      need_kv_heal = true;
    }
  else if (kv->ap_size != target_size || kv->ap_crc32 != actual_crc)
    {
      syslog(LOG_WARNING,
             "boot: AP firmware updated! Auto-healing KV: "
             "size %u -> %u, CRC 0x%08x -> 0x%08x\n",
             (unsigned)kv->ap_size, (unsigned)target_size,
             (unsigned)kv->ap_crc32, (unsigned)actual_crc);
      kv->seq++;
      need_kv_heal = true;
    }

  if (need_kv_heal)
    {
      kv->fail_count  = 0;
      kv->active_flag = F1C100S_BOOTKV_ACTIVE_CONFIRMED;
      kv->ap_size     = target_size;
      kv->ap_crc32    = actual_crc;
      ret = f1c100s_bootkv_write(kv);
      if (ret < 0)
        {
          syslog(LOG_ERR,
                 "boot: auto-healing KV write failed %d (continuing boot)\n",
                 ret);
        }
      else
        {
          syslog(LOG_INFO, "boot: KV auto-healed and synced successfully\n");
        }
    }
  else
    {
      /* Regular boot: record boot attempt. Fail count will be cleared
       * by AP calling bootctl_success().
       */

      kv->fail_count++;
      ret = f1c100s_bootkv_write(kv);
      if (ret < 0)
        {
          syslog(LOG_ERR, "boot: fail_count write failed %d\n", ret);
        }
    }

  /* Step 7: Prepare trampoline copy and jump to AP */

  copy = (target_size + 3u) & ~3u;
  if (copy > F1C100S_AP_STAGING_SIZE)
    {
      copy = target_size & ~3u;
    }

  memcpy((void *)(uintptr_t)F1C100S_TRAMPOLINE_ADDR,
         g_trampoline, sizeof(g_trampoline));

  syslog(LOG_INFO, "boot: jumping to AP at 0x%08x (%u bytes)\n",
         (unsigned)F1C100S_AP_LOAD_ADDR, (unsigned)copy);
  f1c100s_boot_status("Booting AP");

#ifdef CONFIG_F1C100S_WDT
  /* Stop hardware WDT before trampoline so AP's head.S can take over */

  f1c100s_wdt_force_stop();
#endif

  flags = up_irq_save();
  UNUSED(flags);

  tramp = (f1c100s_tramp_t)(uintptr_t)F1C100S_TRAMPOLINE_ADDR;
  tramp(F1C100S_AP_STAGING, F1C100S_AP_LOAD_ADDR, copy,
        F1C100S_AP_LOAD_ADDR);

  /* Not reached */

  return -EIO;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int board_boot_image(FAR const char *path, uint32_t hdr_size)
{
  struct f1c100s_bootkv_s kv;
  int kvret;
  int ret;
  uint32_t ap_size = 0;

  UNUSED(path);
  UNUSED(hdr_size);

  memset(&kv, 0, sizeof(kv));
  kvret = f1c100s_bootkv_read(&kv);

  /* Probe AP partition: if no valid eGON image, stay in BL for fastboot */

  ret = boot_probe_ap(&ap_size);
  if (ret < 0)
    {
      syslog(LOG_ERR, "boot: no valid AP image on flash, stay in BL\n");
      boot_stay_in_bl(&kv, kvret);
    }

  /* Check explicit bootloader request */

  if (kvret == 0 && kv.active_flag == F1C100S_BOOTKV_ACTIVE_BOOTLOADER)
    {
      syslog(LOG_WARNING, "boot: explicit bootloader request, stay in BL\n");
      boot_stay_in_bl(&kv, kvret);
    }

  /* Check boot failure limit */

  if (kvret == 0 &&
      kv.fail_count >= (uint32_t)CONFIG_F1C100S_BOOT_FAIL_LIMIT)
    {
      syslog(LOG_ERR,
             "boot: fail_count %u >= %d, stay in BL window for rescue\n",
             (unsigned)kv.fail_count, CONFIG_F1C100S_BOOT_FAIL_LIMIT);
      boot_stay_in_bl(&kv, kvret);
    }

  /* Direct auto-healing jump */

  ret = boot_load_and_jump(&kv, kvret);
  syslog(LOG_ERR, "boot: load/jump failed %d, stay in BL\n", ret);
  boot_stay_in_bl(&kv, kvret);
}

