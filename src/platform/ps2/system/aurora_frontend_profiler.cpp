/* AURORA_FRONTEND_PROFILER_V4_6_FS_RECOVERY_AUDIO_20261004
 *
 * All-core Aurora/frontend diagnostic profiler.
 * This TU exists only when AURORA_FRONTEND_PROFILER=1.
 *
 * v4.4 fixes v3 attribution:
 * - InputPoll, snapshot/prepare, and frontend input logic are distinct;
 * - audio drain is actually instrumented;
 * - process work around the core and post-render work are separated;
 * - frontend spikes are reported separately instead of poisoning the
 *   continuous-cost baseline.
 */
#include "platform/ps2/system/aurora_snes_cost_profiler.h" /* AURORA_SNES_DEEP_FP_COST_INCLUDE_V3_14_6_20261006 */
#include "aurora_frontend_profiler.h"

#if AURORA_FRONTEND_PROFILER

#include <stdio.h>
#include <string.h>
#include <libcdvd.h>
#include <osd_config.h>

#include "prof.h"
#include "mainloop_bgm.h"
#include "mainloop_ui.h"
#include "mainloop_shared.h" /* AURORA_PROFILER_ROM_IDENTITY_V3_1_20261005 */

namespace
{

/* Same NTSC host-frame budget used by the C4 diagnostics.
 * Report classification only: this mutates no timing or scheduler state. */
static const Uint32 AURORA_FP_FRONTEND_SPIKE_BUDGET = 2460058u;

enum SafeFrameskipClassE
{
    AURORA_FP_FS_UNKNOWN = 0,
    AURORA_FP_FS_OFF,
    AURORA_FP_FS_ON_BLOCKED,
    AURORA_FP_FS_ON_PRESENTED,
    AURORA_FP_FS_ON_SKIPPED,
    AURORA_FP_FS_CLASS_COUNT
};

enum SpanE
{
    SPAN_INPUT_POLL = 0,
    SPAN_INPUT_SNAPSHOT,
    SPAN_INPUT_LOGIC,
    SPAN_PROCESS_PRE_CORE,
    SPAN_CORE,
    SPAN_PROCESS_POST_CORE,
    SPAN_UPLOAD,
    SPAN_RENDER,
    SPAN_RENDER_SETUP,
    SPAN_GAME_DRAW,
    SPAN_UI,
    SPAN_GP_FLUSH,
    SPAN_VBLANK,
    SPAN_AUDIO_DRAIN,
    SPAN_AUDIO_CONVERT,
    SPAN_AUDIO_FLUSH,
    /* AURORA_SNES_CORE_PROFILER_V3_6_20261005: nested drill-down spans. */
    SPAN_SNES_MDMA,
    SPAN_SNES_HDMA_SETUP,
    SPAN_SNES_HDMA_DATA,
    SPAN_SNES_SPC,
    SPAN_SNES_DSP_TOTAL,
    SPAN_SNES_DSP_SYNC,
    SPAN_SNES_DSP_NOISE,
    SPAN_SNES_DSP_VOICES,
    /* AURORA_SNES_DSP_VOICES_PROFILER_V3_5_1_20261005 */
    SPAN_SNES_DSP_ENVELOPE,
    SPAN_SNES_DSP_SAMPLE_NORMAL,
    SPAN_SNES_DSP_SAMPLE_PMON,
    SPAN_SNES_DSP_BRR_DECODE,
    SPAN_SNES_DSP_PITCHMOD_FEEDER,
    SPAN_SNES_DSP_MIX_DRY,
    SPAN_SNES_DSP_MIX_ECHO,
    SPAN_SNES_DSP_ECHO,
    SPAN_SNES_DSP_FINAL,
    SPAN_POST_RENDER,
    SPAN_COUNT
};

struct FrameT
{
    Uint32 start;
    Uint32 state;
    Uint32 core;
    Uint32 cycles[SPAN_COUNT];
    Uint32 spanStart[SPAN_COUNT];
    Bool spanOpen[SPAN_COUNT];
    Bool fsSeen;
    Bool fsEnabled;
    Bool fsAllowed;
    Bool fsSkipped;

    Bool fsDetailSeen;
    Uint32 fsPeriod;
    Int32 fsDiff;
    Uint32 fsLastPresentedWork;
    Bool fsMeaningfulDebt;
    Bool fsMeasuredOverrun;
    Bool fsUnlimited;
    Bool fsDecisionSkipped;
    Bool fsRecoveryPendingAtDecision;

    Bool fsRecoverySeen;
    Uint32 fsRecoveryWork;
    Bool fsRecoveryPendingAtPresent;
    Bool fsRecoveryRebased;

    Uint32 audioConvertInputFrames;
    Uint32 audioFlushFrames;
    Uint32 audioDrainFrames;
    Uint32 spcCalls;
    Uint32 spcRequestedCycles;
    Uint32 spcConsumedCycles;
    Uint32 dspMixCalls;
    Uint32 dspChunks;
    Uint32 dspSamples;

    /* AURORA_SNES_CORE_PROFILER_V3_6_20261005 */
    Uint32 snesCpuBudgetCalls;
    Uint64 snesCpuBudgetCycles;
    Uint32 snesPpuSyncCalls;
    Uint32 snesCostCycles[5];
    Int32 snesCostDepth;
    Int32 snesCostStackBucket[16];
    Uint32 snesCostStackStart[16];
    Uint32 snesPpuCycles[18];
    Int32 snesPpuDepth;
    Int32 snesPpuStackBucket[16];
    Uint32 snesPpuStackStart[16];
    Bool snesProfileInvalid;
    Uint32 snesPpuRenderedLines;
    Uint32 snesPpuModeLines[8];
    Uint32 snesPpuBgMapFetches;
    Uint32 snesPpuBgChrDecodes;
    Uint32 snesPpuMainBgLayers;
    Uint32 snesPpuSubBgLayers;
    Uint32 snesPpuObjOamRefs;
    Uint32 snesPpuObjTiles;
    Uint32 snesPpuObjEnabledLines;
    Uint32 snesPpuObjRangeOverLines;
    Uint32 snesPpuObjTimeOverLines;
    Uint32 snesPpuWindowLines;
    Uint32 snesPpuColorMathLines;
    Uint32 snesPpuSubscreenLines;
    Uint32 snesPpuHiresLines;
    Uint32 snesPpuDirectColorLines;

    /* AURORA_SNES_BGCHR_PROFILER_V3_7_20261005
     * [BG0..3][depth: 2,4,8,other]. */
    Uint32 snesBgChrCalls[4][4];
    Uint32 snesBgChrTiles[4][4];
    Uint32 snesBgChrCycles[4][4];
    Uint32 snesBgChrDecodeCycles[4][4];
    /* flags: offset, mosaic, fine-X != 0, hires subscreen. Overlapping. */
    Uint32 snesBgChrFlagCalls[4];
    Uint32 snesBgChrFlagTiles[4];
    Uint32 snesBgChrFlagCycles[4];
    Uint32 snesBgChrFlagDecodeCycles[4];
    Uint32 snesBgChrMeasuredCalls;
    Uint32 snesBgChrMeasuredTiles;
    Uint32 snesBgChrMeasuredCycles;
    Uint32 snesBgChrMeasuredDecodeCycles;
    Uint32 snesBgChrBadRecords;

    /* AURORA_SNES_DEEP_OPT_V3_14_20261005 */
    Uint32 snesDeepCalls[68];
    Uint32 snesDeepUnits[68];
    Uint32 snesDeepCycles[68];

    /* AURORA_SNES_DSP_VOICES_PROFILER_V3_5_1_20261005 */
    Uint32 dspVoiceEnvelopeCalls;
    Uint32 dspVoiceEnvelopeActive;
    Uint32 dspVoiceSampleNormalCalls;
    Uint32 dspVoiceSamplePmonCalls;
    Uint32 dspVoiceBrrDecodes;
    Uint32 dspVoicePitchmodFeeders;
    Uint32 dspVoiceMixCalls;
    Uint32 dspVoiceMixDryCalls;
    Uint32 dspVoiceMixEchoCalls;
    Uint32 dspVoiceNoiseMixes;
    Uint32 dspVoiceSilentFastpathHits;

    Uint64 dspVoiceSampleNormalSamples;
    Uint64 dspVoiceSamplePmonSamples;
    Uint64 dspVoiceMixSamples;
    Uint64 dspVoiceMixDrySamples;
    Uint64 dspVoiceMixEchoSamples;

    Uint32 dspVoiceGainZeroVolume;
    Uint32 dspVoiceGainEnvelopeZero;
    Uint32 dspVoiceGainVeryLow;
    Uint32 dspVoiceGainLow;
    Uint32 dspVoiceGainNormal;
    Uint32 dspVoiceLowGainPmon;
    Uint32 dspVoiceLowGainEcho;
    Uint32 dspVoiceLowGainFeedsPmon;

    Bool open;
};

struct DerivedT
{
    Uint32 frontend;
    Uint32 renderNoVblank;
    Uint32 renderNoVblankAudio;
    Uint32 otherFrontend;
};

struct BucketT
{
    Uint64 frames;
    Uint64 tick;
    Uint64 span[SPAN_COUNT];
    Uint64 frontendExCoreVblank;
    Uint64 renderExVblank;
    Uint64 renderExVblankAudio;
    Uint64 otherFrontend;

    Uint64 audioConvertInputFrames;
    Uint64 audioFlushFrames;
    Uint64 audioDrainFrames;
    Uint64 spcCalls;
    Uint64 spcRequestedCycles;
    Uint64 spcConsumedCycles;
    Uint64 dspMixCalls;
    Uint64 dspChunks;
    Uint64 dspSamples;

    /* AURORA_SNES_CORE_PROFILER_V3_6_20261005 */
    Uint64 snesCpuBudgetCalls;
    Uint64 snesCpuBudgetCycles;
    Uint64 snesPpuSyncCalls;
    Uint64 snesCostCycles[5];
    Uint64 snesPpuCycles[18];
    Uint64 snesProfileInvalidFrames;
    Uint64 snesPpuRenderedLines;
    Uint64 snesPpuModeLines[8];
    Uint64 snesPpuBgMapFetches;
    Uint64 snesPpuBgChrDecodes;
    Uint64 snesPpuMainBgLayers;
    Uint64 snesPpuSubBgLayers;
    Uint64 snesPpuObjOamRefs;
    Uint64 snesPpuObjTiles;
    Uint64 snesPpuObjEnabledLines;
    Uint64 snesPpuObjRangeOverLines;
    Uint64 snesPpuObjTimeOverLines;
    Uint64 snesPpuWindowLines;
    Uint64 snesPpuColorMathLines;
    Uint64 snesPpuSubscreenLines;
    Uint64 snesPpuHiresLines;
    Uint64 snesPpuDirectColorLines;

    /* AURORA_SNES_BGCHR_PROFILER_V3_7_20261005 */
    Uint64 snesBgChrCalls[4][4];
    Uint64 snesBgChrTiles[4][4];
    Uint64 snesBgChrCycles[4][4];
    Uint64 snesBgChrDecodeCycles[4][4];
    Uint64 snesBgChrFlagCalls[4];
    Uint64 snesBgChrFlagTiles[4];
    Uint64 snesBgChrFlagCycles[4];
    Uint64 snesBgChrFlagDecodeCycles[4];
    Uint64 snesBgChrMeasuredCalls;
    Uint64 snesBgChrMeasuredTiles;
    Uint64 snesBgChrMeasuredCycles;
    Uint64 snesBgChrMeasuredDecodeCycles;
    Uint64 snesBgChrBadRecords;

    /* AURORA_SNES_DEEP_OPT_V3_14_20261005 */
    Uint64 snesDeepCalls[68];
    Uint64 snesDeepUnits[68];
    Uint64 snesDeepCycles[68];

    Uint64 dspVoiceEnvelopeCalls;
    Uint64 dspVoiceEnvelopeActive;
    Uint64 dspVoiceSampleNormalCalls;
    Uint64 dspVoiceSamplePmonCalls;
    Uint64 dspVoiceBrrDecodes;
    Uint64 dspVoicePitchmodFeeders;
    Uint64 dspVoiceMixCalls;
    Uint64 dspVoiceMixDryCalls;
    Uint64 dspVoiceMixEchoCalls;
    Uint64 dspVoiceNoiseMixes;
    Uint64 dspVoiceSilentFastpathHits;

    Uint64 dspVoiceSampleNormalSamples;
    Uint64 dspVoiceSamplePmonSamples;
    Uint64 dspVoiceMixSamples;
    Uint64 dspVoiceMixDrySamples;
    Uint64 dspVoiceMixEchoSamples;

    Uint64 dspVoiceGainZeroVolume;
    Uint64 dspVoiceGainEnvelopeZero;
    Uint64 dspVoiceGainVeryLow;
    Uint64 dspVoiceGainLow;
    Uint64 dspVoiceGainNormal;
    Uint64 dspVoiceLowGainPmon;
    Uint64 dspVoiceLowGainEcho;
    Uint64 dspVoiceLowGainFeedsPmon;
};

struct WorstT
{
    Uint32 serial;
    Uint32 state;
    Uint32 core;
    Uint32 tick;
    Uint32 frontend;
    Uint32 coreCycles;
    Uint32 vblank;
    Uint32 inputPoll;
    Uint32 inputSnapshot;
    Uint32 inputLogic;
    Uint32 preCore;
    Uint32 postCore;
    Uint32 upload;
    Uint32 renderHost;
    Uint32 audioDrain;
    Uint32 postRender;
    Uint32 other;
    Uint32 fsClass;
    Bool spike;
};

struct FsDiagT
{
    Uint64 decisions;
    Uint64 debtDecisions;
    Uint64 overrunDecisions;
    Uint64 unlimitedDecisions;
    Uint64 skippedDecisions;
    Uint64 recoveryPendingDecisions;
    Uint64 recoveryPresented;
    Uint64 recoveryRebases;
    Uint64 periodSum;
    Uint64 latenessSum;
    Uint64 latenessSamples;
    Uint64 lastPresentedWorkSum;
    Uint64 lastPresentedWorkSamples;
    Uint64 recoveryWorkSum;
};

struct StatsT
{
    BucketT stateAll[AURORA_FP_STATE_COUNT];
    BucketT stateNormal[AURORA_FP_STATE_COUNT];
    BucketT stateSpike[AURORA_FP_STATE_COUNT];

    BucketT coreAll[AURORA_FP_CORE_COUNT];
    BucketT coreNormal[AURORA_FP_CORE_COUNT];
    BucketT coreSpike[AURORA_FP_CORE_COUNT];

