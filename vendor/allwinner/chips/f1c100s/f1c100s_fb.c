/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_fb.c
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
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <syslog.h>

#include <nuttx/video/fb.h>
#include <nuttx/kmalloc.h>
#include <nuttx/cache.h>
#include <arch/board/board.h>

#include "chip.h"
#include "f1c100s_softreset.h"
#include "hardware/f1c100s_ccu.h"
#include "hardware/f1c100s_de.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Default LCD configuration (800x480 TFT) */

#ifndef CONFIG_F1C100S_FB_WIDTH
#  define CONFIG_F1C100S_FB_WIDTH   800
#endif

#ifndef CONFIG_F1C100S_FB_HEIGHT
#  define CONFIG_F1C100S_FB_HEIGHT  480
#endif

#ifndef CONFIG_F1C100S_FB_BPP
#  define CONFIG_F1C100S_FB_BPP     16
#endif

/* Calculate stride and buffer size */

#define FB_STRIDE   (CONFIG_F1C100S_FB_WIDTH * (CONFIG_F1C100S_FB_BPP / 8))
#define FB_SIZE     (FB_STRIDE * CONFIG_F1C100S_FB_HEIGHT)

/* Register access macros */

#define getreg32(a)     (*(volatile uint32_t *)(a))
#define putreg32(v, a)  (*(volatile uint32_t *)(a) = (v))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct f1c100s_fb_s
{
  struct fb_vtable_s vtable;    /* FB interface */
  void *fbmem;                  /* Framebuffer memory */
  size_t fblen;                 /* Framebuffer size */
  uint8_t bpp;                  /* Bits per pixel */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int f1c100s_getvideoinfo(struct fb_vtable_s *vtable,
                                struct fb_videoinfo_s *vinfo);
static int f1c100s_getplaneinfo(struct fb_vtable_s *vtable, int planeno,
                                struct fb_planeinfo_s *pinfo);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Default LCD timing configuration.
 * Configured via Kconfig (CONFIG_F1C100S_FB_HFP, etc.).
 * Note: Currently supports a single primary display. Multi-screen support
 * (e.g. concurrent SPI ST7789 + RGB LCD) is a known future enhancement.
 */

#ifndef CONFIG_F1C100S_FB_HFP
#  define CONFIG_F1C100S_FB_HFP       2
#endif
#ifndef CONFIG_F1C100S_FB_HBP
#  define CONFIG_F1C100S_FB_HBP       2
#endif
#ifndef CONFIG_F1C100S_FB_HSW
#  define CONFIG_F1C100S_FB_HSW       41
#endif
#ifndef CONFIG_F1C100S_FB_VFP
#  define CONFIG_F1C100S_FB_VFP       2
#endif
#ifndef CONFIG_F1C100S_FB_VBP
#  define CONFIG_F1C100S_FB_VBP       2
#endif
#ifndef CONFIG_F1C100S_FB_VSW
#  define CONFIG_F1C100S_FB_VSW       10
#endif
#ifndef CONFIG_F1C100S_FB_PLL
#  define CONFIG_F1C100S_FB_PLL       0x80000800
#endif
#ifndef CONFIG_F1C100S_FB_CLKDIV
#  define CONFIG_F1C100S_FB_CLKDIV    24
#endif
#ifndef CONFIG_F1C100S_FB_INV
#  define CONFIG_F1C100S_FB_INV       0
#endif

static const struct f1c100s_lcd_config_s g_lcd_config =
{
  .width  = CONFIG_F1C100S_FB_WIDTH,
  .height = CONFIG_F1C100S_FB_HEIGHT,
  .hfp    = CONFIG_F1C100S_FB_HFP,
  .hbp    = CONFIG_F1C100S_FB_HBP,
  .hsw    = CONFIG_F1C100S_FB_HSW,
  .vfp    = CONFIG_F1C100S_FB_VFP,
  .vbp    = CONFIG_F1C100S_FB_VBP,
  .vsw    = CONFIG_F1C100S_FB_VSW,
  .pll    = CONFIG_F1C100S_FB_PLL,
  .clkdiv = CONFIG_F1C100S_FB_CLKDIV,
  .inv    = CONFIG_F1C100S_FB_INV,
  .mode   = 2,        /* TFT mode */
};

static struct f1c100s_fb_s g_fbdev;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_tcon_init
 ****************************************************************************/

static void f1c100s_tcon_init(const struct f1c100s_lcd_config_s *cfg)
{
  uint32_t val;
  uint32_t bp;
  uint32_t total;
  int timeout;

  /* PLL_VIDEO. Bit 28 is LOCK (read-only). Do not wait forever. */

  putreg32(cfg->pll, F1C_CCU_BASE + CCU_PLL_VIDEO_CTRL);
  for (timeout = 10000;
       timeout > 0 &&
       !(getreg32(F1C_CCU_BASE + CCU_PLL_VIDEO_CTRL) & (1u << 28));
       timeout--)
    {
    }

  if (timeout == 0)
    {
      syslog(LOG_ERR, "f1c100s_fb: PLL_VIDEO lock timeout, pll=0x%08x\n",
             (unsigned)getreg32(F1C_CCU_BASE + CCU_PLL_VIDEO_CTRL));
    }

  /* Module clocks: DEBE/DEFE/TCON from PLL_VIDEO, plus DRAM ports */

  putreg32(1u << 31, F1C_CCU_BASE + CCU_BE_CLK);
  putreg32(1u << 31, F1C_CCU_BASE + CCU_FE_CLK);
  putreg32(1u << 31, F1C_CCU_BASE + CCU_TCON_CLK);

  val = getreg32(F1C_CCU_BASE + CCU_DRAM_CLK_GATING);
  val |= CCU_DRAM_GATE_DEBE | CCU_DRAM_GATE_DEFE;
  putreg32(val, F1C_CCU_BASE + CCU_DRAM_CLK_GATING);

  val = getreg32(F1C_CCU_BASE + CCU_BUS_CLK_GATING1);
  val |= CCU_CLK_GATE_TCON | CCU_CLK_GATE_DEBE | CCU_CLK_GATE_DEFE;
  putreg32(val, F1C_CCU_BASE + CCU_BUS_CLK_GATING1);

  val = getreg32(F1C_CCU_BASE + CCU_BUS_SOFT_RST1);
  val &= ~(CCU_CLK_GATE_TCON | CCU_CLK_GATE_DEBE | CCU_CLK_GATE_DEFE);
  putreg32(val, F1C_CCU_BASE + CCU_BUS_SOFT_RST1);
  val |= CCU_CLK_GATE_TCON | CCU_CLK_GATE_DEBE | CCU_CLK_GATE_DEFE;
  putreg32(val, F1C_CCU_BASE + CCU_BUS_SOFT_RST1);

  /* Disable TCON and tristate pads while programming */

  putreg32(0, F1C100S_TCON_BASE + TCON_CTRL);
  putreg32(0, F1C100S_TCON_BASE + TCON_INT0);
  putreg32(0x0fffffff, F1C100S_TCON_BASE + TCON_T0_CLK);
  putreg32(0xffffffff, F1C100S_TCON_BASE + TCON_T0_IO_CTRL1);
  putreg32(0xffffffff, F1C100S_TCON_BASE + TCON_T1_IO_CTRL1);

  bp = cfg->vbp + cfg->vsw;
  val = TCON_T0_CTRL_EN | (bp << TCON_T0_CTRL_START_DLY_SHIFT);
#ifdef CONFIG_F1C100S_FB_RB_SWAP
  val |= TCON_T0_CTRL_RB_SWAP;
#endif
  putreg32(val, F1C100S_TCON_BASE + TCON_T0_CTRL);

  /* DCLK gate is bits 31:28 (linux bit 31; xboot writes 0xf). */

  putreg32(TCON_T0_CLK_GATE | cfg->clkdiv, F1C100S_TCON_BASE + TCON_T0_CLK);

  val = ((cfg->width - 1) << 16) | (cfg->height - 1);
  putreg32(val, F1C100S_TCON_BASE + TCON_T0_TIMING0);

  total = cfg->width + cfg->hbp + cfg->hfp + cfg->hsw;
  val = ((total - 1) << 16) | (cfg->hbp + cfg->hsw - 1);
  putreg32(val, F1C100S_TCON_BASE + TCON_T0_TIMING1);

  /* V total is vtotal*2 with no -1 (linux SUN4I_TCON0_BASIC2_V_TOTAL). */

  total = cfg->height + cfg->vbp + cfg->vfp + cfg->vsw;
  val = ((total * 2) << 16) | (cfg->vbp + cfg->vsw - 1);
  putreg32(val, F1C100S_TCON_BASE + TCON_T0_TIMING2);

  val = ((cfg->hsw - 1) << 16) | (cfg->vsw - 1);
  putreg32(val, F1C100S_TCON_BASE + TCON_T0_TIMING3);

  putreg32(0, F1C100S_TCON_BASE + TCON_T0_HV_TIMING);
  putreg32(0, F1C100S_TCON_BASE + TCON_T0_CPU_IF);

  /* 16-bit framebuffer on an 18/24-bit RGB panel: TCON FRM dither. */

  putreg32(0x11111111, F1C100S_TCON_BASE + TCON_FRM_SEED0_R);
  putreg32(0x11111111, F1C100S_TCON_BASE + TCON_FRM_SEED0_G);
  putreg32(0x11111111, F1C100S_TCON_BASE + TCON_FRM_SEED0_B);
  putreg32(0x11111111, F1C100S_TCON_BASE + TCON_FRM_SEED1_R);
  putreg32(0x11111111, F1C100S_TCON_BASE + TCON_FRM_SEED1_G);
  putreg32(0x11111111, F1C100S_TCON_BASE + TCON_FRM_SEED1_B);
  putreg32(0x01010000, F1C100S_TCON_BASE + TCON_FRM_TBL0);
  putreg32(0x15151111, F1C100S_TCON_BASE + TCON_FRM_TBL1);
  putreg32(0x57575555, F1C100S_TCON_BASE + TCON_FRM_TBL2);
  putreg32(0x7f7f7777, F1C100S_TCON_BASE + TCON_FRM_TBL3);
  putreg32((1u << 31) | (5u << 4), F1C100S_TCON_BASE + TCON_FRM_CTRL);

  /* DCLK phase=1, H/V sync active-low, sample on falling DCLK.
   * DE stays active-high. INV XORs onto bits 27:24.
   */

  val = (1u << 28) | (1u << 26) | (1u << 25) | (1u << 24);
  val ^= ((uint32_t)cfg->inv << 24);
  putreg32(val, F1C100S_TCON_BASE + TCON_T0_IO_CTRL0);
  putreg32(0, F1C100S_TCON_BASE + TCON_T0_IO_CTRL1);
}

/****************************************************************************
 * Name: f1c100s_debe_init
 ****************************************************************************/

static void f1c100s_debe_init(const struct f1c100s_lcd_config_s *cfg,
                              void *fbmem)
{
  uint32_t val;
  uintptr_t addr = (uintptr_t)fbmem;
  unsigned int i;

  /* Zero the DEBE register block (xboot does 0x800..0x1000 from 0x01e60000). */

  for (i = 0; i < 0x800; i += 4)
    {
      putreg32(0, F1C100S_DEBE_BASE + i);
    }

  val = ((cfg->height - 1) << 16) | (cfg->width - 1);
  putreg32(val, F1C100S_DEBE_BASE + DEBE_DISP_SIZE);
  putreg32(val, F1C100S_DEBE_BASE + DEBE_LAY_SIZE(0));
  putreg32(0, F1C100S_DEBE_BASE + DEBE_LAY_POS(0));

  /* Linewidth is in bits (linux: pitches[0]*8). RGB565 = width << 4. */

  putreg32((uint32_t)cfg->width * CONFIG_F1C100S_FB_BPP,
           F1C100S_DEBE_BASE + DEBE_LAY_STRIDE(0));

  putreg32(addr << 3, F1C100S_DEBE_BASE + DEBE_LAY_FB_ADDRL(0));
  putreg32(addr >> 29, F1C100S_DEBE_BASE + DEBE_LAY_FB_ADDRH(0));

  /* Do not set ATTCTL0 bit0 (that is global-alpha enable, alpha=0
   * makes the layer fully transparent). Layer enable is MODE bit 8.
   */

  putreg32(0, F1C100S_DEBE_BASE + DEBE_LAY_ATTR0(0));
  putreg32(DEBE_FMT_RGB565, F1C100S_DEBE_BASE + DEBE_LAY_ATTR1(0));
  putreg32(0x00000000, F1C100S_DEBE_BASE + DEBE_BACKCOLOR);

  val = DEBE_MODE_EN | DEBE_MODE_START | DEBE_MODE_LAY0_EN;
  putreg32(val, F1C100S_DEBE_BASE + DEBE_MODE_CTRL);

  putreg32(DEBE_REGBUFF_LOAD, F1C100S_DEBE_BASE + DEBE_REG_CTRL);
}

/****************************************************************************
 * Name: f1c100s_lcd_gpio_init
 ****************************************************************************/

static void f1c100s_lcd_gpio_init(void)
{
  /* Configure PD0-PD21 for LCD function (function 2) */

  putreg32(0x22222222, F1C_PIO_BASE + 0x6c);  /* PD_CFG0: PD0-PD7 */
  putreg32(0x22222222, F1C_PIO_BASE + 0x70);  /* PD_CFG1: PD8-PD15 */
  putreg32(0x00222222, F1C_PIO_BASE + 0x74);  /* PD_CFG2: PD16-PD21 */

  /* Set drive strength to level 3 */

  putreg32(0xffffffff, F1C_PIO_BASE + 0x7c);  /* PD_DRV0 */
  putreg32(0x00000fff, F1C_PIO_BASE + 0x80);  /* PD_DRV1 */

  /* Disable pull-up/down */

  putreg32(0, F1C_PIO_BASE + 0x84);           /* PD_PUL0 */
  putreg32(0, F1C_PIO_BASE + 0x88);           /* PD_PUL1 */
}

/****************************************************************************
 * Name: f1c100s_getvideoinfo
 ****************************************************************************/

static int f1c100s_getvideoinfo(struct fb_vtable_s *vtable,
                                struct fb_videoinfo_s *vinfo)
{
  ginfo("vtable=%p vinfo=%p\n", vtable, vinfo);

  if (vtable && vinfo)
    {
      memset(vinfo, 0, sizeof(struct fb_videoinfo_s));
      vinfo->fmt     = FB_FMT_RGB16_565;
      vinfo->xres    = CONFIG_F1C100S_FB_WIDTH;
      vinfo->yres    = CONFIG_F1C100S_FB_HEIGHT;
      vinfo->nplanes = 1;
      return OK;
    }

  gerr("ERROR: Invalid arguments\n");
  return -EINVAL;
}

/****************************************************************************
 * Name: f1c100s_getplaneinfo
 ****************************************************************************/

static int f1c100s_getplaneinfo(struct fb_vtable_s *vtable, int planeno,
                                struct fb_planeinfo_s *pinfo)
{
  ginfo("vtable=%p planeno=%d pinfo=%p\n", vtable, planeno, pinfo);

  if (vtable && planeno == 0 && pinfo)
    {
      struct f1c100s_fb_s *priv = (struct f1c100s_fb_s *)vtable;

      memset(pinfo, 0, sizeof(struct fb_planeinfo_s));
      pinfo->fbmem   = priv->fbmem;
      pinfo->fblen   = priv->fblen;
      pinfo->stride  = FB_STRIDE;
      pinfo->display = 0;
      pinfo->bpp     = priv->bpp;
      return OK;
    }

  gerr("ERROR: Invalid arguments\n");
  return -EINVAL;
}

/****************************************************************************
 * Name: f1c100s_updatearea
 ****************************************************************************/

#ifdef CONFIG_FB_UPDATE
static int f1c100s_updatearea(struct fb_vtable_s *vtable,
                              const struct fb_area_s *area)
{
  /* ARM926EJ-S: Clean entire D-cache to ensure data is in RAM */

  __asm__ __volatile__ (
    "mov r0, #0\n"
    "1:\n"
    "mcr p15, 0, r0, c7, c10, 2\n"  /* Clean D-cache line by set/way */
    "add r0, r0, #32\n"
    "cmp r0, #0x4000\n"
    "blt 1b\n"
    "mcr p15, 0, r0, c7, c10, 4\n"  /* DSB */
    : : : "r0", "memory"
  );

  return OK;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: f1c100s_fb_initialize
 *
 * Description:
 *   Initialize the framebuffer driver
 *
 ****************************************************************************/

int f1c100s_fb_initialize(int display)
{
  struct f1c100s_fb_s *priv = &g_fbdev;
  int ret;

  /* Check if already initialized */

  if (priv->fbmem != NULL)
    {
      ginfo("F1C100s framebuffer already initialized\n");
      return OK;
    }

  ginfo("Initializing F1C100s framebuffer\n");

  /* Allocate framebuffer memory from heap */

  priv->fblen = FB_SIZE;
  priv->bpp   = CONFIG_F1C100S_FB_BPP;
  priv->fbmem = kmm_memalign(4096, priv->fblen);

  if (priv->fbmem == NULL)
    {
      gerr("ERROR: Failed to allocate framebuffer memory\n");
      return -ENOMEM;
    }

  ginfo("Framebuffer: %p, size: %zu\n", priv->fbmem, priv->fblen);

  /* Solid red (RGB565 0xf800) so the panel is on before any fb test. */

    {
      uint16_t *px = (uint16_t *)priv->fbmem;
      size_t n = priv->fblen / sizeof(uint16_t);
      size_t i;

      for (i = 0; i < n; i++)
        {
          px[i] = 0xf800;
        }
    }

  __asm__ __volatile__ (
    "mov r0, #0\n"
    "1:\n"
    "mcr p15, 0, r0, c7, c10, 2\n"
    "add r0, r0, #32\n"
    "cmp r0, #0x4000\n"
    "blt 1b\n"
    "mcr p15, 0, r0, c7, c10, 4\n"
    : : : "r0", "memory"
  );

  f1c100s_lcd_gpio_init();
  f1c100s_tcon_init(&g_lcd_config);
  f1c100s_debe_init(&g_lcd_config, priv->fbmem);
  putreg32(0x80000000, F1C100S_TCON_BASE + TCON_CTRL);

  syslog(LOG_INFO, "RGB LCD %dx%d pll=0x%08x dclkdiv=%u\n",
         CONFIG_F1C100S_FB_WIDTH, CONFIG_F1C100S_FB_HEIGHT,
         (unsigned)g_lcd_config.pll, g_lcd_config.clkdiv);

  /* Setup vtable */

  priv->vtable.getvideoinfo = f1c100s_getvideoinfo;
  priv->vtable.getplaneinfo = f1c100s_getplaneinfo;
#ifdef CONFIG_FB_UPDATE
  priv->vtable.updatearea   = f1c100s_updatearea;
#endif

  /* Register the framebuffer device */

  ret = fb_register_device(display, 0, &priv->vtable);
  if (ret < 0)
    {
      gerr("ERROR: fb_register() failed: %d\n", ret);
      kmm_free(priv->fbmem);
      return ret;
    }

  ginfo("F1C100s framebuffer initialized successfully\n");
  return OK;
}
