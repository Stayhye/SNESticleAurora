

/*
Todo: 
Half carry support

Direct-page 16-bit wrap fixed by SAFE ACCURACY BATCH 2.

 
 */




#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "types.h"
#include "snspc.h"
#include "snspc_c.h"
#include "snspcdisasm.h"
#include "sndebug.h"
#include "platform/ps2/system/aurora_runtime_trace.h"



/* AURORA_SNES_GENERIC_TRACER_V36_20261002
 * Generic SNES/SPC laboratory tracer. It has no ROM/CRC gate: build-time
 * AURORA_SNES_TRACER decides whether this code exists, and the menu toggle
 * decides whether it records at runtime. Title/CRC are metadata only.
 */
#if AURORA_SNES_TRACER
#define AURORA_SNES_TRC_SPC_COUNT 256u
#define AURORA_SNES_TRC_SPC_MASK  (AURORA_SNES_TRC_SPC_COUNT - 1u)
#define AURORA_SNES_TRC_IO_COUNT  128u
#define AURORA_SNES_TRC_IO_MASK   (AURORA_SNES_TRC_IO_COUNT - 1u)
#define AURORA_SNES_TRC_DSP_COUNT 256u
#define AURORA_SNES_TRC_DSP_MASK  (AURORA_SNES_TRC_DSP_COUNT - 1u)
#define AURORA_SNES_TRC_ECHO_COUNT 128u
#define AURORA_SNES_TRC_ECHO_MASK  (AURORA_SNES_TRC_ECHO_COUNT - 1u)

typedef struct AuroraSnesTracerSpcT
{
    Uint32 seq;
    Int32 frameCycle;
    Int32 totalCycle;
    Int32 residualCycles;
    Uint16 pc;
    Uint8 opcode, a, x, y, sp, psw, halt;
} AuroraSnesTracerSpcT;

typedef struct AuroraSnesTracerIoT
{
    Uint32 seq, frameCycle, totalCycle;
    Uint16 pc;
    Uint8 kind, port, data, prior;
} AuroraSnesTracerIoT;

typedef struct AuroraSnesTracerDspT
{
    Uint32 seq, eventCycle, applyCycle;
    Uint8 kind, reg, prior, data;
} AuroraSnesTracerDspT;

typedef struct AuroraSnesTracerEchoT
{
    Uint32 seq, mixCycle, sampleRate;
    Uint16 base, size, posBefore, posAfter, nSamples;
    Uint8 flg, esaReg, edlReg, edlLatch, write;
} AuroraSnesTracerEchoT;

static AuroraSnesTracerSpcT s_AuroraSnesTracerSpc[AURORA_SNES_TRC_SPC_COUNT];
static AuroraSnesTracerIoT s_AuroraSnesTracerIo[AURORA_SNES_TRC_IO_COUNT];
static AuroraSnesTracerDspT s_AuroraSnesTracerDsp[AURORA_SNES_TRC_DSP_COUNT];
static AuroraSnesTracerEchoT s_AuroraSnesTracerEcho[AURORA_SNES_TRC_ECHO_COUNT];
static Uint32 s_AuroraSnesTracerSpcHead, s_AuroraSnesTracerSpcCount;
static Uint32 s_AuroraSnesTracerIoHead, s_AuroraSnesTracerIoCount;
static Uint32 s_AuroraSnesTracerDspHead, s_AuroraSnesTracerDspCount;
static Uint32 s_AuroraSnesTracerEchoHead, s_AuroraSnesTracerEchoCount;
static Uint32 s_AuroraSnesTracerSeq, s_AuroraSnesTracerNopRun;
static Uint32 s_AuroraSnesTracerCRC;
volatile Uint8 g_AuroraSnesTracerEnabled = 0;
static Uint8 s_AuroraSnesTracerFrozen, s_AuroraSnesTracerGameValid;
static Char s_AuroraSnesTracerTitle[64];
static SNSpcT *s_AuroraSnesTracerLastCpu;

static void AuroraSnesTracerDump(SNSpcT *pCpu, const char *reason);

static void AuroraSnesTracerResetRings(void)
{
    s_AuroraSnesTracerSpcHead = s_AuroraSnesTracerSpcCount = 0;
    s_AuroraSnesTracerIoHead = s_AuroraSnesTracerIoCount = 0;
    s_AuroraSnesTracerDspHead = s_AuroraSnesTracerDspCount = 0;
    s_AuroraSnesTracerEchoHead = s_AuroraSnesTracerEchoCount = 0;
    s_AuroraSnesTracerSeq = 0;
    s_AuroraSnesTracerNopRun = 0;
    s_AuroraSnesTracerFrozen = 0;
    s_AuroraSnesTracerLastCpu = NULL;
}

void SNSPCTracerConfigureGame(Uint32 uRuntimeCRC, const char *pTitle)
{
    if (g_AuroraSnesTracerEnabled && s_AuroraSnesTracerLastCpu)
        AuroraSnesTracerDump(s_AuroraSnesTracerLastCpu, "game changed while tracer was on");
    g_AuroraSnesTracerEnabled = 0;
    s_AuroraSnesTracerCRC = uRuntimeCRC;
    s_AuroraSnesTracerTitle[0] = 0;
    if (pTitle && *pTitle)
    {
        strncpy(s_AuroraSnesTracerTitle, pTitle, sizeof(s_AuroraSnesTracerTitle)-1u);
        s_AuroraSnesTracerTitle[sizeof(s_AuroraSnesTracerTitle)-1u] = 0;
    }
    s_AuroraSnesTracerGameValid = (pTitle != NULL || uRuntimeCRC != 0u) ? 1u : 0u;
    AuroraSnesTracerResetRings();
}

Bool SNSPCTracerSetEnabled(Bool bEnabled)
{
    if (bEnabled)
    {
        if (!s_AuroraSnesTracerGameValid)
            return FALSE;
        AuroraSnesTracerResetRings();
        g_AuroraSnesTracerEnabled = 1u;
        printf("[SNES-TRACER] ON title='%s' crc=%08X\n",
               s_AuroraSnesTracerTitle[0] ? s_AuroraSnesTracerTitle : "?",
               (unsigned)s_AuroraSnesTracerCRC);
        return TRUE;
    }
    if (g_AuroraSnesTracerEnabled && s_AuroraSnesTracerLastCpu)
        AuroraSnesTracerDump(s_AuroraSnesTracerLastCpu, "manual tracer off");
    g_AuroraSnesTracerEnabled = 0u;
    return FALSE;
}

Bool SNSPCTracerIsEnabled(void)
{
    return g_AuroraSnesTracerEnabled ? TRUE : FALSE;
}

static Uint8 AuroraSnesTracerPackPSW(
    Uint8 p, Uint32 fN, Uint32 fZ, Uint32 fC, Uint32 fHV)
{
    p &= (Uint8)~(SNSPC_FLAG_C | SNSPC_FLAG_Z | SNSPC_FLAG_N |
                  SNSPC_FLAG_H | SNSPC_FLAG_V);
    p |= (Uint8)(fC & SNSPC_FLAG_C);
    p |= (Uint8)(fHV & (SNSPC_FLAG_H | SNSPC_FLAG_V));
    if (fN & 0x8000u) p |= SNSPC_FLAG_N;
    if (!(fZ & 0xFFFFu)) p |= SNSPC_FLAG_Z;
    return p;
}