    BucketT fsAll[AURORA_FP_CORE_COUNT][AURORA_FP_FS_CLASS_COUNT];
    BucketT fsNormal[AURORA_FP_CORE_COUNT][AURORA_FP_FS_CLASS_COUNT];
    BucketT fsSpike[AURORA_FP_CORE_COUNT][AURORA_FP_FS_CLASS_COUNT];
    FsDiagT fsDiag[AURORA_FP_CORE_COUNT];

    WorstT worst[16];

    Uint64 totalFrames;
    Uint64 normalFrames;
    Uint64 spikeFrames;
    Uint64 gameplayFrames;
    Uint64 menuFrames;
    Uint64 idleFrames;
};

struct StampT
{
    Uint32 year, month, day, hour, minute, second;
};

/* AURORA_PROFILER_ROM_IDENTITY_V3_1_20261005
 * Snapshot metadata is captured before live stats are reset, while the
 * current game object is still loaded. It is diagnostic-only. */
struct MetadataT
{
    Char gameTitle[256];
    Char romFilename[256];
    Char region[32];
    Uint32 core;
    Uint32 runtimeCRC32;
    Bool runtimeCRCValid;
};

struct SnapshotT
{
    StatsT stats;
    StampT stamp;
    MetadataT meta;
    Uint32 serial;
};

static FrameT s_frame;
static StatsT s_live;
static SnapshotT s_pending;
static Uint32 s_hostSerial = 0;
static Uint32 s_dumpSerial = 0;
static Bool s_captureArmed = FALSE;
static Uint32 s_menuFramesNeeded = 0;
static Bool s_writePending = FALSE;
static Bool s_initialIdleDumpDone = FALSE;

static Uint32 Bcd(Uint8 v)
{
    return (((v >> 4) & 15u) * 10u) + (v & 15u);
}

static StampT CaptureStamp(void)
{
    StampT s;
    memset(&s, 0, sizeof(s));
    sceCdCLOCK rtc;
    if (!sceCdReadClock(&rtc))
        return s;
    configConvertToLocalTime(&rtc);
    s.year = 2000u + Bcd(rtc.year);
    s.month = Bcd(rtc.month);
    s.day = Bcd(rtc.day);
    s.hour = Bcd(rtc.hour);
    s.minute = Bcd(rtc.minute);
    s.second = Bcd(rtc.second);
    return s;
}

static Uint64 Avg(Uint64 total, Uint64 n)
{
    return n ? ((total + n / 2u) / n) : 0u;
}

/* AURORA_SNES_CORE_PROFILER_V3_6_20261005 */
static Uint64 SnesScaledDiv(Uint64 n, Uint64 d, Uint64 scale)
{
    return d ? ((n * scale + d / 2u) / d) : 0u;
}

static Uint32 SnesBgBits(Uint32 mask)
{
    mask &= 0x0Fu;
    return ((mask & 1u) ? 1u : 0u) +
           ((mask & 2u) ? 1u : 0u) +
           ((mask & 4u) ? 1u : 0u) +
           ((mask & 8u) ? 1u : 0u);
}

static Uint32 SubSat(Uint32 a, Uint32 b)
{
    return a > b ? a - b : 0u;
}

static const char *StateName(Uint32 s)
{
    switch (s)
    {
        case AURORA_FP_GAMEPLAY: return "GAMEPLAY";
        case AURORA_FP_MENU_WITH_CORE: return "MENU_WITH_CORE";
        case AURORA_FP_IDLE_UI: return "IDLE_UI";
        default: return "UNKNOWN";
    }
}

static const char *CoreName(Uint32 c)
{
    switch (c)
    {
        case AURORA_FP_CORE_SNES: return "SNES";
        case AURORA_FP_CORE_NES: return "NES";
        case AURORA_FP_CORE_FDS: return "FDS";
        case AURORA_FP_CORE_SEGA: return "SEGA";
        case AURORA_FP_CORE_PCE: return "PCE";
        case AURORA_FP_CORE_GB: return "GB";
        case AURORA_FP_CORE_GBA: return "GBA";
        default: return "NONE";
    }
}

/* AURORA_PROFILER_ROM_IDENTITY_V3_1_20261005 */
static void MetadataCopy(Char *dst, Uint32 cap, const char *src)
{
    if (!dst || !cap)
        return;

    Uint32 out = 0;
    if (src)
    {
        while (*src && out + 1u < cap)
        {
            unsigned char c = (unsigned char)*src++;
            if (c < 0x20u || c == 0x7Fu)
                c = (unsigned char)'?';
            dst[out++] = (Char)c;
        }
    }
    dst[out] = 0;
}

static Uint32 SnapshotCore(const StatsT &s)
{
    Uint32 best = AURORA_FP_CORE_NONE;
    Uint64 bestFrames = 0;
    for (Uint32 core = 1; core < AURORA_FP_CORE_COUNT; ++core)
    {
        if (s.coreAll[core].frames > bestFrames)
        {
            bestFrames = s.coreAll[core].frames;
            best = core;
        }
    }
    return best;
}

static MetadataT CaptureMetadata(const StatsT &s)
{
    MetadataT m;
    memset(&m, 0, sizeof(m));
    m.core = SnapshotCore(s);
    MetadataCopy(m.gameTitle, sizeof(m.gameTitle), "UNKNOWN");
    MetadataCopy(m.romFilename, sizeof(m.romFilename),
                 _RomName[0] ? _RomName : "NONE");
    MetadataCopy(m.region, sizeof(m.region), "UNKNOWN");

    switch (m.core)
    {
        case AURORA_FP_CORE_SNES:
            if (_pSnesRom && _pSnesRom->IsLoaded())
            {
                const char *title = _pSnesRom->GetRomTitle();
                MetadataCopy(m.gameTitle, sizeof(m.gameTitle),
                             (title && *title) ? title :
                             (_RomName[0] ? _RomName : "UNKNOWN"));
                m.runtimeCRC32 = _pSnesRom->GetRuntimeCRC32();
                m.runtimeCRCValid = TRUE;
                MetadataCopy(m.region, sizeof(m.region),
                    _pSnesRom->m_eVideoType == SNROM_VIDEO_PAL
                        ? "PAL" : "NTSC");
            }
            break;

        case AURORA_FP_CORE_NES:
            if (_pNesRom && _pNesRom->IsLoaded())
            {
                const char *title = _pNesRom->GetRomTitle();
                MetadataCopy(m.gameTitle, sizeof(m.gameTitle),
                             (title && *title) ? title :
                             (_RomName[0] ? _RomName : "UNKNOWN"));
            }
            break;

        case AURORA_FP_CORE_FDS:
            if (_pNesFDSDisk && _pNesFDSDisk->IsLoaded())
                MetadataCopy(m.gameTitle, sizeof(m.gameTitle),
                             _pNesFDSDisk->GetRomTitle());
            break;

        case AURORA_FP_CORE_SEGA:
            if (_pSegaRom && _pSegaRom->IsLoaded())
            {
                const char *title = _pSegaRom->GetRomTitle();
                MetadataCopy(m.gameTitle, sizeof(m.gameTitle),
                             (title && *title) ? title :
                             (_RomName[0] ? _RomName : "UNKNOWN"));
            }
            break;

        case AURORA_FP_CORE_PCE:
            if (_pPceRom && _pPceRom->IsLoaded())
            {
                const char *title = _pPceRom->GetRomTitle();
                MetadataCopy(m.gameTitle, sizeof(m.gameTitle),
                             (title && *title) ? title :
                             (_RomName[0] ? _RomName : "UNKNOWN"));
            }
            break;

        case AURORA_FP_CORE_GB:
            MetadataCopy(m.gameTitle, sizeof(m.gameTitle),
                         _RomName[0] ? _RomName : "UNKNOWN");
            if (_pGb && _pGb->IsGameLoaded())
            {
                m.runtimeCRC32 = _pGb->GetGameCRC();
                m.runtimeCRCValid = TRUE;
            }
            break;

        case AURORA_FP_CORE_GBA:
            MetadataCopy(m.gameTitle, sizeof(m.gameTitle),
                         _RomName[0] ? _RomName : "UNKNOWN");
            if (_pGba && _pGba->IsGameLoaded())
            {
                m.runtimeCRC32 = _pGba->GetGameCRC();
                m.runtimeCRCValid = TRUE;
            }
            break;

        default:
            MetadataCopy(m.gameTitle, sizeof(m.gameTitle), "NONE");
            MetadataCopy(m.romFilename, sizeof(m.romFilename), "NONE");
            break;
    }

    return m;
}


static const char *SafeFrameskipClassName(Uint32 c)
{
    switch (c)
    {
        case AURORA_FP_FS_OFF: return "FS_OFF";
        case AURORA_FP_FS_ON_BLOCKED: return "FS_ON_BLOCKED";
        case AURORA_FP_FS_ON_PRESENTED: return "FS_ON_PRESENTED";
        case AURORA_FP_FS_ON_SKIPPED: return "FS_ON_SKIPPED";
        default: return "FS_UNKNOWN";
    }
}

static Uint32 SafeFrameskipClassForFrame(void)
{
    if (!s_frame.fsSeen)
        return AURORA_FP_FS_UNKNOWN;
    if (!s_frame.fsEnabled)
        return AURORA_FP_FS_OFF;
    if (!s_frame.fsAllowed)
        return AURORA_FP_FS_ON_BLOCKED;
    return s_frame.fsSkipped
        ? AURORA_FP_FS_ON_SKIPPED
        : AURORA_FP_FS_ON_PRESENTED;
}

static void ResetLive(void)
{
    memset(&s_live, 0, sizeof(s_live));
}

static void BeginSpan(Uint32 span)
{
    if (!s_frame.open || span >= SPAN_COUNT || s_frame.spanOpen[span])
        return;
    s_frame.spanStart[span] = ProfCtrGetCycle();
    s_frame.spanOpen[span] = TRUE;
}

static void EndSpan(Uint32 span)
{
    if (!s_frame.open || span >= SPAN_COUNT || !s_frame.spanOpen[span])
        return;
    const Uint32 now = ProfCtrGetCycle();
    s_frame.cycles[span] += (Uint32)(now - s_frame.spanStart[span]);
    s_frame.spanOpen[span] = FALSE;
}

static DerivedT Derive(Uint32 tick)
{
    DerivedT d;
    const Uint32 core = s_frame.cycles[SPAN_CORE];
    const Uint32 vb = s_frame.cycles[SPAN_VBLANK];
    const Uint32 render = s_frame.cycles[SPAN_RENDER];
    const Uint32 audio = s_frame.cycles[SPAN_AUDIO_DRAIN];

    d.frontend = SubSat(SubSat(tick, core), vb);
    d.renderNoVblank = SubSat(render, vb);
    d.renderNoVblankAudio = SubSat(d.renderNoVblank, audio);

    /* Top-level exclusive partition. upload is nested in post_core.
     * render_setup/game/UI/GP/audio are nested in render_total. */
    Uint32 other = d.frontend;
    other = SubSat(other, s_frame.cycles[SPAN_INPUT_POLL]);
    other = SubSat(other, s_frame.cycles[SPAN_INPUT_SNAPSHOT]);
    other = SubSat(other, s_frame.cycles[SPAN_INPUT_LOGIC]);
    other = SubSat(other, s_frame.cycles[SPAN_PROCESS_PRE_CORE]);
    other = SubSat(other, s_frame.cycles[SPAN_PROCESS_POST_CORE]);
    other = SubSat(other, d.renderNoVblank);
    other = SubSat(other, s_frame.cycles[SPAN_POST_RENDER]);
    d.otherFrontend = other;
    return d;
}

static void AccumulateBucket(BucketT &b, Uint32 tick, const DerivedT &d)
{
    ++b.frames;
    b.tick += tick;
    for (Uint32 i = 0; i < SPAN_COUNT; ++i)
        b.span[i] += s_frame.cycles[i];

    b.frontendExCoreVblank += d.frontend;
    b.renderExVblank += d.renderNoVblank;
    b.renderExVblankAudio += d.renderNoVblankAudio;
    b.otherFrontend += d.otherFrontend;

    b.audioConvertInputFrames += s_frame.audioConvertInputFrames;
    b.audioFlushFrames += s_frame.audioFlushFrames;
    b.audioDrainFrames += s_frame.audioDrainFrames;
    b.spcCalls += s_frame.spcCalls;
    b.spcRequestedCycles += s_frame.spcRequestedCycles;
    b.spcConsumedCycles += s_frame.spcConsumedCycles;
    b.dspMixCalls += s_frame.dspMixCalls;
    b.dspChunks += s_frame.dspChunks;
    b.dspSamples += s_frame.dspSamples;

    /* The cost/PPU stacks must close exactly at the host-frame boundary.
     * Fail closed for this diagnostic block only; all existing profiler
     * statistics remain valid even if a future callsite is mismatched. */
    if (s_frame.core == AURORA_FP_CORE_SNES)
    {
        if (s_frame.snesProfileInvalid ||
            s_frame.snesCostDepth != 0 || s_frame.snesPpuDepth != 0)
        {
            ++b.snesProfileInvalidFrames;
        }
        else
        {
            b.snesCpuBudgetCalls += s_frame.snesCpuBudgetCalls;
            b.snesCpuBudgetCycles += s_frame.snesCpuBudgetCycles;
            b.snesPpuSyncCalls += s_frame.snesPpuSyncCalls;
            for (Uint32 i = 0; i < 5u; ++i)
                b.snesCostCycles[i] += s_frame.snesCostCycles[i];
            for (Uint32 i = 0; i < 18u; ++i)
                b.snesPpuCycles[i] += s_frame.snesPpuCycles[i];
            b.snesPpuRenderedLines += s_frame.snesPpuRenderedLines;
            for (Uint32 i = 0; i < 8u; ++i)
                b.snesPpuModeLines[i] += s_frame.snesPpuModeLines[i];
            b.snesPpuBgMapFetches += s_frame.snesPpuBgMapFetches;
            b.snesPpuBgChrDecodes += s_frame.snesPpuBgChrDecodes;
            b.snesPpuMainBgLayers += s_frame.snesPpuMainBgLayers;
            b.snesPpuSubBgLayers += s_frame.snesPpuSubBgLayers;
            b.snesPpuObjOamRefs += s_frame.snesPpuObjOamRefs;
            b.snesPpuObjTiles += s_frame.snesPpuObjTiles;
            b.snesPpuObjEnabledLines += s_frame.snesPpuObjEnabledLines;
            b.snesPpuObjRangeOverLines += s_frame.snesPpuObjRangeOverLines;
            b.snesPpuObjTimeOverLines += s_frame.snesPpuObjTimeOverLines;
            b.snesPpuWindowLines += s_frame.snesPpuWindowLines;
            b.snesPpuColorMathLines += s_frame.snesPpuColorMathLines;
            b.snesPpuSubscreenLines += s_frame.snesPpuSubscreenLines;
            b.snesPpuHiresLines += s_frame.snesPpuHiresLines;
            b.snesPpuDirectColorLines += s_frame.snesPpuDirectColorLines;

            for (Uint32 bg = 0; bg < 4u; ++bg)
            {
                for (Uint32 depth = 0; depth < 4u; ++depth)
                {
                    b.snesBgChrCalls[bg][depth] += s_frame.snesBgChrCalls[bg][depth];
                    b.snesBgChrTiles[bg][depth] += s_frame.snesBgChrTiles[bg][depth];
                    b.snesBgChrCycles[bg][depth] += s_frame.snesBgChrCycles[bg][depth];
                    b.snesBgChrDecodeCycles[bg][depth] +=
                        s_frame.snesBgChrDecodeCycles[bg][depth];
                }
            }
            for (Uint32 flag = 0; flag < 4u; ++flag)
            {
                b.snesBgChrFlagCalls[flag] += s_frame.snesBgChrFlagCalls[flag];
                b.snesBgChrFlagTiles[flag] += s_frame.snesBgChrFlagTiles[flag];
                b.snesBgChrFlagCycles[flag] += s_frame.snesBgChrFlagCycles[flag];
                b.snesBgChrFlagDecodeCycles[flag] +=
                    s_frame.snesBgChrFlagDecodeCycles[flag];
            }
            b.snesBgChrMeasuredCalls += s_frame.snesBgChrMeasuredCalls;
            b.snesBgChrMeasuredTiles += s_frame.snesBgChrMeasuredTiles;
            b.snesBgChrMeasuredCycles += s_frame.snesBgChrMeasuredCycles;
            b.snesBgChrMeasuredDecodeCycles +=
                s_frame.snesBgChrMeasuredDecodeCycles;
            b.snesBgChrBadRecords += s_frame.snesBgChrBadRecords;
            for (Uint32 i = 0; i < 68u; ++i)
            {
                b.snesDeepCalls[i] += s_frame.snesDeepCalls[i];
                b.snesDeepUnits[i] += s_frame.snesDeepUnits[i];
                b.snesDeepCycles[i] += s_frame.snesDeepCycles[i];
            }
        }
    }

    b.dspVoiceEnvelopeCalls += s_frame.dspVoiceEnvelopeCalls;
    b.dspVoiceEnvelopeActive += s_frame.dspVoiceEnvelopeActive;
    b.dspVoiceSampleNormalCalls += s_frame.dspVoiceSampleNormalCalls;
    b.dspVoiceSamplePmonCalls += s_frame.dspVoiceSamplePmonCalls;
    b.dspVoiceBrrDecodes += s_frame.dspVoiceBrrDecodes;
    b.dspVoicePitchmodFeeders += s_frame.dspVoicePitchmodFeeders;
    b.dspVoiceMixCalls += s_frame.dspVoiceMixCalls;
    b.dspVoiceMixDryCalls += s_frame.dspVoiceMixDryCalls;
    b.dspVoiceMixEchoCalls += s_frame.dspVoiceMixEchoCalls;
    b.dspVoiceNoiseMixes += s_frame.dspVoiceNoiseMixes;
    b.dspVoiceSilentFastpathHits += s_frame.dspVoiceSilentFastpathHits;

    b.dspVoiceSampleNormalSamples += s_frame.dspVoiceSampleNormalSamples;
    b.dspVoiceSamplePmonSamples += s_frame.dspVoiceSamplePmonSamples;
    b.dspVoiceMixSamples += s_frame.dspVoiceMixSamples;
    b.dspVoiceMixDrySamples += s_frame.dspVoiceMixDrySamples;
    b.dspVoiceMixEchoSamples += s_frame.dspVoiceMixEchoSamples;

    b.dspVoiceGainZeroVolume += s_frame.dspVoiceGainZeroVolume;
    b.dspVoiceGainEnvelopeZero += s_frame.dspVoiceGainEnvelopeZero;
    b.dspVoiceGainVeryLow += s_frame.dspVoiceGainVeryLow;
    b.dspVoiceGainLow += s_frame.dspVoiceGainLow;
    b.dspVoiceGainNormal += s_frame.dspVoiceGainNormal;
    b.dspVoiceLowGainPmon += s_frame.dspVoiceLowGainPmon;
    b.dspVoiceLowGainEcho += s_frame.dspVoiceLowGainEcho;
    b.dspVoiceLowGainFeedsPmon += s_frame.dspVoiceLowGainFeedsPmon;
}

static const char *DominantName(const WorstT &w)
{
    const Uint32 values[] =
    {
        w.inputPoll, w.inputSnapshot, w.inputLogic, w.preCore, w.postCore,
        w.renderHost, w.audioDrain, w.postRender, w.other
    };
    static const char *names[] =
    {
        "INPUT_POLL", "INPUT_SNAPSHOT", "INPUT_LOGIC",
        "PRE_CORE_OR_RENDER", "POST_CORE", "RENDER_HOST",
        "AUDIO_DRAIN", "POST_RENDER", "OTHER"
    };

    Uint32 best = 0;
    for (Uint32 i = 1; i < sizeof(values) / sizeof(values[0]); ++i)
        if (values[i] > values[best])
            best = i;
    return names[best];
}

static void InsertWorst(
    Uint32 serial, Uint32 state, Uint32 core,
    Uint32 tick, const DerivedT &d, Bool spike)
{
    WorstT w;
    memset(&w, 0, sizeof(w));
    w.serial = serial;
    w.state = state;
    w.core = core;
    w.tick = tick;
    w.frontend = d.frontend;
    w.coreCycles = s_frame.cycles[SPAN_CORE];
    w.vblank = s_frame.cycles[SPAN_VBLANK];
    w.inputPoll = s_frame.cycles[SPAN_INPUT_POLL];
    w.inputSnapshot = s_frame.cycles[SPAN_INPUT_SNAPSHOT];
    w.inputLogic = s_frame.cycles[SPAN_INPUT_LOGIC];
    w.preCore = s_frame.cycles[SPAN_PROCESS_PRE_CORE];
    w.postCore = s_frame.cycles[SPAN_PROCESS_POST_CORE];
    w.upload = s_frame.cycles[SPAN_UPLOAD];
    w.renderHost = d.renderNoVblankAudio;
    w.audioDrain = s_frame.cycles[SPAN_AUDIO_DRAIN];
    w.postRender = s_frame.cycles[SPAN_POST_RENDER];
    w.other = d.otherFrontend;
    w.fsClass = SafeFrameskipClassForFrame();
    w.spike = spike;

    Int32 pos = -1;
    for (Int32 i = 0; i < 16; ++i)
    {
        if (w.frontend > s_live.worst[i].frontend)
        {
            pos = i;
            break;
        }
    }
    if (pos < 0)
        return;

    for (Int32 i = 15; i > pos; --i)
        s_live.worst[i] = s_live.worst[i - 1];
    s_live.worst[pos] = w;
}

static void SealSnapshot(void)
{
    if (s_writePending || !s_live.totalFrames)
        return;

    memset(&s_pending, 0, sizeof(s_pending));
    s_pending.stats = s_live;
    s_pending.stamp = CaptureStamp();
    s_pending.meta = CaptureMetadata(s_live);
    s_pending.serial = ++s_dumpSerial;

    ResetLive();
    s_captureArmed = FALSE;
    s_menuFramesNeeded = 0;
    s_writePending = TRUE;
}

static Bool WriteBucket(FILE *fp, const char *name, const BucketT &b)
{
    const Uint64 f = b.frames;
    return fprintf(fp,
        "%s frames=%llu tick_avg=%llu "
        "core_avg=%llu frontend_ex_core_vblank_avg=%llu "
        "render_ex_vblank_avg=%llu render_ex_vblank_audio_avg=%llu "
        "input_poll_avg=%llu input_snapshot_prepare_avg=%llu "
        "input_logic_avg=%llu pre_core_or_render_avg=%llu "
        "post_core_avg=%llu upload_avg=%llu "
        "render_total_avg=%llu render_setup_avg=%llu "
        "game_draw_avg=%llu ui_avg=%llu gp_flush_avg=%llu "
        "vblank_avg=%llu audio_drain_avg=%llu post_render_avg=%llu "
        "other_frontend_avg=%llu\n",
        name,
        (unsigned long long)f,
        (unsigned long long)Avg(b.tick, f),
        (unsigned long long)Avg(b.span[SPAN_CORE], f),
        (unsigned long long)Avg(b.frontendExCoreVblank, f),
        (unsigned long long)Avg(b.renderExVblank, f),
        (unsigned long long)Avg(b.renderExVblankAudio, f),
        (unsigned long long)Avg(b.span[SPAN_INPUT_POLL], f),
        (unsigned long long)Avg(b.span[SPAN_INPUT_SNAPSHOT], f),
        (unsigned long long)Avg(b.span[SPAN_INPUT_LOGIC], f),
        (unsigned long long)Avg(b.span[SPAN_PROCESS_PRE_CORE], f),
        (unsigned long long)Avg(b.span[SPAN_PROCESS_POST_CORE], f),
        (unsigned long long)Avg(b.span[SPAN_UPLOAD], f),
        (unsigned long long)Avg(b.span[SPAN_RENDER], f),
        (unsigned long long)Avg(b.span[SPAN_RENDER_SETUP], f),
        (unsigned long long)Avg(b.span[SPAN_GAME_DRAW], f),
        (unsigned long long)Avg(b.span[SPAN_UI], f),
        (unsigned long long)Avg(b.span[SPAN_GP_FLUSH], f),
        (unsigned long long)Avg(b.span[SPAN_VBLANK], f),
        (unsigned long long)Avg(b.span[SPAN_AUDIO_DRAIN], f),
        (unsigned long long)Avg(b.span[SPAN_POST_RENDER], f),
        (unsigned long long)Avg(b.otherFrontend, f)) >= 0
        ? TRUE : FALSE;
}

static Bool WriteStateSet(FILE *fp, const char *section, const BucketT *set)
{
    if (fprintf(fp, "\n[%s]\n", section) < 0)
        return FALSE;
    for (Uint32 i = 0; i < AURORA_FP_STATE_COUNT; ++i)
        if (!WriteBucket(fp, StateName(i), set[i]))
            return FALSE;
    return TRUE;
}

static Bool WriteCoreSet(FILE *fp, const char *section, const BucketT *set)
{
    if (fprintf(fp, "\n[%s]\n", section) < 0)
        return FALSE;
    for (Uint32 i = 0; i < AURORA_FP_CORE_COUNT; ++i)
    {
        if (!set[i].frames)
            continue;
        if (!WriteBucket(fp, CoreName(i), set[i]))
            return FALSE;
    }
    return TRUE;
}

static Bool WriteSafeFrameskipSet(
    FILE *fp,
    const char *section,
    const BucketT set[AURORA_FP_CORE_COUNT][AURORA_FP_FS_CLASS_COUNT])
{
    if (fprintf(fp, "\n[%s]\n", section) < 0)
        return FALSE;

    for (Uint32 core = 0; core < AURORA_FP_CORE_COUNT; ++core)
    {
        for (Uint32 cls = 0; cls < AURORA_FP_FS_CLASS_COUNT; ++cls)
        {
            const BucketT &b = set[core][cls];
            if (!b.frames)
                continue;

            Char label[64];
            snprintf(label, sizeof(label), "%s/%s",
                CoreName(core), SafeFrameskipClassName(cls));
            if (!WriteBucket(fp, label, b))
                return FALSE;
        }
    }
    return TRUE;
}

static Bool WriteSafeFrameskipSummary(FILE *fp, const StatsT &s)
{
    if (fprintf(fp,
        "\n[SAFE_FRAMESKIP_SUMMARY]\n"
        "core,gameplay,fs_off,fs_unknown,fs_on_blocked,"
        "fs_on_presented,fs_on_skipped,allowed_ticks,skip_pct_x100\n") < 0)
        return FALSE;

    for (Uint32 core = 0; core < AURORA_FP_CORE_COUNT; ++core)
    {
        const Uint64 unknown = s.fsAll[core][AURORA_FP_FS_UNKNOWN].frames;
        const Uint64 off = s.fsAll[core][AURORA_FP_FS_OFF].frames;
        const Uint64 blocked = s.fsAll[core][AURORA_FP_FS_ON_BLOCKED].frames;
        const Uint64 presented = s.fsAll[core][AURORA_FP_FS_ON_PRESENTED].frames;
        const Uint64 skipped = s.fsAll[core][AURORA_FP_FS_ON_SKIPPED].frames;
        const Uint64 gameplay = unknown + off + blocked + presented + skipped;
        const Uint64 allowed = presented + skipped;
        const Uint64 skipPctX100 =
            allowed ? ((skipped * 10000u + allowed / 2u) / allowed) : 0u;

        if (!gameplay)
            continue;

        if (fprintf(fp,
            "%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
            CoreName(core),
            (unsigned long long)gameplay,
            (unsigned long long)off,
            (unsigned long long)unknown,
            (unsigned long long)blocked,
            (unsigned long long)presented,
            (unsigned long long)skipped,
            (unsigned long long)allowed,
            (unsigned long long)skipPctX100) < 0)
            return FALSE;
    }
    return TRUE;
}

static Bool WriteAudioSet(
    FILE *fp, const char *section, const BucketT *set)
{
    if (fprintf(fp,
        "\n[%s]\n"
        "core,frames,convert_cycles_avg,flush_cycles_avg,drain_cycles_avg,"
        "convert_input_frames_avg,flush_frames_avg,drain_sent_frames_avg\n",
        section) < 0)
        return FALSE;

    for (Uint32 core = 0; core < AURORA_FP_CORE_COUNT; ++core)
    {
        const BucketT &b = set[core];
        const Uint64 f = b.frames;
        if (!f)
            continue;
        if (fprintf(fp, "%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
            CoreName(core),
            (unsigned long long)f,
            (unsigned long long)Avg(b.span[SPAN_AUDIO_CONVERT], f),
            (unsigned long long)Avg(b.span[SPAN_AUDIO_FLUSH], f),
            (unsigned long long)Avg(b.span[SPAN_AUDIO_DRAIN], f),
            (unsigned long long)Avg(b.audioConvertInputFrames, f),
            (unsigned long long)Avg(b.audioFlushFrames, f),
            (unsigned long long)Avg(b.audioDrainFrames, f)) < 0)
            return FALSE;
    }
    return TRUE;
}

static Bool WriteSnesAudioSet(
    FILE *fp, const char *section, const BucketT *set)
{
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 f = b.frames;

    if (fprintf(fp,
        "\n[%s]\n"
        "frames,spc_exec_avg,dsp_total_avg,dsp_sync_avg,dsp_noise_avg,"
        "dsp_voices_avg,dsp_echo_avg,dsp_final_avg,"
        "spc_calls_avg_x100,spc_requested_cycles_avg,spc_consumed_cycles_avg,"
        "dsp_mix_calls_avg_x100,dsp_chunks_avg_x100,dsp_samples_avg\n",
        section) < 0)
        return FALSE;

    if (!f)
        return TRUE;

    return fprintf(fp,
        "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
        "%llu,%llu,%llu,%llu,%llu,%llu\n",
        (unsigned long long)f,
        (unsigned long long)Avg(b.span[SPAN_SNES_SPC], f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_TOTAL], f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_SYNC], f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_NOISE], f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_VOICES], f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_ECHO], f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_FINAL], f),
        (unsigned long long)Avg(b.spcCalls * 100u, f),
        (unsigned long long)Avg(b.spcRequestedCycles, f),
        (unsigned long long)Avg(b.spcConsumedCycles, f),
        (unsigned long long)Avg(b.dspMixCalls * 100u, f),
        (unsigned long long)Avg(b.dspChunks * 100u, f),
        (unsigned long long)Avg(b.dspSamples, f)) >= 0
        ? TRUE : FALSE;
}

