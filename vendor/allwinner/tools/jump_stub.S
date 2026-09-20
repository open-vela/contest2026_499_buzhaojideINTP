/* Disable MMU/caches then jump to NuttX at 0x80000000 (FEL DRAM boot). */
	.text
	.global _start
_start:
	mrc	p15, 0, r0, c1, c0, 0
	bic	r0, r0, #0x1000		/* I */
	bic	r0, r0, #0x2000		/* V */
	bic	r0, r0, #0x5		/* M + C */
	mcr	p15, 0, r0, c1, c0, 0
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0	/* invalidate I-cache */
	mov	r0, #0
	mov	r1, #0
	mov	r2, #0
	ldr	r3, =0x80000000
	bx	r3