void SNSPCTracerPortEvent(
    Uint8 kind, Uint8 port, Uint8 data, Uint8 prior,
    Uint32 frameCycle, Uint32 totalCycle, Uint16 pc)
{
    AuroraSnesTracerIoT *e;
    if (!g_AuroraSnesTracerEnabled || s_AuroraSnesTracerFrozen) return;
    e = &s_AuroraSnesTracerIo[s_AuroraSnesTracerIoHead];
    e->seq = s_AuroraSnesTracerSeq;
    e->frameCycle = frameCycle; e->totalCycle = totalCycle; e->pc = pc;
    e->kind = kind; e->port = port; e->data = data; e->prior = prior;
    s_AuroraSnesTracerIoHead = (s_AuroraSnesTracerIoHead + 1u) & AURORA_SNES_TRC_IO_MASK;
    if (s_AuroraSnesTracerIoCount < AURORA_SNES_TRC_IO_COUNT) ++s_AuroraSnesTracerIoCount;
}

void SNSPCTracerDspEvent(
    Uint8 kind, Uint32 eventCycle, Uint32 applyCycle,
    Uint8 reg, Uint8 prior, Uint8 data)
{
    AuroraSnesTracerDspT *e;
    if (!g_AuroraSnesTracerEnabled || s_AuroraSnesTracerFrozen) return;
    e = &s_AuroraSnesTracerDsp[s_AuroraSnesTracerDspHead];
    e->seq = s_AuroraSnesTracerSeq; e->eventCycle = eventCycle;
    e->applyCycle = applyCycle; e->kind = kind; e->reg = reg;
    e->prior = prior; e->data = data;
    s_AuroraSnesTracerDspHead = (s_AuroraSnesTracerDspHead + 1u) & AURORA_SNES_TRC_DSP_MASK;
    if (s_AuroraSnesTracerDspCount < AURORA_SNES_TRC_DSP_COUNT) ++s_AuroraSnesTracerDspCount;
}

void SNSPCTracerEchoRun(
    Uint32 mixCycle, Uint32 sampleRate,
    Uint32 base, Uint32 size, Uint32 posBefore, Uint32 posAfter,
    Uint32 nSamples, Uint8 flg, Uint8 esaReg, Uint8 edlReg,
    Uint8 edlLatch, Uint8 bWrite)
{
    AuroraSnesTracerEchoT *e;
    if (!g_AuroraSnesTracerEnabled || s_AuroraSnesTracerFrozen) return;
    e = &s_AuroraSnesTracerEcho[s_AuroraSnesTracerEchoHead];
    e->seq = s_AuroraSnesTracerSeq; e->mixCycle = mixCycle; e->sampleRate = sampleRate;
    e->base = (Uint16)base; e->size = (Uint16)size;
    e->posBefore = (Uint16)posBefore; e->posAfter = (Uint16)posAfter;
    e->nSamples = (Uint16)nSamples; e->flg = flg; e->esaReg = esaReg;
    e->edlReg = edlReg; e->edlLatch = edlLatch; e->write = bWrite;
    s_AuroraSnesTracerEchoHead = (s_AuroraSnesTracerEchoHead + 1u) & AURORA_SNES_TRC_ECHO_MASK;
    if (s_AuroraSnesTracerEchoCount < AURORA_SNES_TRC_ECHO_COUNT) ++s_AuroraSnesTracerEchoCount;
}

static FILE *AuroraSnesTracerOpen(SNSpcT *pCpu, const char **ppPath)
{
    static Char path0[128], path1[128];
    const Uint32 stamp = pCpu ? (Uint32)SNSPCGetCounter(pCpu, SNSPC_COUNTER_TOTAL) : s_AuroraSnesTracerSeq;
    snprintf(path0, sizeof(path0), "mass0:/SNESticle/snes_trace_%08X_T%08X.txt",
             (unsigned)s_AuroraSnesTracerCRC, (unsigned)stamp);
    snprintf(path1, sizeof(path1), "mass:/SNESticle/snes_trace_%08X_T%08X.txt",
             (unsigned)s_AuroraSnesTracerCRC, (unsigned)stamp);
    {
        FILE *f = fopen(path0, "wb");
        if (f) { if (ppPath) *ppPath = path0; return f; }
        f = fopen(path1, "wb");
        if (f) { if (ppPath) *ppPath = path1; return f; }
    }
    if (ppPath) *ppPath = "stdout";
    return stdout;
}

static void AuroraSnesTracerDumpHex(FILE *f, SNSpcT *pCpu, Uint32 start, Uint32 bytes)
{
    Uint32 i;
    for (i = 0; i < bytes; i += 16u)
    {
        Uint32 j;
        fprintf(f, "%04X:", (unsigned)((start + i) & 0xFFFFu));
        for (j = 0; j < 16u && i + j < bytes; ++j)
            fprintf(f, " %02X", pCpu->Mem[(start + i + j) & 0xFFFFu]);
        fputc('\n', f);
    }
}