/* AURORA_SNES_DSP_VOICES_PROFILER_V3_5_1_20261005
 * BRR decode is nested inside sample_normal/sample_pmon and is therefore a
 * drill-down metric, not an extra term in residual.  Dry/echo mixer spans are
 * mutually exclusive and their sum is the old mix_stereo bucket. */
static Uint64 SnesVoiceMixTotal(const BucketT &b)
{
    return b.span[SPAN_SNES_DSP_MIX_DRY] +
           b.span[SPAN_SNES_DSP_MIX_ECHO];
}

static Uint64 SnesVoiceResidualTotal(const BucketT &b)
{
    const Uint64 attributed =
        b.span[SPAN_SNES_DSP_ENVELOPE] +
        b.span[SPAN_SNES_DSP_SAMPLE_NORMAL] +
        b.span[SPAN_SNES_DSP_SAMPLE_PMON] +
        b.span[SPAN_SNES_DSP_PITCHMOD_FEEDER] +
        SnesVoiceMixTotal(b);
    return b.span[SPAN_SNES_DSP_VOICES] > attributed
        ? b.span[SPAN_SNES_DSP_VOICES] - attributed : 0u;
}

static Uint64 SnesVoiceDiv(Uint64 n, Uint64 d)
{
    return d ? n / d : 0u;
}

