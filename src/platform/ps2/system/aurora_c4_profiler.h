
#ifndef AURORA_C4_PROFILER_H
#define AURORA_C4_PROFILER_H

/* AURORA_C4_PROFILER_V8_20261003
 * AURORA_C4_AUDIO_COST_V8_2_20261004
 * Block-level EE audio-cost subsets; no new IOP RPCs.
 * Unified comparative profiler for the "Cursed Four":
 *   Donkey Kong Country
 *   Magical Pop'n
 *   Speedy Gonzales - Los Gatos Bandidos
 *   Top Gear / Top Racer
 *
 * V8 adds:
 * - exclusive core buckets from AuroraSnesCostProfiler
 * - SyncPPU reason/cycle attribution
 * - raster-MMIO catch-up timing
 * - PPU write histogram split by CPU / HDMA / MDMA
 * - per-HDMA-channel active/transfer/bytes/cycles/config
 *
 * All four games use exactly the same probes and report schema.
 * AURORA_C4_PROFILER=0 removes C4 callsites at preprocessing time.
 */
#include "types.h"

#ifndef AURORA_C4_PROFILER
#define AURORA_C4_PROFILER 0
#endif

#if AURORA_C4_PROFILER
#ifdef __cplusplus
extern "C" {
#endif

enum AuroraC4SyncPpuReasonE
{
    AURORA_C4_SYNC_PPU_READ = 0,
    AURORA_C4_SYNC_PPU_WRITE_QUEUE_FULL,
    AURORA_C4_SYNC_PPU_WRITE_DIRECT,
    AURORA_C4_SYNC_MDMA_PRE,
    AURORA_C4_SYNC_MIDFRAME,
    AURORA_C4_SYNC_EVERYLINE,
    AURORA_C4_SYNC_VISIBLE_END,
    AURORA_C4_SYNC_FRAME_FINAL,
    AURORA_C4_SYNC_REASON_COUNT
};

enum AuroraC4PpuWriteSourceE
{
    AURORA_C4_PPUWRITE_CPU = 0,
    AURORA_C4_PPUWRITE_HDMA,
    AURORA_C4_PPUWRITE_MDMA,
    AURORA_C4_PPUWRITE_SOURCE_COUNT
};

enum
{
    AURORA_C4_PPU_REG_COUNT = 64,
    AURORA_C4_HDMA_CHANNELS = 8,
    AURORA_C4_HDMA_MODES = 8
};

typedef struct AuroraC4GsDiagT
{
    Uint32 frames;
    Uint32 lines;
    Uint32 syncCalls;
    Uint32 syncCycles;
    Uint32 copyCycles;
    Uint32 kickCycles;
    Uint32 copyBytes;
    Uint32 paletteUploads;
    Uint32 intensityLines;
    Uint32 directMainLines;
} AuroraC4GsDiagT;

typedef struct AuroraC4AudioDiagT
{
    Uint64 availableCycles;
    Uint64 playCycles;
    Uint64 enqueuedFrames;
    Uint64 requestedFrames;
    Uint64 sentFrames;
    Uint64 droppedFrames;
    Uint64 queueSum;
    Uint32 availableCalls;
    Uint32 playCalls;
    Uint32 drainCalls;
    Uint32 asyncStartCalls;
    Uint32 shortWrites;
    Uint32 queueObs;
    Uint32 queueMin;
    Uint32 queueMax;
    Uint32 queueCurrent;

    /* AURORA_C4_AUDIO_COST_V8_2_20261004 */
    Uint64 resampleCycles;
    Uint64 resampleInFrames;
    Uint64 resampleOutFrames;
    Uint64 packCycles;
    Uint64 packFrames;
    Uint64 availableFramesSum;
    Uint32 resampleCalls;
    Uint32 packCalls;
    Uint32 availableFrameObs;
    Uint32 availableFramesMin;
    Uint32 availableFramesMax;
} AuroraC4AudioDiagT;

extern Bool g_AuroraC4ProfilerActive;

void AuroraC4ProfilerConfigureGame(Uint32 crc32, const char *title);
void AuroraC4ProfilerCoreFrameBegin(Uint32 frameNo, Bool videoTarget);
void AuroraC4ProfilerCoreFrameEnd(Uint32 frameNo);

void AuroraC4ProfilerHostTickBegin(Bool gameplay);
void AuroraC4ProfilerHostCoreBegin(void);
void AuroraC4ProfilerHostCoreEnd(void);
void AuroraC4ProfilerHostRenderBegin(void);
void AuroraC4ProfilerHostRenderEnd(void);
void AuroraC4ProfilerHostTickEnd(void);

void AuroraC4ProfilerGpFlush(Uint32 cycles);
void AuroraC4ProfilerVBlank(Uint32 cycles);
void AuroraC4ProfilerPostAudio(Uint32 cycles, Bool presentationSkipped);
void AuroraC4ProfilerFrameskipDecision(Bool enabled, Bool allowed, Bool skipped);

void AuroraC4ProfilerPpuLine(
    Uint8 mode, Bool mosaic, Bool windowed, Bool colorMath,
    Bool hiresMode56, Bool pseudoHires);
void AuroraC4ProfilerObjLine(
    Uint32 selectedObjs, Uint32 potentialSlivers, Uint32 fetchedSlivers);
void AuroraC4ProfilerHirq(Int32 line);

void AuroraC4ProfilerSyncPpu(Uint32 reason, Uint32 cycles);
void AuroraC4ProfilerRasterCatchup(Uint32 cycles, Bool hdmaRan);

/* AURORA_C4_CHR_SPLIT_PROFILER_V8_3_20261004
 * Raw ASM-kernel timing. Timer closes before bookkeeping, so recorded cycles
 * exclude the diagnostic accumulation itself. */
enum AuroraC4ChrKernelE
{
    AURORA_C4_CHR2_NOOFFSET = 0,
    AURORA_C4_CHR4_NOOFFSET,
    AURORA_C4_CHR2_GENERIC_OFFSET,
    AURORA_C4_CHR4_GENERIC_OFFSET,
    AURORA_C4_CHR_KERNEL_COUNT
};
void AuroraC4ProfilerChrKernel(Uint32 kind, Uint32 cycles, Uint32 tiles);
/* AURORA_C4_CHR4_DATA_MIX_DIAG_V8_3_MIX1_20261004: post-timer CHR4 data-mix diagnostics. */
void AuroraC4ProfilerChr4Mix(
    Uint32 flip0, Uint32 flipH, Uint32 flipV, Uint32 flipHV,
    Uint32 trans0, Uint32 transH, Uint32 transV, Uint32 transHV);

/* V8 hot counters are header-inline: Speedy/Top Gear can issue thousands of
 * PPU/HDMA events per frame, so do not add a function call or a per-channel
 * cycle-counter read to every one of them. CoreFrameBegin resets these arrays;
 * CoreFrameEnd snapshots them into the unified report. */
extern Uint32 g_AuroraC4PpuWritesFrame
    [AURORA_C4_PPUWRITE_SOURCE_COUNT][AURORA_C4_PPU_REG_COUNT];
extern Uint32 g_AuroraC4HdmaActiveFrame[AURORA_C4_HDMA_CHANNELS];
extern Uint32 g_AuroraC4HdmaTransferFrame[AURORA_C4_HDMA_CHANNELS];
extern Uint32 g_AuroraC4HdmaBytesFrame[AURORA_C4_HDMA_CHANNELS];
extern Uint32 g_AuroraC4HdmaModeFrame
    [AURORA_C4_HDMA_CHANNELS][AURORA_C4_HDMA_MODES];
extern Uint32 g_AuroraC4HdmaIndirectFrame[AURORA_C4_HDMA_CHANNELS];
extern Uint32 g_AuroraC4HdmaReverseFrame[AURORA_C4_HDMA_CHANNELS];
extern Uint8 g_AuroraC4HdmaLastDMAPFrame[AURORA_C4_HDMA_CHANNELS];
extern Uint8 g_AuroraC4HdmaLastBBADFrame[AURORA_C4_HDMA_CHANNELS];

static inline void AuroraC4ProfilerPpuWrite(
    Uint32 source, Uint32 reg, Uint32 count)
{
    if (g_AuroraC4ProfilerActive &&
        source < AURORA_C4_PPUWRITE_SOURCE_COUNT &&
        reg < AURORA_C4_PPU_REG_COUNT && count)
        g_AuroraC4PpuWritesFrame[source][reg] += count;
}

static inline void AuroraC4ProfilerPpuWriteBurst(
    Uint32 source, Uint8 bbad, const Uint8 *pattern,
    Uint32 phase, Uint32 count)
{
    if (!g_AuroraC4ProfilerActive ||
        source >= AURORA_C4_PPUWRITE_SOURCE_COUNT ||
        !pattern || !count)
        return;

    Uint32 full = count >> 2;
    Uint32 rem = count & 3u;
    for (Uint32 k = 0; k < 4u; ++k)
    {
        Uint32 idx = (phase + k) & 3u;
        Uint32 n = full + (k < rem ? 1u : 0u);
        Uint32 reg = (Uint32)bbad + (Uint32)pattern[idx];
        if (n && reg < AURORA_C4_PPU_REG_COUNT)
            g_AuroraC4PpuWritesFrame[source][reg] += n;
    }
}

static inline void AuroraC4ProfilerHdmaActive(
    Uint32 channel, Uint8 dmap, Uint8 bbad)
{
    if (!g_AuroraC4ProfilerActive || channel >= AURORA_C4_HDMA_CHANNELS)
        return;
    ++g_AuroraC4HdmaActiveFrame[channel];
    g_AuroraC4HdmaLastDMAPFrame[channel] = dmap;
    g_AuroraC4HdmaLastBBADFrame[channel] = bbad;
}

static inline void AuroraC4ProfilerHdmaTransfer(
    Uint32 channel, Uint8 dmap, Uint8 bbad, Uint32 bytes)
{
    if (!g_AuroraC4ProfilerActive || channel >= AURORA_C4_HDMA_CHANNELS)
        return;
    ++g_AuroraC4HdmaTransferFrame[channel];
    g_AuroraC4HdmaBytesFrame[channel] += bytes;
    ++g_AuroraC4HdmaModeFrame[channel][dmap & 7u];
    if (dmap & 0x40u) ++g_AuroraC4HdmaIndirectFrame[channel];
    if (dmap & 0x80u) ++g_AuroraC4HdmaReverseFrame[channel];
    g_AuroraC4HdmaLastDMAPFrame[channel] = dmap;
    g_AuroraC4HdmaLastBBADFrame[channel] = bbad;
}

void AuroraC4GsDiagReset(void);
void AuroraC4GsDiagRead(AuroraC4GsDiagT *out);
void AuroraC4AudioDiagReset(void);
void AuroraC4AudioDiagRead(AuroraC4AudioDiagT *out);
void AuroraC4AudioDiagResample(Uint32 cycles, Uint32 inFrames, Uint32 outFrames);

void AuroraC4ProfilerMenuOpen(void);
void AuroraC4ProfilerMenuUpdate(void);
void AuroraC4ProfilerCancelPending(void);

#ifdef __cplusplus
}
#endif
#endif /* AURORA_C4_PROFILER */
#endif /* AURORA_C4_PROFILER_H */
