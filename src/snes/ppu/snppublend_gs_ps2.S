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
    .globl AuroraPPUPlanarTo3PS2
    .ent AuroraPPUPlanarTo3PS2
AuroraPPUPlanarTo3PS2:
    move    $15,$8                 /* lookup base */
    addiu   $14,$0,32

.Lplanar_loop:
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

    addiu   $5,$5,1
    addiu   $6,$6,1
    addiu   $7,$7,1
    addiu   $14,$14,-1
    bgtz    $14,.Lplanar_loop
    addiu   $4,$4,8

    jr      $31
    nop
    .end AuroraPPUPlanarTo3PS2