static Bool WriteSnesDspVoicesSet(
    FILE *fp, const char *section, const BucketT *set)
{
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 f = b.frames;
    const Uint64 chunks = b.dspChunks;
    const Uint64 classifiedSlots =
        b.dspVoiceEnvelopeCalls + b.dspVoiceSilentFastpathHits;

    if (fprintf(fp,
        "\n[%s]\n"
        "frames,voices_total_avg,envelope_avg,sample_normal_avg,sample_pmon_avg,"
        "brr_decode_avg,pitchmod_feeder_avg,mix_stereo_avg,mix_dry_avg,"
        "mix_echo_avg,residual_avg,dsp_chunks_avg_x100,"
        "classified_voice_slots_avg_x100,visited_voices_avg_x100,"
        "envelope_active_avg_x100,silent_fastpath_avg_x100,"
        "produced_voices_avg_x100,pmon_attempts_avg_x100,noise_mix_avg_x100,"
        "brr_decodes_avg_x100,pitchmod_feeders_avg_x100,mix_calls_avg_x100,"
        "mix_dry_calls_avg_x100,mix_echo_calls_avg_x100\n",
        section) < 0)
        return FALSE;

    if (!f)
        return TRUE;

    return fprintf(fp,
        "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
        "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        (unsigned long long)f,
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_VOICES], f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_ENVELOPE], f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_SAMPLE_NORMAL], f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_SAMPLE_PMON], f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_BRR_DECODE], f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_PITCHMOD_FEEDER], f),
        (unsigned long long)Avg(SnesVoiceMixTotal(b), f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_MIX_DRY], f),
        (unsigned long long)Avg(b.span[SPAN_SNES_DSP_MIX_ECHO], f),
        (unsigned long long)Avg(SnesVoiceResidualTotal(b), f),
        (unsigned long long)Avg(b.dspChunks * 100u, f),
        (unsigned long long)SnesVoiceDiv(classifiedSlots * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceEnvelopeCalls * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceEnvelopeActive * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceSilentFastpathHits * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceMixCalls * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceSamplePmonCalls * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceNoiseMixes * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceBrrDecodes * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoicePitchmodFeeders * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceMixCalls * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceMixDryCalls * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceMixEchoCalls * 100u, chunks)) >= 0
        ? TRUE : FALSE;
}

static Bool WriteSnesDspVoiceGainSet(
    FILE *fp, const char *section, const BucketT *set)
{
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 f = b.frames;
    const Uint64 chunks = b.dspChunks;
    const Uint64 lowGain =
        b.dspVoiceGainZeroVolume + b.dspVoiceGainEnvelopeZero +
        b.dspVoiceGainVeryLow + b.dspVoiceGainLow;

    if (fprintf(fp,
        "\n[%s]\n"
        "frames,zero_volume_avg_x100,envelope_zero_avg_x100,"
        "gain_1_256_avg_x100,gain_257_1024_avg_x100,gain_gt_1024_avg_x100,"
        "lowgain_le1024_avg_x100,zero_volume_mix_pct_x100,"
        "lowgain_mix_pct_x100,lowgain_pmon_avg_x100,lowgain_echo_avg_x100,"
        "lowgain_feeds_pmon_avg_x100\n",
        section) < 0)
        return FALSE;

    if (!f)
        return TRUE;

    return fprintf(fp,
        "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        (unsigned long long)f,
        (unsigned long long)SnesVoiceDiv(b.dspVoiceGainZeroVolume * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceGainEnvelopeZero * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceGainVeryLow * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceGainLow * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceGainNormal * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(lowGain * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceGainZeroVolume * 10000u,
                                         b.dspVoiceMixCalls),
        (unsigned long long)SnesVoiceDiv(lowGain * 10000u,
                                         b.dspVoiceMixCalls),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceLowGainPmon * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceLowGainEcho * 100u, chunks),
        (unsigned long long)SnesVoiceDiv(b.dspVoiceLowGainFeedsPmon * 100u, chunks)) >= 0
        ? TRUE : FALSE;
}

static Bool WriteSnesDspVoiceSampleCostSet(
    FILE *fp, const char *section, const BucketT *set)
{
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 f = b.frames;
    const Uint64 voiceSamples =
        b.dspVoiceSampleNormalSamples + b.dspVoiceSamplePmonSamples;

    if (fprintf(fp,
        "\n[%s]\n"
        "frames,voice_samples_avg,normal_voice_samples_avg,pmon_voice_samples_avg,"
        "mix_voice_samples_avg,mix_dry_samples_avg,mix_echo_samples_avg,"
        "sample_normal_cycles_per_voice_sample_x1000,"
        "sample_pmon_cycles_per_voice_sample_x1000,"
        "sample_all_cycles_per_voice_sample_x1000,"
        "mix_cycles_per_voice_sample_x1000,"
        "mix_dry_cycles_per_voice_sample_x1000,"
        "mix_echo_cycles_per_voice_sample_x1000\n",
        section) < 0)
        return FALSE;

    if (!f)
        return TRUE;

    return fprintf(fp,
        "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        (unsigned long long)f,
        (unsigned long long)Avg(voiceSamples, f),
        (unsigned long long)Avg(b.dspVoiceSampleNormalSamples, f),
        (unsigned long long)Avg(b.dspVoiceSamplePmonSamples, f),
        (unsigned long long)Avg(b.dspVoiceMixSamples, f),
        (unsigned long long)Avg(b.dspVoiceMixDrySamples, f),
        (unsigned long long)Avg(b.dspVoiceMixEchoSamples, f),
        (unsigned long long)SnesVoiceDiv(
            b.span[SPAN_SNES_DSP_SAMPLE_NORMAL] * 1000u,
            b.dspVoiceSampleNormalSamples),
        (unsigned long long)SnesVoiceDiv(
            b.span[SPAN_SNES_DSP_SAMPLE_PMON] * 1000u,
            b.dspVoiceSamplePmonSamples),
        (unsigned long long)SnesVoiceDiv(
            (b.span[SPAN_SNES_DSP_SAMPLE_NORMAL] +
             b.span[SPAN_SNES_DSP_SAMPLE_PMON]) * 1000u,
            voiceSamples),
        (unsigned long long)SnesVoiceDiv(
            SnesVoiceMixTotal(b) * 1000u, b.dspVoiceMixSamples),
        (unsigned long long)SnesVoiceDiv(
            b.span[SPAN_SNES_DSP_MIX_DRY] * 1000u,
            b.dspVoiceMixDrySamples),
        (unsigned long long)SnesVoiceDiv(
            b.span[SPAN_SNES_DSP_MIX_ECHO] * 1000u,
            b.dspVoiceMixEchoSamples)) >= 0
        ? TRUE : FALSE;
}

/* AURORA_SNES_CORE_PROFILER_V3_6_20261005 */
static Bool WriteSnesCoreBreakdownSet(
    FILE *fp, const char *title, const BucketT *set)
{
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 frames = b.frames;
    Uint64 tracked = 0;
    for (Uint32 i = 0; i < 5u; ++i)
        tracked += b.snesCostCycles[i];
    const Uint64 core = b.span[SPAN_CORE];
    const Uint64 residual = core > tracked ? core - tracked : 0u;

    if (fprintf(fp,
        "\n[%s]\n"
        "frames,core_avg,cpu_avg,ppu_avg,raster_avg,spc_avg,dsp_avg,"
        "tracked_avg,residual_avg,tracked_pct_x100,"
        "cpu_budget_calls_avg_x100,cpu_budget_cycles_avg,"
        "cpu_host_cycles_per_emulated_x1000,ppu_sync_calls_avg_x100,"
        "invalid_frames\n",
        title) < 0) return FALSE;

    return fprintf(fp,
        "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
        "%llu,%llu,%llu,%llu,%llu\n",
        (unsigned long long)frames,
        (unsigned long long)Avg(core, frames),
        (unsigned long long)Avg(b.snesCostCycles[0], frames),
        (unsigned long long)Avg(b.snesCostCycles[1], frames),
        (unsigned long long)Avg(b.snesCostCycles[2], frames),
        (unsigned long long)Avg(b.snesCostCycles[3], frames),
        (unsigned long long)Avg(b.snesCostCycles[4], frames),
        (unsigned long long)Avg(tracked, frames),
        (unsigned long long)Avg(residual, frames),
        (unsigned long long)SnesScaledDiv(tracked, core, 10000u),
        (unsigned long long)SnesScaledDiv(b.snesCpuBudgetCalls, frames, 100u),
        (unsigned long long)Avg(b.snesCpuBudgetCycles, frames),
        (unsigned long long)SnesScaledDiv(
            b.snesCostCycles[0], b.snesCpuBudgetCycles, 1000u),
        (unsigned long long)SnesScaledDiv(b.snesPpuSyncCalls, frames, 100u),
        (unsigned long long)b.snesProfileInvalidFrames) >= 0;
}

static Bool WriteSnesPpuBreakdownSet(
    FILE *fp, const char *title, const BucketT *set)
{
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 frames = b.frames;
    Uint64 detail = 0;
    for (Uint32 i = 0; i < 18u; ++i)
        detail += b.snesPpuCycles[i];
    const Uint64 ppu = b.snesCostCycles[1];
    const Uint64 residual = ppu > detail ? ppu - detail : 0u;

    if (fprintf(fp,
        "\n[%s]\n"
        "frames,ppu_avg,sync_avg,render_other_avg,prep_avg,raster_other_avg,"
        "bg_map_avg,bg_chr_avg,bg_main_avg,bg_sub_avg,bg_other_avg,"
        "obj_legacy_avg,obj_update_avg,obj_fetch_avg,obj_main_avg,obj_sub_avg,"
        "mode7_avg,color_mask_avg,blend_avg,color_other_avg,"
        "detail_total_avg,ppu_unclassified_avg,invalid_frames\n",
        title) < 0) return FALSE;

    if (fprintf(fp, "%llu,%llu",
        (unsigned long long)frames,
        (unsigned long long)Avg(ppu, frames)) < 0) return FALSE;
    for (Uint32 i = 0; i < 18u; ++i)
        if (fprintf(fp, ",%llu",
            (unsigned long long)Avg(b.snesPpuCycles[i], frames)) < 0)
            return FALSE;
    return fprintf(fp, ",%llu,%llu,%llu\n",
        (unsigned long long)Avg(detail, frames),
        (unsigned long long)Avg(residual, frames),
        (unsigned long long)b.snesProfileInvalidFrames) >= 0;
}

static Bool WriteSnesPpuWorkSet(
    FILE *fp, const char *title, const BucketT *set)
{
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 frames = b.frames;
    const Uint64 objLines = b.snesPpuObjEnabledLines;

    if (fprintf(fp,
        "\n[%s]\n"
        "frames,rendered_lines_avg_x100,"
        "mode0_lines_avg_x100,mode1_lines_avg_x100,mode2_lines_avg_x100,"
        "mode3_lines_avg_x100,mode4_lines_avg_x100,mode5_lines_avg_x100,"
        "mode6_lines_avg_x100,mode7_lines_avg_x100,"
        "bg_map_fetches_avg_x100,bg_chr_decodes_avg_x100,"
        "main_bg_layers_avg_x100,sub_bg_layers_avg_x100,"
        "obj_oam_refs_avg_x100,obj_tiles_avg_x100,obj_enabled_lines_avg_x100,"
        "obj_range_over_lines_avg_x100,obj_time_over_lines_avg_x100,"
        "window_lines_avg_x100,color_math_lines_avg_x100,subscreen_lines_avg_x100,"
        "hires_lines_avg_x100,direct_color_lines_avg_x100,"
        "obj_refs_per_enabled_line_x100,obj_tiles_per_enabled_line_x100\n",
        title) < 0) return FALSE;

    if (fprintf(fp, "%llu,%llu",
        (unsigned long long)frames,
        (unsigned long long)SnesScaledDiv(b.snesPpuRenderedLines, frames, 100u)) < 0)
        return FALSE;
    for (Uint32 i = 0; i < 8u; ++i)
        if (fprintf(fp, ",%llu",
            (unsigned long long)SnesScaledDiv(b.snesPpuModeLines[i], frames, 100u)) < 0)
            return FALSE;

    return fprintf(fp,
        ",%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        (unsigned long long)SnesScaledDiv(b.snesPpuBgMapFetches, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuBgChrDecodes, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuMainBgLayers, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuSubBgLayers, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuObjOamRefs, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuObjTiles, frames, 100u),
        (unsigned long long)SnesScaledDiv(objLines, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuObjRangeOverLines, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuObjTimeOverLines, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuWindowLines, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuColorMathLines, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuSubscreenLines, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuHiresLines, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuDirectColorLines, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuObjOamRefs, objLines, 100u),
        (unsigned long long)SnesScaledDiv(b.snesPpuObjTiles, objLines, 100u)) >= 0;
}

