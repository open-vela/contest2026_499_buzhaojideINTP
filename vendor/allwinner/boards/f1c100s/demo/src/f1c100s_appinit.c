/****************************************************************************
 * vendor/allwinner/boards/f1c100s/demo/src/f1c100s_appinit.c
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
#include <nuttx/board.h>
#include <nuttx/arch.h>
#include <errno.h>

#include "f1c100s_demo.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#if defined(CONFIG_BOARD_CRASHDUMP) && \
    !defined(CONFIG_BOARD_CRASHDUMP_CUSTOM)
void board_crashdump(uintptr_t sp, FAR struct tcb_s *tcb,
                     FAR const char *filename, int lineno,
                     FAR const char *msg, FAR void *regs)
{
  up_lowputc('\r');
  up_lowputc('\n');
  up_lowputc('*');
  up_lowputc('*');
  up_lowputc('*');
  up_lowputc(' ');
  up_lowputc('C');
  up_lowputc('R');
  up_lowputc('A');
  up_lowputc('S');
  up_lowputc('H');
  up_lowputc(' ');
  up_lowputc('*');
  up_lowputc('*');
  up_lowputc('*');
  up_lowputc('\r');
  up_lowputc('\n');

  if (filename)
    {
      const char *p = filename;
      while (*p)
        {
          up_lowputc(*p++);
        }
      up_lowputc(':');
    }

  if (lineno > 0)
    {
      char buf[10];
      int i = 0;
      int n = lineno;
      do
        {
          buf[i++] = '0' + (n % 10);
          n /= 10;
        }
      while (n > 0);
      while (i > 0)
        {
          up_lowputc(buf[--i]);
        }
    }

  up_lowputc('\r');
  up_lowputc('\n');

  if (msg)
    {
      const char *p = msg;
      while (*p)
        {
          up_lowputc(*p++);
        }
      up_lowputc('\r');
      up_lowputc('\n');
    }

  for (volatile int i = 0; i < 5000000; i++);
}
#endif

#ifdef CONFIG_BOARD_EARLY_INITIALIZE
void board_early_initialize(void)
{
}
#endif

#ifdef CONFIG_BOARD_LATE_INITIALIZE
void board_late_initialize(void)
{
  f1c100s_bringup();
}
#endif

int board_app_initialize(uintptr_t arg)
{
#ifdef CONFIG_BOARD_LATE_INITIALIZE
  return OK;
#else
  return f1c100s_bringup();
#endif
}

#ifdef CONFIG_BOARDCTL_FINALINIT
int board_app_finalinitialize(uintptr_t arg)
{
  return 0;
}
#endif
