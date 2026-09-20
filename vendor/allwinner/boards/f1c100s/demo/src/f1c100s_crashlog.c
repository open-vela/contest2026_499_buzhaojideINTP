/****************************************************************************
 * vendor/allwinner/boards/f1c100s/demo/src/f1c100s_crashlog.c
 *
 * DRAM-resident crash record (spec §6). Not cleared by BSS init.
 * Survives WDT/soft reset while power stays up; gone on power loss.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <string.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/compiler.h>
#include <nuttx/sched.h>
#include <nuttx/board.h>

#define CRASHLOG_MAGIC  0x43524c47u  /* 'CRLG' */

struct f1c100s_crashlog_s
{
  uint32_t magic;
  uint32_t seq;
  uint32_t sp;
  uint32_t pc;
  uint32_t lineno;
  uint32_t nregs;
  uint32_t regs[16];
  char     filename[80];
  char     msg[80];
};

static struct f1c100s_crashlog_s g_crashlog
  __attribute__((section(".crashlog")));

static void crashlog_copystr(char *dst, size_t dstlen, FAR const char *src)
{
  size_t i;

  if (src == NULL)
    {
      dst[0] = '\0';
      return;
    }

  for (i = 0; i + 1 < dstlen && src[i] != '\0'; i++)
    {
      dst[i] = src[i];
    }

  dst[i] = '\0';
}

void f1c100s_crashlog_print(void)
{
  unsigned i;

  if (g_crashlog.magic != CRASHLOG_MAGIC)
    {
      syslog(LOG_INFO, "crashlog: empty\n");
      return;
    }

  syslog(LOG_ERR, "crashlog seq=%u sp=%08x pc=%08x %s:%u %s\n",
         (unsigned)g_crashlog.seq,
         (unsigned)g_crashlog.sp,
         (unsigned)g_crashlog.pc,
         g_crashlog.filename,
         (unsigned)g_crashlog.lineno,
         g_crashlog.msg);
  for (i = 0; i < g_crashlog.nregs && i < 16; i++)
    {
      syslog(LOG_ERR, "  r%u=%08x\n", i, (unsigned)g_crashlog.regs[i]);
    }
}

#ifdef CONFIG_BOARD_CRASHDUMP_CUSTOM
void board_crashdump(uintptr_t sp, FAR struct tcb_s *tcb,
                     FAR const char *filename, int lineno,
                     FAR const char *msg, FAR void *regs)
{
  uint32_t seq = g_crashlog.seq;

  memset(&g_crashlog, 0, sizeof(g_crashlog));
  g_crashlog.magic  = CRASHLOG_MAGIC;
  g_crashlog.seq    = seq + 1;
  g_crashlog.sp     = (uint32_t)sp;
  g_crashlog.lineno = (uint32_t)lineno;
  crashlog_copystr(g_crashlog.filename, sizeof(g_crashlog.filename),
                   filename);
  crashlog_copystr(g_crashlog.msg, sizeof(g_crashlog.msg), msg);

  UNUSED(tcb);

  if (regs != NULL)
    {
      FAR const uint32_t *r = (FAR const uint32_t *)regs;
      unsigned i;

      g_crashlog.nregs = 16;
      for (i = 0; i < 16; i++)
        {
          g_crashlog.regs[i] = r[i];
        }

      g_crashlog.pc = r[15];
    }

}
#endif