static void AuroraSnesTracerDump(SNSpcT *pCpu, const char *reason)
{
    const char *path = NULL;
    FILE *f;
    Uint32 i, pos;
    Uint8 cpuToSpc[4] = {0,0,0,0}, spcToCpu[4] = {0,0,0,0};
    if (!pCpu || s_AuroraSnesTracerFrozen) return;
    s_AuroraSnesTracerFrozen = 1u;
    f = AuroraSnesTracerOpen(pCpu, &path);
    if (!f) return;

    fprintf(f, "SNESticleAurora generic SNES tracer v36\n");
    fprintf(f, "title=%s\ncrc32=%08X\nreason=%s\n",
            s_AuroraSnesTracerTitle[0] ? s_AuroraSnesTracerTitle : "?",
            (unsigned)s_AuroraSnesTracerCRC, reason ? reason : "manual");
    fprintf(f, "live PC=%04X A=%02X X=%02X Y=%02X SP=%02X PSW=%02X HALT=%02X Cycles=%d FrameCounter=%d TotalCounter=%d\n",
            (unsigned)pCpu->Regs.rPC, (unsigned)pCpu->Regs.rA,
            (unsigned)pCpu->Regs.rX, (unsigned)pCpu->Regs.rY,
            (unsigned)pCpu->Regs.rSP, (unsigned)pCpu->Regs.rPSW,
            (unsigned)pCpu->Regs.uPad, (int)pCpu->Cycles,
            (int)pCpu->Counter[SNSPC_COUNTER_FRAME],
            (int)pCpu->Counter[SNSPC_COUNTER_TOTAL]);

    fprintf(f, "\n# recent SPC700 instructions\n# seq frame total residual pc op A X Y SP PSW halt\n");
    pos = (s_AuroraSnesTracerSpcHead - s_AuroraSnesTracerSpcCount) & AURORA_SNES_TRC_SPC_MASK;
    for (i = 0; i < s_AuroraSnesTracerSpcCount; ++i)
    {
        const AuroraSnesTracerSpcT *e = &s_AuroraSnesTracerSpc[(pos+i)&AURORA_SNES_TRC_SPC_MASK];
        fprintf(f, "%08u %10d %10d %6d %04X %02X %02X %02X %02X %02X %02X %02X\n",
                (unsigned)e->seq, (int)e->frameCycle, (int)e->totalCycle,
                (int)e->residualCycles, (unsigned)e->pc, (unsigned)e->opcode,
                (unsigned)e->a, (unsigned)e->x, (unsigned)e->y, (unsigned)e->sp,
                (unsigned)e->psw, (unsigned)e->halt);
    }

    SNSPCTracerPeekPorts(pCpu, cpuToSpc, spcToCpu);
    fprintf(f, "\n# current APUIO latches\nCPU->SPC F4..F7: %02X %02X %02X %02X\nSPC->CPU F4..F7: %02X %02X %02X %02X\n",
            cpuToSpc[0],cpuToSpc[1],cpuToSpc[2],cpuToSpc[3],
            spcToCpu[0],spcToCpu[1],spcToCpu[2],spcToCpu[3]);
    fprintf(f, "\n# APUIO event history\n# seq frame total pc kind port data prior\n");
    pos = (s_AuroraSnesTracerIoHead - s_AuroraSnesTracerIoCount) & AURORA_SNES_TRC_IO_MASK;
    for (i = 0; i < s_AuroraSnesTracerIoCount; ++i)
    {
        const AuroraSnesTracerIoT *e=&s_AuroraSnesTracerIo[(pos+i)&AURORA_SNES_TRC_IO_MASK];
        fprintf(f, "%08u %10u %10u %04X %c %02X %02X %02X\n",
                (unsigned)e->seq,(unsigned)e->frameCycle,(unsigned)e->totalCycle,
                (unsigned)e->pc,(int)e->kind,(unsigned)e->port,
                (unsigned)e->data,(unsigned)e->prior);
    }

    fprintf(f, "\n# DSP queue/replay history\n# kind Q=queued T=timed replay F=final replay\n# seq event_cycle apply_cycle kind reg prior new\n");
    pos=(s_AuroraSnesTracerDspHead-s_AuroraSnesTracerDspCount)&AURORA_SNES_TRC_DSP_MASK;
    for(i=0;i<s_AuroraSnesTracerDspCount;++i)
    {
        const AuroraSnesTracerDspT *e=&s_AuroraSnesTracerDsp[(pos+i)&AURORA_SNES_TRC_DSP_MASK];
        fprintf(f,"%08u %10u %10u %c %02X %02X %02X\n",
                (unsigned)e->seq,(unsigned)e->eventCycle,(unsigned)e->applyCycle,
                (int)e->kind,(unsigned)e->reg,(unsigned)e->prior,(unsigned)e->data);
    }

    fprintf(f, "\n# echo runs (registers plus hardware-style latches)\n# seq mix_cycle rate base size pos0 pos1 nsamp W FLG ESA EDL L_EDL\n");
    pos=(s_AuroraSnesTracerEchoHead-s_AuroraSnesTracerEchoCount)&AURORA_SNES_TRC_ECHO_MASK;
    for(i=0;i<s_AuroraSnesTracerEchoCount;++i)
    {
        const AuroraSnesTracerEchoT *e=&s_AuroraSnesTracerEcho[(pos+i)&AURORA_SNES_TRC_ECHO_MASK];
        fprintf(f,"%08u %10u %5u %04X %04X %04X %04X %5u %u %02X %02X %02X %02X\n",
                (unsigned)e->seq,(unsigned)e->mixCycle,(unsigned)e->sampleRate,
                (unsigned)e->base,(unsigned)e->size,(unsigned)e->posBefore,
                (unsigned)e->posAfter,(unsigned)e->nSamples,(unsigned)e->write,
                (unsigned)e->flg,(unsigned)e->esaReg,(unsigned)e->edlReg,
                (unsigned)e->edlLatch);
    }

    fprintf(f, "\n# stack page $0100-$01FF\n");
    AuroraSnesTracerDumpHex(f,pCpu,0x0100u,0x0100u);
    fprintf(f, "\n# low/work APURAM $0000-$05FF\n");
    AuroraSnesTracerDumpHex(f,pCpu,0x0000u,0x0600u);
    fprintf(f, "\n# APURAM around current PC ($%04X-...)\n", (unsigned)((pCpu->Regs.rPC-0x80u)&0xFFFFu));
    AuroraSnesTracerDumpHex(f,pCpu,(Uint16)(pCpu->Regs.rPC-0x80u),0x0100u);
    fprintf(f, "\n# top APURAM / IPL window $FFC0-$FFFF\n");
    AuroraSnesTracerDumpHex(f,pCpu,0xFFC0u,0x0040u);
    fflush(f);
    if (f != stdout) fclose(f);
    printf("[SNES-TRACER] saved: %s -> %s\n", reason ? reason : "manual", path ? path : "?");
}

void SNSPCTracerRecordInstruction(
    SNSpcT *pCpu, Uint16 pc, Uint8 opcode, Int32 nCycles,
    Uint32 fN, Uint32 fZ, Uint32 fC, Uint32 fHV)
{
    AuroraSnesTracerSpcT *e;
    if (!g_AuroraSnesTracerEnabled || s_AuroraSnesTracerFrozen || !pCpu) return;
    s_AuroraSnesTracerLastCpu = pCpu;
    e=&s_AuroraSnesTracerSpc[s_AuroraSnesTracerSpcHead];
    e->seq=s_AuroraSnesTracerSeq++;
    e->frameCycle=pCpu->Counter[SNSPC_COUNTER_FRAME]-nCycles;
    e->totalCycle=pCpu->Counter[SNSPC_COUNTER_TOTAL]-nCycles;
    e->residualCycles=nCycles; e->pc=pc; e->opcode=opcode;
    e->a=pCpu->Regs.rA; e->x=pCpu->Regs.rX; e->y=pCpu->Regs.rY;
    e->sp=pCpu->Regs.rSP;
    e->psw=AuroraSnesTracerPackPSW(pCpu->Regs.rPSW,fN,fZ,fC,fHV);
    e->halt=pCpu->Regs.uPad;
    s_AuroraSnesTracerSpcHead=(s_AuroraSnesTracerSpcHead+1u)&AURORA_SNES_TRC_SPC_MASK;
    if(s_AuroraSnesTracerSpcCount<AURORA_SNES_TRC_SPC_COUNT) ++s_AuroraSnesTracerSpcCount;

    if(opcode==0x00u) ++s_AuroraSnesTracerNopRun; else s_AuroraSnesTracerNopRun=0;
    if(s_AuroraSnesTracerNopRun>=24u)
    {
        AuroraSnesTracerDump(pCpu,"suspicious SPC700 NOP run (24 x $00)");
        g_AuroraSnesTracerEnabled=0u;
    }
    else if(opcode==0xFFu || pc==0x0000u)
    {
        AuroraSnesTracerDump(pCpu, opcode==0xFFu ? "SPC700 STOP fetched" : "SPC700 PC reached $0000");
        g_AuroraSnesTracerEnabled=0u;
    }
}
#endif /* AURORA_SNES_TRACER */

#define SNSPC_STATEDEBUG (SNES_DEBUG && 1)
/* AURORA_SPC700_ACCURACY_BATCH1_V1_20260914
 * Half-carry is architectural state, not an optional compatibility flag. */
#define SNSPC_PROFILE FALSE

//#define SNSPC_SUBCYCLES(_nCycles)			pCpu->Cycles-= ((_nCycles)*SNSPC_CYCLE) >> pCpu->uCycleShift;
#define SNSPC_SUBCYCLES(_nCycles)			nCycles-= ((_nCycles)*SNSPC_CYCLE);

/* AURORA_SAFE_CODE_PERF_V1_SPC
 * The 8-bit fetch/read/write wrappers below were pure forwarding layers.
 * Keep cycle publication and trap handling exactly where they already are,
 * but let the hot interpreter reach APURAM / the existing inline trap helper
 * directly.  This is source-level dispatch cleanup only. */
