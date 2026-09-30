/* AURORA_SNES_AUDIO_MIPS_FINAL_20260927
 * PS2 EE/R5900 recursive BRR filter.
 *
 * Nibble expansion/range handling remains in C.  This kernel receives the
 * same 16 signed intermediate samples and performs the exact active Filter3
 * recurrence, clamp, Int16 wrap-after-double, and history update.
 *
 * EABI:
 *   $4 pOut, $5 pIn, $6 filter, $7 prev0, $8 prev1
 */

        .text
        .p2align 4
        .set    noreorder
        .set    noat

        .macro  BRR_STORE_AND_NEXT loop
        slt     $12,$14,$10
        beqz    $12,1f
        nop
        move    $10,$14
1:
        slt     $12,$10,$15
        beqz    $12,2f
        nop
        move    $10,$15
2:
        sll     $12,$10,1
        sh      $12,0x00($4)
        move    $8,$7
        sll     $12,$12,16
        sra     $7,$12,16
        addiu   $4,$4,2
        addiu   $5,$5,2
        addiu   $9,$9,-1
        bgtz    $9,\loop
        nop
        jr      $31
        nop
        .endm

        .globl  _SNSpcBRRFilterPS2
        .ent    _SNSpcBRRFilterPS2
_SNSpcBRRFilterPS2:
        andi    $6,$6,3
        addiu   $9,$0,16
        addiu   $14,$0,32767
        addiu   $15,$0,-32768
        beq     $6,$0,.Lbrr_f0
        addiu   $2,$0,1
        beq     $6,$2,.Lbrr_f1
        addiu   $2,$0,2
        beq     $6,$2,.Lbrr_f2
        nop
        b       .Lbrr_f3
        nop

.Lbrr_f0:
        lh      $10,0x00($5)
        sra     $10,$10,1
        BRR_STORE_AND_NEXT .Lbrr_f0

.Lbrr_f1:
        lh      $10,0x00($5)
        sra     $10,$10,1
        sra     $12,$7,1
        addu    $10,$10,$12
        subu    $12,$0,$7
        sra     $12,$12,5
        addu    $10,$10,$12
        BRR_STORE_AND_NEXT .Lbrr_f1

.Lbrr_f2:
        lh      $10,0x00($5)
        sra     $10,$10,1
        sra     $11,$8,1
        addu    $10,$10,$7
        subu    $10,$10,$11
        sra     $12,$11,4
        addu    $10,$10,$12
        sll     $12,$7,1
        addu    $12,$12,$7
        subu    $12,$0,$12
        sra     $12,$12,6
        addu    $10,$10,$12
        BRR_STORE_AND_NEXT .Lbrr_f2

.Lbrr_f3:
        lh      $10,0x00($5)
        sra     $10,$10,1
        sra     $11,$8,1
        addu    $10,$10,$7
        subu    $10,$10,$11
        sll     $12,$7,3
        sll     $13,$7,2
        addu    $12,$12,$13
        addu    $12,$12,$7
        subu    $12,$0,$12
        sra     $12,$12,7
        addu    $10,$10,$12
        sll     $12,$11,1
        addu    $12,$12,$11
        sra     $12,$12,4
        addu    $10,$10,$12
        BRR_STORE_AND_NEXT .Lbrr_f3

        .end    _SNSpcBRRFilterPS2
        .set    at
        .set    reorder
