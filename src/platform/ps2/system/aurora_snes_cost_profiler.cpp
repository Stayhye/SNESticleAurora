/* AURORA_SNES_COST_PROFILER_V1_20260920 */
/* AURORA_SNES_PPU_BREAKDOWN_V2_20260920 */
/* AURORA_SNES_PPU_FOCUS_V3_20260920 */
/* AURORA_SNES_V3_NEWPROFILER_FRAMEK_20260920 */
#include "aurora_snes_cost_profiler.h"

#if AURORA_SNES_COST_PROFILER

#include <stdio.h>
#include <string.h>

#include "font.h"
#include "prof.h"

namespace
{

enum
{
    AURORA_SNES_COST_WINDOW_FRAMES = 30,
    AURORA_SNES_COST_STACK_MAX = 16,
    AURORA_SNES_PPU_STACK_MAX = 16,

    AURORA_SNES_PPU_WORK_MAP = 0,
    AURORA_SNES_PPU_WORK_CHR,
    AURORA_SNES_PPU_WORK_MAIN,
    AURORA_SNES_PPU_WORK_SUB,
    AURORA_SNES_PPU_WORK_COUNT
};

struct AuroraSnesCostScopeT
{
    Int32 bucket;
    Uint32 start;
};

static Bool s_enabled = FALSE;
static Bool s_frameActive = FALSE;
static Bool s_haveSnapshot = FALSE;
static Uint32 s_frameStart = 0;

static Uint32 s_frameCycles[AURORA_SNES_COST_BUCKET_COUNT] = { 0,0,0,0,0 };
static AuroraSnesCostScopeT s_stack[AURORA_SNES_COST_STACK_MAX];
static Int32 s_depth = 0;

static Uint32 s_framePpuCycles[AURORA_SNES_PPU_DETAIL_COUNT] = { 0 };
static AuroraSnesCostScopeT s_ppuStack[AURORA_SNES_PPU_STACK_MAX];
static Int32 s_ppuDepth = 0;
static Uint32 s_framePpuWork[AURORA_SNES_PPU_WORK_COUNT] = { 0,0,0,0 };

static Uint64 s_windowTotal = 0;
static Uint64 s_windowBuckets[AURORA_SNES_COST_BUCKET_COUNT] = { 0,0,0,0,0 };
static Uint64 s_windowPpu[AURORA_SNES_PPU_DETAIL_COUNT] = { 0 };
static Uint64 s_windowPpuWork[AURORA_SNES_PPU_WORK_COUNT] = { 0,0,0,0 };
static Uint32 s_windowFrames = 0;

/* Tenths of one percent: 463 == 46.3%. */
static Uint32 s_pct10[AURORA_SNES_COST_BUCKET_COUNT] = { 0,0,0,0,0 };
static Uint32 s_trackedPct10 = 0;
static Uint32 s_ppuPct10[AURORA_SNES_PPU_DETAIL_COUNT] = { 0 };
static Uint32 s_ppuSplitPct10 = 0;
static Uint32 s_focusPct10 = 0;
/* Tenths of 1K host cycles/frame: 1234 == 123.4K. */
static Uint32 s_frameK10 = 0;
static Uint32 s_workPerFrame[AURORA_SNES_PPU_WORK_COUNT] = { 0,0,0,0 };

static void ResetWindow(void)
{
    s_windowTotal = 0;
    memset(s_windowBuckets, 0, sizeof(s_windowBuckets));
    memset(s_windowPpu, 0, sizeof(s_windowPpu));
    memset(s_windowPpuWork, 0, sizeof(s_windowPpuWork));
    s_windowFrames = 0;
}

static Uint32 Percent10(Uint64 part, Uint64 total)
{
    if (!total)
        return 0;

    Uint64 value = (part * 1000u + total / 2u) / total;
    if (value > 1000u)
        value = 1000u;
    return (Uint32)value;
}

static Uint32 CountBgBits(Uint32 mask)
{
    mask &= 0x0Fu;
    return ((mask & 0x01u) ? 1u : 0u) +
           ((mask & 0x02u) ? 1u : 0u) +
           ((mask & 0x04u) ? 1u : 0u) +
           ((mask & 0x08u) ? 1u : 0u);
}

static void DrawCentered(Int32 y, const Char *text)
{
    const Int32 x = 128 - FontGetStrWidth(text) / 2;
    FontColor4f(0.0f, 0.0f, 0.0f, 1.0f);
    FontPrintf(x + 1, y + 1, "%s", text);
    FontColor4f(1.0f, 1.0f, 1.0f, 1.0f);
    FontPrintf(x, y, "%s", text);
}

static void DrawPercentLine(Int32 y, const Char *label, Uint32 pct10)
{
    Char line[48];
    snprintf(line, sizeof(line), "%-11s %3u.%u%%",
             label, (unsigned)(pct10 / 10u), (unsigned)(pct10 % 10u));
    DrawCentered(y, line);
}

/* AURORA_SNES_PROFILER_DEEP_V5_20260920: compact diagnostic pair; presentation only. */
static void DrawPairPercentLine(Int32 y,
                                const Char *a, Uint32 pa,
                                const Char *b, Uint32 pb)
{
    Char line[64];
    snprintf(line, sizeof(line), "%-6s %2u.%u%%  %-6s %2u.%u%%",
             a, (unsigned)(pa / 10u), (unsigned)(pa % 10u),
             b, (unsigned)(pb / 10u), (unsigned)(pb % 10u));
    DrawCentered(y, line);
}

static void AppendPct(Char *dst, Uint32 dstSize, const Char *label, Uint32 pct10)
{
    const Uint32 used = (Uint32)strlen(dst);
    if (used >= dstSize)
        return;
    snprintf(dst + used, dstSize - used, "%s%u.%u",
             label, (unsigned)(pct10 / 10u), (unsigned)(pct10 % 10u));
}

} /* namespace */

