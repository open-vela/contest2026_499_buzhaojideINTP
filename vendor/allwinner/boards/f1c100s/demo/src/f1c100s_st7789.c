/****************************************************************************
 * vendor/allwinner/boards/f1c100s/demo/src/f1c100s_st7789.c
 *
 * ST7789 240x240 status text for NuttX-BL (spec §2), on top of NuttX's
 * standard drivers/lcd/st7789.c + LCD_FRAMEBUFFER front end -- not a
 * hand-rolled command sequence. This file only supplies the three
 * board_lcd_*() hooks up_fbinitialize() expects (SPI/GPIO bring-up,
 * reset sequence, and handing st7789_lcdinitialize() its spi_dev_s),
 * plus the 5x7 text rendering used to draw status strings via the
 * resulting lcd_dev_s's putrun().
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <nuttx/arch.h>
#include <nuttx/spi/spi.h>
#include <nuttx/ioexpander/gpio.h>
#include <nuttx/lcd/lcd.h>
#include <nuttx/lcd/st7789.h>
#include <nuttx/video/fb.h>

#include "hardware/f1c100s_gpio.h"
#include "hardware/f1c100s_spi.h"
#include "f1c100s_st7789.h"

#ifdef CONFIG_F1C100S_ST7789

#define ST7789_W           CONFIG_LCD_ST7789_XRES
#define ST7789_H           CONFIG_LCD_ST7789_YRES

#define COLOR_BG           0x0000
#define COLOR_FG           0xffff

#define GLYPH_W             12   /* 6 font columns x 2 horizontal scale */
#define GLYPH_H             16   /* 8 font rows x 2 vertical scale */

#define ST7789_PORT(p)     (GPIO_BASE + (uint32_t)(p) * 0x24u)
#define ST7789_DC_PORT     ST7789_PORT(CONFIG_F1C100S_ST7789_DC_PORT)
#define ST7789_DC_PIN      ((uint8_t)CONFIG_F1C100S_ST7789_DC_PIN)
#define ST7789_RST_PORT    ST7789_PORT(CONFIG_F1C100S_ST7789_RST_PORT)
#define ST7789_RST_PIN     ((uint8_t)CONFIG_F1C100S_ST7789_RST_PIN)

