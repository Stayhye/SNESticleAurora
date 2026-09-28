/* AURORA_PPU_R5900_DIRECT_BG_V5_20260927
 * Exact PS2 host kernels for the existing 8-bit BG renderer.
 *
 * These routines do not advance emulated state.  They translate the same
 * already-selected VRAM rows, palette metadata and visibility masks as the
 * C++ reference path.  EABI is used explicitly: the first eight integer /
 * pointer arguments are $4..$11.  No incoming argument is read from stack.
 */

        .text
        .p2align 4
        .set    noreorder
        .set    noat

/*
 * struct AuroraPPUChrDirectAsmCtx {
 *   const Uint64 *planeNormal;   // +0
 *   const Uint64 *planeHFlip;    // +4
 *   const Uint8  *maskReverse;   // +8
 *   const Uint64 *palLookup;     // +12
 * };
 *
 * void AuroraPPUFetchCHR2Direct64PS2(
 *   const Uint16 *vram, Uint32 base, const SnesRenderTileT *tiles,
 *   Int32 nTiles, Uint32 scrollY, Uint8 *dest, Uint8 *mask,
 *   const AuroraPPUChrDirectAsmCtx *ctx);
 *
 * SnesRenderTileT layout checked by C++ wrapper:
 *   +0 uTile (u16), +2 uPal, +3 uFlip, +4 uOffsetY, size 6.
 */
        .globl  AuroraPPUFetchCHR2Direct64PS2
        .ent    AuroraPPUFetchCHR2Direct64PS2
AuroraPPUFetchCHR2Direct64PS2:
        addiu   $29,$29,-0x80
        sq      $16,0x00($29)
        sq      $17,0x10($29)
        sq      $18,0x20($29)
        sq      $19,0x30($29)
        sq      $20,0x40($29)
        sq      $21,0x50($29)
        sq      $22,0x60($29)
        sq      $23,0x70($29)

        move    $16,$4                  /* vram */
        move    $17,$5                  /* base */
        move    $18,$6                  /* tiles */
        move    $19,$7                  /* nTiles */
        move    $20,$8                  /* scrollY */
        move    $21,$9                  /* dest */
        move    $22,$10                 /* mask */
        move    $23,$11                 /* ctx */

        blez    $19,.Lchr2_done
        nop

.Lchr2_loop:
        lhu     $2,0($18)               /* tile */
        lbu     $3,3($18)               /* flip: bit0 H, bit1 V */
        lbu     $12,4($18)              /* offsetY */
        sll     $2,$2,3                 /* tile * 8 words */
        addu    $2,$2,$17
        andi    $2,$2,0x7fff            /* tileAddr */
        addu    $12,$12,$20
        andi    $13,$3,2
        beq     $13,$0,.Lchr2_no_vflip
        nop
        xori    $12,$12,7
.Lchr2_no_vflip:
        addu    $2,$2,$12               /* rowAddr */
        andi    $12,$2,0x7fff
        sll     $12,$12,1               /* Uint16 -> bytes */
        addu    $12,$16,$12
        lhu     $13,0($12)              /* plane01 pair */
        andi    $10,$13,0x00ff          /* plane0 */
        srl     $11,$13,8               /* plane1 */
        or      $12,$10,$11             /* source opacity byte */

        andi    $13,$3,1
        beq     $13,$0,.Lchr2_normal
        nop
        lw      $24,4($23)              /* H-flipped plane lookup */
        move    $14,$12                 /* identity mask after H flip */
        beq     $0,$0,.Lchr2_lookup_ready
        nop
.Lchr2_normal:
        lw      $24,0($23)              /* canonical plane lookup */
        lw      $25,8($23)              /* bit-reverse mask lookup */
        addu    $25,$25,$12
        lbu     $14,0($25)
.Lchr2_lookup_ready:
        beq     $14,$0,.Lchr2_transparent
        nop

        sll     $25,$10,3
        addu    $25,$24,$25
        ld      $15,0($25)
        sll     $25,$11,3
        addu    $25,$24,$25
        ld      $13,0($25)
        dsll    $13,$13,1
        or      $15,$15,$13

        lbu     $10,2($18)              /* palette */
        lw      $25,12($23)
        sll     $11,$10,3
        addu    $25,$25,$11
        ld      $11,0($25)
        or      $15,$15,$11             /* palette metadata in all bytes */

        andi    $13,$10,8
        move    $12,$0                  /* priority mask defaults to zero */
        beq     $13,$0,.Lchr2_output
        nop
        move    $12,$14
        beq     $0,$0,.Lchr2_output
        nop

