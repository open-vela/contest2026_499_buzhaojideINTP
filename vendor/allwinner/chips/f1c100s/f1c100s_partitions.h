/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c100s_partitions.h
 *
 * F1C100s SPI-NOR (W25, 16 MiB) partition layout.
 *
 * ============================ SINGLE SOURCE OF TRUTH =====================
 * Spec: docs/superpowers/specs/2026-09-17-f1c100s-bl-ap-boot-architecture-design.md §3
 *
 * Consumers:
 *   - NuttX BL / AP: KV library, board_boot_image(), fastboot flash targets
 *   - awboot: CONFIG_KERNEL_OFFSET is compile-time asserted against
 *     F1C100S_PART_BL_OFFSET (see awboot lib/loader/raw_loader.c)
 *
 * Chip: Winbond W25 series 16 MiB SPI-NOR. Page 256 B, sector 4 KiB,
 * block 64 KiB. Layout measured 2026-09-17 via `xfel spinor read`.
 *
 *   0x000000 +------------------+
 *            | awboot SPL       |  24 KiB   BROM eGON load; do not move
 *   0x006000 +------------------+
 *            | reserved/align   |  40 KiB   currently erased (0xFF)
 *   0x010000 +------------------+
 *            | NuttX-BL         | 640 KiB   miniboot + fastboot + ST7789
 *   0x0B0000 +------------------+
 *            | KV state         |   4 KiB   one erase sector, dual-slot
 *   0x0B1000 +------------------+
 *            | NuttX-AP         | 3.0 MiB   RTOS application image
 *   0x3B1000 +------------------+
 *            | DATA (LittleFS)  | ~12.3 MiB internal non-volatile storage
 *   0x1000000 +-----------------+
 *
 * No A/B AP slot: brick recovery is BL's job.
 *
 * This header is C preprocessor only (no NuttX types) so awboot can include
 * it without pulling in nuttx/config.h.
 ****************************************************************************/

#ifndef __F1C100S_PARTITIONS_H
#define __F1C100S_PARTITIONS_H

/* Geometry */

#define F1C100S_NOR_PAGE_SIZE      256
#define F1C100S_NOR_SECTOR_SIZE    4096
#define F1C100S_NOR_BLOCK_SIZE     (64 * 1024)
#define F1C100S_NOR_TOTAL_SIZE     (16 * 1024 * 1024)

/* Partition offsets/sizes in BYTES */

#define F1C100S_PART_SPL_OFFSET    0x000000
#define F1C100S_PART_SPL_SIZE      0x006000   /* 24 KiB, matches measured SPL */

#define F1C100S_PART_RESV_OFFSET   0x006000
#define F1C100S_PART_RESV_SIZE     0x00A000   /* 40 KiB align to 64 KiB */

#define F1C100S_PART_BL_OFFSET     0x010000
#define F1C100S_PART_BL_SIZE       0x0A0000   /* 640 KiB */

#define F1C100S_PART_KV_OFFSET     0x0B0000
#define F1C100S_PART_KV_SIZE       F1C100S_NOR_SECTOR_SIZE  /* 4 KiB */

#define F1C100S_PART_AP_OFFSET     0x0B1000
#define F1C100S_PART_AP_SIZE       (3 * 1024 * 1024)        /* 3 MiB = 0x300000 */

#define F1C100S_PART_DATA_OFFSET   (F1C100S_PART_AP_OFFSET + F1C100S_PART_AP_SIZE) /* 0x3B1000 */
#define F1C100S_PART_DATA_SIZE     (F1C100S_NOR_TOTAL_SIZE - F1C100S_PART_DATA_OFFSET) /* 0xC4F000 (~12.308 MiB) */

/* Compile-time layout checks */

#if (F1C100S_PART_SPL_OFFSET + F1C100S_PART_SPL_SIZE) != F1C100S_PART_RESV_OFFSET
#  error "F1C100s partition map: SPL/RESV not contiguous"
#endif
#if (F1C100S_PART_RESV_OFFSET + F1C100S_PART_RESV_SIZE) != F1C100S_PART_BL_OFFSET
#  error "F1C100s partition map: RESV/BL not contiguous"
#endif
#if (F1C100S_PART_BL_OFFSET + F1C100S_PART_BL_SIZE) != F1C100S_PART_KV_OFFSET
#  error "F1C100s partition map: BL/KV not contiguous"
#endif
#if (F1C100S_PART_KV_OFFSET + F1C100S_PART_KV_SIZE) != F1C100S_PART_AP_OFFSET
#  error "F1C100s partition map: KV/AP not contiguous"
#endif
#if (F1C100S_PART_AP_OFFSET + F1C100S_PART_AP_SIZE) != F1C100S_PART_DATA_OFFSET
#  error "F1C100s partition map: AP/DATA not contiguous"
#endif
#if (F1C100S_PART_DATA_OFFSET + F1C100S_PART_DATA_SIZE) != F1C100S_NOR_TOTAL_SIZE
#  error "F1C100s partition map: DATA does not fill the rest of flash"
#endif
#if (F1C100S_PART_KV_OFFSET % F1C100S_NOR_SECTOR_SIZE) != 0
#  error "F1C100s KV region must be sector-aligned"
#endif
#if (F1C100S_PART_AP_OFFSET % F1C100S_NOR_SECTOR_SIZE) != 0
#  error "F1C100s AP region must be sector-aligned"
#endif
#if (F1C100S_PART_DATA_OFFSET % F1C100S_NOR_SECTOR_SIZE) != 0
#  error "F1C100s DATA region must be sector-aligned"
#endif
#if (F1C100S_PART_DATA_SIZE % F1C100S_NOR_SECTOR_SIZE) != 0
#  error "F1C100s DATA size must be sector-aligned"
#endif

#endif /* __F1C100S_PARTITIONS_H */
