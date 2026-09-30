/* AURORA_SNES_AUDIO_MIPS_FINAL_20260927
 * PS2 EE/R5900 audio kernels.
 *
 * IMPORTANT ABI NOTE:
 * The modern PS2 EE toolchain uses the MIPS EABI register convention.
 * Integer/pointer arguments 1..8 arrive in $4..$11 (a0..a7).  Do not use
 * the old o32 assumption that argument 5 starts at 16(sp).
 *
 * _MixChannelEchoPS2 deliberately accepts exactly eight arguments.  The final
 * three scalar values are grouped in a small caller-owned tail structure, so
 * this translation unit never depends on stack-argument slot layout.
 */

        .text
        .p2align 4
        .set    noreorder
        .set    noat

/*
 * void _MixChannel(Int32 *outL, Int32 *outR, Int16 *in, Uint8 *env,
 *                  Uint16 *frac, Int32 n, Int32 volL, Int32 volR)
 *
 * EABI:
 *   $4 outL, $5 outR, $6 in, $7 env,
 *   $8 frac, $9 n, $10 volL, $11 volR
 */
        .globl  _MixChannel
        .ent    _MixChannel
_MixChannel:
        move    $24,$8
        move    $25,$9
        move    $2,$10
        move    $3,$11

        pcpyh   $2,$2
        pcpyld  $2,$2,$2
        pcpyh   $3,$3
        pcpyld  $3,$3,$3

.Lmix_channel_loop:
        lq      $10,0x00($24)
        lq      $8,0x00($6)
        pnor    $11,$10,$0
        lq      $9,0x10($6)
        pextlh  $12,$10,$11
        pextuh  $13,$10,$11
        psrlh   $12,$12,1
        psrlh   $13,$13,1
        ld      $10,0x00($7)
        phmadh  $8,$8,$12
        pextlb  $10,$0,$10
        phmadh  $9,$9,$13
        pextuh  $11,$0,$10
        pextlh  $10,$0,$10
        psraw   $8,$8,15
        pmulth  $8,$8,$10
        psraw   $9,$9,15
        pmulth  $9,$9,$11
        addiu   $6,$6,0x20
        addiu   $24,$24,0x10
        addiu   $7,$7,0x08
        psraw   $8,$8,7
        pmulth  $10,$8,$3
        psraw   $9,$9,7
        pmulth  $11,$9,$3
        pmulth  $8,$8,$2
        lq      $12,0x00($4)
        pmulth  $9,$9,$2
        lq      $13,0x10($4)
        lq      $14,0x00($5)
        lq      $15,0x10($5)
        paddsw  $12,$12,$8
        paddsw  $13,$13,$9
        paddsw  $14,$14,$10
        paddsw  $15,$15,$11
        sq      $12,0x00($4)
        sq      $13,0x10($4)
        sq      $14,0x00($5)
        sq      $15,0x10($5)
        addiu   $25,$25,-8
        addiu   $4,$4,0x20
        bgtz    $25,.Lmix_channel_loop
        addiu   $5,$5,0x20
        jr      $31
        nop
        .end    _MixChannel

/*
 * struct AuroraMixChannelEchoTail {
 *     Int32 nSamples;
 *     Int32 iVolLeft;
 *     Int32 iVolRight;
 * };
 *
 * void _MixChannelEchoPS2(Int32 *outL, Int32 *outR,
 *                         Int16 *echoL, Int16 *echoR,
 *                         Int16 *in, Uint8 *env, Uint16 *frac,
 *                         const AuroraMixChannelEchoTail *tail)
 *
 * EABI:
 *   $4 outL, $5 outR, $6 echoL, $7 echoR,
 *   $8 in, $9 env, $10 frac, $11 tail
 */
        .globl  _MixChannelEchoPS2
        .ent    _MixChannelEchoPS2
_MixChannelEchoPS2:
        addiu   $29,$29,-0x20
        sq      $16,0x00($29)
        sq      $17,0x10($29)
        move    $24,$8
        move    $25,$9
        move    $2,$10
        lw      $3,0x00($11)
        lw      $16,0x04($11)
        lw      $17,0x08($11)
        pcpyh   $16,$16
        pcpyld  $16,$16,$16
        pcpyh   $17,$17
        pcpyld  $17,$17,$17

