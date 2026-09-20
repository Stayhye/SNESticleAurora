#ifndef AURORA_RUNTIME_TRACE_H
#define AURORA_RUNTIME_TRACE_H

/*
 * Aurora Runtime Trace
 * ====================
 *
 * Project-wide diagnostic infrastructure owned by Aurora. SNES is the first
 * client, not the architectural boundary.
 *
 * Keep three concerns separate:
 *   1. engine/storage: aurora_runtime_trace.c
 *   2. probe policy:   aurora_runtime_trace_profile.h
 *   3. client hooks:   currently SNES call sites / instruction rings
 *
 * Investigation-specific policy (game/frame/line selection) must live in the
 * profile, never in the storage engine or emulator hot paths.
 *
 * Existing numeric IDs are a binary contract. Add; do not renumber.
 */
/* AURORA_RUNTIME_TRACE_INFRA_V1_20260918 */
/* AURORA_SNES_BINARY_TRACE_V6D_SPARSE_HIGHSIGNAL_20260918 */
/* AURORA_SNES_BINARY_TRACE_V7_VISUAL_BREADCRUMBS_20260918 */
/* AURORA_SNES_BINARY_TRACE_V7_R9_DKC_PHASEPROBE_20260918 */
/* AURORA_SNES_BINARY_TRACE_V7_R10_RETURN_BOUNDARY_20260918 */

#include "types.h"

#ifndef AURORA_RUNTIME_TRACE
#define AURORA_RUNTIME_TRACE 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct SNSpc_t;

#if AURORA_RUNTIME_TRACE

extern volatile Uint32 g_AuroraTraceEnabled;

/* Binary record IDs: existing values are wire-format stable. */
enum
{
    ATR_APUIO_R = 0x01,
    ATR_APUIO_W = 0x02,
    ATR_SPCIO_R = 0x03,
    ATR_SPCIO_W = 0x04,
    ATR_DSP_Q   = 0x05,
    ATR_DSP_W   = 0x06,
    ATR_KON     = 0x07,
    ATR_KOFF    = 0x08,
    ATR_BRR     = 0x09,
    ATR_MIX     = 0x0A,
    ATR_MDMA    = 0x0B,
    ATR_HDMA    = 0x0C,
    ATR_HEART   = 0x0D,
    ATR_TOGGLE  = 0x0E,
    ATR_CLOSE_FENCE = 0x0F,
    ATR_CLOSE_ACK = 0x10,
    ATR_PHASE = 0x11,
    ATR_SNAPSHOT = 0xF1,
    ATR_CONTEXT = 0xF0
};

enum
{
    ATR_F_PRE  = 0x01,
    ATR_F_POST = 0x02
};

enum
{
    ATR_P_NONE = 0,
    ATR_P_COMMIT = 1,
    ATR_P_CONTEXT = 2,
    ATR_P_FLUSH = 3 /* V6D: fflush only */
};

enum
{
    ATR_SNAP_FRAME_BEGIN = 1,
    ATR_SNAP_FRAME_END   = 2,
    ATR_SNAP_MDMA        = 3,
    ATR_SNAP_HDMA        = 4,
    ATR_SNAP_APUIO       = 5,
    ATR_SNAP_BRR         = 6
};

/* Generic durable phase record with the current SNES client's phase
 * namespace. The engine does not select game/frame policy. */