static Bool WriteSnesDmaBreakdownSet(
    FILE *fp, const char *title, const BucketT *set)
{
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 frames = b.frames;
    if (fprintf(fp,
        "\n[%s]\n"
        "frames,raster_bucket_avg,mdma_avg,hdma_setup_avg,hdma_data_avg\n",
        title) < 0) return FALSE;
    return fprintf(fp, "%llu,%llu,%llu,%llu,%llu\n",
        (unsigned long long)frames,
        (unsigned long long)Avg(b.snesCostCycles[2], frames),
        (unsigned long long)Avg(b.span[SPAN_SNES_MDMA], frames),
        (unsigned long long)Avg(b.span[SPAN_SNES_HDMA_SETUP], frames),
        (unsigned long long)Avg(b.span[SPAN_SNES_HDMA_DATA], frames)) >= 0;
}

/* AURORA_SNES_BGCHR_PROFILER_V3_7_20261005 */
static const char *SnesBgChrDepthName(Uint32 depth)
{
    static const char *const names[4] = { "2", "4", "8", "other" };
    return depth < 4u ? names[depth] : "?";
}

static Bool WriteSnesBgChrSummarySet(
    FILE *fp, const char *title, const BucketT *set)
{
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 frames = b.frames;
    const Uint64 ppuBgChr = b.snesPpuCycles[5];
    const Uint64 measured = b.snesBgChrMeasuredCycles;
    const Uint64 decode = b.snesBgChrMeasuredDecodeCycles;
    const Uint64 post = measured >= decode ? measured - decode : 0u;
    const Uint64 outer = ppuBgChr >= measured ? ppuBgChr - measured : 0u;

    if (fprintf(fp,
        "\n[%s]\n"
        "frames,ppu_bg_chr_avg,measured_total_avg,decode_avg,post_avg,"
        "outer_residual_avg,calls_avg_x100,tiles_avg_x100,"
        "cycles_per_call_x1000,cycles_per_tile_x1000,"
        "decode_cycles_per_tile_x1000,post_cycles_per_tile_x1000,bad_records\n",
        title) < 0) return FALSE;

    return fprintf(fp,
        "%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
        (unsigned long long)frames,
        (unsigned long long)Avg(ppuBgChr, frames),
        (unsigned long long)Avg(measured, frames),
        (unsigned long long)Avg(decode, frames),
        (unsigned long long)Avg(post, frames),
        (unsigned long long)Avg(outer, frames),
        (unsigned long long)SnesScaledDiv(b.snesBgChrMeasuredCalls, frames, 100u),
        (unsigned long long)SnesScaledDiv(b.snesBgChrMeasuredTiles, frames, 100u),
        (unsigned long long)SnesScaledDiv(measured, b.snesBgChrMeasuredCalls, 1000u),
        (unsigned long long)SnesScaledDiv(measured, b.snesBgChrMeasuredTiles, 1000u),
        (unsigned long long)SnesScaledDiv(decode, b.snesBgChrMeasuredTiles, 1000u),
        (unsigned long long)SnesScaledDiv(post, b.snesBgChrMeasuredTiles, 1000u),
        (unsigned long long)b.snesBgChrBadRecords) >= 0;
}

static Bool WriteSnesBgChrLayerDepthSet(
    FILE *fp, const char *title, const BucketT *set)
{
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 frames = b.frames;
    if (fprintf(fp,
        "\n[%s]\n"
        "bg,bitdepth,calls_avg_x100,tiles_avg_x100,total_cycles_avg,"
        "decode_cycles_avg,post_cycles_avg,cycles_per_call_x1000,"
        "cycles_per_tile_x1000,decode_cycles_per_tile_x1000,"
        "post_cycles_per_tile_x1000\n", title) < 0) return FALSE;

    for (Uint32 bg = 0; bg < 4u; ++bg)
    {
        for (Uint32 depth = 0; depth < 4u; ++depth)
        {
            const Uint64 calls = b.snesBgChrCalls[bg][depth];
            const Uint64 tiles = b.snesBgChrTiles[bg][depth];
            const Uint64 total = b.snesBgChrCycles[bg][depth];
            const Uint64 decode = b.snesBgChrDecodeCycles[bg][depth];
            const Uint64 post = total >= decode ? total - decode : 0u;
            if (fprintf(fp,
                "BG%u,%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
                (unsigned)(bg + 1u), SnesBgChrDepthName(depth),
                (unsigned long long)SnesScaledDiv(calls, frames, 100u),
                (unsigned long long)SnesScaledDiv(tiles, frames, 100u),
                (unsigned long long)Avg(total, frames),
                (unsigned long long)Avg(decode, frames),
                (unsigned long long)Avg(post, frames),
                (unsigned long long)SnesScaledDiv(total, calls, 1000u),
                (unsigned long long)SnesScaledDiv(total, tiles, 1000u),
                (unsigned long long)SnesScaledDiv(decode, tiles, 1000u),
                (unsigned long long)SnesScaledDiv(post, tiles, 1000u)) < 0)
                return FALSE;
        }
    }
    return TRUE;
}

static Bool WriteSnesBgChrFlagsSet(
    FILE *fp, const char *title, const BucketT *set)
{
    static const char *const names[4] = {
        "OFFSET", "MOSAIC", "FINE_X_NONZERO", "HIRES_SUBSCREEN"
    };
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 frames = b.frames;
    if (fprintf(fp,
        "\n[%s]\n"
        "flag,calls_avg_x100,tiles_avg_x100,total_cycles_avg,decode_cycles_avg,"
        "post_cycles_avg,cycles_per_tile_x1000,decode_cycles_per_tile_x1000,"
        "post_cycles_per_tile_x1000\n", title) < 0) return FALSE;

    for (Uint32 flag = 0; flag < 4u; ++flag)
    {
        const Uint64 calls = b.snesBgChrFlagCalls[flag];
        const Uint64 tiles = b.snesBgChrFlagTiles[flag];
        const Uint64 total = b.snesBgChrFlagCycles[flag];
        const Uint64 decode = b.snesBgChrFlagDecodeCycles[flag];
        const Uint64 post = total >= decode ? total - decode : 0u;
        if (fprintf(fp, "%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
            names[flag],
            (unsigned long long)SnesScaledDiv(calls, frames, 100u),
            (unsigned long long)SnesScaledDiv(tiles, frames, 100u),
            (unsigned long long)Avg(total, frames),
            (unsigned long long)Avg(decode, frames),
            (unsigned long long)Avg(post, frames),
            (unsigned long long)SnesScaledDiv(total, tiles, 1000u),
            (unsigned long long)SnesScaledDiv(decode, tiles, 1000u),
            (unsigned long long)SnesScaledDiv(post, tiles, 1000u)) < 0)
            return FALSE;
    }
    return TRUE;
}

/* AURORA_SNES_DEEP_OPT_V3_14_20261005 */
static const char *SnesDeepName(Uint32 i)
{
    static const char *const names[68] = {
        "M7_SETUP",
        "M7_FETCH_REPEAT_DY0",
        "M7_FETCH_REPEAT_UNITX",
        "M7_FETCH_REPEAT_GENERIC",
        "M7_FETCH_CLAMP",
        "M7_FETCH_BLACK",
        "M7_HALF_EXPAND",
        "M7_OPAQUE",
        "M7_EXTBG_COPY",
        "M7_EXTBG_PRIORITY",
        "M7_EXTBG_OPAQUE",
        "M7_MOSAIC_BG1",
        "M7_MOSAIC_BG2",
        "PPU_QUEUE_APPLY",
        "PPU_VBLANK_DRAIN",
        "BLEND_PLANAR",
        "BLEND_GIF_WAIT",
        "BLEND_LIST_REBUILD",
        "BLEND_STAGE_COPY",
        "BLEND_SET_PARAMS",
        "BLEND_KICK",
        "BG_MAIN_BG1",
        "BG_MAIN_BG2",
        "BG_MAIN_BG3",
        "BG_MAIN_BG4",
        "BG_SUB_BG1",
        "BG_SUB_BG2",
        "BG_SUB_BG3",
        "BG_SUB_BG4",
        "OBJ_ROTATE",
        "OBJ_FETCH_DECODE",
        "HDMA_DATA_PHASE",
        "HDMA_TABLE_PHASE",
        "MDMA_FAST",
        "MDMA_ACCURATE",
        "MDMA_READ",
        "MDMA_SDD1",
        "CPU_EXEC",
        "CPU_SA1",
        "CPU_WAI",
        "BG_MAIN_DIRECT",
        "BG_SUB_DIRECT",
        "CHR2_OPAQUE_ROWS",
        "CHR2_TRANSPARENT_ROWS",
        "CHR4_OPAQUE_ROWS",
        "CHR4_TRANSPARENT_ROWS",
        "CHR2_OPAQUE_FLIP0",
        "CHR2_OPAQUE_FLIPPED",
        "CHR4_OPAQUE_FLIP0",
        "CHR4_OPAQUE_FLIPPED",
        "M7_CLAMP_GENERIC_INSIDE_PIXELS",
        "M7_CLAMP_GENERIC_OUTSIDE_PIXELS",
        "M7_CLAMP_GENERIC_TILE_REUSE",
        "M7_CLAMP_GENERIC_TILE_LOADS",
        "M7_CLAMP_GENERIC_BOUNDARY_TRANSITIONS",
        "M7_CLAMP_GENERIC_ALL_INSIDE_CALLS",
        "M7_CLAMP_GENERIC_MIXED_CALLS",
        "M7_CLAMP_GENERIC_ALL_OUTSIDE_CALLS",
        "M7_CLAMP_DX0_CALLS",
        "M7_CLAMP_DY0_CALLS",
        "M7_CLAMP_UNITX_CALLS",
        "M7_CLAMP_UNITY_CALLS",
        "OBJ_CACHE_HITS",
        "OBJ_CACHE_MISSES",
        "OBJ_MISS_TRANSPARENT",
        "OBJ_MISS_OPAQUE",
        "OBJ_HFLIP_TILES",
        "OBJ_SECOND_TABLE_TILES"
    };
    return i < 68u ? names[i] : "?";
}

static Bool WriteSnesDeepSet(FILE *fp, const char *title, const BucketT *set)
{
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 frames = b.frames;
    if (fprintf(fp,
        "\n[%s]\n"
        "bucket,calls_avg_x100,units_avg_x100,cycles_avg,cycles_per_call_x1000,cycles_per_unit_x1000\n",
        title) < 0) return FALSE;
    for (Uint32 i = 0; i < 68u; ++i)
    {
        if (fprintf(fp, "%s,%llu,%llu,%llu,%llu,%llu\n",
            SnesDeepName(i),
            (unsigned long long)SnesScaledDiv(b.snesDeepCalls[i], frames, 100u),
            (unsigned long long)SnesScaledDiv(b.snesDeepUnits[i], frames, 100u),
            (unsigned long long)Avg(b.snesDeepCycles[i], frames),
            (unsigned long long)SnesScaledDiv(b.snesDeepCycles[i], b.snesDeepCalls[i], 1000u),
            (unsigned long long)SnesScaledDiv(b.snesDeepCycles[i], b.snesDeepUnits[i], 1000u)) < 0)
            return FALSE;
    }
    return TRUE;
}

static Bool WriteSnesDeepReconcileRow(FILE *fp, const char *name,
    Uint64 frames, Uint64 parent, Uint64 child)
{
    const Uint64 residual = parent >= child ? parent - child : 0u;
    return fprintf(fp, "%s,%llu,%llu,%llu,%llu\n", name,
        (unsigned long long)Avg(parent, frames),
        (unsigned long long)Avg(child, frames),
        (unsigned long long)Avg(residual, frames),
        (unsigned long long)SnesScaledDiv(child, parent, 10000u)) >= 0;
}

static Bool WriteSnesDeepReconcileSet(FILE *fp, const char *title, const BucketT *set)
{
    const BucketT &b = set[AURORA_FP_CORE_SNES];
    const Uint64 f = b.frames;
    Uint64 mode7=0, blend=0, bgmain=0, bgsub=0, obj=0, hdma=0, mdma=0, cpu=0;
    for (Uint32 i=0;i<=12u;++i) mode7 += b.snesDeepCycles[i];
    for (Uint32 i=15u;i<=20u;++i) blend += b.snesDeepCycles[i];
    for (Uint32 i=21u;i<=24u;++i) bgmain += b.snesDeepCycles[i];
    bgmain += b.snesDeepCycles[40];
    for (Uint32 i=25u;i<=28u;++i) bgsub += b.snesDeepCycles[i];
    bgsub += b.snesDeepCycles[41];
    obj = b.snesDeepCycles[29] + b.snesDeepCycles[30];
    hdma = b.snesDeepCycles[31] + b.snesDeepCycles[32];
    for (Uint32 i=33u;i<=36u;++i) mdma += b.snesDeepCycles[i];
    cpu = b.snesDeepCycles[37] + b.snesDeepCycles[38];
    const Uint64 sync = b.snesDeepCycles[13] + b.snesDeepCycles[14];

    if (fprintf(fp, "\n[%s]\nfamily,parent_avg,children_avg,residual_avg,children_pct_x100\n", title) < 0)
        return FALSE;
    if (!WriteSnesDeepReconcileRow(fp,"MODE7",f,b.snesPpuCycles[14],mode7)) return FALSE;
    if (!WriteSnesDeepReconcileRow(fp,"PPU_SYNC",f,b.snesPpuCycles[0],sync)) return FALSE;
    if (!WriteSnesDeepReconcileRow(fp,"BG_MAIN",f,b.snesPpuCycles[6],bgmain)) return FALSE;
    if (!WriteSnesDeepReconcileRow(fp,"BG_SUB",f,b.snesPpuCycles[7],bgsub)) return FALSE;
    if (!WriteSnesDeepReconcileRow(fp,"OBJ_FETCH",f,b.snesPpuCycles[11],obj)) return FALSE;
    if (!WriteSnesDeepReconcileRow(fp,"BLEND",f,b.snesPpuCycles[16],blend)) return FALSE;
    if (!WriteSnesDeepReconcileRow(fp,"MDMA",f,b.span[SPAN_SNES_MDMA],mdma)) return FALSE;
    if (!WriteSnesDeepReconcileRow(fp,"HDMA",f,b.span[SPAN_SNES_HDMA_DATA],hdma)) return FALSE;
    if (!WriteSnesDeepReconcileRow(fp,"CPU",f,b.snesCostCycles[0],cpu)) return FALSE;
    return TRUE;
}