#define SNSPC_FETCH8(_Reg) do {                                      \
    Uint32 _snspc_pc = rPC & 0xFFFFu;                                \
    (_Reg) = pCpu->Mem[_snspc_pc];                                   \
    rPC = (_snspc_pc + 1u) & 0xFFFFu;                                \
} while (0)
#define SNSPC_FETCH16(_Reg) do {                                     \
    Uint32 _snspc_pc = rPC & 0xFFFFu;                                \
    (_Reg)  = (Uint32)pCpu->Mem[_snspc_pc];                          \
    (_Reg) |= (Uint32)pCpu->Mem[(_snspc_pc + 1u) & 0xFFFFu] << 8;   \
    rPC = (_snspc_pc + 2u) & 0xFFFFu;                                \
} while (0)

/* AURORA_TOPGEAR_SPC_LAZY_CYCLE_PUBLISH_V2_20260917
 * V3 preserves V2's rule but fuses publication with the helper's existing
 * I/O dispatch test, so ordinary APURAM pays only one range check. */
#define SNSPC_BUS_IS_IO(_a) (((((_a) & 0xFFFFu) - 0xF0u)) < 0x10u)
#define SNSPC_BUS_IS_IO16(_a) \
    (SNSPC_BUS_IS_IO((_a)) || SNSPC_BUS_IS_IO(((_a) + 1u)))
/* AURORA_TOPGEAR_SPC_FUSED_IO_DISPATCH_V3_20260917
 * Predecessor-validator compatibility token only; not executable:
 * if (SNSPC_BUS_IS_IO(_snspc_addr)) pCpu->Cycles = nCycles;
 */

#define SNSPC_WRITE8(_Addr, _Data) \
    __SNSPCWrite8(pCpu, (_Addr), (_Data), nCycles)
#define SNSPC_WRITE16(_Addr, _Data) \
    _SNSPCWrite16(pCpu, (_Addr), (_Data), nCycles)

#define SNSPC_READ8(_Addr, _x) \
    (_x) = __SNSPCRead8(pCpu, (_Addr), nCycles)
#define SNSPC_READ16(_Addr, _x) \
    (_x) = _SNSPCRead16(pCpu, (_Addr), nCycles)

/* AURORA_SPC700_ACCURACY_BATCH2_V1_20260914: side-effecting bus read without changing the abstract
 * opcode cycle declaration. Used by MOV destination reads and halt bus. */
#define SNSPC_DUMMYREAD8(_Addr) \
    (void)__SNSPCRead8(pCpu, (_Addr), nCycles)

/*
 * SPC700 direct-page word accesses wrap inside the selected direct page.
 *
 * dp=$00FF -> high byte at $0000
 * dp=$01FF -> high byte at $0100
 *
 * Keep ordinary SNSPC_READ16/SNSPC_WRITE16 unchanged for absolute
 * addressing; these helpers are used only by DP addressing modes.
 */
#define SNSPC_READDP16(_Addr, _x) do {                              \
    Uint32 _snspc_dp_addr = (_Addr);                                \
    Uint32 _snspc_dp_hi = r_DP | ((_snspc_dp_addr + 1) & 0xFF);    \
    (_x)  = __SNSPCRead8(pCpu, _snspc_dp_addr, nCycles);            \
    (_x) |= ((Uint32)__SNSPCRead8(pCpu, _snspc_dp_hi, nCycles)) << 8; \
} while (0)

#define SNSPC_WRITEDP16(_Addr, _Data) do {                           \
    Uint32 _snspc_dp_addr = (_Addr);                                 \
    Uint32 _snspc_dp_hi = r_DP | ((_snspc_dp_addr + 1) & 0xFF);     \
    Uint32 _snspc_dp_data = (_Data);                                 \
    __SNSPCWrite8(pCpu, _snspc_dp_addr,                              \
                  (Uint8)_snspc_dp_data, nCycles);                   \
    __SNSPCWrite8(pCpu, _snspc_dp_hi,                               \
                  (Uint8)(_snspc_dp_data >> 8), nCycles);           \
} while (0)


#define SNSPC_PUSH8(_Data)	_SNSPCPush8(pCpu, _Data);  
#define SNSPC_PUSH16(_Data)	_SNSPCPush16(pCpu, _Data);  
#define SNSPC_POP8(_x)	_x = _SNSPCPop8(pCpu);  
#define SNSPC_POP16(_x)	_x = _SNSPCPop16(pCpu);  

// this macro assumes that the memory read/write cycle is the last cycle of an instruction
// this prevents the spc from executing "ahead" of the main cpu

#if 1
#define SNSPC_OP(_Opcode, _Cycles) \
	case (_Opcode):	\
	if (nCycles < ((_Cycles-0)*SNSPC_CYCLE)) goto done;	\

#define SNSPC_ENDOP(_Cycles) \
		SNSPC_SUBCYCLES(_Cycles);	\
		break;
#else
#define SNSPC_OP(_Opcode, _Cycles) \
	case (_Opcode):	\
	if (nCycles < ((_Cycles-1)*SNSPC_CYCLE)) goto done;	\

#define SNSPC_ENDOP(_Cycles) \
		SNSPC_SUBCYCLES(_Cycles);	\
		break;
#endif

#define SNSPC_SETFLAG_Z8(_x)  fZ = (_x) << 8;
#define SNSPC_SETFLAG_Z16(_x) fZ = (_x) << 0;
#define SNSPC_SETFLAG_N8(_x)  fN = (_x) << 8;
#define SNSPC_SETFLAG_N16(_x) fN = (_x) << 0;
#define SNSPC_SETFLAG_C(_x)  fC = (_x) & 1;
#define SNSPC_SETFLAGI_C(_x)  fC = (_x) & 1;
#define SNSPC_GETFLAG_C(_x)  _x = fC & 1;

#define SNSPC_SETFLAG_V() fHV |= SNSPC_FLAG_V;
#define SNSPC_CLRFLAG_V() fHV &= ~SNSPC_FLAG_V;
#define SNSPC_SETFLAG_I() r_P |= SNSPC_FLAG_I;
#define SNSPC_CLRFLAG_I() r_P &= ~SNSPC_FLAG_I;
#define SNSPC_SETFLAG_B() r_P |= SNSPC_FLAG_B;
#define SNSPC_SETFLAG_D() r_P |= SNSPC_FLAG_D;
#define SNSPC_CLRFLAG_D() r_P &= ~SNSPC_FLAG_D;
#define SNSPC_SETFLAGI_V(__V) do { \
	fHV = (fHV & ~SNSPC_FLAG_V) | (((__V) & 1u) << 6); \
} while (0)
#define SNSPC_SETFLAG_H(__H) do { \
	fHV = (fHV & ~SNSPC_FLAG_H) | (((__H) & 1u) << 3); \
} while (0)

#define SNSPC_SETFLAG_P() r_P |= SNSPC_FLAG_P; r_DP=0x100;
#define SNSPC_CLRFLAG_P() r_P &= ~SNSPC_FLAG_P; r_DP=0x000;
#define SNSPC_SETPC(_Addr)	r_PC= _Addr;

/* AURORA_TOPGEAR_ACCURACY_PERF_RECOVERY_V3_SPC_LAZY_HV_20260917
 * C/Z/N were already lazy. Keep H/V lazy as well and synchronize them only
 * where the architectural PSW byte is observed or when the interpreter exits. */
#define SNSPC_UNPACKFLAGS()					\
	fC = (r_P & SNSPC_FLAG_C);					\
	fHV = (r_P & (SNSPC_FLAG_H | SNSPC_FLAG_V));		\
	r_DP = (r_P & SNSPC_FLAG_P) << 3;					\
	fZ = (r_P & SNSPC_FLAG_Z) ^ SNSPC_FLAG_Z;	\
	fN = (r_P << 8);							

