#ifndef AURORA_SMK_PROFILER_H
#define AURORA_SMK_PROFILER_H

/* AURORA_SMK_PROFILER_V2_20261003
 * Exact-CRC Super Mario Kart profiler for DSP-1 + Mode 7.
 * V2 adds path-local Clamp timing and O(1) scanline geometry diagnostics.
 *
 * Build-only capability: this header is referenced from hot paths only inside
 * #if AURORA_SMK_PROFILER blocks, and aurora_smk_profiler.cpp is absent from
 * SRCS when the flag is 0.  A normal build therefore contains no profiler
 * branch, counter, string, buffer, file writer or menu hook.
 *
 * Supported normalized/headerless CRC32 identities:
 *   CD80DB86 Super Mario Kart (USA)
 *   C8002453 Super Mario Kart (Japan)
 *   56410E5E Super Mario Kart (Europe)
 */

#include "types.h"

#ifndef AURORA_SMK_PROFILER
#define AURORA_SMK_PROFILER 0
#endif

#if AURORA_SMK_PROFILER

#ifdef __cplusplus
extern "C" {
#endif

extern Bool g_AuroraSmkProfilerActive;

void AuroraSmkProfilerConfigureGame(Uint32 crc32, const char *title);
void AuroraSmkProfilerFrameBegin(void);
void AuroraSmkProfilerFrameEnd(void);
void AuroraSmkProfilerDsp1Command(Uint8 opcode, Uint32 cycles);
void AuroraSmkProfilerMode7Line(
    Uint8 m7sel, Uint8 mosaic, Uint8 softwareHacks,
    Int32 x, Int32 y, Int32 dx, Int32 dy,
    Int32 m7b, Int32 m7c, Int32 nPixels,
    Uint32 setupCycles, Uint32 fetchCycles, Uint32 postCycles,
    Uint32 totalCycles);
void AuroraSmkProfilerMode7Compose(Int32 subScreen, Uint32 cycles);

/* Menu transition only: seal counters immediately, write later from menu tick. */
void AuroraSmkProfilerMenuOpen(void);
void AuroraSmkProfilerMenuUpdate(void);
void AuroraSmkProfilerCancelPending(void);

#ifdef __cplusplus
}
#endif

#endif /* AURORA_SMK_PROFILER */
#endif /* AURORA_SMK_PROFILER_H */
