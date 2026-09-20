/****************************************************************************
 * vendor/allwinner/chips/f1c100s/hardware/f1c100s_de.h
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

#ifndef __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_DE_H
#define __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_DE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Base Addresses */

#define F1C100S_TCON_BASE       0x01C0C000
#define F1C100S_TVE_BASE        0x01C0A000
#define F1C100S_DEBE_BASE       0x01E60800

/* TCON Register Offsets */

#define TCON_CTRL               0x00    /* TCON Control */
#define TCON_INT0               0x04    /* Interrupt 0 */
#define TCON_INT1               0x08    /* Interrupt 1 */
#define TCON_FRM_CTRL           0x10    /* FRM Control */
#define TCON_FRM_SEED0_R        0x14    /* FRM Seed0 R */
#define TCON_FRM_SEED0_G        0x18    /* FRM Seed0 G */
#define TCON_FRM_SEED0_B        0x1C    /* FRM Seed0 B */
#define TCON_FRM_SEED1_R        0x20    /* FRM Seed1 R */
#define TCON_FRM_SEED1_G        0x24    /* FRM Seed1 G */
#define TCON_FRM_SEED1_B        0x28    /* FRM Seed1 B */
#define TCON_FRM_TBL0           0x2C    /* FRM Table 0 */
#define TCON_FRM_TBL1           0x30    /* FRM Table 1 */
#define TCON_FRM_TBL2           0x34    /* FRM Table 2 */
#define TCON_FRM_TBL3           0x38    /* FRM Table 3 */
#define TCON_T0_CTRL            0x40    /* TCON0 Control */
#define TCON_T0_CLK             0x44    /* TCON0 Clock Control */
#define TCON_T0_TIMING0         0x48    /* TCON0 Basic Timing 0 */
#define TCON_T0_TIMING1         0x4C    /* TCON0 Basic Timing 1 */
#define TCON_T0_TIMING2         0x50    /* TCON0 Basic Timing 2 */
#define TCON_T0_TIMING3         0x54    /* TCON0 Basic Timing 3 */
#define TCON_T0_HV_TIMING       0x58    /* TCON0 HV Timing */
#define TCON_T0_CPU_IF          0x60    /* TCON0 CPU Interface Control */
#define TCON_T0_CPU_WR          0x64    /* TCON0 CPU Write Data */
#define TCON_T0_CPU_RD          0x68    /* TCON0 CPU Read Data */
#define TCON_T0_CPU_RDNX        0x6C    /* TCON0 CPU Read Back Data */
#define TCON_T0_IO_CTRL0        0x88    /* TCON0 IO Control 0 */
#define TCON_T0_IO_CTRL1        0x8C    /* TCON0 IO Control 1 */
#define TCON_T1_CTRL            0x90    /* TCON1 Control */
#define TCON_T1_TIMING0         0x94    /* TCON1 Basic Timing 0 */
#define TCON_T1_TIMING1         0x98    /* TCON1 Basic Timing 1 */
#define TCON_T1_TIMING2         0x9C    /* TCON1 Basic Timing 2 */
#define TCON_T1_TIMING3         0xA0    /* TCON1 Basic Timing 3 */
#define TCON_T1_TIMING4         0xA4    /* TCON1 Basic Timing 4 */
#define TCON_T1_TIMING5         0xA8    /* TCON1 Basic Timing 5 */
#define TCON_T1_IO_CTRL0        0xF0    /* TCON1 IO Control 0 */
#define TCON_T1_IO_CTRL1        0xF4    /* TCON1 IO Control 1 */
#define TCON_DBG_INFO           0xFC    /* TCON Debug Information */

/* TCON_CTRL bits */

#define TCON_CTRL_EN            (1 << 31)
#define TCON_CTRL_IF_SEL        (1 << 0)  /* 0=TCON0, 1=TCON1 */

/* TCON_T0_CTRL bits */