Bool AuroraSnesCostProfilerSetEnabled(Bool enabled)
{
    s_enabled = enabled ? TRUE : FALSE;
    s_frameActive = FALSE;
    s_haveSnapshot = FALSE;
    s_depth = 0;
    s_ppuDepth = 0;
    memset(s_frameCycles, 0, sizeof(s_frameCycles));
    memset(s_framePpuCycles, 0, sizeof(s_framePpuCycles));
    memset(s_framePpuWork, 0, sizeof(s_framePpuWork));
    memset(s_pct10, 0, sizeof(s_pct10));
    memset(s_ppuPct10, 0, sizeof(s_ppuPct10));
    memset(s_workPerFrame, 0, sizeof(s_workPerFrame));
    s_trackedPct10 = 0;
    s_ppuSplitPct10 = 0;
    s_focusPct10 = 0;
    s_frameK10 = 0;
    ResetWindow();
    return s_enabled;
}

Bool AuroraSnesCostProfilerIsEnabled(void)
{
    return s_enabled;
}

void AuroraSnesCostProfilerFrameBegin(void)
{
    if (!s_enabled)
        return;

    memset(s_frameCycles, 0, sizeof(s_frameCycles));
    memset(s_framePpuCycles, 0, sizeof(s_framePpuCycles));
    memset(s_framePpuWork, 0, sizeof(s_framePpuWork));
    s_depth = 0;
    s_ppuDepth = 0;
    s_frameStart = ProfCtrGetCycle();
    s_frameActive = TRUE;
}

void AuroraSnesCostProfilerScopeEnter(Int32 bucket)
{
    if (!s_enabled || !s_frameActive || s_depth < 0 ||
        bucket < 0 || bucket >= AURORA_SNES_COST_BUCKET_COUNT)
        return;

    const Uint32 now = ProfCtrGetCycle();
    if (s_depth > 0)
    {
        AuroraSnesCostScopeT &parent = s_stack[s_depth - 1];
        s_frameCycles[parent.bucket] += (Uint32)(now - parent.start);
    }
    if (s_depth >= AURORA_SNES_COST_STACK_MAX)
    {
        s_depth = -1;
        return;
    }
    s_stack[s_depth].bucket = bucket;
    s_stack[s_depth].start = now;
    ++s_depth;
}

