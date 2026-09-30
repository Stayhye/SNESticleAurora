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
/* AURORA_DKC_CHR24_CTX_HOIST_R5900_V22_20260928
 * AURORA_DKC_CHR24_SCHEDULE_R5900_V25_20260928
 *
 * Exact V22 CHR2 transform with R5900 scheduling only:
 * - fourth context load occupies the entry branch delay slot;
 * - H-flip lookup selection occupies its branch delay slot;
 * - first plane-index shift occupies the transparency branch delay slot;
 * - palette priority uses sign-expanded bit 3 instead of a branch;
 * - pMask increment occupies the loop-back delay slot.
 *
 * No source address, VRAM wrap, flip, palette, opacity or priority rule changes.
 * Leaf function; caller-saved registers only.
 */
        .globl  AuroraPPUFetchCHR2Direct64PS2
        .ent    AuroraPPUFetchCHR2Direct64PS2
AuroraPPUFetchCHR2Direct64PS2:
        lw      $12,0($11)               /* plane normal */
        lw      $13,4($11)               /* plane H-flip */
        lw      $14,8($11)               /* reverse-mask table */
        blez    $7,.Lchr2_v25_done
        lw      $15,12($11)              /* palette LUT; safe delay-slot load */

.Lchr2_v25_loop:
        lhu     $2,0($6)
        lbu     $3,3($6)
        lbu     $11,4($6)
        sll     $2,$2,3
        addu    $2,$2,$5
        andi    $2,$2,0x7fff
        addu    $11,$11,$8
        andi    $24,$3,2
        beq     $24,$0,.Lchr2_v25_no_vflip
        nop
        xori    $11,$11,7
.Lchr2_v25_no_vflip:
        addu    $2,$2,$11
        andi    $2,$2,0x7fff
        sll     $2,$2,1
        addu    $2,$4,$2
        lhu     $25,0($2)
        andi    $2,$25,0x00ff            /* p0 */
        srl     $11,$25,8                /* p1 */
        or      $24,$2,$11               /* source opacity */

        andi    $25,$3,1
        beq     $25,$0,.Lchr2_v25_normal
        move    $25,$13                  /* H-flip lookup in delay slot */
        b       .Lchr2_v25_mask_ready
        nop
.Lchr2_v25_normal:
        move    $25,$12
        addu    $24,$14,$24
        lbu     $24,0($24)
.Lchr2_v25_mask_ready:
        sb      $24,0($10)               /* exact opacity result */
        beq     $24,$0,.Lchr2_v25_transparent
        sll     $2,$2,3                  /* first lookup address work */

        addu    $2,$25,$2
        ld      $3,0($2)
        sll     $11,$11,3
        addu    $11,$25,$11
        ld      $2,0($11)
        dsll    $2,$2,1
        or      $3,$3,$2

        lbu     $11,2($6)                /* palette */
        sll     $2,$11,3
        addu    $2,$15,$2
        ld      $2,0($2)
        or      $3,$3,$2

        /* priorityMask = (palette bit3) ? opacity : 0, branch-free */
        sll     $11,$11,28
        sra     $11,$11,31
        and     $2,$24,$11
        b       .Lchr2_v25_output
        nop

.Lchr2_v25_transparent:
        move    $3,$0
        move    $2,$0

.Lchr2_v25_output:
        sd      $3,0($9)
        sb      $2,48($10)
        addiu   $6,$6,6
        addiu   $9,$9,8
        addiu   $7,$7,-1
        bgtz    $7,.Lchr2_v25_loop
        addiu   $10,$10,1

.Lchr2_v25_done:
        jr      $31
        nop
        .end    AuroraPPUFetchCHR2Direct64PS2
/* Same EABI argument map as the 2bpp kernel; ctx->palLookup is the 4bpp
 * palette table and base uses 16 VRAM words per tile. */
/* AURORA_DKC_CHR4_CTX_HOIST_R5900_V22_20260928
 * AURORA_DKC_CHR4_SCHEDULE_R5900_V25_20260928
 *
 * Exact V22 CHR4 row/wrap/flip transform with instruction scheduling only.
 * Same safe delay-slot/priority-mask strategy as CHR2.  No PPU decisions move
 * into assembly and no emulated timing/state is changed.
 */
        .globl  AuroraPPUFetchCHR4Direct64PS2
        .ent    AuroraPPUFetchCHR4Direct64PS2
AuroraPPUFetchCHR4Direct64PS2:
        lw      $12,0($11)               /* plane normal */
        lw      $13,4($11)               /* plane H-flip */
        lw      $14,8($11)               /* reverse-mask table */
        blez    $7,.Lchr4_v25_done
        lw      $15,12($11)              /* palette LUT; safe delay-slot load */

