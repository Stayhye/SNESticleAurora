#ifndef AURORA_RUNTIME_TRACE_PROFILE_H
#define AURORA_RUNTIME_TRACE_PROFILE_H

/*
 * Aurora Runtime Trace — probe profile
 * ====================================
 *
 * This file is POLICY, not the trace engine.
 *
 * The engine (aurora_runtime_trace.c) owns:
 *   - binary format and record sequencing;
 *   - crash-safe persistence / close fences / ACKs;
 *   - instruction rings and context dumps;
 *   - generic event and phase emission.
 *
 * A profile decides WHEN expensive probes are active. Temporary game/frame/
 * line conditions belong here instead of being hard-coded in the engine.
 *
 * Current client: SNES.
 * Future cores may reuse the engine and add their own client selectors.
 */

#include <string.h>
#include "types.h"
#include "platform/ps2/system/aurora_runtime_trace.h"

#define AURORA_TRACE_PROFILE_SCOPE_DISABLED    0
#define AURORA_TRACE_PROFILE_SCOPE_EXACT_FRAME 1
#define AURORA_TRACE_PROFILE_SCOPE_FRAME_RANGE 2
#define AURORA_TRACE_PROFILE_SCOPE_ALL_FRAMES  3

/*
 * Active profile.
 *
 * This preserves the current V7R10 DKC investigation exactly, but the choice
 * now lives here rather than inside the engine.
 *
 * Useful alternatives:
 *   boot/black-screen window:
 *     SCOPE = FRAME_RANGE, FIRST = 0, LAST = 16
 *   all traced frames:
 *     SCOPE = ALL_FRAMES
 *   lightweight trace only:
 *     SCOPE = DISABLED
 */
/* AURORA_TRACE_PROFILE_DKC_HUNT_V1_20260918
 *
 * DKC hard-freeze hunt
 * --------------------
 * Captures a window, not one guessed frame. Observed failing runs have left
 * their last durable FrameBegin around emu frames 156 and 161; 150..170 leaves
 * margin on both sides while keeping the expensive close/reopen probes bounded.
 *
 * This is deliberately a PROFILE. Nothing below belongs in the trace engine.
 */
#define AURORA_TRACE_PROFILE_NAME \
    "snes-dkc-hunt-150-170-coarse-raster"
#define AURORA_TRACE_PROFILE_CORE "SNES"
#define AURORA_TRACE_PROFILE_SCOPE \
    AURORA_TRACE_PROFILE_SCOPE_FRAME_RANGE
#define AURORA_TRACE_PROFILE_FRAME_FIRST 150u
#define AURORA_TRACE_PROFILE_FRAME_LAST  170u

/* Keep enough local CPU history to recognize the code around each boundary. */
#define AURORA_TRACE_PROFILE_PHASE_CONTEXT_TAIL 8u

/* Every FrameBegin in the hunt window must already be durable before core work. */
#define AURORA_TRACE_PROFILE_FORCE_FRAME_COMMIT TRUE

/* Coarse raster grid. Hardware-sensitive boundaries below are always added. */
#define AURORA_TRACE_PROFILE_SNES_LINE_STRIDE 64u

static inline Bool AuroraTraceProfileCoreMatches(const char *core)
{
    const char *wanted = AURORA_TRACE_PROFILE_CORE;

    /* Empty target means any current/future Aurora trace client. */
    if (!wanted[0])
        return TRUE;
    if (!core)
        return FALSE;
    return strcmp(core, wanted) == 0 ? TRUE : FALSE;
}

static inline Bool AuroraTraceProfileFrameActive(const char *core, Uint32 frame)
{
    if (!AuroraTraceProfileCoreMatches(core))
        return FALSE;

#if AURORA_TRACE_PROFILE_SCOPE == AURORA_TRACE_PROFILE_SCOPE_DISABLED
    (void)frame;
    return FALSE;
#elif AURORA_TRACE_PROFILE_SCOPE == AURORA_TRACE_PROFILE_SCOPE_EXACT_FRAME
    return frame == AURORA_TRACE_PROFILE_FRAME_FIRST ? TRUE : FALSE;
#elif AURORA_TRACE_PROFILE_SCOPE == AURORA_TRACE_PROFILE_SCOPE_FRAME_RANGE
    return (frame >= AURORA_TRACE_PROFILE_FRAME_FIRST &&
            frame <= AURORA_TRACE_PROFILE_FRAME_LAST) ? TRUE : FALSE;
#elif AURORA_TRACE_PROFILE_SCOPE == AURORA_TRACE_PROFILE_SCOPE_ALL_FRAMES
    (void)frame;
    return TRUE;
#else
#error "Unknown AURORA_TRACE_PROFILE_SCOPE"
#endif
}

/*
 * Expensive durable phases selected by this investigation.
 *
 * We intentionally do NOT persist every R9/R10 phase on every frame. That
 * would make storage I/O part of the experiment. The selected before/after
 * pairs divide ExecuteFrame into useful regions while the scanline helper
 * narrows a visible/vblank scheduler stall.
 */
/* AURORA_EE_HANG_WATCHDOG_DKC_V2_20260918
 * DKC phase detail is RAM-only for the watchdog. No phase hook may trigger
 * fclose/reopen in the critical window. */
static inline Bool AuroraTraceProfilePhaseSelected(Uint16 phase)
{
    (void)phase;
    return FALSE;
}

static inline Bool AuroraTraceProfileForceFrameCommit(void)
{
    return AURORA_TRACE_PROFILE_FORCE_FRAME_COMMIT ? TRUE : FALSE;
}

/*
 * SNES client selector.
 *
 * Add/remove raster checkpoints here for PPU/IRQ/DMA investigations; do not
 * put game-specific line conditions into SnesSystem::ExecuteLine().
 */
static inline Bool AuroraTraceProfileSnesLineSelected(Uint32 line)
{
#if AURORA_TRACE_PROFILE_SNES_LINE_STRIDE > 0
    if ((line % AURORA_TRACE_PROFILE_SNES_LINE_STRIDE) == 0u)
        return TRUE;
#endif

    if (line == 223u || line == 224u ||
        line == 239u || line == 240u ||
        line == 261u || line == 311u)
        return TRUE;

    return FALSE;
}

#endif