void AuroraSnesCostProfilerScopeLeave(Int32 bucket)
{
    if (!s_enabled || !s_frameActive || s_depth < 0)
        return;
    if (s_depth == 0)
    {
        s_depth = -1;
        return;
    }

    const Uint32 now = ProfCtrGetCycle();
    AuroraSnesCostScopeT &scope = s_stack[s_depth - 1];
    if (scope.bucket != bucket)
    {
        s_depth = -1;
        return;
    }
    s_frameCycles[scope.bucket] += (Uint32)(now - scope.start);
    --s_depth;
    if (s_depth > 0)
        s_stack[s_depth - 1].start = now;
}

void AuroraSnesCostProfilerPpuEnter(Int32 bucket)
{
    if (!s_enabled || !s_frameActive || s_ppuDepth < 0 ||
        bucket < 0 || bucket >= AURORA_SNES_PPU_DETAIL_COUNT)
        return;

    const Uint32 now = ProfCtrGetCycle();
    if (s_ppuDepth > 0)
    {
        AuroraSnesCostScopeT &parent = s_ppuStack[s_ppuDepth - 1];
        s_framePpuCycles[parent.bucket] += (Uint32)(now - parent.start);
    }
    if (s_ppuDepth >= AURORA_SNES_PPU_STACK_MAX)
    {
        s_ppuDepth = -1;
        return;
    }
    s_ppuStack[s_ppuDepth].bucket = bucket;
    s_ppuStack[s_ppuDepth].start = now;
    ++s_ppuDepth;
}

void AuroraSnesCostProfilerPpuLeave(Int32 bucket)
{
    if (!s_enabled || !s_frameActive || s_ppuDepth < 0)
        return;
    if (s_ppuDepth == 0)
    {
        s_ppuDepth = -1;
        return;
    }

    const Uint32 now = ProfCtrGetCycle();
    AuroraSnesCostScopeT &scope = s_ppuStack[s_ppuDepth - 1];
    if (scope.bucket != bucket)
    {
        s_ppuDepth = -1;
        return;
    }
    s_framePpuCycles[scope.bucket] += (Uint32)(now - scope.start);
    --s_ppuDepth;
    if (s_ppuDepth > 0)
        s_ppuStack[s_ppuDepth - 1].start = now;
}

void AuroraSnesCostProfilerPpuWork(Uint32 mainMask, Uint32 subMask,
                                   Uint32 mapFetches, Uint32 chrDecodes)
{
    if (!s_enabled || !s_frameActive)
        return;

    s_framePpuWork[AURORA_SNES_PPU_WORK_MAP] += mapFetches;
    s_framePpuWork[AURORA_SNES_PPU_WORK_CHR] += chrDecodes;
    s_framePpuWork[AURORA_SNES_PPU_WORK_MAIN] += CountBgBits(mainMask);
    s_framePpuWork[AURORA_SNES_PPU_WORK_SUB] += CountBgBits(subMask);
}

