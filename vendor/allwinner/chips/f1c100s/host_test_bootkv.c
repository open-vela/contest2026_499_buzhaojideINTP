/****************************************************************************
 * vendor/allwinner/chips/f1c100s/host_test_bootkv.c
 *
 * Host-side protocol test for the dual-slot KV atomicity rules.
 *
 *   gcc -std=c99 -Wall -Werror -DF1C100S_BOOTKV_HOSTTEST \
 *       -I. -o /tmp/bootkv_host_test \
 *       host_test_bootkv.c f1c100s_bootkv.c \
 *   && /tmp/bootkv_host_test
 ****************************************************************************/

#define F1C100S_BOOTKV_HOSTTEST 1

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include "f1c100s_bootkv.h"

static int g_fail;
static int g_pass;

#define EXPECT(cond) \
  do \
    { \
      if (cond) \
        { \
          g_pass++; \
        } \
      else \
        { \
          fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
          g_fail++; \
        } \
    } \
  while (0)

static int slot_valid_magic(const uint8_t *sector, int slot)
{
  const uint8_t *p = sector + (size_t)slot * F1C100S_BOOTKV_SLOT_SIZE;
  uint32_t magic = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  return magic == F1C100S_BOOTKV_MAGIC;
}

static int slot_blank(const uint8_t *sector, int slot)
{
  const uint8_t *p = sector + (size_t)slot * F1C100S_BOOTKV_SLOT_SIZE;
  size_t i;

  for (i = 0; i < F1C100S_BOOTKV_SLOT_SIZE; i++)
    {
      if (p[i] != 0xffu)
        {
          return 0;
        }
    }

  return 1;
}

static void fill_kv(struct f1c100s_bootkv_s *kv, uint32_t fail,
                    uint32_t active, uint32_t size, uint32_t crc)
{
  memset(kv, 0, sizeof(*kv));
  kv->fail_count  = fail;
  kv->active_flag = active;
  kv->ap_size     = size;
  kv->ap_crc32    = crc;
}

static void test_blank_decode(void)
{
  uint8_t sector[F1C100S_PART_KV_SIZE];
  struct f1c100s_bootkv_s kv;

  memset(sector, 0xff, sizeof(sector));
  EXPECT(f1c100s_bootkv_decode(sector, &kv) == F1C100S_BOOTKV_BLANK);
  EXPECT(kv.seq == 0);
  EXPECT(kv.fail_count == 0);
}

static void test_first_commit(void)
{
  uint8_t sector[F1C100S_PART_KV_SIZE];
  struct f1c100s_bootkv_s in;
  struct f1c100s_bootkv_s out;

  memset(sector, 0xff, sizeof(sector));
  fill_kv(&in, 0, F1C100S_BOOTKV_ACTIVE_PENDING, 0x1234, 0xabcdef01);
  EXPECT(f1c100s_bootkv_commit_ram(sector, &in) == 0);
  EXPECT(f1c100s_bootkv_decode(sector, &out) == 0);
  EXPECT(out.seq == 1);
  EXPECT(out.fail_count == 0);
  EXPECT(out.active_flag == F1C100S_BOOTKV_ACTIVE_PENDING);
  EXPECT(out.ap_size == 0x1234);
  EXPECT(out.ap_crc32 == 0xabcdef01);
  EXPECT(slot_valid_magic(sector, 0));
  EXPECT(slot_blank(sector, 1));
}

static void test_ping_pong_and_erase(void)
{
  uint8_t sector[F1C100S_PART_KV_SIZE];
  struct f1c100s_bootkv_s in;
  struct f1c100s_bootkv_s out;
  int i;

  memset(sector, 0xff, sizeof(sector));

  for (i = 0; i < 8; i++)
    {
      fill_kv(&in, (uint32_t)i, F1C100S_BOOTKV_ACTIVE_CONFIRMED,
              1000u + (uint32_t)i, 0x11111111u * (uint32_t)(i + 1));
      EXPECT(f1c100s_bootkv_commit_ram(sector, &in) == 0);
      EXPECT(f1c100s_bootkv_decode(sector, &out) == 0);
      EXPECT(out.seq == (uint32_t)(i + 1));
      EXPECT(out.fail_count == (uint32_t)i);
      EXPECT(out.ap_size == 1000u + (uint32_t)i);

      if (i == 0)
        {
          EXPECT(slot_valid_magic(sector, 0));
          EXPECT(slot_blank(sector, 1));
        }
      else if ((i % 2) == 1)
        {
          /* Second, fourth, ... write lands in slot 1; slot 0 tombstoned. */

          EXPECT(!slot_valid_magic(sector, 0));
          EXPECT(slot_valid_magic(sector, 1));
          EXPECT(!slot_blank(sector, 0));
        }
      else
        {
          /* Erase + slot 0. Slot 1 is blank again. */

          EXPECT(slot_valid_magic(sector, 0));
          EXPECT(slot_blank(sector, 1));
        }
    }
}