.Lchr4_v25_loop:
        lhu     $2,0($6)
        lbu     $3,3($6)
        lbu     $11,4($6)
        sll     $2,$2,4
        addu    $2,$2,$5
        andi    $2,$2,0x7fff
        addu    $11,$11,$8
        andi    $24,$3,2
        beq     $24,$0,.Lchr4_v25_no_vflip
        nop
        xori    $11,$11,7
.Lchr4_v25_no_vflip:
        addu    $2,$2,$11

        andi    $24,$2,0x7fff
        sll     $24,$24,1
        addu    $24,$4,$24
        lhu     $24,0($24)               /* pair01 */

        addiu   $25,$2,8
        andi    $25,$25,0x7fff
        sll     $25,$25,1
        addu    $25,$4,$25
        lhu     $25,0($25)               /* pair23 */

        srl     $11,$24,8
        or      $11,$11,$24
        srl     $2,$25,8
        or      $2,$2,$25
        or      $11,$11,$2
        andi    $11,$11,0x00ff           /* source opacity */

        andi    $2,$3,1
        beq     $2,$0,.Lchr4_v25_normal
        move    $2,$13                   /* H-flip lookup in delay slot */
        b       .Lchr4_v25_mask_ready
        nop
.Lchr4_v25_normal:
        move    $2,$12
        addu    $3,$14,$11
        lbu     $11,0($3)
.Lchr4_v25_mask_ready:
        sb      $11,0($10)
        beq     $11,$0,.Lchr4_v25_transparent
        andi    $3,$24,0x00ff            /* first plane extraction */

        sll     $3,$3,3
        addu    $3,$2,$3
        ld      $3,0($3)                 /* result p0 */

        srl     $24,$24,8
        sll     $24,$24,3
        addu    $24,$2,$24
        ld      $24,0($24)
        dsll    $24,$24,1
        or      $3,$3,$24

        andi    $24,$25,0x00ff
        sll     $24,$24,3
        addu    $24,$2,$24
        ld      $24,0($24)
        dsll    $24,$24,2
        or      $3,$3,$24

        srl     $25,$25,8
        sll     $25,$25,3
        addu    $25,$2,$25
        ld      $25,0($25)
        dsll    $25,$25,3
        or      $3,$3,$25

        lbu     $24,2($6)                /* palette */
        sll     $2,$24,3
        addu    $2,$15,$2
        ld      $2,0($2)
        or      $3,$3,$2

        /* priorityMask = (palette bit3) ? opacity : 0, branch-free */
        sll     $24,$24,28
        sra     $24,$24,31
        and     $2,$11,$24
        b       .Lchr4_v25_output
        nop

.Lchr4_v25_transparent:
        move    $3,$0
        move    $2,$0

.Lchr4_v25_output:
        sd      $3,0($9)
        sb      $2,48($10)
        addiu   $6,$6,6
        addiu   $9,$9,8
        addiu   $7,$7,-1
        bgtz    $7,.Lchr4_v25_loop
        addiu   $10,$10,1

.Lchr4_v25_done:
        jr      $31
        nop
        .end    AuroraPPUFetchCHR4Direct64PS2
/* AURORA_DKC_BGDATA_DELAY_SLOT_R5900_V26_20260928
 * Scheduling-only change: pointer increments occupy already-existing branch
 * delay slots.  Data, masks, qfsrv semantics and tile count are unchanged.
 */

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
        addiu   $6,$6,2

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
        addiu   $8,$8,-2
        bgtz    $8,.Lrender_o_loop
        addiu   $4,$4,0x10

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
        addiu   $6,$6,2

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
        addiu   $8,$8,-2
        bgtz    $8,.Lrender_loop
        addiu   $4,$4,0x10

.Lrender_done:
        jr      $31
        nop
        .end    AuroraPPURenderBGDataPS2
/* AURORA_PPU_R5900_CHR8_V16_20260928
 * Exact 8bpp sibling of the existing direct 2bpp/4bpp CHR kernels.
 *
 * void AuroraPPUFetchCHR8Direct64PS2(
 *   const Uint16 *vram, Uint32 base, const SnesRenderTileT *tiles,
 *   Int32 nTiles, Uint32 scrollY, Uint8 *dest, Uint8 *mask,
 *   const AuroraPPUChrDirectAsmCtx *ctx);
 *
 * EABI: $4..$11. No incoming stack arguments.
 * SnesRenderTileT: +0 tile(u16), +2 pal, +3 flip, +4 offsetY, size 6.
 * SnesPPUTile8T row pairs are at byte offsets 0x00/0x10/0x20/0x30.
 */
        .p2align 4
        .globl  AuroraPPUFetchCHR8Direct64PS2
        .ent    AuroraPPUFetchCHR8Direct64PS2