void AuroraSnesCostProfilerFrameEnd(void)
{
    if (!s_enabled || !s_frameActive)
        return;

    const Uint32 now = ProfCtrGetCycle();
    const Uint32 total = (Uint32)(now - s_frameStart);
    s_frameActive = FALSE;

    /* Fail closed: a mismatched scope invalidates the whole sample. */
    if (s_depth != 0 || s_ppuDepth != 0 || total == 0)
    {
        s_depth = 0;
        s_ppuDepth = 0;
        return;
    }

    s_windowTotal += total;
    for (Int32 i = 0; i < AURORA_SNES_COST_BUCKET_COUNT; ++i)
        s_windowBuckets[i] += s_frameCycles[i];
    for (Int32 i = 0; i < AURORA_SNES_PPU_DETAIL_COUNT; ++i)
        s_windowPpu[i] += s_framePpuCycles[i];
    for (Int32 i = 0; i < AURORA_SNES_PPU_WORK_COUNT; ++i)
        s_windowPpuWork[i] += s_framePpuWork[i];
    ++s_windowFrames;

    if (s_windowFrames >= AURORA_SNES_COST_WINDOW_FRAMES)
    {
        Uint64 tracked = 0;
        Uint64 ppuSplit = 0;
        Uint64 focus = 0;

        for (Int32 i = 0; i < AURORA_SNES_COST_BUCKET_COUNT; ++i)
        {
            tracked += s_windowBuckets[i];
            s_pct10[i] = Percent10(s_windowBuckets[i], s_windowTotal);
        }
        for (Int32 i = 0; i < AURORA_SNES_PPU_DETAIL_COUNT; ++i)
        {
            ppuSplit += s_windowPpu[i];
            s_ppuPct10[i] = Percent10(s_windowPpu[i], s_windowTotal);
        }

        focus += s_windowPpu[AURORA_SNES_PPU_DETAIL_BG_MAP];
        focus += s_windowPpu[AURORA_SNES_PPU_DETAIL_BG_CHR];
        focus += s_windowPpu[AURORA_SNES_PPU_DETAIL_BG_MAIN];
        focus += s_windowPpu[AURORA_SNES_PPU_DETAIL_BG_SUB];
        focus += s_windowPpu[AURORA_SNES_PPU_DETAIL_BG_OTHER];
        focus += s_windowPpu[AURORA_SNES_PPU_DETAIL_COLOR_MASK];
        focus += s_windowPpu[AURORA_SNES_PPU_DETAIL_BLEND];
        focus += s_windowPpu[AURORA_SNES_PPU_DETAIL_COLOR_OTHER];

        for (Int32 i = 0; i < AURORA_SNES_PPU_WORK_COUNT; ++i)
            s_workPerFrame[i] = (Uint32)((s_windowPpuWork[i] +
                s_windowFrames / 2u) / s_windowFrames);

        s_trackedPct10 = Percent10(tracked, s_windowTotal);
        s_ppuSplitPct10 = Percent10(ppuSplit, s_windowTotal);
        s_focusPct10 = Percent10(focus, s_windowTotal);

        /* Average absolute host cost of the same 30 valid emulated
         * frames used for all percentages above.  Keep one decimal
         * place in K-cycles so small improvements remain visible.
         * This intentionally includes profiler overhead; compare only
         * builds using the same profiler configuration. */
        const Uint64 frameKDen = (Uint64)s_windowFrames * 1000u;
        s_frameK10 = (Uint32)((s_windowTotal * 10u + frameKDen / 2u) /
                              frameKDen);
        s_haveSnapshot = TRUE;
        ResetWindow();
    }
}