enum
{
    ATR_PHASE_FRAME_ENTER        = 0x0101,
    ATR_PHASE_REGION_READY       = 0x0102,
    ATR_PHASE_INPUT_LATCHED      = 0x0103,
    ATR_PHASE_COUNTERS_RESET     = 0x0104,
    ATR_PHASE_RENDER_BEGIN       = 0x0105,
    ATR_PHASE_PPU_BEGIN          = 0x0106,
    ATR_PHASE_VISIBLE_BEGIN      = 0x0107,
    ATR_PHASE_VISIBLE_END        = 0x0108,
    ATR_PHASE_RENDER_SYNC_BEGIN  = 0x0109,
    ATR_PHASE_RENDER_SYNC_END    = 0x010A,
    ATR_PHASE_PPU_END            = 0x010B,
    ATR_PHASE_RENDER_END         = 0x010C,
    ATR_PHASE_VBLANK_BEGIN       = 0x010D,
    ATR_PHASE_NMI_SIGNALED       = 0x010E,
    ATR_PHASE_VBLANK_END         = 0x010F,
    ATR_PHASE_FINAL_PPU_BEGIN    = 0x0110,
    ATR_PHASE_FINAL_PPU_END      = 0x0111,
    ATR_PHASE_FIELD_ADVANCED     = 0x0112,
    ATR_PHASE_SPC_SYNC_BEGIN     = 0x0113,
    ATR_PHASE_SPC_SYNC_END       = 0x0114,
    ATR_PHASE_SPC_TIMERS_DONE    = 0x0115,
    ATR_PHASE_MIX_BEGIN          = 0x0116,
    ATR_PHASE_MIX_END            = 0x0117,
    ATR_PHASE_FRAME_EXIT         = 0x0118,
    ATR_PHASE_FRAME_INCREMENTED  = 0x0119,
    ATR_PHASE_MID_SYNC_BEGIN     = 0x0120,
    ATR_PHASE_MID_SYNC_END       = 0x0121,
    ATR_PHASE_LINE_ENTER         = 0x0201,
    ATR_PHASE_HOST_RETURNED       = 0x0301
};

/* Engine lifecycle / capture ownership. */
Bool AuroraTraceToggleRuntime(void);
Bool AuroraTraceIsEnabled(void);
const char *AuroraTraceProfileName(void);
void AuroraTraceBeginGame(const char *game, const char *core);
void AuroraTraceClose(void);

/* Generic low-cost timeline and durable phase interfaces. */
void AuroraTracePeriodic(Uint32 frame);
void AuroraTraceFrameBegin(Uint32 emuFrame);
void AuroraTraceFrameEnd(Uint32 emuFrame);
void AuroraTraceBreadcrumb(Uint8 kind, Uint16 payload);
void AuroraTracePhase(Uint16 phase, Uint32 a, Uint32 b);

/* Current SNES client helper; selection policy lives in the profile header. */
void AuroraTraceLinePhase(Uint32 line, Uint32 cpuFrame);

/* Generic fixed-size event record. */
void AuroraTraceRecord(
    Uint8 type, Uint8 flags, Uint16 x, Uint32 a, Uint32 b, Uint8 persist);

void AuroraTraceArmVoice(Int32 channel);
void AuroraTraceBrrPre(Int32 channel, Uint16 addr, Int32 prev14, Int32 prev15);
void AuroraTraceBrrPost(Int32 channel, Uint16 addr, Uint8 flags,
                        Int32 sample0, Int32 sample1,
                        Int32 sample14, Int32 sample15);

void AuroraRuntimeTraceSPC(struct SNSpc_t *cpu, Uint16 pc, Uint8 opcode);

#else

#define g_AuroraTraceEnabled 0u
#define AuroraTraceToggleRuntime() FALSE
#define AuroraTraceIsEnabled() FALSE
#define AuroraTraceProfileName() "disabled"
#define AuroraTraceBeginGame(...) ((void)0)
#define AuroraTraceClose(...) ((void)0)
#define AuroraTracePeriodic(...) ((void)0)
#define AuroraTraceFrameBegin(...) ((void)0)
#define AuroraTraceFrameEnd(...) ((void)0)
#define AuroraTraceBreadcrumb(...) ((void)0)
#define AuroraTracePhase(...) ((void)0)
#define AuroraTraceLinePhase(...) ((void)0)
#define AuroraTraceRecord(...) ((void)0)
#define AuroraTraceArmVoice(...) ((void)0)
#define AuroraTraceBrrPre(...) ((void)0)
#define AuroraTraceBrrPost(...) ((void)0)
#define AuroraRuntimeTraceSPC(...) ((void)0)

#endif

#ifdef __cplusplus
}
#endif
#endif