.Lmix_channel_echo_loop:
        lq      $10,0x00($2)
        lq      $8,0x00($24)
        pnor    $11,$10,$0
        lq      $9,0x10($24)
        pextlh  $12,$10,$11
        pextuh  $13,$10,$11
        psrlh   $12,$12,1
        psrlh   $13,$13,1
        ld      $10,0x00($25)
        phmadh  $8,$8,$12
        pextlb  $10,$0,$10
        phmadh  $9,$9,$13
        pextuh  $11,$0,$10
        pextlh  $10,$0,$10
        psraw   $8,$8,15
        pmulth  $8,$8,$10
        psraw   $9,$9,15
        pmulth  $9,$9,$11
        addiu   $24,$24,0x20
        addiu   $2,$2,0x10
        addiu   $25,$25,0x08
        psraw   $8,$8,7
        pmulth  $10,$8,$17
        psraw   $9,$9,7
        pmulth  $11,$9,$17
        pmulth  $8,$8,$16
        lq      $12,0x00($4)
        pmulth  $9,$9,$16
        lq      $13,0x10($4)
        lq      $14,0x00($5)
        lq      $15,0x10($5)
        paddsw  $12,$12,$8
        paddsw  $13,$13,$9
        paddsw  $14,$14,$10
        paddsw  $15,$15,$11
        sq      $12,0x00($4)
        sq      $13,0x10($4)
        sq      $14,0x00($5)
        sq      $15,0x10($5)
        psraw   $10,$10,7
        psraw   $11,$11,7
        psraw   $8,$8,7
        psraw   $9,$9,7
        lq      $12,0x00($6)
        lq      $13,0x00($7)
        ppach   $8,$9,$8
        ppach   $9,$11,$10
        paddsh  $12,$12,$8
        paddsh  $13,$13,$9
        sq      $12,0x00($6)
        sq      $13,0x00($7)
        addiu   $6,$6,0x10
        addiu   $7,$7,0x10
        addiu   $3,$3,-8
        addiu   $4,$4,0x20
        bgtz    $3,.Lmix_channel_echo_loop
        addiu   $5,$5,0x20

        lq      $16,0x00($29)
        lq      $17,0x10($29)
        addiu   $29,$29,0x20
        jr      $31
        nop
        .end    _MixChannelEchoPS2

/*
 * void _MixEcho(Int16 *out, Int32 *main, Int16 *echo, Int32 n,
 *               Int32 mainVol, Int32 echoVol)
 *
 * EABI: $4..$9
 */
        .globl  _MixEcho
        .ent    _MixEcho
_MixEcho:
        move    $2,$8
        move    $3,$9
        pcpyh   $2,$2
        pcpyld  $2,$2,$2
        pcpyh   $3,$3
        pcpyld  $3,$3,$3
        pnor    $14,$0,$0
        psrlw   $14,$14,17
        pnor    $15,$0,$0
        psllw   $15,$15,15

.Lmix_echo_loop:
        lq      $8,0x00($5)
        lq      $9,0x10($5)
        lq      $10,0x00($6)
        psraw   $8,$8,7
        psraw   $9,$9,7
        pminw   $8,$8,$14
        pminw   $9,$9,$14
        pmaxw   $8,$8,$15
        pmaxw   $9,$9,$15
        ppach   $8,$9,$8
        pmulth  $0,$8,$2
        pmaddh  $0,$10,$3
        pmflo   $8
        pmfhi   $9
        psraw   $8,$8,6
        psraw   $9,$9,6
        pminw   $8,$8,$14
        pminw   $9,$9,$14
        pmaxw   $8,$8,$15
        pmaxw   $9,$9,$15
        pcpyld  $10,$9,$8
        pcpyud  $11,$8,$9
        ppach   $10,$11,$10
        sq      $10,0x00($4)
        addiu   $4,$4,0x10
        addiu   $7,$7,-8
        addiu   $5,$5,0x20
        bgtz    $7,.Lmix_echo_loop
        addiu   $6,$6,0x10
        jr      $31
        nop
        .end    _MixEcho

/*
 * void _SNSpcDspMemset64(Uint64 *dest, Int32 nDwords)
 *
 * dest is 16-byte aligned in SNSpcDspDataT.  Preserve the old helper's exact
 * nDwords*8 byte footprint, using 128-bit stores whenever possible.
 */
        .globl  _SNSpcDspMemset64
        .ent    _SNSpcDspMemset64
_SNSpcDspMemset64:
        blez    $5,.Lclear_done
        nop
.Lclear_4:
        slti    $2,$5,4
        bnez    $2,.Lclear_tail
        nop
        sq      $0,0x00($4)
        sq      $0,0x10($4)
        addiu   $4,$4,0x20
        addiu   $5,$5,-4
        b       .Lclear_4
        nop
.Lclear_tail:
        slti    $2,$5,2
        bnez    $2,.Lclear_one
        nop
        sq      $0,0x00($4)
        addiu   $4,$4,0x10
        addiu   $5,$5,-2
.Lclear_one:
        blez    $5,.Lclear_done
        nop
        sd      $0,0x00($4)
.Lclear_done:
        jr      $31
        nop
        .end    _SNSpcDspMemset64

        .set    at
        .set    reorder