static void test_two_valid_picks_newer(void)
{
  uint8_t a[F1C100S_PART_KV_SIZE];
  uint8_t b[F1C100S_PART_KV_SIZE];
  uint8_t mixed[F1C100S_PART_KV_SIZE];
  struct f1c100s_bootkv_s in;
  struct f1c100s_bootkv_s out;

  memset(a, 0xff, sizeof(a));
  fill_kv(&in, 1, F1C100S_BOOTKV_ACTIVE_PENDING, 10, 20);
  EXPECT(f1c100s_bootkv_commit_ram(a, &in) == 0);

  memcpy(b, a, sizeof(b));
  fill_kv(&in, 2, F1C100S_BOOTKV_ACTIVE_CONFIRMED, 11, 21);
  EXPECT(f1c100s_bootkv_commit_ram(b, &in) == 0);

  /* Simulate power loss after programming the new slot and before
   * tombstone: take slot 0 from `a` (still valid) and slot 1 from `b`.
   */

  memcpy(mixed, b, sizeof(mixed));
  memcpy(mixed, a, F1C100S_BOOTKV_SLOT_SIZE);

  EXPECT(slot_valid_magic(mixed, 0));
  EXPECT(slot_valid_magic(mixed, 1));
  EXPECT(f1c100s_bootkv_decode(mixed, &out) == 0);
  EXPECT(out.seq == 2);
  EXPECT(out.fail_count == 2);
  EXPECT(out.active_flag == F1C100S_BOOTKV_ACTIVE_CONFIRMED);
}

static void test_partial_new_keeps_old(void)
{
  uint8_t sector[F1C100S_PART_KV_SIZE];
  uint8_t after[F1C100S_PART_KV_SIZE];
  struct f1c100s_bootkv_s in;
  struct f1c100s_bootkv_s out;
  size_t i;

  memset(sector, 0xff, sizeof(sector));
  fill_kv(&in, 3, F1C100S_BOOTKV_ACTIVE_CONFIRMED, 50, 60);
  EXPECT(f1c100s_bootkv_commit_ram(sector, &in) == 0);

  memcpy(after, sector, sizeof(after));
  fill_kv(&in, 4, F1C100S_BOOTKV_ACTIVE_PENDING, 51, 61);
  EXPECT(f1c100s_bootkv_commit_ram(after, &in) == 0);

  /* Power loss mid-program of slot 1: only the first 8 bytes made it. */

  memcpy(sector + F1C100S_BOOTKV_SLOT_SIZE,
         after + F1C100S_BOOTKV_SLOT_SIZE, 8);
  for (i = 8; i < F1C100S_BOOTKV_REC_SIZE; i++)
    {
      sector[F1C100S_BOOTKV_SLOT_SIZE + i] = 0xff;
    }

  EXPECT(f1c100s_bootkv_decode(sector, &out) == 0);
  EXPECT(out.seq == 1);
  EXPECT(out.fail_count == 3);
}

static void test_partial_tombstone_keeps_new(void)
{
  uint8_t sector[F1C100S_PART_KV_SIZE];
  uint8_t after[F1C100S_PART_KV_SIZE];
  struct f1c100s_bootkv_s in;
  struct f1c100s_bootkv_s out;

  memset(sector, 0xff, sizeof(sector));
  fill_kv(&in, 5, F1C100S_BOOTKV_ACTIVE_PENDING, 70, 80);
  EXPECT(f1c100s_bootkv_commit_ram(sector, &in) == 0);

  memcpy(after, sector, sizeof(after));
  fill_kv(&in, 6, F1C100S_BOOTKV_ACTIVE_CONFIRMED, 71, 81);
  EXPECT(f1c100s_bootkv_commit_ram(after, &in) == 0);

  /* New slot fully programmed; old magic only partially cleared
   * (low byte 0, rest of original magic). CRC of old will fail too.
   */

  memcpy(sector, after, sizeof(sector));
  sector[0] = 0x00; /* was 'K' */
  /* restore the rest of slot 0 from the pre-tombstone image: already
   * in `after` slot 0 is fully tombstoned, so rebuild from first commit
   * + new slot.
   */

  memset(sector, 0xff, sizeof(sector));
  fill_kv(&in, 5, F1C100S_BOOTKV_ACTIVE_PENDING, 70, 80);
  EXPECT(f1c100s_bootkv_commit_ram(sector, &in) == 0);
  memcpy(sector + F1C100S_BOOTKV_SLOT_SIZE,
         after + F1C100S_BOOTKV_SLOT_SIZE, F1C100S_BOOTKV_SLOT_SIZE);
  sector[0] = 0x00;

  EXPECT(f1c100s_bootkv_decode(sector, &out) == 0);
  EXPECT(out.seq == 2);
  EXPECT(out.fail_count == 6);
}

