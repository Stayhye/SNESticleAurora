/* AURORA_SPC_SAMPLE_RUN_R5900_V1_20260927
 * Exact PS2 inner loops for normal S-DSP sample emission and noise.
 * All integer/sample semantics remain owned by the surrounding C++ code.
 */

    .text
    .p2align 3
    .set noreorder

/*
 * Int32 AuroraSpcOutputSampleRunPS2(
 *     const Int16 *pBlockBase, Int16 *pOut, Uint16 *pFrac,
 *     Int32 *pPhase, Int32 iPhaseInc, Int32 nSamples);
 *
 * EABI: $4..$9. Stop immediately before the next BRR block boundary and
 * return the number of emitted samples in $2. The C++ caller performs the
 * exact existing FetchBlock() state transition between runs.
 */
    .globl AuroraSpcOutputSampleRunPS2
    .ent AuroraSpcOutputSampleRunPS2
AuroraSpcOutputSampleRunPS2:
    blez    $9,.Lsample_none
    nop

    lw      $10,0($7)              /* phase */
    lui     $24,0x000e             /* 14 << 16 */
    slt     $11,$10,$24
    beq     $11,$0,.Lsample_none
    move    $2,$0                  /* processed = 0 */

.Lsample_loop:
    sra     $11,$10,16
    addiu   $11,$11,16
    sll     $11,$11,1
    addu    $11,$4,$11
    lh      $12,0($11)
    lh      $13,2($11)
    sh      $10,0($6)              /* low 16 bits = interpolation fraction */
    sh      $12,0($5)
    sh      $13,2($5)
    addiu   $6,$6,2
    addiu   $5,$5,4
    addiu   $9,$9,-1
    addu    $10,$10,$8
    addiu   $2,$2,1

    blez    $9,.Lsample_done
    slt     $11,$10,$24
    bne     $11,$0,.Lsample_loop
    nop

.Lsample_done:
    sw      $10,0($7)
    jr      $31
    nop

.Lsample_none:
    jr      $31
    move    $2,$0
    .end AuroraSpcOutputSampleRunPS2

/*
 * void AuroraSpcOutputNoiseRunPS2(
 *     Int16 *pOut, Uint16 *pFrac, Int32 nSamples,
 *     Uint32 *pCounter, Uint32 *pNoise, Uint32 period, Uint32 offset);
 *
 * EABI: $4..$10. This is the complete per-sample loop from OutputNoise().
 */
    .p2align 3
    .globl AuroraSpcOutputNoiseRunPS2
    .ent AuroraSpcOutputNoiseRunPS2
AuroraSpcOutputNoiseRunPS2:
    blez    $6,.Lnoise_return
    nop

    lw      $11,0($7)              /* counter */
    lw      $12,0($8)              /* 15-bit LFSR */
    bne     $9,$0,.Lnoise_rate
    nop

.Lnoise_zero_loop:
    bne     $11,$0,.Lnoise_zero_have_counter
    nop
    addiu   $11,$0,30720
.Lnoise_zero_have_counter:
    addiu   $11,$11,-1
    sll     $13,$12,1
    sh      $13,0($4)
    sh      $13,2($4)
    sh      $0,0($5)
    addiu   $4,$4,4
    addiu   $5,$5,2
    addiu   $6,$6,-1
    bgtz    $6,.Lnoise_zero_loop
    nop
    b       .Lnoise_store
    nop

.Lnoise_rate:
    bne     $11,$0,.Lnoise_first_nonzero
    addiu   $13,$0,30719
    b       .Lnoise_first_ready
    nop
.Lnoise_first_nonzero:
    addiu   $13,$11,-1
.Lnoise_first_ready:
    addu    $13,$13,$10
    divu    $13,$9
    mfhi    $14
    addiu   $14,$14,1              /* eventCountdown */

.Lnoise_rate_loop:
    bne     $11,$0,.Lnoise_have_counter
    nop
    addiu   $11,$0,30720
.Lnoise_have_counter:
    addiu   $11,$11,-1
    addiu   $14,$14,-1
    bne     $14,$0,.Lnoise_no_event
    nop

    sll     $13,$12,13
    sll     $15,$12,14
    xor     $13,$13,$15
    andi    $13,$13,0x4000
    srl     $12,$12,1
    or      $12,$12,$13
    move    $14,$9

.Lnoise_no_event:
    sll     $13,$12,1
    sh      $13,0($4)
    sh      $13,2($4)
    sh      $0,0($5)
    addiu   $4,$4,4
    addiu   $5,$5,2
    addiu   $6,$6,-1
    bgtz    $6,.Lnoise_rate_loop
    nop

.Lnoise_store:
    sw      $11,0($7)
    sw      $12,0($8)
.Lnoise_return:
    jr      $31
    nop
    .end AuroraSpcOutputNoiseRunPS2