#define TCON_T0_CTRL_EN               (1 << 31)
#define TCON_T0_CTRL_RB_SWAP          (1 << 23)
#define TCON_T0_CTRL_START_DLY_SHIFT  4
#define TCON_T0_CTRL_SRC_SEL_SHIFT    0

/* TCON_T0_CLK bits */

#define TCON_T0_CLK_EN          (1 << 28)
#define TCON_T0_CLK_DIV_MASK    0x0F

/* DEBE Register Offsets (relative to DEBE_BASE) */

#define DEBE_MODE_CTRL          0x00    /* Mode Control */
#define DEBE_BACKCOLOR          0x04    /* Background Color */
#define DEBE_DISP_SIZE          0x08    /* Display Size */
#define DEBE_LAY_SIZE(n)        (0x10 + (n) * 4)   /* Layer Size */
#define DEBE_LAY_POS(n)         (0x20 + (n) * 4)   /* Layer Position */
#define DEBE_LAY_STRIDE(n)      (0x40 + (n) * 4)   /* Layer Stride */
#define DEBE_LAY_FB_ADDRL(n)    (0x50 + (n) * 4)   /* Layer FB Addr Low */
#define DEBE_LAY_FB_ADDRH(n)    (0x60 + (n) * 4)   /* Layer FB Addr High */
#define DEBE_REG_CTRL           0x70    /* Buffer Control */
#define DEBE_COLOR_KEY_MAX      0x80    /* Color Key Max */
#define DEBE_COLOR_KEY_MIN      0x84    /* Color Key Min */
#define DEBE_COLOR_KEY_CFG      0x88    /* Color Key Config */
#define DEBE_LAY_ATTR0(n)       (0x90 + (n) * 4)   /* Layer Attr 0 */
#define DEBE_LAY_ATTR1(n)       (0xA0 + (n) * 4)   /* Layer Attr 1 */
#define DEBE_HWC_CTRL           0xD8    /* HWC Control */
#define DEBE_HWC_FORMAT         0xE0    /* HWC Format */
#define DEBE_WB_CTRL            0xF0    /* Write Back Control */
#define DEBE_WB_ADDR            0xF4    /* Write Back Address */
#define DEBE_WB_STRIDE          0xF8    /* Write Back Stride */
#define DEBE_YUV_IN_CTRL        0x120   /* YUV Input Control */
#define DEBE_YUV_ADDR(n)        (0x130 + (n) * 4)  /* YUV Address */
#define DEBE_YUV_STRIDE(n)      (0x140 + (n) * 4)  /* YUV Stride */
#define DEBE_COLOR_COEF(n)      (0x150 + (n) * 4)  /* Color Coef */
#define DEBE_PALETTE            0x200   /* Palette (256 entries) */

/* DEBE_MODE_CTRL bits */

#define DEBE_MODE_EN            (1 << 0)
#define DEBE_MODE_START         (1 << 1)
#define DEBE_MODE_INTERLACE     (1 << 5)
#define DEBE_MODE_LAY0_EN       (1 << 8)   /* Layer 0 enable */
#define DEBE_MODE_LAY1_EN       (1 << 9)   /* Layer 1 enable */
#define DEBE_MODE_LAY2_EN       (1 << 10)  /* Layer 2 enable */
#define DEBE_MODE_LAY3_EN       (1 << 11)  /* Layer 3 enable */

/* DEBE Layer Attribute 0 bits (linux sun4i_backend.h ATTCTL_REG0).
 * Bit 0 is global-alpha enable, NOT layer enable. Layer enable is
 * DEBE_MODE_LAY0_EN. Alpha value lives in bits 31:24.
 */

#define DEBE_LAY_ATTR0_GLBALPHA_EN   (1u << 0)
#define DEBE_LAY_ATTR0_GLBALPHA(x)   ((uint32_t)(x) << 24)
#define DEBE_LAY_ATTR0_FMT_SHIFT  8
#define DEBE_LAY_ATTR0_FMT_MASK   (0xF << 8)

/* Layer formats */

