/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_bootkv.c
 *
 * Dual-slot boot KV. Protocol is documented in f1c100s_bootkv.h.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#ifdef F1C100S_BOOTKV_HOSTTEST
#  include <stdint.h>
#  include <string.h>
#  include <errno.h>
#  ifndef FAR
#    define FAR
#  endif
#else
#  include <nuttx/config.h>
#  include <nuttx/mtd/mtd.h>
#  include <nuttx/mutex.h>
#  include <syslog.h>
#  include <inttypes.h>
#  include <string.h>
#  include <errno.h>
#endif

#include "f1c100s_bootkv.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Compile-time size check: 7 × uint32_t fields. */

typedef char f1c100s_bootkv_rec_size_check[
  (F1C100S_BOOTKV_REC_SIZE == 28) ? 1 : -1];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t bootkv_get_le32(FAR const uint8_t *p)
{
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void bootkv_put_le32(FAR uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v);
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

/* IEEE 802.3 / gzip CRC-32 (poly 0xedb88320). Independent of libc so the
 * host protocol test and the firmware share one implementation.
 */

static uint32_t bootkv_crc32(FAR const uint8_t *data, size_t len)
{
  uint32_t crc = 0xffffffffu;
  size_t i;
  int b;

  for (i = 0; i < len; i++)
    {
      crc ^= data[i];
      for (b = 0; b < 8; b++)
        {
          if ((crc & 1u) != 0)
            {
              crc = (crc >> 1) ^ 0xedb88320u;
            }
          else
            {
              crc >>= 1;
            }
        }
    }

  return ~crc;
}

static int bootkv_slot_blank(FAR const uint8_t *slot)
{
  size_t i;

  for (i = 0; i < F1C100S_BOOTKV_SLOT_SIZE; i++)
    {
      if (slot[i] != 0xffu)
        {
          return 0;
        }
    }

  return 1;
}

static int bootkv_slot_valid(FAR const uint8_t *slot,
                             FAR struct f1c100s_bootkv_s *out)
{
  uint32_t magic;
  uint32_t crc;
  uint32_t expect;

  magic = bootkv_get_le32(slot + F1C100S_BOOTKV_OFF_MAGIC);
  if (magic != F1C100S_BOOTKV_MAGIC)
    {
      return 0;
    }

  crc = bootkv_get_le32(slot + F1C100S_BOOTKV_OFF_CRC32);
  expect = bootkv_crc32(slot, F1C100S_BOOTKV_OFF_CRC32);
  if (crc != expect)
    {
      return 0;
    }

  if (out != NULL)
    {
      out->seq         = bootkv_get_le32(slot + F1C100S_BOOTKV_OFF_SEQ);
      out->fail_count  = bootkv_get_le32(slot + F1C100S_BOOTKV_OFF_FAIL_COUNT);
      out->active_flag = bootkv_get_le32(slot + F1C100S_BOOTKV_OFF_ACTIVE);
      out->ap_size     = bootkv_get_le32(slot + F1C100S_BOOTKV_OFF_AP_SIZE);
      out->ap_crc32    = bootkv_get_le32(slot + F1C100S_BOOTKV_OFF_AP_CRC32);
    }

  return 1;
}

/* Serial-number comparison: (int32_t)(a - b) > 0 means a is newer.
 * seq 0 is never stored; wrap 0xffffffff → 1, and 1 is still newer
 * than 0xffffffff under this arithmetic ((int32_t)(1 - 0xffffffff) == 2).
 */

static int bootkv_seq_newer(uint32_t a, uint32_t b)
{
  return (int32_t)(a - b) > 0;
}

static uint32_t bootkv_next_seq(uint32_t seq)
{
  seq++;
  if (seq == 0)
    {
      seq = 1;
    }

  return seq;
}

static void bootkv_pack(FAR uint8_t *rec, FAR const struct f1c100s_bootkv_s *in,
                        uint32_t seq)
{
  memset(rec, 0xff, F1C100S_BOOTKV_REC_SIZE);
  bootkv_put_le32(rec + F1C100S_BOOTKV_OFF_MAGIC, F1C100S_BOOTKV_MAGIC);
  bootkv_put_le32(rec + F1C100S_BOOTKV_OFF_SEQ, seq);
  bootkv_put_le32(rec + F1C100S_BOOTKV_OFF_FAIL_COUNT, in->fail_count);
  bootkv_put_le32(rec + F1C100S_BOOTKV_OFF_ACTIVE, in->active_flag);
  bootkv_put_le32(rec + F1C100S_BOOTKV_OFF_AP_SIZE, in->ap_size);
  bootkv_put_le32(rec + F1C100S_BOOTKV_OFF_AP_CRC32, in->ap_crc32);
  bootkv_put_le32(rec + F1C100S_BOOTKV_OFF_CRC32,
                  bootkv_crc32(rec, F1C100S_BOOTKV_OFF_CRC32));
}

static void bootkv_program(FAR uint8_t *dst, FAR const uint8_t *src,
                           size_t n)
{
  size_t i;

  for (i = 0; i < n; i++)
    {
      dst[i] &= src[i];
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int f1c100s_bootkv_decode(FAR const uint8_t *sector,
                          FAR struct f1c100s_bootkv_s *out)
{
  FAR const uint8_t *s0 = sector;
  FAR const uint8_t *s1 = sector + F1C100S_BOOTKV_SLOT_SIZE;
  struct f1c100s_bootkv_s a;
  struct f1c100s_bootkv_s b;
  int v0;
  int v1;

  if (sector == NULL || out == NULL)
    {
      return -EINVAL;
    }

  memset(out, 0, sizeof(*out));

  v0 = bootkv_slot_valid(s0, &a);
  v1 = bootkv_slot_valid(s1, &b);

  if (v0 && v1)
    {
      *out = bootkv_seq_newer(a.seq, b.seq) ? a : b;
      return 0;
    }

  if (v0)
    {
      *out = a;
      return 0;
    }

  if (v1)
    {
      *out = b;
      return 0;
    }

  if (bootkv_slot_blank(s0) && bootkv_slot_blank(s1))
    {
      return F1C100S_BOOTKV_BLANK;
    }

  return -EIO;
}

int f1c100s_bootkv_commit_ram(FAR uint8_t *sector,
                              FAR const struct f1c100s_bootkv_s *in)
{
  FAR uint8_t *s0;
  FAR uint8_t *s1;
  struct f1c100s_bootkv_s cur;
  uint8_t rec[F1C100S_BOOTKV_REC_SIZE];
  uint32_t seq;
  int ret;
  int blank0;
  int blank1;
  int target;
  int tomb;
  int v0;
  int v1;

  if (sector == NULL || in == NULL)
    {
      return -EINVAL;
    }

  if (in->ap_size > F1C100S_PART_AP_SIZE)
    {
      return -E2BIG;
    }

  s0 = sector;
  s1 = sector + F1C100S_BOOTKV_SLOT_SIZE;
  blank0 = bootkv_slot_blank(s0);
  blank1 = bootkv_slot_blank(s1);
  v0 = bootkv_slot_valid(s0, NULL);
  v1 = bootkv_slot_valid(s1, NULL);

  ret = f1c100s_bootkv_decode(sector, &cur);
  if (ret < 0 && ret != -EIO)
    {
      return ret;
    }

  if (ret == 0)
    {
      seq = bootkv_next_seq(cur.seq);
    }
  else
    {
      /* BLANK or corrupt: start a fresh sequence. */

      seq = 1;
    }

  bootkv_pack(rec, in, seq);

  tomb = -1;

  if (blank0 || blank1)
    {
      /* Program-only path: never erase while a blank slot remains.
       * Prefer slot 0 when both are blank so the first write is
       * deterministic.
       */

      if (blank0)
        {
          target = 0;
          if (v1)
            {
              tomb = 1;
            }
        }
      else
        {
          target = 1;
          if (v0)
            {
              tomb = 0;
            }
        }

      bootkv_program(sector + (size_t)target * F1C100S_BOOTKV_SLOT_SIZE,
                     rec, F1C100S_BOOTKV_REC_SIZE);
      if (tomb >= 0)
        {
          uint8_t zero[4] =
            {
              0, 0, 0, 0
            };

          bootkv_program(sector + (size_t)tomb * F1C100S_BOOTKV_SLOT_SIZE,
                         zero, 4);
        }
    }
  else
    {
      /* Both slots occupied: the documented dual-loss window. */

      memset(sector, 0xff, F1C100S_PART_KV_SIZE);
      bootkv_program(s0, rec, F1C100S_BOOTKV_REC_SIZE);
    }

  return 0;
}

#ifndef F1C100S_BOOTKV_HOSTTEST

/****************************************************************************
 * Private Data (MTD backend)
 ****************************************************************************/

static FAR struct mtd_dev_s *g_mtd;
static off_t                 g_offset;
static uint32_t              g_blocksize;
static uint32_t              g_erasesize;
static off_t                 g_eblock;
static off_t                 g_rwblock;
static size_t                g_npages;
static mutex_t               g_lock = NXMUTEX_INITIALIZER;
static uint8_t               g_sector[F1C100S_PART_KV_SIZE];
static uint8_t               g_orig[F1C100S_PART_KV_SIZE];

/****************************************************************************
 * Private Functions (MTD backend)
 ****************************************************************************/

static int bootkv_needs_erase(FAR const uint8_t *oldb,
                              FAR const uint8_t *newb, size_t n)
{
  size_t i;

  for (i = 0; i < n; i++)
    {
      if ((newb[i] & (uint8_t)~oldb[i]) != 0)
        {
          return 1;
        }
    }

  return 0;
}

static int bootkv_read_sector(FAR uint8_t *buf)
{
  ssize_t n;

  if (g_mtd->read != NULL)
    {
      n = MTD_READ(g_mtd, g_offset, F1C100S_PART_KV_SIZE, buf);
      if (n != (ssize_t)F1C100S_PART_KV_SIZE)
        {
          return n < 0 ? (int)n : -EIO;
        }

      return 0;
    }

  n = MTD_BREAD(g_mtd, g_rwblock, g_npages, buf);
  if (n != (ssize_t)g_npages)
    {
      return n < 0 ? (int)n : -EIO;
    }

  return 0;
}

static int bootkv_write_pages(FAR const uint8_t *oldb,
                              FAR const uint8_t *newb)
{
  size_t page;
  ssize_t n;

  for (page = 0; page < g_npages; page++)
    {
      size_t off = page * g_blocksize;
      if (memcmp(oldb + off, newb + off, g_blocksize) == 0)
        {
          continue;
        }

      n = MTD_BWRITE(g_mtd, g_rwblock + (off_t)page, 1, newb + off);
      if (n != 1)
        {
          return n < 0 ? (int)n : -EIO;
        }
    }

  return 0;
}

/****************************************************************************
 * Public Functions (MTD backend)
 ****************************************************************************/

int f1c100s_bootkv_bind(FAR struct mtd_dev_s *mtd, off_t offset)
{
  struct mtd_geometry_s geo;
  int ret;

  if (mtd == NULL)
    {
      return -EINVAL;
    }

  memset(&geo, 0, sizeof(geo));
  ret = MTD_IOCTL(mtd, MTDIOC_GEOMETRY, (unsigned long)(uintptr_t)&geo);
  if (ret < 0)
    {
      return ret;
    }

  if (geo.blocksize == 0 || geo.erasesize == 0)
    {
      return -EINVAL;
    }

  if ((geo.erasesize % geo.blocksize) != 0)
    {
      return -EINVAL;
    }

  if ((offset % geo.erasesize) != 0)
    {
      return -EINVAL;
    }

  if (geo.erasesize != F1C100S_PART_KV_SIZE)
    {
      syslog(LOG_ERR,
             "bootkv: erasesize %" PRIu32 " != KV size %u\n",
             geo.erasesize, (unsigned)F1C100S_PART_KV_SIZE);
      return -EINVAL;
    }

  nxmutex_lock(&g_lock);
  g_mtd       = mtd;
  g_offset    = offset;
  g_blocksize = geo.blocksize;
  g_erasesize = geo.erasesize;
  g_eblock    = offset / (off_t)geo.erasesize;
  g_rwblock   = offset / (off_t)geo.blocksize;
  g_npages    = F1C100S_PART_KV_SIZE / geo.blocksize;
  nxmutex_unlock(&g_lock);

  syslog(LOG_INFO,
         "bootkv: bound offset=0x%lx erase=%" PRIu32 " page=%" PRIu32 "\n",
         (unsigned long)offset, geo.erasesize, geo.blocksize);
  return 0;
}

int f1c100s_bootkv_read(FAR struct f1c100s_bootkv_s *out)
{
  int ret;

  if (out == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_lock);
  if (g_mtd == NULL)
    {
      nxmutex_unlock(&g_lock);
      return -ENODEV;
    }

  ret = bootkv_read_sector(g_sector);
  if (ret < 0)
    {
      nxmutex_unlock(&g_lock);
      return ret;
    }

  ret = f1c100s_bootkv_decode(g_sector, out);
  nxmutex_unlock(&g_lock);
  return ret;
}

int f1c100s_bootkv_write(FAR const struct f1c100s_bootkv_s *in)
{
  int ret;
  int vret;
  struct f1c100s_bootkv_s check;

  if (in == NULL)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_lock);
  if (g_mtd == NULL)
    {
      nxmutex_unlock(&g_lock);
      return -ENODEV;
    }

  ret = bootkv_read_sector(g_orig);
  if (ret < 0)
    {
      nxmutex_unlock(&g_lock);
      return ret;
    }

  memcpy(g_sector, g_orig, F1C100S_PART_KV_SIZE);
  ret = f1c100s_bootkv_commit_ram(g_sector, in);
  if (ret < 0)
    {
      nxmutex_unlock(&g_lock);
      return ret;
    }

  if (bootkv_needs_erase(g_orig, g_sector, F1C100S_PART_KV_SIZE))
    {
      ret = MTD_ERASE(g_mtd, g_eblock, 1);
      if (ret < 0)
        {
          nxmutex_unlock(&g_lock);
          return ret;
        }

      memset(g_orig, 0xff, F1C100S_PART_KV_SIZE);
    }

  ret = bootkv_write_pages(g_orig, g_sector);
  if (ret < 0)
    {
      nxmutex_unlock(&g_lock);
      return ret;
    }

  ret = bootkv_read_sector(g_orig);
  if (ret < 0)
    {
      nxmutex_unlock(&g_lock);
      return ret;
    }

  vret = f1c100s_bootkv_decode(g_orig, &check);
  nxmutex_unlock(&g_lock);

  if (vret != 0)
    {
      syslog(LOG_ERR, "bootkv: verify decode failed %d\n", vret);
      return vret < 0 ? vret : -EIO;
    }

  if (check.fail_count != in->fail_count ||
      check.active_flag != in->active_flag ||
      check.ap_size != in->ap_size ||
      check.ap_crc32 != in->ap_crc32)
    {
      syslog(LOG_ERR, "bootkv: verify mismatch\n");
      return -EIO;
    }

  return 0;
}

int f1c100s_bootkv_flash_read(off_t offset, FAR void *buffer, size_t nbytes)
{
  ssize_t n;
  int ret = 0;

  if (buffer == NULL || nbytes == 0)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_lock);
  if (g_mtd == NULL)
    {
      nxmutex_unlock(&g_lock);
      return -ENODEV;
    }

  if (g_mtd->read != NULL)
    {
      n = MTD_READ(g_mtd, offset, nbytes, buffer);
      if (n != (ssize_t)nbytes)
        {
          ret = n < 0 ? (int)n : -EIO;
        }
    }
  else
    {
      size_t page = (size_t)g_blocksize;
      off_t start;
      size_t count;

      if (page == 0 || (offset % (off_t)page) != 0 || (nbytes % page) != 0)
        {
          nxmutex_unlock(&g_lock);
          return -EINVAL;
        }

      start = offset / (off_t)page;
      count = nbytes / page;
      n = MTD_BREAD(g_mtd, start, count, buffer);
      if (n != (ssize_t)count)
        {
          ret = n < 0 ? (int)n : -EIO;
        }
    }

  nxmutex_unlock(&g_lock);
  return ret;
}

#endif /* !F1C100S_BOOTKV_HOSTTEST */
