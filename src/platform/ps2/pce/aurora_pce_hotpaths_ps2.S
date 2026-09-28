/* AURORA_PCE_MIPS_HOTPATHS_V4_20260928
 * Original Aurora PS2/R5900 leaf kernels for Beetle PCE Fast.
 *
 * Deliberately excluded: HuC6280 scheduler/dispatch, IRQ/timer logic, PSG
 * timing, VDC timing, callback I/O and emulated-state orchestration.
 */

    .set noreorder
    .set noat
    .text
    .align 4

/* int32 AuroraPceBlipReadStereoPS2(
 *   const int32 *buffer, int16 *out, int32 count,
 *   int32 bass_shift, int32 reader_accum)
 * EE EABI: a0..a4 == $4..$8.
 */
    .globl AuroraPceBlipReadStereoPS2
    .ent AuroraPceBlipReadStereoPS2
AuroraPceBlipReadStereoPS2:
    blez    $6, .Lblip_done
    nop
.Lblip_loop:
    sra     $9, $8, 14             /* s = accum >> (30 - 16) */
    sll     $10, $9, 16
    sra     $10, $10, 16           /* sign-extended int16(s) */
    beq     $10, $9, .Lblip_store
    nop
    sra     $10, $9, 24
    ori     $11, $0, 0x7fff
    subu    $9, $11, $10           /* exact Blip_Buffer clipping rule */
.Lblip_store:
    sh      $9, 0($5)
    addiu   $5, $5, 4              /* stereo output: leave other channel */
    lw      $9, 0($4)
    addiu   $4, $4, 4
    srav    $10, $8, $7
    subu    $9, $9, $10
    addu    $8, $8, $9             /* 32-bit wrapping add, same hot data domain */
    addiu   $6, $6, -1
    bgtz    $6, .Lblip_loop
    nop
.Lblip_done:
    move    $2, $8
    jr      $31
    nop
    .end AuroraPceBlipReadStereoPS2

/* void AuroraPceDrawBgTilesPS2(
 *   bat_base, cg_base, target, tile_count,
 *   bat_boom, bat_width_mask, &cg_mask, color_lut)
 * EE EABI: a0..a7 == $4..$11.
 */
    .globl AuroraPceDrawBgTilesPS2
    .ent AuroraPceDrawBgTilesPS2
AuroraPceDrawBgTilesPS2:
    blez    $7, .Lbg_done
    nop
    ld      $12, 0($10)            /* cg_mask */
.Lbg_loop:
    sll     $13, $8, 1
    addu    $13, $4, $13
    lhu     $14, 0($13)            /* BAT entry */

    andi    $15, $14, 0x0fff
    sll     $15, $15, 6            /* tile * 8 uint64 == tile * 64 bytes */
    addu    $15, $5, $15
    ld      $15, 0($15)
    and     $15, $15, $12

    srl     $13, $14, 12
    sll     $13, $13, 3
    addu    $13, $11, $13
    ld      $13, 0($13)
    or      $15, $15, $13
    sd      $15, 0($6)

    addiu   $6, $6, 8
    addiu   $8, $8, 1
    and     $8, $8, $9
    addiu   $7, $7, -1
    bgtz    $7, .Lbg_loop
    nop
.Lbg_done:
    jr      $31
    nop
    .end AuroraPceDrawBgTilesPS2

/* Byte-at-a-time on purpose: HuC6280 block moves have sequential overlap
 * semantics.  Preloading multiple bytes before stores would be incorrect. */
    .globl AuroraPceBmtCopyIncPS2
    .ent AuroraPceBmtCopyIncPS2
AuroraPceBmtCopyIncPS2:
    beqz    $6, .Lbmt_inc_done
    nop
.Lbmt_inc_loop:
    lbu     $8, 0($5)
    sb      $8, 0($4)
    addiu   $5, $5, 1
    addiu   $4, $4, 1
    addiu   $6, $6, -1
    bnez    $6, .Lbmt_inc_loop
    nop
.Lbmt_inc_done:
    jr      $31
    nop
    .end AuroraPceBmtCopyIncPS2

    .globl AuroraPceBmtCopyDecPS2
    .ent AuroraPceBmtCopyDecPS2
AuroraPceBmtCopyDecPS2:
    beqz    $6, .Lbmt_dec_done
    nop
.Lbmt_dec_loop:
    lbu     $8, 0($5)
    sb      $8, 0($4)
    addiu   $5, $5, -1
    addiu   $4, $4, -1
    addiu   $6, $6, -1
    bnez    $6, .Lbmt_dec_loop
    nop
.Lbmt_dec_done:
    jr      $31
    nop
    .end AuroraPceBmtCopyDecPS2

    .set at
    .set reorder