static Bool WriteSafeFrameskipDiagnostic(FILE *fp, const StatsT &s)
{
    if (fprintf(fp,
        "\n[SAFE_FRAMESKIP_DIAGNOSTIC]\n"
        "core,decisions,debt,overrun_trigger,unlimited,skips,"
        "recovery_pending_decisions,recovery_presented,recovery_rebases,"
        "period_avg,lateness_avg,last_presented_work_avg,recovery_work_avg\n") < 0)
        return FALSE;

    for (Uint32 core = 0; core < AURORA_FP_CORE_COUNT; ++core)
    {
        const FsDiagT &d = s.fsDiag[core];
        if (!d.decisions && !d.recoveryPresented)
            continue;

        if (fprintf(fp,
            "%s,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
            "%llu,%llu,%llu,%llu\n",
            CoreName(core),
            (unsigned long long)d.decisions,
            (unsigned long long)d.debtDecisions,
            (unsigned long long)d.overrunDecisions,
            (unsigned long long)d.unlimitedDecisions,
            (unsigned long long)d.skippedDecisions,
            (unsigned long long)d.recoveryPendingDecisions,
            (unsigned long long)d.recoveryPresented,
            (unsigned long long)d.recoveryRebases,
            (unsigned long long)Avg(d.periodSum, d.decisions),
            (unsigned long long)Avg(d.latenessSum, d.latenessSamples),
            (unsigned long long)Avg(
                d.lastPresentedWorkSum, d.lastPresentedWorkSamples),
            (unsigned long long)Avg(
                d.recoveryWorkSum, d.recoveryPresented)) < 0)
            return FALSE;
    }
    return TRUE;
}

static void AccumulateSafeFrameskipDiagnostic(Uint32 core)
{
    if (core >= AURORA_FP_CORE_COUNT)
        return;

    FsDiagT &d = s_live.fsDiag[core];

    if (s_frame.fsDetailSeen)
    {
        ++d.decisions;
        d.periodSum += s_frame.fsPeriod;
        if (s_frame.fsMeaningfulDebt) ++d.debtDecisions;
        if (s_frame.fsMeasuredOverrun) ++d.overrunDecisions;
        if (s_frame.fsUnlimited) ++d.unlimitedDecisions;
        if (s_frame.fsDecisionSkipped) ++d.skippedDecisions;
        if (s_frame.fsRecoveryPendingAtDecision)
            ++d.recoveryPendingDecisions;
        if (s_frame.fsDiff < 0)
        {
            d.latenessSum += (Uint32)(0u - (Uint32)s_frame.fsDiff);
            ++d.latenessSamples;
        }
        if (s_frame.fsLastPresentedWork)
        {
            d.lastPresentedWorkSum += s_frame.fsLastPresentedWork;
            ++d.lastPresentedWorkSamples;
        }
    }

    if (s_frame.fsRecoverySeen && s_frame.fsRecoveryPendingAtPresent)
    {
        ++d.recoveryPresented;
        d.recoveryWorkSum += s_frame.fsRecoveryWork;
        if (s_frame.fsRecoveryRebased)
            ++d.recoveryRebases;
    }
}


/* AURORA_PROFILER_ROM_IDENTITY_V3_1_20261005 */
static Bool WriteIdentityHeader(FILE *fp)
{
    const MetadataT &m = s_pending.meta;

    if (fprintf(fp,
        "game_title=%s\n"
        "rom_filename=%s\n",
        m.gameTitle[0] ? m.gameTitle : "UNKNOWN",
        m.romFilename[0] ? m.romFilename : "UNKNOWN") < 0)
        return FALSE;

    if (m.runtimeCRCValid)
    {
        if (fprintf(fp, "runtime_crc32=%08X\n",
                    (unsigned)m.runtimeCRC32) < 0)
            return FALSE;
    }
    else if (fprintf(fp, "runtime_crc32=UNKNOWN\n") < 0)
    {
        return FALSE;
    }

    return fprintf(fp,
        "core=%s\n"
        "region=%s\n",
        CoreName(m.core),
        m.region[0] ? m.region : "UNKNOWN") >= 0
        ? TRUE : FALSE;
}

