/****************************************************************************
 * vendor/allwinner/boards/f1c100s/demo/src/f1c100s_st7789.h
 *
 * Minimal ST7789 240x240 status text for NuttX-BL (spec §2).
 ****************************************************************************/

#ifndef __F1C100S_ST7789_H
#define __F1C100S_ST7789_H

#include <nuttx/config.h>

#ifdef CONFIG_F1C100S_ST7789
int f1c100s_st7789_initialize(void);
void f1c100s_boot_status(const char *msg);
#else
#  define f1c100s_st7789_initialize()  (0)
#  define f1c100s_boot_status(msg)     ((void)(msg))
#endif

#endif