#define DEBE_FMT_RGB565         (5 << 8)
#define DEBE_FMT_RGB888         (9 << 8)
#define DEBE_FMT_ARGB8888       (10 << 8)

/* TVE Register Offsets */

#define TVE_ENABLE              0x00
#define TVE_CFG1                0x04
#define TVE_DAC1                0x08
#define TVE_NOTCH_DELAY         0x0C
#define TVE_CHROMA_FREQ         0x10
#define TVE_FB_PORCH            0x14
#define TVE_HD_VS               0x18
#define TVE_LINE_NUM            0x1C
#define TVE_LEVEL               0x20
#define TVE_DAC2                0x24
#define TVE_CB_RESET            0x100
#define TVE_VS_NUM              0x104
#define TVE_FILTER              0x108
#define TVE_CBCR_LEVEL          0x10C
#define TVE_TINT_PHASE          0x110
#define TVE_B_WIDTH             0x114
#define TVE_CBCR_GAIN           0x118
#define TVE_SYNC_LEVEL          0x11C
#define TVE_WHITE_LEVEL         0x120
#define TVE_ACT_LINE            0x124
#define TVE_CHROMA_BW           0x128
#define TVE_CFG2                0x12C
#define TVE_RESYNC              0x130
#define TVE_SLAVE               0x134
#define TVE_CFG3                0x138
#define TVE_CFG4                0x13C

/* CCU Display Clock Registers */

#define CCU_PLL_VIDEO_CTRL      0x10
#define CCU_DRAM_CLK_GATING     0x100
#define CCU_BE_CLK              0x104
#define CCU_FE_CLK              0x10C
#define CCU_TCON_CLK            0x118
#define CCU_TVE_CLK             0x120
#define CCU_BUS_CLK_GATING1     0x64
/* CCU_BUS_SOFT_RST1 defined in f1c100s_softreset.h */

/* Clock gating bits */

#define CCU_CLK_GATE_TCON       (1 << 4)
#define CCU_CLK_GATE_TVE        (1 << 10)
#define CCU_CLK_GATE_DEBE       (1 << 12)
#define CCU_CLK_GATE_DEFE       (1 << 14)

/* DRAM clock gates so DEBE/DEFE can fetch from DRAM (ccu-suniv 0x100) */

#define CCU_DRAM_GATE_DEFE      (1u << 24)
#define CCU_DRAM_GATE_DEBE      (1u << 26)

/* TCON0 DCLK: gate is bits 31:28 (xboot 0xf<<28; linux uses bit 31) */

#define TCON_T0_CLK_GATE        (0xfu << 28)

/* DEBE register-buffer load (self-clearing). Stride is in bits. */

#define DEBE_REGBUFF_LOAD       (1u << 0)

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Display configuration */

struct f1c100s_lcd_config_s
{
  uint16_t width;       /* Horizontal resolution */
  uint16_t height;      /* Vertical resolution */
  uint16_t hfp;         /* Horizontal front porch */
  uint16_t hbp;         /* Horizontal back porch */
  uint16_t hsw;         /* Horizontal sync width */
  uint16_t vfp;         /* Vertical front porch */
  uint16_t vbp;         /* Vertical back porch */
  uint16_t vsw;         /* Vertical sync width */
  uint32_t pll;         /* PLL_VIDEO_CTRL value */
  uint8_t  clkdiv;      /* Dot clock divider */
  uint8_t  inv;         /* IO polarity invert */
  uint8_t  mode;        /* 0=PAL, 1=NTSC, 2=TFT */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
extern "C"
{
#endif

/****************************************************************************
 * Name: f1c100s_fb_initialize
 *
 * Description:
 *   Initialize the framebuffer driver
 *
 * Input Parameters:
 *   display - Display number (0)
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.
 *
 ****************************************************************************/

int f1c100s_fb_initialize(int display);

#ifdef __cplusplus
}
#endif

#endif /* __VENDOR_ALLWINNER_CHIPS_F1C100S_HARDWARE_F1C100S_DE_H */