static Bool WriteDump(FILE *fp)
{
    const StatsT &s = s_pending.stats;

    if (fprintf(fp,
        "SNESticleAurora frontend profiler v4.6\n"
        "schema=aurora-frontend-v4.6\n"
        "timestamp=%04u-%02u-%02u %02u:%02u:%02u\n"
        "sample_frames=%llu gameplay=%llu menu_with_core=%llu idle_ui=%llu\n"
        "normal_frames=%llu spike_frames=%llu "
        "frontend_spike_budget_cycles=%u\n",
        (unsigned)s_pending.stamp.year,
        (unsigned)s_pending.stamp.month,
        (unsigned)s_pending.stamp.day,
        (unsigned)s_pending.stamp.hour,
        (unsigned)s_pending.stamp.minute,
        (unsigned)s_pending.stamp.second,
        (unsigned long long)s.totalFrames,
        (unsigned long long)s.gameplayFrames,
        (unsigned long long)s.menuFrames,
        (unsigned long long)s.idleFrames,
        (unsigned long long)s.normalFrames,
        (unsigned long long)s.spikeFrames,
        (unsigned)AURORA_FP_FRONTEND_SPIKE_BUDGET) < 0)
        return FALSE;

    if (!WriteIdentityHeader(fp))
        return FALSE;

    if (fprintf(fp,
        "\n[METRIC_NOTES]\n"
        "frontend_ex_core_vblank = whole host tick - measured core - blocking GSK_SyncFlip.\n"
        "render_ex_vblank = MainLoopRender total - blocking GSK_SyncFlip.\n"
        "render_ex_vblank_audio = render_ex_vblank - measured async audio drain.\n"
        "NORMAL = frontend_ex_core_vblank <= frontend_spike_budget_cycles.\n"
        "SPIKE = frontend_ex_core_vblank > frontend_spike_budget_cycles.\n"
        "pre_core_or_render = after frontend input until core begin; in UI/no-core, until render begin.\n"
        "post_core = exact core end until render begin; upload is a nested diagnostic subset.\n"
        "other_frontend = residual after the top-level exclusive partition.\n"
        "snes_core buckets are exclusive host-cost scopes; residual is core minus their sum.\n"
        "snes_ppu detail scopes are exclusive; OBJ fetch/main/sub are phase-labeled.\n"
        "SNES DMA spans are drill-downs and are not added to core tracked totals.\n"
        "PPU work counters are counts only; they do not alter renderer policy.\n") < 0)
        return FALSE;

    if (!WriteStateSet(fp, "STATE_ALL", s.stateAll)) return FALSE;
    if (!WriteStateSet(fp, "STATE_NORMAL", s.stateNormal)) return FALSE;
    if (!WriteStateSet(fp, "STATE_SPIKE", s.stateSpike)) return FALSE;

    if (!WriteCoreSet(fp, "CORE_GAMEPLAY_ALL", s.coreAll)) return FALSE;
    if (!WriteCoreSet(fp, "CORE_GAMEPLAY_NORMAL", s.coreNormal)) return FALSE;
    if (!WriteCoreSet(fp, "CORE_GAMEPLAY_SPIKE", s.coreSpike)) return FALSE;

    if (!WriteSafeFrameskipSummary(fp, s)) return FALSE;
    if (!WriteSafeFrameskipSet(fp, "SAFE_FRAMESKIP_ALL", s.fsAll)) return FALSE;
    if (!WriteSafeFrameskipSet(fp, "SAFE_FRAMESKIP_NORMAL", s.fsNormal)) return FALSE;
    if (!WriteSafeFrameskipSet(fp, "SAFE_FRAMESKIP_SPIKE", s.fsSpike)) return FALSE;
    if (!WriteSafeFrameskipDiagnostic(fp, s)) return FALSE;

    if (!WriteAudioSet(fp, "AURORA_AUDIO_ALL", s.coreAll)) return FALSE;
    if (!WriteAudioSet(fp, "AURORA_AUDIO_NORMAL", s.coreNormal)) return FALSE;
    if (!WriteSnesAudioSet(fp, "SNES_AUDIO_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesAudioSet(fp, "SNES_AUDIO_NORMAL", s.coreNormal)) return FALSE;
    if (!WriteSnesDspVoicesSet(fp, "SNES_DSP_VOICES_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesDspVoicesSet(fp, "SNES_DSP_VOICES_NORMAL", s.coreNormal)) return FALSE;
    if (!WriteSnesDspVoiceGainSet(fp, "SNES_DSP_VOICE_GAIN_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesDspVoiceGainSet(fp, "SNES_DSP_VOICE_GAIN_NORMAL", s.coreNormal)) return FALSE;
    if (!WriteSnesDspVoiceSampleCostSet(fp, "SNES_DSP_VOICE_SAMPLE_COST_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesDspVoiceSampleCostSet(fp, "SNES_DSP_VOICE_SAMPLE_COST_NORMAL", s.coreNormal)) return FALSE;

    if (!WriteSnesCoreBreakdownSet(fp, "SNES_CORE_BREAKDOWN_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesCoreBreakdownSet(fp, "SNES_CORE_BREAKDOWN_NORMAL", s.coreNormal)) return FALSE;
    if (!WriteSnesPpuBreakdownSet(fp, "SNES_PPU_BREAKDOWN_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesPpuBreakdownSet(fp, "SNES_PPU_BREAKDOWN_NORMAL", s.coreNormal)) return FALSE;
    if (!WriteSnesPpuWorkSet(fp, "SNES_PPU_WORK_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesPpuWorkSet(fp, "SNES_PPU_WORK_NORMAL", s.coreNormal)) return FALSE;
    if (!WriteSnesDmaBreakdownSet(fp, "SNES_DMA_BREAKDOWN_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesDmaBreakdownSet(fp, "SNES_DMA_BREAKDOWN_NORMAL", s.coreNormal)) return FALSE;

    if (!WriteSnesBgChrSummarySet(fp, "SNES_BG_CHR_SUMMARY_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesBgChrSummarySet(fp, "SNES_BG_CHR_SUMMARY_NORMAL", s.coreNormal)) return FALSE;
    if (!WriteSnesBgChrLayerDepthSet(fp, "SNES_BG_CHR_LAYER_DEPTH_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesBgChrLayerDepthSet(fp, "SNES_BG_CHR_LAYER_DEPTH_NORMAL", s.coreNormal)) return FALSE;
    if (!WriteSnesBgChrFlagsSet(fp, "SNES_BG_CHR_FLAGS_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesBgChrFlagsSet(fp, "SNES_BG_CHR_FLAGS_NORMAL", s.coreNormal)) return FALSE;

    if (!WriteSnesDeepSet(fp, "SNES_DEEP_BREAKDOWN_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesDeepSet(fp, "SNES_DEEP_BREAKDOWN_NORMAL", s.coreNormal)) return FALSE;
    if (!WriteSnesDeepReconcileSet(fp, "SNES_DEEP_RECONCILE_ALL", s.coreAll)) return FALSE;
    if (!WriteSnesDeepReconcileSet(fp, "SNES_DEEP_RECONCILE_NORMAL", s.coreNormal)) return FALSE;

    if (fprintf(fp,
        "\n[WORST_FRONTEND]\n"
        "rank,host_tick,class,state,core,fs_class,dominant,tick,frontend,"
        "core,vblank,input_poll,input_snapshot_prepare,input_logic,"
        "pre_core_or_render,post_core,upload,render_host,audio_drain,"
        "post_render,other\n") < 0)
        return FALSE;

    for (Int32 i = 0; i < 16; ++i)
    {
        const WorstT &w = s.worst[i];
        if (!w.frontend)
            continue;

        if (fprintf(fp,
            "%d,%u,%s,%s,%s,%s,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
            i + 1,
            (unsigned)w.serial,
            w.spike ? "SPIKE" : "NORMAL",
            StateName(w.state),
            CoreName(w.core),
            SafeFrameskipClassName(w.fsClass),
            DominantName(w),
            (unsigned)w.tick,
            (unsigned)w.frontend,
            (unsigned)w.coreCycles,
            (unsigned)w.vblank,
            (unsigned)w.inputPoll,
            (unsigned)w.inputSnapshot,
            (unsigned)w.inputLogic,
            (unsigned)w.preCore,
            (unsigned)w.postCore,
            (unsigned)w.upload,
            (unsigned)w.renderHost,
            (unsigned)w.audioDrain,
            (unsigned)w.postRender,
            (unsigned)w.other) < 0)
            return FALSE;
    }

    if (fprintf(fp,
        "\n[NOTES]\n"
        "- Diagnostic-only: AURORA_FRONTEND_PROFILER=0 still removes the implementation TU and callsites.\n"
        "- Supports SNES, NES/QuickNES, FDS/FCEUmm, Sega/PicoDrive, PCE, GB/Gambatte and GBA/gpSP.\n"
        "- SPIKE split is report-only; it does not suppress, delay, skip, or reschedule emulator work.\n"
        "- input_snapshot_prepare includes pad snapshots plus tiny button-OR preparation before _MainLoopInputProcess.\n"
        "- upload is nested in post_core and must not be added to post_core again.\n"
        "- render_setup/game_draw/ui/gp_flush/audio_drain are nested in render_total.\n"
        "- audio_drain measures existing Aud_BufferedAsyncStart call(s); audio behavior is unchanged.\n"
        "- FS telemetry records debt/period/recovery in the same host tick as frontend/audio data.\n"
        "- v4.6 host scheduler may rebase stale NORMAL-policy debt only after a real post-skip presentation proves host work fits one learned period.\n"
        "- CRC unlimited policy is excluded from that recovery rebase.\n"
        "- FS_OFF / FS_ON_PRESENTED / FS_ON_SKIPPED / FS_ON_BLOCKED are separated per core; mixed sessions remain analyzable.\n"
        "- SNES SPC requested/consumed cycles and DSP samples/chunks are read-only diagnostics; emulated audio timing is not changed.\n"
        "- skip_pct_x100 is percentage times 100 among allowed FS_ON ticks (e.g. 1250 = 12.50%%).\n"
        "- Standalone auroraprofile is the clean frontend baseline; do not combine with c4profile for final numbers.\n"
        "- No filesystem write occurs during gameplay. Menu entry waits 60 visible menu frames before snapshot.\n"
        "- Initial no-core browser auto-snapshots once after 120 IDLE_UI frames.\n") < 0)
        return FALSE;

    return ferror(fp) ? FALSE : TRUE;
}

static Bool SavePending(void)
{
    Char path[192];
    const StampT &t = s_pending.stamp;
    snprintf(path, sizeof(path),
        "mass0:/SNESticle/auroraprof_%04u%02u%02u_%02u%02u%02u_%03u.txt",
        (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
        (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second,
        (unsigned)s_pending.serial);

    BgmIOBegin();
    FILE *fp = fopen(path, "wb");
    if (!fp)
    {
        snprintf(path, sizeof(path),
            "mass0:/AURORAPROF_%04u%02u%02u_%02u%02u%02u_%03u.txt",
            (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
            (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second,
            (unsigned)s_pending.serial);
        fp = fopen(path, "wb");
    }

    Bool ok = fp ? WriteDump(fp) : FALSE;
    if (fp)
    {
        if (fflush(fp) != 0) ok = FALSE;
        if (fclose(fp) != 0) ok = FALSE;
    }
    BgmIOEnd();

    if (ok)
        MainLoopStatusPrintf(180, "Aurora profiler v4.6 saved: %s", path);
    else
        MainLoopStatusPrintf(180, "Aurora profiler v4.6: dump write failed.");

    return ok;
}

} /* namespace */

extern "C" {

void AuroraFrontendProfilerTickBegin(void)
{
    if (s_writePending)
    {
        s_frame.open = FALSE;
        return;
    }

    memset(&s_frame, 0, sizeof(s_frame));
    s_frame.state = AURORA_FP_IDLE_UI;
    s_frame.core = AURORA_FP_CORE_NONE;
    s_frame.start = ProfCtrGetCycle();
    s_frame.open = TRUE;
}

void AuroraFrontendProfilerSetContext(Uint32 state, Uint32 core)
{
    if (!s_frame.open)
        return;

    s_frame.state =
        state < AURORA_FP_STATE_COUNT ? state : AURORA_FP_IDLE_UI;

    if (s_frame.state == AURORA_FP_IDLE_UI)
        s_frame.core = AURORA_FP_CORE_NONE;
    else
        s_frame.core =
            core < AURORA_FP_CORE_COUNT ? core : AURORA_FP_CORE_NONE;

    BeginSpan(SPAN_PROCESS_PRE_CORE);
}

void AuroraFrontendProfilerSafeFrameskip(
    Bool enabled, Bool allowed, Bool skipped)
{
    if (!s_frame.open)
        return;

    s_frame.fsSeen = TRUE;
    s_frame.fsEnabled = enabled ? TRUE : FALSE;
    s_frame.fsAllowed = allowed ? TRUE : FALSE;
    s_frame.fsSkipped = skipped ? TRUE : FALSE;
}

void AuroraFrontendProfilerSafeFrameskipDecision(
    Uint32 period, Int32 diff, Uint32 lastPresentedWork,
    Bool meaningfulDebt, Bool measuredOverrun,
    Bool unlimited, Bool skipped, Bool recoveryPending)
{
    if (!s_frame.open)
        return;

    s_frame.fsDetailSeen = TRUE;
    s_frame.fsPeriod = period;
    s_frame.fsDiff = diff;
    s_frame.fsLastPresentedWork = lastPresentedWork;
    s_frame.fsMeaningfulDebt = meaningfulDebt ? TRUE : FALSE;
    s_frame.fsMeasuredOverrun = measuredOverrun ? TRUE : FALSE;
    s_frame.fsUnlimited = unlimited ? TRUE : FALSE;
    s_frame.fsDecisionSkipped = skipped ? TRUE : FALSE;
    s_frame.fsRecoveryPendingAtDecision =
        recoveryPending ? TRUE : FALSE;
}

void AuroraFrontendProfilerSafeFrameskipRecovery(
    Uint32 presentedWork, Bool recoveryPending, Bool rebased)
{
    if (!s_frame.open)
        return;

    s_frame.fsRecoverySeen = TRUE;
    s_frame.fsRecoveryWork = presentedWork;
    s_frame.fsRecoveryPendingAtPresent =
        recoveryPending ? TRUE : FALSE;
    s_frame.fsRecoveryRebased = rebased ? TRUE : FALSE;
}

void AuroraFrontendProfilerTickEnd(void)
{
    if (!s_frame.open)
        return;

    EndSpan(SPAN_POST_RENDER);

    const Uint32 now = ProfCtrGetCycle();
    const Uint32 tick = (Uint32)(now - s_frame.start);

    for (Uint32 i = 0; i < SPAN_COUNT; ++i)
        s_frame.spanOpen[i] = FALSE;

    const Uint32 state =
        s_frame.state < AURORA_FP_STATE_COUNT
            ? s_frame.state : AURORA_FP_IDLE_UI;
    const Uint32 core =
        s_frame.core < AURORA_FP_CORE_COUNT
            ? s_frame.core : AURORA_FP_CORE_NONE;

    const DerivedT d = Derive(tick);
    const Bool spike =
        d.frontend > AURORA_FP_FRONTEND_SPIKE_BUDGET ? TRUE : FALSE;

    AccumulateBucket(s_live.stateAll[state], tick, d);
    if (spike) AccumulateBucket(s_live.stateSpike[state], tick, d);
    else       AccumulateBucket(s_live.stateNormal[state], tick, d);

    ++s_live.totalFrames;
    if (spike) ++s_live.spikeFrames;
    else       ++s_live.normalFrames;

    if (state == AURORA_FP_GAMEPLAY)
    {
        ++s_live.gameplayFrames;
        AccumulateBucket(s_live.coreAll[core], tick, d);
        if (spike) AccumulateBucket(s_live.coreSpike[core], tick, d);
        else       AccumulateBucket(s_live.coreNormal[core], tick, d);

        const Uint32 fsClass = SafeFrameskipClassForFrame();
        AccumulateSafeFrameskipDiagnostic(core);
        AccumulateBucket(s_live.fsAll[core][fsClass], tick, d);
        if (spike)
            AccumulateBucket(s_live.fsSpike[core][fsClass], tick, d);
        else
            AccumulateBucket(s_live.fsNormal[core][fsClass], tick, d);
    }
    else if (state == AURORA_FP_MENU_WITH_CORE)
    {
        ++s_live.menuFrames;
    }
    else
    {
        ++s_live.idleFrames;
    }

    InsertWorst(++s_hostSerial, state, core, tick, d, spike);
    s_frame.open = FALSE;

    if (s_captureArmed && state != AURORA_FP_GAMEPLAY)
    {
        if (s_menuFramesNeeded > 0)
            --s_menuFramesNeeded;
        if (s_menuFramesNeeded == 0)
            SealSnapshot();
    }
    else if (!s_initialIdleDumpDone &&
             !s_captureArmed &&
             !s_writePending &&
             s_live.gameplayFrames == 0 &&
             s_live.idleFrames >= 120)
    {
        s_initialIdleDumpDone = TRUE;
        SealSnapshot();
    }
}

void AuroraFrontendProfilerInputPollBegin(void) { BeginSpan(SPAN_INPUT_POLL); }
void AuroraFrontendProfilerInputPollEnd(void) { EndSpan(SPAN_INPUT_POLL); }
void AuroraFrontendProfilerInputSnapshotBegin(void) { BeginSpan(SPAN_INPUT_SNAPSHOT); }

void AuroraFrontendProfilerFrontendInputBegin(void)
{
    EndSpan(SPAN_INPUT_SNAPSHOT);
    BeginSpan(SPAN_INPUT_LOGIC);
}

void AuroraFrontendProfilerFrontendInputEnd(void)
{
    EndSpan(SPAN_INPUT_LOGIC);
}

void AuroraFrontendProfilerCoreBegin(void)
{
    EndSpan(SPAN_PROCESS_PRE_CORE);
    BeginSpan(SPAN_CORE);
}

void AuroraFrontendProfilerCoreEnd(void)
{
    EndSpan(SPAN_CORE);
    BeginSpan(SPAN_PROCESS_POST_CORE);
}

void AuroraFrontendProfilerUploadBegin(void) { BeginSpan(SPAN_UPLOAD); }
void AuroraFrontendProfilerUploadEnd(void) { EndSpan(SPAN_UPLOAD); }

void AuroraFrontendProfilerRenderBegin(void)
{
    EndSpan(SPAN_PROCESS_POST_CORE);
    EndSpan(SPAN_PROCESS_PRE_CORE);
    BeginSpan(SPAN_RENDER);
}

void AuroraFrontendProfilerRenderEnd(void)
{
    EndSpan(SPAN_RENDER);
    BeginSpan(SPAN_POST_RENDER);
}

void AuroraFrontendProfilerRenderSetupBegin(void) { BeginSpan(SPAN_RENDER_SETUP); }
void AuroraFrontendProfilerRenderSetupEnd(void) { EndSpan(SPAN_RENDER_SETUP); }
void AuroraFrontendProfilerGameDrawBegin(void) { BeginSpan(SPAN_GAME_DRAW); }
void AuroraFrontendProfilerGameDrawEnd(void) { EndSpan(SPAN_GAME_DRAW); }
void AuroraFrontendProfilerUiBegin(void) { BeginSpan(SPAN_UI); }
void AuroraFrontendProfilerUiEnd(void) { EndSpan(SPAN_UI); }
void AuroraFrontendProfilerGpFlushBegin(void) { BeginSpan(SPAN_GP_FLUSH); }
void AuroraFrontendProfilerGpFlushEnd(void) { EndSpan(SPAN_GP_FLUSH); }
void AuroraFrontendProfilerVBlankBegin(void) { BeginSpan(SPAN_VBLANK); }
void AuroraFrontendProfilerVBlankEnd(void) { EndSpan(SPAN_VBLANK); }
void AuroraFrontendProfilerAudioDrainBegin(void) { BeginSpan(SPAN_AUDIO_DRAIN); }
void AuroraFrontendProfilerAudioDrainEnd(void) { EndSpan(SPAN_AUDIO_DRAIN); }

void AuroraFrontendProfilerAudioDrainFrames(Uint32 frames)
{
    if (s_frame.open)
        s_frame.audioDrainFrames += frames;
}

void AuroraFrontendProfilerAudioConvertBegin(Uint32 inputFrames)
{
    if (s_frame.open)
        s_frame.audioConvertInputFrames += inputFrames;
    BeginSpan(SPAN_AUDIO_CONVERT);
}
void AuroraFrontendProfilerAudioConvertEnd(void)
{
    EndSpan(SPAN_AUDIO_CONVERT);
}

void AuroraFrontendProfilerAudioFlushBegin(Uint32 outputFrames)
{
    if (s_frame.open)
        s_frame.audioFlushFrames += outputFrames;
    BeginSpan(SPAN_AUDIO_FLUSH);
}
void AuroraFrontendProfilerAudioFlushEnd(void)
{
    EndSpan(SPAN_AUDIO_FLUSH);
}

/* AURORA_SNES_CORE_PROFILER_V3_6_20261005 */
/* AURORA_SNES_BGCHR_PROFILER_V3_7_20261005 */
void AuroraFrontendProfilerSnesBgChrRecord(
    Uint32 bgIndex, Uint32 bitDepth, Uint32 tiles,
    Bool offset, Bool mosaic, Bool fineXNonZero, Bool hiresSubscreen,
    Uint32 startCycles, Uint32 decodeEndCycles, Uint32 endCycles)
{
    if (!s_frame.open || s_frame.core != AURORA_FP_CORE_SNES || bgIndex >= 4u)
        return;

    Uint32 depth;
    if (bitDepth == 2u) depth = 0u;
    else if (bitDepth == 4u) depth = 1u;
    else if (bitDepth == 8u) depth = 2u;
    else depth = 3u;

    const Uint32 decode = (Uint32)(decodeEndCycles - startCycles);
    const Uint32 total = (Uint32)(endCycles - startCycles);
    if (decode > total)
    {
        ++s_frame.snesBgChrBadRecords;
        return;
    }

    ++s_frame.snesBgChrCalls[bgIndex][depth];
    s_frame.snesBgChrTiles[bgIndex][depth] += tiles;
    s_frame.snesBgChrCycles[bgIndex][depth] += total;
    s_frame.snesBgChrDecodeCycles[bgIndex][depth] += decode;
    ++s_frame.snesBgChrMeasuredCalls;
    s_frame.snesBgChrMeasuredTiles += tiles;
    s_frame.snesBgChrMeasuredCycles += total;
    s_frame.snesBgChrMeasuredDecodeCycles += decode;

    const Bool flags[4] = { offset, mosaic, fineXNonZero, hiresSubscreen };
    for (Uint32 i = 0; i < 4u; ++i)
    {
        if (!flags[i]) continue;
        ++s_frame.snesBgChrFlagCalls[i];
        s_frame.snesBgChrFlagTiles[i] += tiles;
        s_frame.snesBgChrFlagCycles[i] += total;
        s_frame.snesBgChrFlagDecodeCycles[i] += decode;
    }
}

/* AURORA_SNES_DEEP_OPT_V3_14_20261005 */
void AuroraFrontendProfilerSnesDeepRecord(
    Uint32 bucket, Uint32 cycles, Uint32 units)
{
    if (!s_frame.open || s_frame.core != AURORA_FP_CORE_SNES ||
        s_frame.snesProfileInvalid || bucket >= 68u)
        return;
    ++s_frame.snesDeepCalls[bucket];
    s_frame.snesDeepUnits[bucket] += units;
    s_frame.snesDeepCycles[bucket] += cycles;
}

void AuroraFrontendProfilerSnesChrTraitsRecord(
    Uint32 bitDepth, Uint32 opaqueRows, Uint32 transparentRows,
    Uint32 flip0OpaqueRows, Uint32 flippedOpaqueRows)
{
    if (!s_frame.open || s_frame.core != AURORA_FP_CORE_SNES ||
        s_frame.snesProfileInvalid || (bitDepth != 2u && bitDepth != 4u))
        return;
    const Uint32 base = bitDepth == 4u
        ? AURORA_SNES_DEEP_CHR4_OPAQUE_ROWS : AURORA_SNES_DEEP_CHR2_OPAQUE_ROWS;
    const Uint32 flipBase = bitDepth == 4u
        ? AURORA_SNES_DEEP_CHR4_OPAQUE_FLIP0 : AURORA_SNES_DEEP_CHR2_OPAQUE_FLIP0;
    ++s_frame.snesDeepCalls[base];
    s_frame.snesDeepUnits[base] += opaqueRows;
    ++s_frame.snesDeepCalls[base + 1u];
    s_frame.snesDeepUnits[base + 1u] += transparentRows;
    ++s_frame.snesDeepCalls[flipBase];
    s_frame.snesDeepUnits[flipBase] += flip0OpaqueRows;
    ++s_frame.snesDeepCalls[flipBase + 1u];
    s_frame.snesDeepUnits[flipBase + 1u] += flippedOpaqueRows;
}

void AuroraFrontendProfilerSnesCostScopeEnter(Int32 bucket)
{
    if (!s_frame.open || s_frame.core != AURORA_FP_CORE_SNES ||
        s_frame.snesProfileInvalid)
        return;
    if (bucket < 0 || bucket >= 5)
    {
        s_frame.snesProfileInvalid = TRUE;
        return;
    }

    const Uint32 now = ProfCtrGetCycle();
    if (s_frame.snesCostDepth > 0)
    {
        const Int32 parent =
            s_frame.snesCostStackBucket[s_frame.snesCostDepth - 1];
        s_frame.snesCostCycles[parent] +=
            (Uint32)(now - s_frame.snesCostStackStart[s_frame.snesCostDepth - 1]);
    }
    if (s_frame.snesCostDepth >= 16)
    {
        s_frame.snesProfileInvalid = TRUE;
        return;
    }
    s_frame.snesCostStackBucket[s_frame.snesCostDepth] = bucket;
    s_frame.snesCostStackStart[s_frame.snesCostDepth] = now;
    ++s_frame.snesCostDepth;
}

void AuroraFrontendProfilerSnesCostScopeLeave(Int32 bucket)
{
    if (!s_frame.open || s_frame.core != AURORA_FP_CORE_SNES ||
        s_frame.snesProfileInvalid)
        return;
    if (s_frame.snesCostDepth <= 0 ||
        s_frame.snesCostStackBucket[s_frame.snesCostDepth - 1] != bucket)
    {
        s_frame.snesProfileInvalid = TRUE;
        return;
    }

    const Uint32 now = ProfCtrGetCycle();
    s_frame.snesCostCycles[bucket] +=
        (Uint32)(now - s_frame.snesCostStackStart[s_frame.snesCostDepth - 1]);
    --s_frame.snesCostDepth;
    if (s_frame.snesCostDepth > 0)
        s_frame.snesCostStackStart[s_frame.snesCostDepth - 1] = now;
}

void AuroraFrontendProfilerSnesPpuDetailEnter(Int32 bucket)
{
    if (!s_frame.open || s_frame.core != AURORA_FP_CORE_SNES ||
        s_frame.snesProfileInvalid)
        return;
    if (bucket < 0 || bucket >= 18)
    {
        s_frame.snesProfileInvalid = TRUE;
        return;
    }

    const Uint32 now = ProfCtrGetCycle();
    if (s_frame.snesPpuDepth > 0)
    {
        const Int32 parent =
            s_frame.snesPpuStackBucket[s_frame.snesPpuDepth - 1];
        s_frame.snesPpuCycles[parent] +=
            (Uint32)(now - s_frame.snesPpuStackStart[s_frame.snesPpuDepth - 1]);
    }
    if (s_frame.snesPpuDepth >= 16)
    {
        s_frame.snesProfileInvalid = TRUE;
        return;
    }
    s_frame.snesPpuStackBucket[s_frame.snesPpuDepth] = bucket;
    s_frame.snesPpuStackStart[s_frame.snesPpuDepth] = now;
    ++s_frame.snesPpuDepth;
}

void AuroraFrontendProfilerSnesPpuDetailLeave(Int32 bucket)
{
    if (!s_frame.open || s_frame.core != AURORA_FP_CORE_SNES ||
        s_frame.snesProfileInvalid)
        return;
    if (s_frame.snesPpuDepth <= 0 ||
        s_frame.snesPpuStackBucket[s_frame.snesPpuDepth - 1] != bucket)
    {
        s_frame.snesProfileInvalid = TRUE;
        return;
    }

    const Uint32 now = ProfCtrGetCycle();
    s_frame.snesPpuCycles[bucket] +=
        (Uint32)(now - s_frame.snesPpuStackStart[s_frame.snesPpuDepth - 1]);
    --s_frame.snesPpuDepth;
    if (s_frame.snesPpuDepth > 0)
        s_frame.snesPpuStackStart[s_frame.snesPpuDepth - 1] = now;
}

void AuroraFrontendProfilerSnesPpuWork(
    Uint32 mainMask, Uint32 subMask, Uint32 mapFetches, Uint32 chrDecodes)
{
    if (!s_frame.open || s_frame.core != AURORA_FP_CORE_SNES ||
        s_frame.snesProfileInvalid)
        return;
    s_frame.snesPpuBgMapFetches += mapFetches;
    s_frame.snesPpuBgChrDecodes += chrDecodes;
    s_frame.snesPpuMainBgLayers += SnesBgBits(mainMask);
    s_frame.snesPpuSubBgLayers += SnesBgBits(subMask);
}

void AuroraFrontendProfilerSnesPpuLine(
    Uint32 bgMode, Uint32 objOamRefs, Uint32 objTiles,
    Bool objEnabled, Bool rangeOver, Bool timeOver,
    Bool windowed, Bool colorMath, Bool subscreen,
    Bool hires, Bool directColor)
{
    if (!s_frame.open || s_frame.core != AURORA_FP_CORE_SNES ||
        s_frame.snesProfileInvalid)
        return;
    ++s_frame.snesPpuRenderedLines;
    if (bgMode < 8u) ++s_frame.snesPpuModeLines[bgMode];
    s_frame.snesPpuObjOamRefs += objOamRefs;
    s_frame.snesPpuObjTiles += objTiles;
    if (objEnabled) ++s_frame.snesPpuObjEnabledLines;
    if (rangeOver) ++s_frame.snesPpuObjRangeOverLines;
    if (timeOver) ++s_frame.snesPpuObjTimeOverLines;
    if (windowed) ++s_frame.snesPpuWindowLines;
    if (colorMath) ++s_frame.snesPpuColorMathLines;
    if (subscreen) ++s_frame.snesPpuSubscreenLines;
    if (hires) ++s_frame.snesPpuHiresLines;
    if (directColor) ++s_frame.snesPpuDirectColorLines;
}

void AuroraFrontendProfilerSnesCpuBudget(Uint32 cycles)
{
    if (!s_frame.open || s_frame.core != AURORA_FP_CORE_SNES) return;
    ++s_frame.snesCpuBudgetCalls;
    s_frame.snesCpuBudgetCycles += cycles;
}

void AuroraFrontendProfilerSnesPpuSyncCall(void)
{
    if (s_frame.open && s_frame.core == AURORA_FP_CORE_SNES)
        ++s_frame.snesPpuSyncCalls;
}

void AuroraFrontendProfilerSnesMdmaBegin(void) { BeginSpan(SPAN_SNES_MDMA); }
void AuroraFrontendProfilerSnesMdmaEnd(void) { EndSpan(SPAN_SNES_MDMA); }
void AuroraFrontendProfilerSnesHdmaSetupBegin(void) { BeginSpan(SPAN_SNES_HDMA_SETUP); }
void AuroraFrontendProfilerSnesHdmaSetupEnd(void) { EndSpan(SPAN_SNES_HDMA_SETUP); }
void AuroraFrontendProfilerSnesHdmaDataBegin(void) { BeginSpan(SPAN_SNES_HDMA_DATA); }
void AuroraFrontendProfilerSnesHdmaDataEnd(void) { EndSpan(SPAN_SNES_HDMA_DATA); }

void AuroraFrontendProfilerSnesSpcBegin(Uint32 requestedCycles)
{
    if (s_frame.open)
    {
        ++s_frame.spcCalls;
        s_frame.spcRequestedCycles += requestedCycles;
    }
    BeginSpan(SPAN_SNES_SPC);
}
void AuroraFrontendProfilerSnesSpcEnd(Uint32 consumedCycles)
{
    if (s_frame.open)
        s_frame.spcConsumedCycles += consumedCycles;
    EndSpan(SPAN_SNES_SPC);
}

void AuroraFrontendProfilerSnesDspBegin(void)
{
    if (s_frame.open)
        ++s_frame.dspMixCalls;
    BeginSpan(SPAN_SNES_DSP_TOTAL);
}
void AuroraFrontendProfilerSnesDspEnd(void)
{
    EndSpan(SPAN_SNES_DSP_TOTAL);
}
void AuroraFrontendProfilerSnesDspChunk(Uint32 samples)
{
    if (s_frame.open)
    {
        ++s_frame.dspChunks;
        s_frame.dspSamples += samples;
    }
}
void AuroraFrontendProfilerSnesDspSyncBegin(void)
    { BeginSpan(SPAN_SNES_DSP_SYNC); }
void AuroraFrontendProfilerSnesDspSyncEnd(void)
    { EndSpan(SPAN_SNES_DSP_SYNC); }
void AuroraFrontendProfilerSnesDspNoiseBegin(void)
    { BeginSpan(SPAN_SNES_DSP_NOISE); }
void AuroraFrontendProfilerSnesDspNoiseEnd(void)
    { EndSpan(SPAN_SNES_DSP_NOISE); }
void AuroraFrontendProfilerSnesDspVoicesBegin(void)
    { BeginSpan(SPAN_SNES_DSP_VOICES); }
void AuroraFrontendProfilerSnesDspVoicesEnd(void)
    { EndSpan(SPAN_SNES_DSP_VOICES); }

/* AURORA_SNES_DSP_VOICES_PROFILER_V3_5_1_20261005 */
void AuroraFrontendProfilerSnesDspEnvelopeBegin(void)
{
    if (s_frame.open) ++s_frame.dspVoiceEnvelopeCalls;
    BeginSpan(SPAN_SNES_DSP_ENVELOPE);
}
void AuroraFrontendProfilerSnesDspEnvelopeEnd(Bool active)
{
    if (s_frame.open && active) ++s_frame.dspVoiceEnvelopeActive;
    EndSpan(SPAN_SNES_DSP_ENVELOPE);
}

void AuroraFrontendProfilerSnesDspSampleNormalBegin(void)
{
    if (s_frame.open) ++s_frame.dspVoiceSampleNormalCalls;
    BeginSpan(SPAN_SNES_DSP_SAMPLE_NORMAL);
}
void AuroraFrontendProfilerSnesDspSampleNormalEnd(Bool produced, Uint32 samples)
{
    if (s_frame.open && produced)
        s_frame.dspVoiceSampleNormalSamples += samples;
    EndSpan(SPAN_SNES_DSP_SAMPLE_NORMAL);
}

void AuroraFrontendProfilerSnesDspSamplePmonBegin(void)
{
    if (s_frame.open) ++s_frame.dspVoiceSamplePmonCalls;
    BeginSpan(SPAN_SNES_DSP_SAMPLE_PMON);
}
void AuroraFrontendProfilerSnesDspSamplePmonEnd(Bool produced, Uint32 samples)
{
    if (s_frame.open && produced)
        s_frame.dspVoiceSamplePmonSamples += samples;
    EndSpan(SPAN_SNES_DSP_SAMPLE_PMON);
}

void AuroraFrontendProfilerSnesDspBrrDecodeBegin(void)
{
    if (s_frame.open) ++s_frame.dspVoiceBrrDecodes;
    BeginSpan(SPAN_SNES_DSP_BRR_DECODE);
}
void AuroraFrontendProfilerSnesDspBrrDecodeEnd(void)
    { EndSpan(SPAN_SNES_DSP_BRR_DECODE); }

void AuroraFrontendProfilerSnesDspPitchmodFeederBegin(void)
{
    if (s_frame.open) ++s_frame.dspVoicePitchmodFeeders;
    BeginSpan(SPAN_SNES_DSP_PITCHMOD_FEEDER);
}
void AuroraFrontendProfilerSnesDspPitchmodFeederEnd(void)
    { EndSpan(SPAN_SNES_DSP_PITCHMOD_FEEDER); }

void AuroraFrontendProfilerSnesDspMixStereoBegin(
    Bool noise, Bool echo, Uint32 samples)
{
    if (s_frame.open)
    {
        ++s_frame.dspVoiceMixCalls;
        s_frame.dspVoiceMixSamples += samples;
        if (noise) ++s_frame.dspVoiceNoiseMixes;
        if (echo)
        {
            ++s_frame.dspVoiceMixEchoCalls;
            s_frame.dspVoiceMixEchoSamples += samples;
        }
        else
        {
            ++s_frame.dspVoiceMixDryCalls;
            s_frame.dspVoiceMixDrySamples += samples;
        }
    }
    BeginSpan(echo ? SPAN_SNES_DSP_MIX_ECHO : SPAN_SNES_DSP_MIX_DRY);
}
void AuroraFrontendProfilerSnesDspMixStereoEnd(Bool echo)
    { EndSpan(echo ? SPAN_SNES_DSP_MIX_ECHO : SPAN_SNES_DSP_MIX_DRY); }

void AuroraFrontendProfilerSnesDspSilentFastpath(void)
{
    if (s_frame.open) ++s_frame.dspVoiceSilentFastpathHits;
}

void AuroraFrontendProfilerSnesDspVoiceGain(
    Uint32 envProxy, Int32 volL, Int32 volR,
    Bool pmon, Bool echo, Bool feedsPitchMod)
{
    if (!s_frame.open) return;

    const Uint32 absL = (Uint32)(volL < 0 ? -volL : volL);
    const Uint32 absR = (Uint32)(volR < 0 ? -volR : volR);
    const Uint32 maxVol = absL > absR ? absL : absR;
    const Uint32 gain = envProxy * maxVol;

    if (!maxVol)
        ++s_frame.dspVoiceGainZeroVolume;
    else if (!envProxy)
        ++s_frame.dspVoiceGainEnvelopeZero;
    else if (gain <= 256u)
        ++s_frame.dspVoiceGainVeryLow;
    else if (gain <= 1024u)
        ++s_frame.dspVoiceGainLow;
    else
        ++s_frame.dspVoiceGainNormal;

    if (gain <= 1024u)
    {
        if (pmon) ++s_frame.dspVoiceLowGainPmon;
        if (echo) ++s_frame.dspVoiceLowGainEcho;
        if (feedsPitchMod) ++s_frame.dspVoiceLowGainFeedsPmon;
    }
}

void AuroraFrontendProfilerSnesDspEchoBegin(void)
    { BeginSpan(SPAN_SNES_DSP_ECHO); }
void AuroraFrontendProfilerSnesDspEchoEnd(void)
    { EndSpan(SPAN_SNES_DSP_ECHO); }
void AuroraFrontendProfilerSnesDspFinalBegin(void)
    { BeginSpan(SPAN_SNES_DSP_FINAL); }
void AuroraFrontendProfilerSnesDspFinalEnd(void)
    { EndSpan(SPAN_SNES_DSP_FINAL); }

void AuroraFrontendProfilerMenuEnter(void)
{
    if (s_writePending || s_captureArmed || !s_live.gameplayFrames)
        return;
    s_captureArmed = TRUE;
    s_menuFramesNeeded = 60u;
}

void AuroraFrontendProfilerMenuUpdate(Bool canWrite)
{
    if (!canWrite || !s_writePending)
        return;
    (void)SavePending();
    s_writePending = FALSE;
}

} /* extern "C" */

#endif /* AURORA_FRONTEND_PROFILER */
