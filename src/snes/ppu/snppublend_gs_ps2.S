/* AURORA_PPU_GS_PLANAR_R5900_V1_20260927
 * Expand three 32-byte SNMask planes into 256 packed 3-bit GS attributes.
 * Semantics are exactly lookup0 | (lookup1 << 1) | (lookup2 << 2).
 */

    .text
    .p2align 3
    .set noreorder

/*
 * void AuroraPPUPlanarTo3PS2(
 *     Uint8 *pDest, const Uint8 *pMask0, const Uint8 *pMask1,
 *     const Uint8 *pMask2, const Uint64 *pLookup64);
 *
 * EABI: $4..$8.
 */
/* AURORA_DKC_PLANAR_SPR_UNROLL4_V24_20260928
 * Exact lookup0 | (lookup1 << 1) | (lookup2 << 2) expansion. Four source
 * bytes per iteration: 32 source bytes -> 8 branches. Lookup base is supplied
 * by C++ and points at the identical scratchpad mirror during visible render.
 */
        .globl AuroraPPUPlanarTo3PS2
        .ent AuroraPPUPlanarTo3PS2
AuroraPPUPlanarTo3PS2:
        move    $15,$8
        addiu   $14,$0,8

.Lplanar_u4_loop:
        lbu     $8,0($5)
        lbu     $9,0($6)
        lbu     $10,0($7)
        sll     $8,$8,3
        sll     $9,$9,3
        sll     $10,$10,3
        addu    $8,$15,$8
        addu    $9,$15,$9
        addu    $10,$15,$10
        ld      $8,0($8)
        ld      $9,0($9)
        ld      $10,0($10)
        dsll    $9,$9,1
        dsll    $10,$10,2
        or      $8,$8,$9
        or      $8,$8,$10
        sd      $8,0($4)

        lbu     $8,1($5)
        lbu     $9,1($6)
        lbu     $10,1($7)
        sll     $8,$8,3
        sll     $9,$9,3
        sll     $10,$10,3
        addu    $8,$15,$8
        addu    $9,$15,$9
        addu    $10,$15,$10
        ld      $8,0($8)
        ld      $9,0($9)
        ld      $10,0($10)
        dsll    $9,$9,1
        dsll    $10,$10,2
        or      $8,$8,$9
        or      $8,$8,$10
        sd      $8,8($4)

        lbu     $8,2($5)
        lbu     $9,2($6)
        lbu     $10,2($7)
        sll     $8,$8,3
        sll     $9,$9,3
        sll     $10,$10,3
        addu    $8,$15,$8
        addu    $9,$15,$9
        addu    $10,$15,$10
        ld      $8,0($8)
        ld      $9,0($9)
        ld      $10,0($10)
        dsll    $9,$9,1
        dsll    $10,$10,2
        or      $8,$8,$9
        or      $8,$8,$10
        sd      $8,16($4)

        lbu     $8,3($5)
        lbu     $9,3($6)
        lbu     $10,3($7)
        sll     $8,$8,3
        sll     $9,$9,3
        sll     $10,$10,3
        addu    $8,$15,$8
        addu    $9,$15,$9
        addu    $10,$15,$10
        ld      $8,0($8)
        ld      $9,0($9)
        ld      $10,0($10)
        dsll    $9,$9,1
        dsll    $10,$10,2
        or      $8,$8,$9
        or      $8,$8,$10
        sd      $8,24($4)

        addiu   $5,$5,4
        addiu   $6,$6,4
        addiu   $7,$7,4
        addiu   $14,$14,-1
        bgtz    $14,.Lplanar_u4_loop
        addiu   $4,$4,32

        jr      $31
        nop
        .end AuroraPPUPlanarTo3PS2
/* AURORA_PPU_GS_COPY_R5900_V20_20260928
 * void AuroraPPUCopyBlendPayloadPS2(Uint8 *dst, const Uint8 *src,
 *                                    Uint32 nQwords128);
 *
 * Caller proves 16-byte alignment and exact 256/768-byte footprints in C++.
 * Copy only host-side scanline staging data after DmaSyncGIF(); no GS/emulated
 * state transition occurs in this leaf kernel.
 */
        .p2align 4
        .globl AuroraPPUCopyBlendPayloadPS2
        .ent AuroraPPUCopyBlendPayloadPS2
AuroraPPUCopyBlendPayloadPS2:
        /* AURORA_PPU_GS_COPY_R5900_V20_20260928
         * AURORA_DKC_GS_COPY_UNROLL8_R5900_V28_20260928
         *
         * Same host-only qword copy contract as V20. The hot 256/768-byte
         * scanline payloads (16/48 qwords) and V30's aligned 1 KiB CLUT
         * (64 qwords) stay in the 8-qword loop. V20's 4-qword/scalar tails
         * remain so this symbol keeps its generic positive-count behavior.
         */
        blez    $6,.Lblend_copy_v28_done
        slti    $2,$6,8

        bnez    $2,.Lblend_copy_v28_check4
        nop

.Lblend_copy_v28_8:
        lq      $8,0x00($5)
        lq      $9,0x10($5)
        lq      $10,0x20($5)
        lq      $11,0x30($5)
        lq      $12,0x40($5)
        lq      $13,0x50($5)
        lq      $14,0x60($5)
        lq      $15,0x70($5)
        sq      $8,0x00($4)
        sq      $9,0x10($4)
        sq      $10,0x20($4)
        sq      $11,0x30($4)
        sq      $12,0x40($4)
        sq      $13,0x50($4)
        sq      $14,0x60($4)
        sq      $15,0x70($4)
        addiu   $5,$5,0x80
        addiu   $6,$6,-8
        slti    $2,$6,8
        beq     $2,$0,.Lblend_copy_v28_8
        addiu   $4,$4,0x80

.Lblend_copy_v28_check4:
        slti    $2,$6,4
        bnez    $2,.Lblend_copy_v28_tail
        nop

.Lblend_copy_v28_4:
        lq      $8,0x00($5)
        lq      $9,0x10($5)
        lq      $10,0x20($5)
        lq      $11,0x30($5)
        sq      $8,0x00($4)
        sq      $9,0x10($4)
        sq      $10,0x20($4)
        sq      $11,0x30($4)
        addiu   $5,$5,0x40
        addiu   $6,$6,-4
        slti    $2,$6,4
        beq     $2,$0,.Lblend_copy_v28_4
        addiu   $4,$4,0x40

.Lblend_copy_v28_tail:
        blez    $6,.Lblend_copy_v28_done
        nop
.Lblend_copy_v28_tail_loop:
        lq      $8,0x00($5)
        sq      $8,0x00($4)
        addiu   $5,$5,0x10
        addiu   $6,$6,-1
        bgtz    $6,.Lblend_copy_v28_tail_loop
        addiu   $4,$4,0x10

.Lblend_copy_v28_done:
        jr      $31
        nop
        .end AuroraPPUCopyBlendPayloadPS2