.Lchr2_transparent:
        move    $15,$0
        move    $12,$0

.Lchr2_output:
        sd      $15,0($21)
        sb      $14,0($22)
        sb      $12,48($22)
        addiu   $18,$18,6
        addiu   $21,$21,8
        addiu   $22,$22,1
        addiu   $19,$19,-1
        bgtz    $19,.Lchr2_loop
        nop

.Lchr2_done:
        lq      $16,0x00($29)
        lq      $17,0x10($29)
        lq      $18,0x20($29)
        lq      $19,0x30($29)
        lq      $20,0x40($29)
        lq      $21,0x50($29)
        lq      $22,0x60($29)
        lq      $23,0x70($29)
        addiu   $29,$29,0x80
        jr      $31
        nop
        .end    AuroraPPUFetchCHR2Direct64PS2

/* Same EABI argument map as the 2bpp kernel; ctx->palLookup is the 4bpp
 * palette table and base uses 16 VRAM words per tile. */
        .globl  AuroraPPUFetchCHR4Direct64PS2
        .ent    AuroraPPUFetchCHR4Direct64PS2
AuroraPPUFetchCHR4Direct64PS2:
        addiu   $29,$29,-0x80
        sq      $16,0x00($29)
        sq      $17,0x10($29)
        sq      $18,0x20($29)
        sq      $19,0x30($29)
        sq      $20,0x40($29)
        sq      $21,0x50($29)
        sq      $22,0x60($29)
        sq      $23,0x70($29)

        move    $16,$4
        move    $17,$5
        move    $18,$6
        move    $19,$7
        move    $20,$8
        move    $21,$9
        move    $22,$10
        move    $23,$11

        blez    $19,.Lchr4_done
        nop

.Lchr4_loop:
        lhu     $2,0($18)
        lbu     $3,3($18)
        lbu     $4,4($18)
        sll     $2,$2,4                 /* tile * 16 words */
        addu    $2,$2,$17
        andi    $2,$2,0x7fff
        addu    $4,$4,$20
        andi    $5,$3,2
        beq     $5,$0,.Lchr4_no_vflip
        nop
        xori    $4,$4,7
.Lchr4_no_vflip:
        addu    $2,$2,$4                /* rowAddr */

        andi    $8,$2,0x7fff
        sll     $8,$8,1
        addu    $8,$16,$8
        lhu     $9,0($8)                /* plane01 */
        andi    $10,$9,0x00ff
        srl     $11,$9,8

        addiu   $8,$2,8
        andi    $8,$8,0x7fff
        sll     $8,$8,1
        addu    $8,$16,$8
        lhu     $9,0($8)                /* plane23 */
        andi    $12,$9,0x00ff
        srl     $13,$9,8

        or      $4,$10,$11
        or      $4,$4,$12
        or      $4,$4,$13               /* source opacity byte */

        andi    $5,$3,1
        beq     $5,$0,.Lchr4_normal
        nop
        lw      $24,4($23)
        move    $14,$4
        beq     $0,$0,.Lchr4_lookup_ready
        nop
.Lchr4_normal:
        lw      $24,0($23)
        lw      $25,8($23)
        addu    $25,$25,$4
        lbu     $14,0($25)
.Lchr4_lookup_ready:
        beq     $14,$0,.Lchr4_transparent
        nop

        sll     $25,$10,3
        addu    $25,$24,$25
        ld      $15,0($25)

        sll     $25,$11,3
        addu    $25,$24,$25
        ld      $6,0($25)
        dsll    $6,$6,1
        or      $15,$15,$6

        sll     $25,$12,3
        addu    $25,$24,$25
        ld      $6,0($25)
        dsll    $6,$6,2
        or      $15,$15,$6

        sll     $25,$13,3
        addu    $25,$24,$25
        ld      $6,0($25)
        dsll    $6,$6,3
        or      $15,$15,$6

        lbu     $5,2($18)
        lw      $25,12($23)
        sll     $7,$5,3
        addu    $25,$25,$7
        ld      $7,0($25)
        or      $15,$15,$7

        andi    $6,$5,8
        move    $7,$0
        beq     $6,$0,.Lchr4_output
        nop
        move    $7,$14
        beq     $0,$0,.Lchr4_output
        nop