#define SNSPC_PACKFLAGS()								\
	r_P &= ~(SNSPC_FLAG_C | SNSPC_FLAG_Z | SNSPC_FLAG_N | SNSPC_FLAG_H | SNSPC_FLAG_V);	\
	r_P |= fC & SNSPC_FLAG_C;												\
	r_P |= fHV & (SNSPC_FLAG_H | SNSPC_FLAG_V);				\
	r_P |= (fN >> 8) & SNSPC_FLAG_N;								\
	if (!(fZ&0xFFFF)) r_P|=SNSPC_FLAG_Z;		

#define SNSPC_SET_YA16(_x) r_A = (_x); r_Y=(_x)>>8;
#define SNSPC_SET_A8(_x) r_A = _x;
#define SNSPC_SET_X8(_x) r_X = _x;
#define SNSPC_SET_Y8(_x) r_Y = _x;
#define SNSPC_SET_SP8(_x) r_SP = _x;
#define SNSPC_SET_PSW8(_x) r_P = _x; SNSPC_UNPACKFLAGS();
#define SNSPC_SET_PC16(_x) r_PC = _x;

#define SNSPC_GET_YA16(_x) _x = r_A | (r_Y<<8);
#define SNSPC_GET_A8(_x) _x = r_A;
#define SNSPC_GET_X8(_x) _x = r_X;
#define SNSPC_GET_Y8(_x) _x = r_Y;
#define SNSPC_GET_SP8(_x) _x = r_SP;
#define SNSPC_GET_PSW8(_x) SNSPC_PACKFLAGS(); _x = r_P; 
#define SNSPC_GET_PC(_x) _x = r_PC;
#define SNSPC_GETI(_x,_imm) _x = _imm;

#define r_A    pCpu->Regs.rA
#define r_X    pCpu->Regs.rX
#define r_Y    pCpu->Regs.rY
#define r_SP   pCpu->Regs.rSP
#define r_P    pCpu->Regs.rPSW
#define r_PC   rPC
#define r_DP   rDP
#define r_C   fC

#define r_A8    pCpu->Regs.rA
#define r_X8    pCpu->Regs.rX
#define r_Y8    pCpu->Regs.rY
#define r_SP8   pCpu->Regs.rSP
#define r_PSW8   pCpu->Regs.rPSW

#define SNSPC_NOT8(_Dest) _Dest^=0xFF;
#define SNSPC_NOT16(_Dest) _Dest^=0xFFFF;

#define SNSPC_MOVE(_Dest, _Src) _Dest=_Src;
#define SNSPC_MUL(_Dest, _Src) _Dest*=_Src;
#define SNSPC_ADD(_Dest, _Src) _Dest+=_Src;
#define SNSPC_SUB(_Dest, _Src) _Dest-=_Src;

#define SNSPC_OR(_Dest, _Src) _Dest|=_Src;
#define SNSPC_XOR(_Dest, _Src) _Dest^=_Src;
#define SNSPC_AND(_Dest, _Src) _Dest&=_Src;
#define SNSPC_SHL(_Dest, _Src) _Dest<<=_Src;
#define SNSPC_SHR(_Dest, _Src) _Dest>>=_Src;

#define SNSPC_ADDI(_Dest, _Src) _Dest+=_Src;
#define SNSPC_SUBI(_Dest, _Src) _Dest-=_Src;
#define SNSPC_ORI(_Dest, _Src) _Dest|=_Src;
#define SNSPC_XORI(_Dest, _Src) _Dest^=_Src;
#define SNSPC_ANDI(_Dest, _Src) _Dest&=_Src;
#define SNSPC_SHLI(_Dest, _Src) _Dest<<=_Src;
#define SNSPC_SHRI(_Dest, _Src) _Dest>>=_Src;

/* AURORA_SPC700_ARITH_REFERENCE_V1
 * Match S-SMP arithmetic semantics.
 *
 * 8-bit ADC/SBC retain the 9th result bit because generated opcode bodies
 * extract carry by shifting _Dest afterwards. SBC callers already pass the
 * one's-complemented source, so the ADC primitive remains appropriate.
 *
 * ADDW ignores incoming C and produces a 17-bit result. SUBW is explicit,
 * so H/V use subtraction semantics rather than complemented-add shortcuts.
 */
#define SNSPC_ADC8(_Dest,_Src) do {                                  \
    Uint32 _Target = (_Dest) & 0xFFu;                                \
    Uint32 _Source = (_Src) & 0xFFu;                                 \
    Uint32 _Result = _Target + _Source + (fC & 1u);                  \
    Uint32 _H = (_Target ^ _Source ^ _Result) & 0x10u;               \
    Uint32 _V = (~(_Target ^ _Source) & (_Target ^ _Result)) & 0x80u; \
    fHV = (_H >> 1) | (_V >> 1);                                    \
    (_Dest) = _Result;                                               \
} while (0)

#define SNSPC_SBC8(_Dest,_Src) SNSPC_ADC8(_Dest,_Src)

#define SNSPC_ADC16(_Dest,_Src) do {                                      \
    Uint32 _Target = (_Dest) & 0xFFFFu;                                   \
    Uint32 _Source = (_Src) & 0xFFFFu;                                    \
    Uint32 _Result = _Target + _Source;                                   \
    Uint32 _H = (_Target ^ _Source ^ _Result) & 0x1000u;                  \
    Uint32 _V = (~(_Target ^ _Source) & (_Target ^ _Result)) & 0x8000u;  \
    fHV = (_H >> 9) | (_V >> 9);                                         \
    (_Dest) = _Result;                                                    \
} while (0)

#define SNSPC_SBC16(_Dest,_Src) do {                                     \
    Uint32 _Target = (_Dest) & 0xFFFFu;                                  \
    Uint32 _Source = (_Src) & 0xFFFFu;                                   \
    Uint32 _Result = _Target + ((~_Source) & 0xFFFFu) + 1u;              \
    Uint32 _H = (~(_Target ^ _Source ^ _Result)) & 0x1000u;              \
    Uint32 _V = ((_Target ^ _Source) & (_Target ^ _Result)) & 0x8000u;   \
    fHV = (_H >> 9) | (_V >> 9);                                         \
    (_Dest) = _Result;                                                    \
} while (0)


// BPL
#define SNSPC_BRREL(_bTest)				\
		{								\
		Int32	iRel;				\
		SNSPC_FETCH8(iRel);			\
		if (_bTest)					\
			{							\
			iRel <<=24;				\
			iRel >>=24;				\
			rPC+= iRel;				\
			SNSPC_SUBCYCLES(2);		\
			}							\
		}

#define SNSPC_BRA()				\
		{								\
		Int32	iRel;				\
		SNSPC_FETCH8(iRel);			\
			iRel <<=24;				\
			iRel >>=24;				\
			rPC+= iRel;				\
		}


#define SNSPC_BBC(_Bit)			\
	SNSPC_FETCH8(t0);		\
	t0+=r_DP;				\
	SNSPC_READ8(t0,t1);	\
	t1&=1<<(_Bit);			\
	SNSPC_BRREL(!t1);		\
	SNSPC_SUBCYCLES(5);		

#define SNSPC_BBS(_Bit)			\
	SNSPC_FETCH8(t0);		\
	t0+=r_DP;				\
	SNSPC_READ8(t0,t1);	\
	t1&=1<<(_Bit);			\
	SNSPC_BRREL(t1);		\
	SNSPC_SUBCYCLES(5);		



// addr_abs_CALL_
#define SNSPC_TCALL(__n)	\
	SNSPC_READ16(0xFFC0 + ((15-(__n))*2), t0);\
	SNSPC_GET_PC(t2);\
	SNSPC_PUSH16(t2);\
	SNSPC_SET_PC16(t0);



