/****************************************************************************
 * vendor/allwinner/chips/f1c100s/f1c_head.S
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

	.file	"f1c100s_head.S"

/****************************************************************************
 * Configuration
 ****************************************************************************/

/* Memory configuration - must match chip configuration */
#ifndef CONFIG_RAM_SIZE
#  define CONFIG_RAM_SIZE (32*1024*1024)
#endif

#ifndef CONFIG_RAM_START
#  define CONFIG_RAM_START 0x80000000
#endif

#ifndef CONFIG_RAM_VSTART
#  define CONFIG_RAM_VSTART 0x80000000
#endif

/* RX_NSECTIONS - Number of 1MB sections for executable region */
#define RX_NSECTIONS ((CONFIG_RAM_SIZE+0x000fffff) >> 20)

/* PSR mode bits */
#define PSR_MODE_SYS    0x0000001f
#define PSR_I_BIT       0x00000080
#define PSR_F_BIT       0x00000040

/* CP15 Control Register bits */
#define CR_M            0x00000001  /* MMU enable */
#define CR_A            0x00000002  /* Alignment abort enable */
#define CR_C            0x00000004  /* Dcache enable */
#define CR_W            0x00000008  /* Write buffer enable */
#define CR_P            0x00000010  /* 32-bit exception handler */
#define CR_D            0x00000020  /* 32-bit data address range */
#define CR_L            0x00000040  /* Implementation defined */
#define CR_B            0x00000080  /* Big endian */
#define CR_S            0x00000100  /* System MMU protection */
#define CR_R            0x00000200  /* ROM MMU protection */
#define CR_F            0x00000400  /* Implementation defined */
#define CR_Z            0x00000800  /* Implementation defined */
#define CR_I            0x00001000  /* Icache enable */
#define CR_V            0x00002000  /* Vectors relocated to 0xffff0000 */

/* MMU flags */
#define MMU_L1_SECTION       0x00000002
#define MMU_L1_CACHEABLE     0x00000008
#define MMU_L1_BUFFERABLE    0x00000004
#define MMU_L1_AP_RW         0x00000400
#define MMU_MEMFLAGS         (MMU_L1_SECTION | MMU_L1_CACHEABLE | MMU_L1_BUFFERABLE | MMU_L1_AP_RW)

/* Page table - place at fixed address in low memory to avoid boundary issues */
#define PGTABLE_SIZE         0x00004000
#define PGTABLE_BASE_PADDR   0x80100000
#define PGTABLE_BASE_VADDR   0x80100000

/* NuttX start addresses */
#define NUTTX_START_PADDR    CONFIG_RAM_START
#define NUTTX_START_VADDR    CONFIG_RAM_VSTART

/* Section size */
#define SECTION_SIZE         0x00100000

/****************************************************************************
 * eGON Header - 96 bytes
 * This header allows NuttX to be booted by awboot bootloader.
 * The jump instruction also allows QEMU to boot directly.
 ****************************************************************************/

	.section .egon_header, "ax"
	.arm
	.global	_egon_header
_egon_header:
	b	__start                     /* +0x00: Jump to __start (B +96) */
	.ascii	"eGON.NTX"              /* +0x04: Magic for NuttX */
	.long	0x5F0A6C39              /* +0x0C: Checksum placeholder */
	.long	0                       /* +0x10: Length placeholder */
	.ascii	"NTX\x01"               /* +0x14: SPL signature */
	.long	0                       /* +0x18: fel_script_address */
	.long	0                       /* +0x1C: fel_uenv_length */
	.long	0                       /* +0x20: dt_name_offset */
	.long	0                       /* +0x24: reserved1 */
	.long	0                       /* +0x28: boot_media */
	.long	0, 0, 0, 0, 0, 0, 0, 0  /* +0x2C: string_pool[0-7] */
	.long	0, 0, 0, 0, 0           /* +0x4C: string_pool[8-12] */
	/* Total: 96 bytes (0x60) */

/****************************************************************************
 * Assembly Macros
 ****************************************************************************/

/****************************************************************************
 * Name: __start
 ****************************************************************************/

	.text
	.syntax	unified
	.arm
	.global	__start
	.type	__start, #function

__start:
	ldr		r0, =0x01c25000
	mov		r1, #0x58      /* 'X' */
	str		r1, [r0]

	/* Kill/restart watchdog immediately */
	ldr		r0, =0x01c20ca0
	ldr		r1, =0x14af    /* (0xa57 << 1) | 1: restart WDT */
	str		r1, [r0, #0x10]
	mov		r1, #0         /* disable WDT_MODE */
	str		r1, [r0, #0x18]

	/* Make sure we are in SVC mode with all IRQs disabled */
	mov		r0, #(0x13 | PSR_I_BIT | PSR_F_BIT)
	msr		cpsr_c, r0

	/* Disable MMU, caches, write buffer, and force Low Vectors (0x0) */
	mrc		p15, 0, r0, c1, c0, 0
	bic		r0, r0, #(CR_M | CR_C | CR_W)
	bic		r0, r0, #CR_V
	bic		r0, r0, #CR_I
	mcr		p15, 0, r0, c1, c0, 0

	/* Invalidate caches and TLB */
	mov		r0, #0
	mcr		p15, 0, r0, c7, c7, 0	/* Invalidate I+D caches */
	mcr		p15, 0, r0, c8, c7, 0	/* Invalidate TLB */

	ldr		r0, =0x01c25000
	mov		r1, #0x59      /* 'Y' */
	str		r1, [r0]

	/* Copy vector table to 0x0 */
	ldr		r0, =_vector_start
	mov		r1, #0
	ldmia		r0!, {r2-r9}
	stmia		r1!, {r2-r9}
	ldmia		r0!, {r2-r9}
	stmia		r1!, {r2-r9}

	/* Switch to SYS mode */
	mov		r0, #(PSR_MODE_SYS | PSR_I_BIT | PSR_F_BIT)
	msr		cpsr_c, r0

	/* Zero BSS */
	ldr		r0, .Lsbss
	ldr		r1, .Lebss
	mov		r2, #0
.Lbssclear:
	cmp		r0, r1
	strcc		r2, [r0], #4
	bcc		.Lbssclear

	/* Setup stack */
	ldr		sp, .Lstacktop

	/* Clear frame pointer */
	mov		fp, #0

	ldr		r0, =0x01c25000
	mov		r1, #0x5a      /* 'Z' */
	str		r1, [r0]

	/* Call board initialization */
	bl		arm_boot

	/* Branch to OS entry point */
	mov		lr, #0
	b		nx_start

.Lsbss:
	.long	_sbss
.Lebss:
	.long	_ebss
.Lstacktop:
	.long	_ebss+CONFIG_IDLETHREAD_STACKSIZE

	.size	__start, .-__start

/****************************************************************************
 * Data section variables
 ****************************************************************************/

	.data
	.align	4
	.globl	g_idle_topstack
	.type	g_idle_topstack, object

g_idle_topstack:
	.long	_ebss+CONFIG_IDLETHREAD_STACKSIZE
	.size	g_idle_topstack, .-g_idle_topstack
	.end