AuroraPPUFetchCHR8Direct64PS2:
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

        blez    $19,.Lchr8_done
        nop

.Lchr8_loop:
        lhu     $2,0($18)               /* tile */
        lbu     $3,3($18)               /* flip: bit0 H, bit1 V */
        lbu     $4,4($18)               /* offsetY */
        sll     $2,$2,5                 /* tile * 32 words */
        addu    $2,$2,$17
        andi    $2,$2,0x7fff
        addu    $4,$4,$20
        andi    $5,$3,2
        beq     $5,$0,.Lchr8_no_vflip
        nop
        xori    $4,$4,7
.Lchr8_no_vflip:
        addu    $2,$2,$4                /* row word address */
        andi    $2,$2,0x7fff
        sll     $2,$2,1                 /* words -> bytes */
        addu    $2,$16,$2

        /* Preserve the reference SnesPPUTile8T layout exactly. */
        lhu     $8,0x00($2)             /* p0 | p1<<8 */
        lhu     $9,0x10($2)             /* p2 | p3<<8 */
        lhu     $10,0x20($2)            /* p4 | p5<<8 */
        lhu     $11,0x30($2)            /* p6 | p7<<8 */

        /* One source-opacity byte. */
        or      $12,$8,$9
        or      $12,$12,$10
        or      $12,$12,$11
        srl     $13,$12,8
        or      $12,$12,$13
        andi    $12,$12,0x00ff

        andi    $13,$3,1
        beq     $13,$0,.Lchr8_normal
        nop
        lw      $24,4($23)              /* H-flipped plane lookup */
        move    $14,$12                 /* identity opacity mask */
        beq     $0,$0,.Lchr8_lookup_ready
        nop
.Lchr8_normal:
        lw      $24,0($23)              /* canonical plane lookup */
        lw      $25,8($23)              /* bit-reverse mask table */
        addu    $25,$25,$12
        lbu     $14,0($25)
.Lchr8_lookup_ready:
        beq     $14,$0,.Lchr8_transparent
        nop

        /* Decode planes 0..7 into one 64-bit 8-pixel indexed row. */
        andi    $12,$8,0x00ff
        sll     $25,$12,3
        addu    $25,$24,$25
        ld      $15,0($25)

        srl     $12,$8,8
        sll     $25,$12,3
        addu    $25,$24,$25
        ld      $13,0($25)
        dsll    $13,$13,1
        or      $15,$15,$13

        andi    $12,$9,0x00ff
        sll     $25,$12,3
        addu    $25,$24,$25
        ld      $13,0($25)
        dsll    $13,$13,2
        or      $15,$15,$13

        srl     $12,$9,8
        sll     $25,$12,3
        addu    $25,$24,$25
        ld      $13,0($25)
        dsll    $13,$13,3
        or      $15,$15,$13

        andi    $12,$10,0x00ff
        sll     $25,$12,3
        addu    $25,$24,$25
        ld      $13,0($25)
        dsll    $13,$13,4
        or      $15,$15,$13

        srl     $12,$10,8
        sll     $25,$12,3
        addu    $25,$24,$25
        ld      $13,0($25)
        dsll    $13,$13,5
        or      $15,$15,$13

        andi    $12,$11,0x00ff
        sll     $25,$12,3
        addu    $25,$24,$25
        ld      $13,0($25)
        dsll    $13,$13,6
        or      $15,$15,$13

        srl     $12,$11,8
        sll     $25,$12,3
        addu    $25,$24,$25
        ld      $13,0($25)
        dsll    $13,$13,7
        or      $15,$15,$13

        lbu     $12,2($18)              /* palette field carries priority bit */
        andi    $12,$12,8
        move    $13,$0
        beq     $12,$0,.Lchr8_output
        nop
        move    $13,$14
        beq     $0,$0,.Lchr8_output
        nop

.Lchr8_transparent:
        move    $15,$0
        move    $13,$0

.Lchr8_output:
        sd      $15,0($21)
        sb      $14,0($22)
        sb      $13,48($22)
        addiu   $18,$18,6
        addiu   $21,$21,8
        addiu   $22,$22,1
        addiu   $19,$19,-1
        bgtz    $19,.Lchr8_loop
        nop

.Lchr8_done:
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
        .end    AuroraPPUFetchCHR8Direct64PS2