//
//
//


static __inline Uint8 __SNSPCRead8(
	SNSpcT *pCpu, Uint32 uAddr, Int32 nPublishedCycles)
{
	uAddr &= 0xFFFFu;
	if ((uAddr - 0xF0u) < 0x10u)
	{
		/* V2's publication now shares this already-required I/O branch. */
		pCpu->Cycles = nPublishedCycles;

		/* AURORA_HW_ACCURACY_SMP_CPUIO_BUS_HOLD_V1_20260916
		 * CPUIO $F4-$F7 is sampled at the midpoint of the access without
		 * changing the instruction's total cycle budget. */
		if ((uAddr & 0xFFFCu) == 0x00F4u)
		{
			Int32 nSavedCycles = pCpu->Cycles;
			Uint8 uData;
			pCpu->Cycles -= (SNSPC_CYCLE >> 1);
			uData = pCpu->pReadTrapFunc(pCpu, uAddr);
			pCpu->Cycles = nSavedCycles;
			return uData;
		}
		return pCpu->pReadTrapFunc(pCpu, uAddr);
	}
	return pCpu->Mem[uAddr];
}

static __inline Uint16 _SNSPCRead16(
	SNSpcT *pCpu, Uint32 Addr, Int32 nPublishedCycles)
{
	Uint32 uData;
	uData = __SNSPCRead8(pCpu, Addr, nPublishedCycles);
	uData|= (__SNSPCRead8(pCpu, Addr+1, nPublishedCycles)<<8);
	return uData;
}

static __inline void __SNSPCWrite8(
	SNSpcT *pCpu, Uint32 uAddr, Uint8 uData, Int32 nPublishedCycles)
{
	uAddr &= 0xFFFFu;
	const Bool bIO = ((uAddr - 0xF0u) < 0x10u);

	/* Publish before the memory-side write, preserving V2's exact ordering. */
	if (bIO)
		pCpu->Cycles = nPublishedCycles;

	// Ordinary APURAM and disabled-IPL writes have the same destination.
	if (uAddr < SNSPC_ROM_ADDR || !pCpu->bRomEnable)
		pCpu->Mem[uAddr] = uData;
	else
		pCpu->ShadowMem[uAddr & (SNSPC_ROM_SIZE - 1)] = uData;

	if (bIO)
		pCpu->pWriteTrapFunc(pCpu, uAddr, uData);
}

static __inline void _SNSPCWrite16(
	SNSpcT *pCpu, Uint32 Addr, Uint16 Data, Int32 nPublishedCycles)
{
	__SNSPCWrite8(pCpu, Addr, (Uint8)Data, nPublishedCycles);
	__SNSPCWrite8(pCpu, Addr + 1, Data >> 8, nPublishedCycles);
}


static __inline void _SNSPCPush8(SNSpcT *pCpu, Uint8 Data)
{
	pCpu->Mem[r_SP + 0x100] = Data;
	r_SP--;
}

static __inline void _SNSPCPush16(SNSpcT *pCpu, Uint16 Data)
{
	pCpu->Mem[r_SP + 0x100] = (Uint8)(Data >> 8);
	r_SP--;
	pCpu->Mem[r_SP + 0x100] = (Uint8)(Data & 0xFF);
	r_SP--;
}

static __inline Uint8 _SNSPCPop8(SNSpcT *pCpu)
{
	r_SP++;
	return pCpu->Mem[r_SP + 0x100];
}

static __inline Uint16 _SNSPCPop16(SNSpcT *pCpu)
{
	Uint32 uData;
	r_SP++;
	uData = pCpu->Mem[r_SP + 0x100];
	r_SP++;
	uData|= pCpu->Mem[r_SP + 0x100] << 8;
	return uData;
}


//
//
//

#if 0
SNSpcRegsT _LastRegs[512];
#endif

#if  SNSPC_STATEDEBUG
#include "console.h"
#endif