.Lchr4_transparent:
        move    $15,$0
        move    $7,$0

.Lchr4_output:
        sd      $15,0($21)
        sb      $14,0($22)
        sb      $7,48($22)
        addiu   $18,$18,6
        addiu   $21,$21,8
        addiu   $22,$22,1
        addiu   $19,$19,-1
        bgtz    $19,.Lchr4_loop
        nop

.Lchr4_done:
        lq      $16,0x00($29)
        lq      $17,0x10($29)
        lq      $18,0x20($29)
        lq      $19,0x30($29)
        lq      $20,0x40($29)
        lq      $21,0x50($29)
        lq      $22,0x60($29)
        lq      $23,0x70($29)
        addiu   $29,$29,0x80
        jr      $31
        nop
        .end    AuroraPPUFetchCHR4Direct64PS2

/*
 * void AuroraPPURenderBGDataOPS2(Uint8 *line, Uint8 *src, const Uint8 *maskData,
 *                                Uint32 scrollX, Int32 nTiles,
 *                                const Uint64 *maskLookup);
 *
 * Same qfsrv/PlaneLookup[1] operation sequence as the previous inline-MMI
 * implementation, but the complete 16-pixel loop now stays in assembly.
 */
        .globl  AuroraPPURenderBGDataOPS2
        .ent    AuroraPPURenderBGDataOPS2
AuroraPPURenderBGDataOPS2:
        andi    $2,$7,7
        addu    $5,$5,$2
        mtsab   $5,0
        blez    $8,.Lrender_o_done
        nop

.Lrender_o_loop:
        lhu     $2,0($6)
        sq      $0,0x00($4)
        beq     $2,$0,.Lrender_o_advance
        nop

        lq      $10,0x00($5)
        lq      $11,0x10($5)
        qfsrv   $10,$11,$10
        ori     $3,$0,0xffff
        beq     $2,$3,.Lrender_o_store
        nop

        andi    $3,$2,0x00ff
        sll     $3,$3,3
        addu    $3,$9,$3
        ld      $12,0($3)
        srl     $3,$2,8
        sll     $3,$3,3
        addu    $3,$9,$3
        ld      $13,0($3)
        pcpyld  $14,$13,$12
        pceqb   $14,$14,$0
        por     $10,$10,$14
        pxor    $10,$10,$14

.Lrender_o_store:
        sq      $10,0x00($4)
.Lrender_o_advance:
        addiu   $5,$5,0x10
        addiu   $4,$4,0x10
        addiu   $6,$6,2
        addiu   $8,$8,-2
        bgtz    $8,.Lrender_o_loop
        nop

.Lrender_o_done:
        jr      $31
        nop
        .end    AuroraPPURenderBGDataOPS2

        .globl  AuroraPPURenderBGDataPS2
        .ent    AuroraPPURenderBGDataPS2
AuroraPPURenderBGDataPS2:
        andi    $2,$7,7
        addu    $5,$5,$2
        mtsab   $5,0
        blez    $8,.Lrender_done
        nop

.Lrender_loop:
        lhu     $2,0($6)
        beq     $2,$0,.Lrender_advance
        nop

        lq      $10,0x00($5)
        lq      $11,0x10($5)
        qfsrv   $10,$11,$10
        ori     $3,$0,0xffff
        beq     $2,$3,.Lrender_store
        nop

        andi    $3,$2,0x00ff
        sll     $3,$3,3
        addu    $3,$9,$3
        ld      $12,0($3)
        srl     $3,$2,8
        sll     $3,$3,3
        addu    $3,$9,$3
        ld      $13,0($3)
        lq      $15,0x00($4)
        pcpyld  $14,$13,$12
        pceqb   $14,$14,$0
        pand    $15,$15,$14
        por     $10,$10,$14
        pxor    $10,$10,$14
        por     $10,$10,$15

.Lrender_store:
        sq      $10,0x00($4)
.Lrender_advance:
        addiu   $5,$5,0x10
        addiu   $4,$4,0x10
        addiu   $6,$6,2
        addiu   $8,$8,-2
        bgtz    $8,.Lrender_loop
        nop

.Lrender_done:
        jr      $31
        nop
        .end    AuroraPPURenderBGDataPS2