void AuroraSnesCostProfilerDrawOverlay(void)
{
    if (!s_enabled)
        return;

    FontSelect(2);
    if (!s_haveSnapshot)
    {
        DrawCentered(102, "SNES COST PROFILER V3");
        DrawCentered(114, "collecting 30 frames...");
        return;
    }

    {
        Char line[48];
        snprintf(line, sizeof(line), "FRAME K %u.%u",
                 (unsigned)(s_frameK10 / 10u),
                 (unsigned)(s_frameK10 % 10u));
        DrawCentered(27, line);
    }

    DrawCentered(38, "SNES COST / 30F");
    {
        Char line[96];
        line[0] = 0;
        AppendPct(line, sizeof(line), "CPU ", s_pct10[AURORA_SNES_COST_CPU]);
        AppendPct(line, sizeof(line), "  PPU ", s_pct10[AURORA_SNES_COST_PPU]);
        AppendPct(line, sizeof(line), "  DMA ", s_pct10[AURORA_SNES_COST_RASTER]);
        DrawCentered(49, line);
    }
    {
        Char line[96];
        line[0] = 0;
        AppendPct(line, sizeof(line), "SPC ", s_pct10[AURORA_SNES_COST_SPC]);
        AppendPct(line, sizeof(line), "  DSP ", s_pct10[AURORA_SNES_COST_DSP]);
        AppendPct(line, sizeof(line), "  TRK ", s_trackedPct10);
        DrawCentered(60, line);
    }

    /* AURORA_SNES_PROFILER_DEEP_V5_20260920
     * Deep PPU view. Existing V2/V3 timers only; OBJ scopes were relabeled,
     * not added, so profiler call count is unchanged. */
    const Uint32 objPct10 =
        s_ppuPct10[AURORA_SNES_PPU_DETAIL_OBJ_UPDATE] +
        s_ppuPct10[AURORA_SNES_PPU_DETAIL_OBJ_FETCH] +
        s_ppuPct10[AURORA_SNES_PPU_DETAIL_OBJ_MAIN] +
        s_ppuPct10[AURORA_SNES_PPU_DETAIL_OBJ_SUB];

    DrawCentered(75, "PPU DEEP / FRAME %");
    DrawPairPercentLine(86,
        "SYNC", s_ppuPct10[AURORA_SNES_PPU_DETAIL_SYNC],
        "REND", s_ppuPct10[AURORA_SNES_PPU_DETAIL_RENDER]);
    DrawPairPercentLine(97,
        "PREP", s_ppuPct10[AURORA_SNES_PPU_DETAIL_PREP],
        "RASTER", s_ppuPct10[AURORA_SNES_PPU_DETAIL_RASTER]);

    DrawPercentLine(108, "OBJ TOT", objPct10);
    DrawPairPercentLine(119,
        "UPD", s_ppuPct10[AURORA_SNES_PPU_DETAIL_OBJ_UPDATE],
        "FETCH", s_ppuPct10[AURORA_SNES_PPU_DETAIL_OBJ_FETCH]);
    DrawPairPercentLine(130,
        "MAIN", s_ppuPct10[AURORA_SNES_PPU_DETAIL_OBJ_MAIN],
        "SUB", s_ppuPct10[AURORA_SNES_PPU_DETAIL_OBJ_SUB]);

    DrawPairPercentLine(141,
        "MODE7", s_ppuPct10[AURORA_SNES_PPU_DETAIL_MODE7],
        "BGCHR", s_ppuPct10[AURORA_SNES_PPU_DETAIL_BG_CHR]);
    DrawPairPercentLine(152,
        "BGMAP", s_ppuPct10[AURORA_SNES_PPU_DETAIL_BG_MAP],
        "BGMAIN", s_ppuPct10[AURORA_SNES_PPU_DETAIL_BG_MAIN]);
    DrawPairPercentLine(163,
        "BGSUB", s_ppuPct10[AURORA_SNES_PPU_DETAIL_BG_SUB],
        "BGOTH", s_ppuPct10[AURORA_SNES_PPU_DETAIL_BG_OTHER]);

    DrawPairPercentLine(174,
        "CMASK", s_ppuPct10[AURORA_SNES_PPU_DETAIL_COLOR_MASK],
        "BLEND", s_ppuPct10[AURORA_SNES_PPU_DETAIL_BLEND]);
    DrawPairPercentLine(185,
        "COTH", s_ppuPct10[AURORA_SNES_PPU_DETAIL_COLOR_OTHER],
        "BG+COL", s_focusPct10);
    DrawPercentLine(196, "PPU SPLIT", s_ppuSplitPct10);

    {
        Char line[64];
        snprintf(line, sizeof(line), "WORK/F MAP %u  CHR %u",
                 (unsigned)s_workPerFrame[AURORA_SNES_PPU_WORK_MAP],
                 (unsigned)s_workPerFrame[AURORA_SNES_PPU_WORK_CHR]);
        DrawCentered(207, line);
    }
    {
        Char line[64];
        snprintf(line, sizeof(line), "LAY/F MAIN %u  SUB %u",
                 (unsigned)s_workPerFrame[AURORA_SNES_PPU_WORK_MAIN],
                 (unsigned)s_workPerFrame[AURORA_SNES_PPU_WORK_SUB]);
        DrawCentered(218, line);
    }
}

#endif /* AURORA_SNES_COST_PROFILER */