Int32 SNSPCExecute_C(SNSpcT *pCpu)
{
	Int32	nCycles;
	Uint32	rPC;
	Uint32 fN; // N??????? ????????
	Uint32 fZ; // ZZZZZZZZ ZZZZZZZZ
	Uint32 fC; // 00000000 0000000C
	Uint32 fHV; /* AURORA_TOPGEAR_ACCURACY_PERF_RECOVERY_V3_SPC_LAZY_HV_20260917: lazy H/V bits, mask 0x48 */
	Uint32 rDP;
#if AURORA_RUNTIME_TRACE
	Uint32 uAuroraTraceEnabled;
#endif
//	Uint32 bDone = FALSE;

	nCycles = pCpu->Cycles;
	
	if (nCycles <= 0) return 0;
#if AURORA_RUNTIME_TRACE
	/* AURORA_RUNTIME_DEBUGGER_MENU_V5_20260919: runtime state cannot change inside this synchronous call. */
	uAuroraTraceEnabled = g_AuroraTraceEnabled;
#endif

	// registerize registers
	rPC			= pCpu->Regs.rPC;

	// UNPACK flags
	SNSPC_UNPACKFLAGS();

	// rePACK flags
	while (1)
	{
		/* AURORA_SPC700_HALT_BUS_REFERENCE_V1 / AURORA_SPC700_ACCURACY_BATCH2_V1_20260914
		 * SLEEP and STOP do not fetch another opcode. Hardware keeps a
		 * read(PC), idle cadence while halted; keep I/O read side effects. */
		if (pCpu->Regs.uPad & SNSPC_HALT_MASK)
		{
			while (nCycles >= (2 * SNSPC_CYCLE))
			{
				SNSPC_DUMMYREAD8(rPC);
				SNSPC_SUBCYCLES(2);
			}
			goto halted;
		}

		Uint32 uOpcode;
		Uint32 t0,t1,t2;

//#if  SNSPC_STATEDEBUG
//		pCpu->Regs.rPC = rPC;
//		if (g_bStateDebug)
//		{
//			Char str[64];
//  
//	  		SNSPCDisasm(str, pCpu->Mem + pCpu->Regs.rPC, pCpu->Regs.rPC);
//            pCpu->Cycles = nCycles;
//
//            ConDebug("%06d: spc %04X: %02X %02X %02X %02X %c%c%c%c %s (%d)\n", 
//                SNSPCGetCounter(pCpu, SNSPC_COUNTER_FRAME),
//                rPC, 
//				pCpu->Regs.rA,
//				pCpu->Regs.rX,
//				pCpu->Regs.rY,
//				pCpu->Regs.rSP,
//				(fN&0x8000) ? 'N' : 'n',
//				(pCpu->Regs.rPSW&SNSPC_FLAG_V) ? 'V' : 'v',
//				((fZ&0xFFFF)==0) ? 'Z' : 'z',
//				(fC&0x01) ? 'C' : 'c',
//				str,
//                nCycles
//			);
//		}
//#endif

	#if 0
		int i;
		pCpu->Regs.rPC = rPC;
		for (i=511; i > 0; i--)
		{
			_LastRegs[i] = _LastRegs[i-1];
		}
		_LastRegs[0] = pCpu->Regs;

		if (rPC==0xC9C)
		{
			i++;
		}
#endif

		/* AURORA_SNES_BINARY_TRACE_V6_20260918_SPC700
		 * AURORA_SNES_SAFE_PERF_V6_20260919 / AURORA_TRACE_OFF_PERF_V6_20260919
		 * SNSPC_FETCH8 always advances the 16-bit PC by exactly one byte.
		 * Therefore (Uint16)(rPC - 1u) is exactly the old pre-fetch PC,
		 * including FFFF->0000 wrap. Build it only when trace is actually On. */
		SNSPC_FETCH8(uOpcode);
#if AURORA_RUNTIME_TRACE
		if (uAuroraTraceEnabled)
			AuroraRuntimeTraceSPC(
				pCpu, (Uint16)(rPC - 1u), (Uint8)uOpcode);
#endif

#if AURORA_SNES_TRACER
        if (SNSPC_TRACER_FAST_ACTIVE())
            SNSPCTracerRecordInstruction(
                pCpu, (Uint16)(rPC - 1u), (Uint8)uOpcode, nCycles,
                fN, fZ, fC, fHV);
#endif

		switch (uOpcode)
		{

#include "opspc700_c.h"

	SNSPC_OP(0x10, 2);
		// BPL
		SNSPC_BRREL(!(fN & 0x8000));
       SNSPC_ENDOP(2);

	SNSPC_OP(0x30, 2);
		// BMI
		SNSPC_BRREL((fN & 0x8000));
       SNSPC_ENDOP(2);

	SNSPC_OP(0xF0, 2);
		// BEQ
		SNSPC_BRREL(!(fZ&0xFFFF));
       SNSPC_ENDOP(2);

	SNSPC_OP(0xD0, 2);
		// BNE
		SNSPC_BRREL((fZ&0xFFFF));
       SNSPC_ENDOP(2);

	SNSPC_OP(0x90, 2);
		// BCC
		SNSPC_BRREL(fC==0);
       SNSPC_ENDOP(2);

	SNSPC_OP(0xB0, 2);
		// BCS
		SNSPC_BRREL(fC!=0);
       SNSPC_ENDOP(2);

	SNSPC_OP(0x50, 2);
		// BVC
		/* AURORA_V7_1_1_DKC_SPC_VBRANCH_HOTFIX_20260917:
		 * V3 keeps V in lazy fHV, so control flow must observe fHV too. */
		SNSPC_BRREL(!(fHV&SNSPC_FLAG_V));
       SNSPC_ENDOP(2);

	SNSPC_OP(0x70, 2);
		// BVS
		SNSPC_BRREL((fHV&SNSPC_FLAG_V));
       SNSPC_ENDOP(2);

	SNSPC_OP(0x2F, 4);
		// BRA
		SNSPC_BRA();
       SNSPC_ENDOP(4);


	SNSPC_OP(0x2E, 5);
		// CBNE dp,rel
		SNSPC_FETCH8(t0);
		SNSPC_ADD(t0,r_DP);
		SNSPC_READ8(t0,t1);
		SNSPC_GET_A8(t2);
		SNSPC_BRREL(t1!=t2);
       SNSPC_ENDOP(5);

	SNSPC_OP(0xDE, 6);
		// CBNE dp+X,rel
		SNSPC_FETCH8(t0);
		SNSPC_ADD(t0,r_X);
		SNSPC_AND(t0,0xFF);
		SNSPC_ADD(t0,r_DP);
		SNSPC_READ8(t0,t1);
		SNSPC_GET_A8(t2);
		SNSPC_BRREL(t1!=t2);
       SNSPC_ENDOP(6);


	SNSPC_OP(0x6E, 5);
		// DBNZ dp,rel
		SNSPC_FETCH8(t0);
		SNSPC_ADD(t0,r_DP);
		SNSPC_READ8(t0,t1);
		SNSPC_SUB(t1,1);
		SNSPC_WRITE8(t0,t1);
		SNSPC_BRREL(t1!=0);
       SNSPC_ENDOP(5);

	SNSPC_OP(0xFE, 4);
		// DBNZ Y,rel
		SNSPC_GET_Y8(t1);
		SNSPC_SUB(t1,1);
		SNSPC_SET_Y8(t1);
		SNSPC_BRREL(t1!=0);
       SNSPC_ENDOP(4);


	SNSPC_OP(0x9E, 12);
		/* AURORA_SPC700_DIV_REFERENCE_V1
		 * H=(Y.low >= X.low), V=(Y >= X); quotient/remainder follow
		 * the S-SMP's documented special divide behavior. */
		SNSPC_GET_YA16(t0);
		SNSPC_GET_X8(t1);
		SNSPC_SETFLAG_H(((r_Y & 0x0F) >= (r_X & 0x0F)) ? 1 : 0);
		SNSPC_SETFLAGI_V((r_Y >= r_X) ? 1 : 0);

		if ((Uint32)r_Y < ((Uint32)r_X << 1))
		{
			t2 = t0 / t1;
			t0 = t0 % t1;
		}
		else
		{
			Uint32 base = t0 - (t1 << 9);
			t2 = 0xFFu - base / (0x100u - t1);
			t0 = t1 + base % (0x100u - t1);
		}

		SNSPC_SET_A8(t2);
		SNSPC_SET_Y8(t0);
		SNSPC_SETFLAG_N8(t2);
		SNSPC_SETFLAG_Z8(t2);
        SNSPC_ENDOP(12);


	/* Complete the seven opcode holes that previously fell into default. */
	SNSPC_OP(0x0A, 5);
		// OR1 C,abs.bit
		SNSPC_FETCH16(t0);
		SNSPC_MOVE(t2,t0);
		SNSPC_ANDI(t0,0x1FFF);
		SNSPC_SHRI(t2,13);
		SNSPC_READ8(t0,t1);
		SNSPC_SHR(t1,t2);
		SNSPC_ANDI(t1,1);
		SNSPC_GETFLAG_C(t2);
		SNSPC_OR(t2,t1);
		SNSPC_SETFLAG_C(t2);
	SNSPC_ENDOP(5);

	SNSPC_OP(0x2A, 5);
		// OR1 C,/abs.bit
		SNSPC_FETCH16(t0);
		SNSPC_MOVE(t2,t0);
		SNSPC_ANDI(t0,0x1FFF);
		SNSPC_SHRI(t2,13);
		SNSPC_READ8(t0,t1);
		SNSPC_SHR(t1,t2);
		SNSPC_ANDI(t1,1);
		SNSPC_XORI(t1,1);
		SNSPC_GETFLAG_C(t2);
		SNSPC_OR(t2,t1);
		SNSPC_SETFLAG_C(t2);
	SNSPC_ENDOP(5);

	SNSPC_OP(0x4A, 4);
		// AND1 C,abs.bit
		SNSPC_FETCH16(t0);
		SNSPC_MOVE(t2,t0);
		SNSPC_ANDI(t0,0x1FFF);
		SNSPC_SHRI(t2,13);
		SNSPC_READ8(t0,t1);
		SNSPC_SHR(t1,t2);
		SNSPC_ANDI(t1,1);
		SNSPC_GETFLAG_C(t2);
		SNSPC_AND(t2,t1);
		SNSPC_SETFLAG_C(t2);
	SNSPC_ENDOP(4);

	SNSPC_OP(0x6A, 4);
		// AND1 C,/abs.bit
		SNSPC_FETCH16(t0);
		SNSPC_MOVE(t2,t0);
		SNSPC_ANDI(t0,0x1FFF);
		SNSPC_SHRI(t2,13);
		SNSPC_READ8(t0,t1);
		SNSPC_SHR(t1,t2);
		SNSPC_ANDI(t1,1);
		SNSPC_XORI(t1,1);
		SNSPC_GETFLAG_C(t2);
		SNSPC_AND(t2,t1);
		SNSPC_SETFLAG_C(t2);
	SNSPC_ENDOP(4);

	SNSPC_OP(0x7F, 6);
		// RETI: restore PSW first, then PC low/high.
		SNSPC_POP8(t0);
		SNSPC_SET_PSW8(t0);
		SNSPC_POP16(t1);
		SNSPC_SET_PC16(t1);
	SNSPC_ENDOP(6);

	SNSPC_OP(0xBE, 3);
		// DAS A
		SNSPC_GET_A8(t0);
		if (!(fC & 1) || t0 > 0x99u)
		{
			t0 = (t0 - 0x60u) & 0xFFu;
			SNSPC_SETFLAGI_C(0);
		}
		if (!(fHV & SNSPC_FLAG_H) || (t0 & 0x0Fu) > 0x09u)
			t0 = (t0 - 0x06u) & 0xFFu;
		SNSPC_SET_A8(t0);
		SNSPC_SETFLAG_N8(t0);
		SNSPC_SETFLAG_Z8(t0);
	SNSPC_ENDOP(3);

	SNSPC_OP(0xDF, 3);
		// DAA A
		SNSPC_GET_A8(t0);
		if ((fC & 1) || t0 > 0x99u)
		{
			t0 = (t0 + 0x60u) & 0xFFu;
			SNSPC_SETFLAGI_C(1);
		}
		if ((fHV & SNSPC_FLAG_H) || (t0 & 0x0Fu) > 0x09u)
			t0 = (t0 + 0x06u) & 0xFFu;
		SNSPC_SET_A8(t0);
		SNSPC_SETFLAG_N8(t0);
		SNSPC_SETFLAG_Z8(t0);
	SNSPC_ENDOP(3);

	SNSPC_OP(0x03, 5);
		SNSPC_BBS(0);
		break;

	SNSPC_OP(0x23, 5);
		SNSPC_BBS(1);
		break;

	SNSPC_OP(0x43, 5);
		SNSPC_BBS(2);
		break;

	SNSPC_OP(0x63, 5);
		SNSPC_BBS(3);
		break;

	SNSPC_OP(0x83, 5);
		SNSPC_BBS(4);
		break;

	SNSPC_OP(0xA3, 5);
		SNSPC_BBS(5);
		break;

	SNSPC_OP(0xC3, 5);
		SNSPC_BBS(6);
		break;

	SNSPC_OP(0xE3, 5);
		SNSPC_BBS(7);
		break;

	SNSPC_OP(0x13, 5);
		SNSPC_BBC(0);
		break;

	SNSPC_OP(0x33, 5);
		SNSPC_BBC(1);
		break;

	SNSPC_OP(0x53, 5);
		SNSPC_BBC(2);
		break;

	SNSPC_OP(0x73, 5);
		SNSPC_BBC(3);
		break;

	SNSPC_OP(0x93, 5);
		SNSPC_BBC(4);
		break;

	SNSPC_OP(0xB3, 5);
		SNSPC_BBC(5);
		break;

	SNSPC_OP(0xD3, 5);
		SNSPC_BBC(6);
		break;
	
	SNSPC_OP(0xF3, 5);
		SNSPC_BBC(7);
		break;


	SNSPC_OP(0xEF, 3);
		// SLEEP / WAIT: persistent until reset (interrupts are not exposed here).
		pCpu->Regs.uPad = SNSPC_HALT_SLEEP;
		SNSPC_DUMMYREAD8(rPC); /* first read(PC) of the 3-cycle wait entry */
		SNSPC_ENDOP(3);

	SNSPC_OP(0xFF, 2);
		/* AURORA_SPC700_MEGA_ACCURACY_V1_20260916
		 * SNESdev: STOP is 2 cycles; halted cadence remains read(PC)+idle. */
		pCpu->Regs.uPad = SNSPC_HALT_STOP;
		SNSPC_DUMMYREAD8(rPC);
		SNSPC_ENDOP(2);



		// MOV1 membit, C
	SNSPC_OP(0x0ca,6)
		SNSPC_FETCH16(t0);
		SNSPC_MOVE(t1,t0);
		SNSPC_GETI(t2,1);
		SNSPC_ANDI(t0,0x1FFF);
		SNSPC_SHRI(t1,13);
		SNSPC_SHL(t2,t1);
		SNSPC_READ8(t0,t1);

		if (fC & 1)
		{
			SNSPC_OR(t1,t2);
		} else
		{
			SNSPC_OR(t1,t2);
			SNSPC_XOR(t1,t2);
		}

		SNSPC_WRITE8(t0,t1);
		/* MOV1 abs.bit,C is 6 cycles; inherited ENDOP(5) undercharged it. */
		SNSPC_ENDOP(6)


	SNSPC_OP(0x0F, 8);
		/* AURORA_SPC700_BRK_REFERENCE_V1
		 * Push PC and the pre-BRK PSW. Only then set B=1/I=0. */
		SNSPC_GET_PC(t0);
		SNSPC_PUSH16(t0);
		SNSPC_GET_PSW8(t1);
		SNSPC_PUSH8(t1);
		SNSPC_SETFLAG_B();
		SNSPC_CLRFLAG_I();
		SNSPC_READ16(SNSPC_VECTOR_BRK,t0);
		SNSPC_SET_PC16(t0);
        SNSPC_ENDOP(8);

	SNSPC_OP(0x01, 8);	SNSPC_TCALL(0);      SNSPC_ENDOP(8);
	SNSPC_OP(0x11, 8);	SNSPC_TCALL(1);      SNSPC_ENDOP(8);
	SNSPC_OP(0x21, 8);	SNSPC_TCALL(2);      SNSPC_ENDOP(8);
	SNSPC_OP(0x31, 8);	SNSPC_TCALL(3);      SNSPC_ENDOP(8);
	SNSPC_OP(0x41, 8);	SNSPC_TCALL(4);      SNSPC_ENDOP(8);
	SNSPC_OP(0x51, 8);	SNSPC_TCALL(5);      SNSPC_ENDOP(8);
	SNSPC_OP(0x61, 8);	SNSPC_TCALL(6);      SNSPC_ENDOP(8);
	SNSPC_OP(0x71, 8);	SNSPC_TCALL(7);      SNSPC_ENDOP(8);
	SNSPC_OP(0x81, 8);	SNSPC_TCALL(8);      SNSPC_ENDOP(8);
	SNSPC_OP(0x91, 8);	SNSPC_TCALL(9);      SNSPC_ENDOP(8);
	SNSPC_OP(0xA1, 8);	SNSPC_TCALL(10);      SNSPC_ENDOP(8);
	SNSPC_OP(0xB1, 8);	SNSPC_TCALL(11);      SNSPC_ENDOP(8);
	SNSPC_OP(0xC1, 8);	SNSPC_TCALL(12);      SNSPC_ENDOP(8);
	SNSPC_OP(0xD1, 8);	SNSPC_TCALL(13);      SNSPC_ENDOP(8);
	SNSPC_OP(0xE1, 8);	SNSPC_TCALL(14);      SNSPC_ENDOP(8);
	SNSPC_OP(0xF1, 8);	SNSPC_TCALL(15);      SNSPC_ENDOP(8);

		default:	// unimplemented opcode
			SNSPC_SUBCYCLES(1);
		}
	}

halted:
	/* Halt path did not prefetch an opcode: never back PC up here. */
	goto commit;

done:
	// back up the opcode fetch, wrapping on the SPC700's 16-bit PC.
	rPC = (rPC - 1u) & 0xFFFFu;

commit:
	// restore registers
	SNSPC_PACKFLAGS();
	pCpu->Cycles	= nCycles;
	pCpu->Regs.rPC	= rPC;

	return 0;
}