static const uint8_t g_font5x7[95][5] =
{
  { 0x00, 0x00, 0x00, 0x00, 0x00 },
  { 0x00, 0x00, 0x5f, 0x00, 0x00 },
  { 0x00, 0x07, 0x00, 0x07, 0x00 },
  { 0x14, 0x7f, 0x14, 0x7f, 0x14 },
  { 0x24, 0x2a, 0x7f, 0x2a, 0x12 },
  { 0x23, 0x13, 0x08, 0x64, 0x62 },
  { 0x36, 0x49, 0x56, 0x20, 0x50 },
  { 0x00, 0x08, 0x07, 0x03, 0x00 },
  { 0x00, 0x1c, 0x22, 0x41, 0x00 },
  { 0x00, 0x41, 0x22, 0x1c, 0x00 },
  { 0x2a, 0x1c, 0x7f, 0x1c, 0x2a },
  { 0x08, 0x08, 0x3e, 0x08, 0x08 },
  { 0x00, 0x80, 0x70, 0x30, 0x00 },
  { 0x08, 0x08, 0x08, 0x08, 0x08 },
  { 0x00, 0x00, 0x60, 0x60, 0x00 },
  { 0x20, 0x10, 0x08, 0x04, 0x02 },
  { 0x3e, 0x51, 0x49, 0x45, 0x3e },
  { 0x00, 0x42, 0x7f, 0x40, 0x00 },
  { 0x72, 0x49, 0x49, 0x49, 0x46 },
  { 0x21, 0x41, 0x49, 0x4d, 0x33 },
  { 0x18, 0x14, 0x12, 0x7f, 0x10 },
  { 0x27, 0x45, 0x45, 0x45, 0x39 },
  { 0x3c, 0x4a, 0x49, 0x49, 0x31 },
  { 0x41, 0x21, 0x11, 0x09, 0x07 },
  { 0x36, 0x49, 0x49, 0x49, 0x36 },
  { 0x46, 0x49, 0x49, 0x29, 0x1e },
  { 0x00, 0x00, 0x14, 0x00, 0x00 },
  { 0x00, 0x40, 0x34, 0x00, 0x00 },
  { 0x00, 0x08, 0x14, 0x22, 0x41 },
  { 0x14, 0x14, 0x14, 0x14, 0x14 },
  { 0x00, 0x41, 0x22, 0x14, 0x08 },
  { 0x02, 0x01, 0x59, 0x09, 0x06 },
  { 0x3e, 0x41, 0x5d, 0x59, 0x4e },
  { 0x7c, 0x12, 0x11, 0x12, 0x7c },
  { 0x7f, 0x49, 0x49, 0x49, 0x36 },
  { 0x3e, 0x41, 0x41, 0x41, 0x22 },
  { 0x7f, 0x41, 0x41, 0x41, 0x3e },
  { 0x7f, 0x49, 0x49, 0x49, 0x41 },
  { 0x7f, 0x09, 0x09, 0x09, 0x01 },
  { 0x3e, 0x41, 0x41, 0x51, 0x73 },
  { 0x7f, 0x08, 0x08, 0x08, 0x7f },
  { 0x00, 0x41, 0x7f, 0x41, 0x00 },
  { 0x20, 0x40, 0x41, 0x3f, 0x01 },
  { 0x7f, 0x08, 0x14, 0x22, 0x41 },
  { 0x7f, 0x40, 0x40, 0x40, 0x40 },
  { 0x7f, 0x02, 0x1c, 0x02, 0x7f },
  { 0x7f, 0x04, 0x08, 0x10, 0x7f },
  { 0x3e, 0x41, 0x41, 0x41, 0x3e },
  { 0x7f, 0x09, 0x09, 0x09, 0x06 },
  { 0x3e, 0x41, 0x51, 0x21, 0x5e },
  { 0x7f, 0x09, 0x19, 0x29, 0x46 },
  { 0x26, 0x49, 0x49, 0x49, 0x32 },
  { 0x03, 0x01, 0x7f, 0x01, 0x03 },
  { 0x3f, 0x40, 0x40, 0x40, 0x3f },
  { 0x1f, 0x20, 0x40, 0x20, 0x1f },
  { 0x3f, 0x40, 0x38, 0x40, 0x3f },
  { 0x63, 0x14, 0x08, 0x14, 0x63 },
  { 0x03, 0x04, 0x78, 0x04, 0x03 },
  { 0x61, 0x59, 0x49, 0x4d, 0x43 },
  { 0x00, 0x7f, 0x41, 0x41, 0x41 },
  { 0x02, 0x04, 0x08, 0x10, 0x20 },
  { 0x00, 0x41, 0x41, 0x41, 0x7f },
  { 0x04, 0x02, 0x01, 0x02, 0x04 },
  { 0x40, 0x40, 0x40, 0x40, 0x40 },
  { 0x00, 0x03, 0x07, 0x08, 0x00 },
  { 0x20, 0x54, 0x54, 0x78, 0x40 },
  { 0x7f, 0x28, 0x44, 0x44, 0x38 },
  { 0x38, 0x44, 0x44, 0x44, 0x28 },
  { 0x38, 0x44, 0x44, 0x28, 0x7f },
  { 0x38, 0x54, 0x54, 0x54, 0x18 },
  { 0x00, 0x08, 0x7e, 0x09, 0x02 },
  { 0x18, 0xa4, 0xa4, 0x9c, 0x78 },
  { 0x7f, 0x08, 0x04, 0x04, 0x78 },
  { 0x00, 0x44, 0x7d, 0x40, 0x00 },
  { 0x20, 0x40, 0x40, 0x3d, 0x00 },
  { 0x7f, 0x10, 0x28, 0x44, 0x00 },
  { 0x00, 0x41, 0x7f, 0x40, 0x00 },
  { 0x7c, 0x04, 0x78, 0x04, 0x78 },
  { 0x7c, 0x08, 0x04, 0x04, 0x78 },
  { 0x38, 0x44, 0x44, 0x44, 0x38 },
  { 0xfc, 0x18, 0x24, 0x24, 0x18 },
  { 0x18, 0x24, 0x24, 0x18, 0xfc },
  { 0x7c, 0x08, 0x04, 0x04, 0x08 },
  { 0x48, 0x54, 0x54, 0x54, 0x24 },
  { 0x04, 0x04, 0x3f, 0x44, 0x24 },
  { 0x3c, 0x40, 0x40, 0x20, 0x7c },
  { 0x1c, 0x20, 0x40, 0x20, 0x1c },
  { 0x3c, 0x40, 0x30, 0x40, 0x3c },
  { 0x44, 0x28, 0x10, 0x28, 0x44 },
  { 0x4c, 0x90, 0x90, 0x90, 0x7c },
  { 0x44, 0x64, 0x54, 0x4c, 0x44 },
  { 0x00, 0x08, 0x36, 0x41, 0x00 },
  { 0x00, 0x00, 0x77, 0x00, 0x00 },
  { 0x00, 0x41, 0x36, 0x08, 0x00 },
  { 0x02, 0x01, 0x02, 0x04, 0x02 }
};