/* AURORA_PPU_BG_MAP_R5900_V17_20260928
 * Ordinary BG tilemap fetch kernels. These are deliberately leaf-only:
 * no emulated clock/state is advanced and no PPU ownership decision occurs.
 *
 * EABI:
 *   BG8 : $4 uAddr, $5 pTile, $6 nTiles, $7 ppScreen
 *   BG16: $4 uAddr, $5 pTile, $6 nTiles, $7 ppScreen, $8 uFlipXOR
 *
 * SnesRenderTileT is compile-time checked by snppubg.cpp:
 *   +0 uTile(u16), +2 uPal, +3 uFlip, +4 uOffsetY, size 6.
 */
        .p2align 4
        .globl  AuroraPPUFetchBG8x8MapPS2
        .ent    AuroraPPUFetchBG8x8MapPS2
AuroraPPUFetchBG8x8MapPS2:
        /* AURORA_DKC_BGMAP_DELAY_SCHEDULE_R5900_V29_20260928
         * V17 semantics exactly; only safe delay slots are occupied.
         */
        blez    $6,.Lbg8_v29_done
        srl     $8,$4,10

        andi    $8,$8,3
        sll     $8,$8,2
        addu    $8,$7,$8
        lw      $8,0($8)

.Lbg8_v29_loop:
        andi    $9,$4,0x03ff
        sll     $9,$9,1
        addu    $9,$8,$9
        lhu     $10,0($9)
        addiu   $4,$4,1

        andi    $11,$4,0x001f
        bnez    $11,.Lbg8_v29_no_wrap
        andi    $9,$10,0x03ff
        addiu   $4,$4,-32
        xori    $4,$4,0x0400
        srl     $8,$4,10
        andi    $8,$8,3
        sll     $8,$8,2
        addu    $8,$7,$8
        lw      $8,0($8)

.Lbg8_v29_no_wrap:
        sh      $9,0($5)
        srl     $9,$10,10
        andi    $9,$9,0x000f
        sb      $9,2($5)
        srl     $9,$10,14
        andi    $9,$9,3
        sb      $9,3($5)
        sb      $0,4($5)

        addiu   $6,$6,-1
        bgtz    $6,.Lbg8_v29_loop
        addiu   $5,$5,6

.Lbg8_v29_done:
        jr      $31
        nop
        .end    AuroraPPUFetchBG8x8MapPS2
        .globl  AuroraPPUFetchBG16x16MapPS2
        .ent    AuroraPPUFetchBG16x16MapPS2
AuroraPPUFetchBG16x16MapPS2:
        /* AURORA_DKC_BG16MAP_DELAY_SCHEDULE_R5900_V29_20260928
         * Same V17 map wrap, 16x16 quadrant, flip/palette metadata and
         * nTiles accounting. Only branch delay slots carry invariant work.
         */
        blez    $6,.Lbg16_v29_done
        srl     $9,$4,10

        andi    $9,$9,3
        sll     $9,$9,2
        addu    $9,$7,$9
        lw      $9,0($9)

.Lbg16_v29_loop:
        andi    $10,$4,0x03ff
        sll     $10,$10,1
        addu    $10,$9,$10
        lhu     $10,0($10)
        addiu   $4,$4,1

        andi    $11,$4,0x001f
        bnez    $11,.Lbg16_v29_no_wrap
        andi    $11,$10,0x03ff
        addiu   $4,$4,-32
        xori    $4,$4,0x0400
        srl     $9,$4,10
        andi    $9,$9,3
        sll     $9,$9,2
        addu    $9,$7,$9
        lw      $9,0($9)

.Lbg16_v29_no_wrap:
        srl     $12,$10,14
        andi    $12,$12,3
        srl     $13,$10,10
        andi    $13,$13,0x000f

        xor     $14,$12,$8
        andi    $15,$14,2
        sll     $15,$15,3
        andi    $24,$14,1
        addu    $25,$15,$24
        addu    $25,$11,$25
        andi    $25,$25,0x03ff
        sh      $25,0($5)
        sb      $13,2($5)
        sb      $12,3($5)
        sb      $0,4($5)
        addiu   $5,$5,6
        addiu   $6,$6,-1

        andi    $25,$8,1
        bnez    $25,.Lbg16_v29_after_second
        xori    $24,$24,1
        addu    $25,$15,$24
        addu    $25,$11,$25
        andi    $25,$25,0x03ff
        sh      $25,0($5)
        sb      $13,2($5)
        sb      $12,3($5)
        sb      $0,4($5)
        addiu   $5,$5,6
        addiu   $6,$6,-1

.Lbg16_v29_after_second:
        addiu   $25,$0,-2
        bgtz    $6,.Lbg16_v29_loop
        and     $8,$8,$25

.Lbg16_v29_done:
        jr      $31
        nop
        .end    AuroraPPUFetchBG16x16MapPS2
