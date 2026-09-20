/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_bootkv.h
 *
 * Dual-slot boot KV in the 4 KiB SPI-NOR sector at F1C100S_PART_KV_OFFSET.
 * Spec: 2026-09-17-f1c100s-bl-ap-boot-architecture-design.md §4 / §10.
 *
 * On-media layout (one W25 erase sector, 4096 bytes):
 *
 *   offset 0x000  slot 0 (2048 bytes)
 *   offset 0x800  slot 1 (2048 bytes)
 *
 * Each slot starts with a 28-byte little-endian record; the rest is 0xFF:
 *
 *   0  magic        F1C100S_BOOTKV_MAGIC ('K','V','0','1')
 *   4  seq          commit serial, starts at 1, skips 0
 *   8  fail_count
 *  12  active_flag  NONE / PENDING / CONFIRMED
 *  16  ap_size
 *  20  ap_crc32
 *  24  crc32        IEEE 802.3 of bytes [0, 24)
 *
 * ---------------------------------------------------------------------------
 * Atomicity protocol (power-fail: at most one slot is lost)
 * ---------------------------------------------------------------------------
 *
 * NOR constraint: a programmed bit can only go 1→0; 0→1 needs a sector
 * erase. The KV region is a single 4 KiB sector (spec §3), so the two
 * slots share one erase. Dual-slot ping-pong is therefore:
 *
 *   1. Prefer a slot whose 2048 bytes are still 0xFF (program-only, no
 *      erase). Encode the new record, program it, verify.
 *   2. Only then tombstone the previous valid slot by programming its
 *      magic to 0x00000000 (1→0, no erase). Magic was chosen with mixed
 *      1-bits so this is always a legal NOR operation.
 *   3. When neither slot is blank (valid + tombstone, or two valid from
 *      an interrupted tombstone), erase the whole sector, then program
 *      slot 0. This is the only window in which both copies can be lost.
 *
 * Recovery on read:
 *
 *   both 0xFF                         → BLANK (never written)
 *   one valid (magic + crc)           → use it
 *   two valid                         → higher seq (serial-number
 *                                       arithmetic; seq 0 is reserved)
 *   neither valid, not both blank     → -EIO (interrupted erase / bit rot)
 *
 * Failure-mode table (the case that must not yield two damaged copies
 * except the documented erase window):
 *
 *   during program of new slot     old still valid, new crc fails  → old
 *   new programmed, tombstone not  two valid                       → newer
 *   during tombstone of old magic  new valid, old magic ≠ MAGIC    → new
 *   during sector erase            both undefined                  → -EIO
 *
 * After -EIO the next write erases and starts seq at 1. BL must treat
 * BLANK and -EIO as "do not jump AP".
 *
 * Erase happens at most every other write. KV is written rarely
 * (fail_count++, bootctl_success, AP flash), so the window is small.
 ****************************************************************************/

#ifndef __F1C100S_BOOTKV_H
#define __F1C100S_BOOTKV_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <sys/types.h>

#ifndef F1C100S_BOOTKV_HOSTTEST
#  include <nuttx/config.h>
#endif

#ifndef FAR
#  define FAR
#endif

#include "f1c100s_partitions.h"

#ifdef __cplusplus
extern "C"
{
#endif

struct mtd_dev_s;

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#if F1C100S_PART_KV_SIZE != F1C100S_NOR_SECTOR_SIZE
#  error "bootkv assumes the KV region is exactly one NOR sector"
#endif

#define F1C100S_BOOTKV_SLOTS           2
#define F1C100S_BOOTKV_SLOT_SIZE       (F1C100S_PART_KV_SIZE / F1C100S_BOOTKV_SLOTS)

#define F1C100S_BOOTKV_MAGIC           0x3130564bu  /* 'K','V','0','1' LE */

#define F1C100S_BOOTKV_OFF_MAGIC       0
#define F1C100S_BOOTKV_OFF_SEQ         4
#define F1C100S_BOOTKV_OFF_FAIL_COUNT  8
#define F1C100S_BOOTKV_OFF_ACTIVE      12
#define F1C100S_BOOTKV_OFF_AP_SIZE     16
#define F1C100S_BOOTKV_OFF_AP_CRC32    20
#define F1C100S_BOOTKV_OFF_CRC32       24
#define F1C100S_BOOTKV_REC_SIZE        28

#define F1C100S_BOOTKV_ACTIVE_NONE      0u
#define F1C100S_BOOTKV_ACTIVE_PENDING   1u
#define F1C100S_BOOTKV_ACTIVE_CONFIRMED 2u
#define F1C100S_BOOTKV_ACTIVE_BOOTLOADER 3u

/* f1c100s_bootkv_decode / read: sector has never been written. */

#define F1C100S_BOOTKV_BLANK           1

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct f1c100s_bootkv_s
{
  uint32_t fail_count;
  uint32_t active_flag;
  uint32_t ap_size;
  uint32_t ap_crc32;
  uint32_t seq;           /* 0 means BLANK / not yet committed */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Decode one 4 KiB sector image. Returns 0, F1C100S_BOOTKV_BLANK, or -EIO. */

int f1c100s_bootkv_decode(const uint8_t *sector,
                          struct f1c100s_bootkv_s *out);

/* Apply one commit to a RAM sector using NOR 1→0 / erase semantics.
 * Used by the host protocol test and by the MTD backend.
 * in->seq is ignored; the new seq is old+1 (or 1 if blank).
 */

int f1c100s_bootkv_commit_ram(uint8_t *sector,
                              const struct f1c100s_bootkv_s *in);

#ifndef F1C100S_BOOTKV_HOSTTEST

/* Bind the W25 MTD. offset is the byte offset of the KV sector inside
 * that MTD (F1C100S_PART_KV_OFFSET when mtd is the whole chip).
 */

int f1c100s_bootkv_bind(FAR struct mtd_dev_s *mtd, off_t offset);

int f1c100s_bootkv_read(FAR struct f1c100s_bootkv_s *out);
int f1c100s_bootkv_write(FAR const struct f1c100s_bootkv_s *in);

/* Byte read anywhere on the bound MTD (whole W25 when offset is
 * F1C100S_PART_KV_OFFSET at bind). Used by BL to pull the AP image.
 */

int f1c100s_bootkv_flash_read(off_t offset, FAR void *buffer, size_t nbytes);

#endif /* !F1C100S_BOOTKV_HOSTTEST */

#ifdef __cplusplus
}
#endif

#endif /* __F1C100S_BOOTKV_H */