static FAR struct spi_dev_s *g_spi;
static FAR struct lcd_dev_s *g_lcd;
static struct lcd_planeinfo_s g_pinfo;
static bool g_ready;

/* DC/RST are registered as named /dev/gpio devices
 * (f1c_gpio_register_output): opened once here, toggled via
 * ioctl(GPIOC_WRITE) from then on -- by this file for the reset
 * sequence, and by f1c100s_spi.c's SPI_CMDDATA() for the ST7789
 * command/data line once drivers/lcd/st7789.c takes over.
 */

static int g_dc_fd  = -1;
static int g_rst_fd = -1;

static void st7789_rst(bool value)
{
  ioctl(g_rst_fd, GPIOC_WRITE, (unsigned long)value);
}

/****************************************************************************
 * Name: st7789_putrow
 *
 * Description:
 *   Fill one scanline segment with a solid color and hand it to the
 *   generic LCD driver's putrun().
 *
 ****************************************************************************/

static void st7789_putrow(int row, int col, int npixels, uint16_t rgb)
{
  uint16_t linebuf[ST7789_W];
  int i;

  if (row < 0 || row >= ST7789_H || npixels <= 0)
    {
      return;
    }

  for (i = 0; i < npixels; i++)
    {
      linebuf[i] = rgb;
    }

  g_pinfo.putrun(g_lcd, (fb_coord_t)row, (fb_coord_t)col,
                 (FAR const uint8_t *)linebuf, (size_t)npixels);
}

static void st7789_fill(uint16_t rgb)
{
  int row;

  for (row = 0; row < ST7789_H; row++)
    {
      st7789_putrow(row, 0, ST7789_W, rgb);
    }
}

static void st7789_glyph(int x, int y, char ch, uint16_t fg, uint16_t bg)
{
  const uint8_t *col;
  uint16_t linebuf[GLYPH_W];
  int cy;
  int sy;
  int cx;
  int sx;
  unsigned idx;

  if (ch < 32 || ch > 126)
    {
      ch = '?';
    }

  idx = (unsigned)ch - 32;
  col = g_font5x7[idx];

  for (cy = 0; cy < 8; cy++)
    {
      for (cx = 0; cx < 6; cx++)
        {
          uint16_t pix = bg;

          if (cx < 5 && (col[cx] & (1u << cy)) != 0)
            {
              pix = fg;
            }

          for (sx = 0; sx < 2; sx++)
            {
              linebuf[cx * 2 + sx] = pix;
            }
        }

      for (sy = 0; sy < 2; sy++)
        {
          g_pinfo.putrun(g_lcd, (fb_coord_t)(y + cy * 2 + sy),
                        (fb_coord_t)x, (FAR const uint8_t *)linebuf,
                        GLYPH_W);
        }
    }
}