static void test_interrupted_erase_is_eio(void)
{
  uint8_t sector[F1C100S_PART_KV_SIZE];
  struct f1c100s_bootkv_s out;

  memset(sector, 0x00, sizeof(sector));
  EXPECT(f1c100s_bootkv_decode(sector, &out) == -EIO);

  /* Recover by committing: erase-window survivor path. */

  {
    struct f1c100s_bootkv_s in;
    fill_kv(&in, 0, F1C100S_BOOTKV_ACTIVE_NONE, 0, 0);
    EXPECT(f1c100s_bootkv_commit_ram(sector, &in) == 0);
    EXPECT(f1c100s_bootkv_decode(sector, &out) == 0);
    EXPECT(out.seq == 1);
  }
}

static void test_seq_wrap_skips_zero(void)
{
  uint8_t sector[F1C100S_PART_KV_SIZE];
  struct f1c100s_bootkv_s in;
  struct f1c100s_bootkv_s out;
  uint8_t *p;
  uint32_t crc;
  uint32_t c;
  int b;
  size_t i;

  memset(sector, 0xff, sizeof(sector));

  /* Craft a valid seq=0xffffffff record in slot 0. */

  p = sector;
  p[0] = 'K';
  p[1] = 'V';
  p[2] = '0';
  p[3] = '1';
  p[4] = 0xff;
  p[5] = 0xff;
  p[6] = 0xff;
  p[7] = 0xff;          /* seq = 0xffffffff */
  p[8] = 9;
  p[9] = 0;
  p[10] = 0;
  p[11] = 0;            /* fail_count = 9 */
  p[12] = 2;
  p[13] = 0;
  p[14] = 0;
  p[15] = 0;            /* CONFIRMED */
  p[16] = 0;
  p[17] = 0;
  p[18] = 0;
  p[19] = 0;
  p[20] = 0;
  p[21] = 0;
  p[22] = 0;
  p[23] = 0;

  crc = 0xffffffffu;
  for (i = 0; i < 24; i++)
    {
      crc ^= p[i];
      for (b = 0; b < 8; b++)
        {
          crc = (crc & 1u) ? (crc >> 1) ^ 0xedb88320u : (crc >> 1);
        }
    }

  c = ~crc;
  p[24] = (uint8_t)c;
  p[25] = (uint8_t)(c >> 8);
  p[26] = (uint8_t)(c >> 16);
  p[27] = (uint8_t)(c >> 24);

  EXPECT(f1c100s_bootkv_decode(sector, &out) == 0);
  EXPECT(out.seq == 0xffffffffu);

  fill_kv(&in, 0, F1C100S_BOOTKV_ACTIVE_CONFIRMED, 0, 0);
  EXPECT(f1c100s_bootkv_commit_ram(sector, &in) == 0);
  EXPECT(f1c100s_bootkv_decode(sector, &out) == 0);
  EXPECT(out.seq == 1);
}

static void test_ap_size_rejected(void)
{
  uint8_t sector[F1C100S_PART_KV_SIZE];
  struct f1c100s_bootkv_s in;

  memset(sector, 0xff, sizeof(sector));
  fill_kv(&in, 0, 0, F1C100S_PART_AP_SIZE + 1, 0);
  EXPECT(f1c100s_bootkv_commit_ram(sector, &in) == -E2BIG);
}

int main(void)
{
  test_blank_decode();
  test_first_commit();
  test_ping_pong_and_erase();
  test_two_valid_picks_newer();
  test_partial_new_keeps_old();
  test_partial_tombstone_keeps_new();
  test_interrupted_erase_is_eio();
  test_seq_wrap_skips_zero();
  test_ap_size_rejected();

  printf("bootkv host test: %d pass, %d fail\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