static void st7789_text(int x, int y, FAR const char *s)
{
  while (s != NULL && *s != '\0')
    {
      st7789_glyph(x, y, *s, COLOR_FG, COLOR_BG);
      x += GLYPH_W;
      if (x > ST7789_W - GLYPH_W)
        {
          x = 8;
          y += 18;
        }

      s++;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: board_lcd_initialize
 *
 * Description:
 *   Called by up_fbinitialize()/lcd_framebuffer.c before
 *   board_lcd_getdev(). Brings up SPI1, registers/opens the DC and RST
 *   /dev/gpio devices, and runs the hardware reset sequence. The actual
 *   ST7789 command init sequence is drivers/lcd/st7789.c's job, run
 *   from st7789_lcdinitialize() in board_lcd_getdev() below.
 *
 ****************************************************************************/

int board_lcd_initialize(void)
{
  g_spi = f1c100s_spibus_initialize(1);
  if (g_spi == NULL)
    {
      syslog(LOG_ERR, "st7789: SPI1 init failed\n");
      return -ENODEV;
    }

  f1c_gpio_register_output(ST7789_DC_PORT, ST7789_DC_PIN,
                           GPIO_DRV_1, "lcd_dc");
  f1c_gpio_register_output(ST7789_RST_PORT, ST7789_RST_PIN,
                           GPIO_DRV_1, "lcd_rst");

  g_dc_fd  = open("/dev/lcd_dc", O_WRONLY);
  g_rst_fd = open("/dev/lcd_rst", O_WRONLY);
  if (g_dc_fd < 0 || g_rst_fd < 0)
    {
      syslog(LOG_ERR, "st7789: /dev/lcd_dc or /dev/lcd_rst open failed\n");
      return -ENODEV;
    }

  st7789_rst(false);
  up_mdelay(10);
  st7789_rst(true);
  up_mdelay(120);

  return OK;
}

/****************************************************************************
 * Name: board_lcd_getdev
 *
 * Description:
 *   Bind the SPI1 bus to the generic ST7789 driver and return its
 *   lcd_dev_s. Runs the real ST7789 init command sequence internally
 *   (drivers/lcd/st7789.c), via SPI_CMDDATA() in f1c100s_spi.c for the
 *   DC line.
 *
 ****************************************************************************/

FAR struct lcd_dev_s *board_lcd_getdev(int devno)
{
  syslog(LOG_INFO, "st7789: calling st7789_lcdinitialize\n");
  g_lcd = st7789_lcdinitialize(g_spi);
  syslog(LOG_INFO, "st7789: lcdinitialize returned %p\n", g_lcd);
  if (g_lcd == NULL)
    {
      syslog(LOG_ERR, "st7789: st7789_lcdinitialize failed\n");
      return NULL;
    }

  if (g_lcd->getplaneinfo(g_lcd, 0, &g_pinfo) < 0 || g_pinfo.putrun == NULL)
    {
      syslog(LOG_ERR, "st7789: getplaneinfo failed\n");
      g_lcd = NULL;
      return NULL;
    }

  g_lcd->setpower(g_lcd, CONFIG_LCD_MAXPOWER);
  g_ready = true;

  syslog(LOG_INFO, "st7789: %dx%d on SPI1 DC=P%c%d RST=P%c%d\n",
         ST7789_W, ST7789_H,
         'A' + CONFIG_F1C100S_ST7789_DC_PORT,
         CONFIG_F1C100S_ST7789_DC_PIN,
         'A' + CONFIG_F1C100S_ST7789_RST_PORT,
         CONFIG_F1C100S_ST7789_RST_PIN);

  return g_lcd;
}

/****************************************************************************
 * Name: board_lcd_uninitialize
 ****************************************************************************/

void board_lcd_uninitialize(void)
{
  if (g_lcd != NULL)
    {
      g_lcd->setpower(g_lcd, 0);
    }
}

/****************************************************************************
 * Name: f1c100s_st7789_initialize
 *
 * Description:
 *   Board bringup entry point (called from f1c100s_bringup.c). Wraps
 *   fb_register(0, 0), which drives up_fbinitialize() (the generic
 *   hook that calls board_lcd_initialize()/board_lcd_getdev() above)
 *   and then, because CONFIG_LCD_FRAMEBUFFER is set, registers
 *   /dev/fb0 so standard framebuffer tools (apps/examples/fb) can
 *   exercise this screen. up_fbinitialize() alone only builds the
 *   lcd_dev_s/vtable -- it does not create the /dev/fb0 node itself.
 *   BL and AP never run at the same time, so both always using fb0
 *   here is not a conflict; the AP-only RGB LCD takes /dev/fb1.
 *
 ****************************************************************************/

int f1c100s_st7789_initialize(void)
{
  return fb_register(0, 0);
}

void f1c100s_boot_status(const char *msg)
{
  if (!g_ready)
    {
      return;
    }

  st7789_fill(COLOR_BG);
#ifdef CONFIG_F1C100S_ST7789_AP
  st7789_text(16, 40, "F1C100s AP");
#else
  st7789_text(16, 40, "F1C100s BL");
#endif
  if (msg != NULL)
    {
      st7789_text(16, 120, msg);
    }
}

#endif /* CONFIG_F1C100S_ST7789 */
