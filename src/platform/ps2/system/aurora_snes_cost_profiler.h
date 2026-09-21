#ifndef AURORA_SNES_COST_PROFILER_H
#define AURORA_SNES_COST_PROFILER_H

/* AURORA_SNES_COST_PROFILER_V1_20260920
 * AURORA_SNES_PPU_BREAKDOWN_V2_20260920
 * AURORA_SNES_PPU_FOCUS_V3_20260920
 * Diagnostic-only SNES host-cycle profiler.
 *
 * Top-level buckets and PPU-detail buckets are independently exclusive.
 * Entering a child scope pauses its parent. Thus BG OTHER and COLOR OTHER
 * contain only work not charged to their named child scopes.
 *
 * Build capability is controlled by AURORA_SNES_COST_PROFILER. Runtime state
 * starts OFF and is intentionally not serialized in video.cfg.
 */

#include "types.h"

#ifndef AURORA_SNES_COST_PROFILER
#define AURORA_SNES_COST_PROFILER 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum AuroraSnesCostBucketE
{
    AURORA_SNES_COST_CPU = 0,
    AURORA_SNES_COST_PPU,
    AURORA_SNES_COST_RASTER,
    AURORA_SNES_COST_SPC,
    AURORA_SNES_COST_DSP,
    AURORA_SNES_COST_BUCKET_COUNT
};

enum AuroraSnesPpuDetailBucketE
{
    AURORA_SNES_PPU_DETAIL_SYNC = 0,
    AURORA_SNES_PPU_DETAIL_RENDER,
    AURORA_SNES_PPU_DETAIL_PREP,
    AURORA_SNES_PPU_DETAIL_RASTER,

    AURORA_SNES_PPU_DETAIL_BG_MAP,
    AURORA_SNES_PPU_DETAIL_BG_CHR,
    AURORA_SNES_PPU_DETAIL_BG_MAIN,
    AURORA_SNES_PPU_DETAIL_BG_SUB,
    AURORA_SNES_PPU_DETAIL_BG_OTHER,

    AURORA_SNES_PPU_DETAIL_OBJ, /* V4 legacy aggregate slot; V5 leaves it unused. */
    /* AURORA_SNES_PROFILER_DEEP_V5_20260920: existing OBJ scopes, relabeled by phase; no new timing scopes. */
    AURORA_SNES_PPU_DETAIL_OBJ_UPDATE,
    AURORA_SNES_PPU_DETAIL_OBJ_FETCH,
    AURORA_SNES_PPU_DETAIL_OBJ_MAIN,
    AURORA_SNES_PPU_DETAIL_OBJ_SUB,
    AURORA_SNES_PPU_DETAIL_MODE7,

    AURORA_SNES_PPU_DETAIL_COLOR_MASK,
    AURORA_SNES_PPU_DETAIL_BLEND,
    AURORA_SNES_PPU_DETAIL_COLOR_OTHER,

    AURORA_SNES_PPU_DETAIL_COUNT
};

#if AURORA_SNES_COST_PROFILER

Bool AuroraSnesCostProfilerSetEnabled(Bool enabled);
Bool AuroraSnesCostProfilerIsEnabled(void);
void AuroraSnesCostProfilerFrameBegin(void);
void AuroraSnesCostProfilerFrameEnd(void);
void AuroraSnesCostProfilerScopeEnter(Int32 bucket);
void AuroraSnesCostProfilerScopeLeave(Int32 bucket);
void AuroraSnesCostProfilerPpuEnter(Int32 bucket);
void AuroraSnesCostProfilerPpuLeave(Int32 bucket);
void AuroraSnesCostProfilerPpuWork(Uint32 mainMask, Uint32 subMask,
                                   Uint32 mapFetches, Uint32 chrDecodes);
void AuroraSnesCostProfilerDrawOverlay(void);

#else

#define AuroraSnesCostProfilerSetEnabled(_enabled) FALSE
#define AuroraSnesCostProfilerIsEnabled() FALSE
#define AuroraSnesCostProfilerFrameBegin() ((void)0)
#define AuroraSnesCostProfilerFrameEnd() ((void)0)
#define AuroraSnesCostProfilerScopeEnter(_bucket) ((void)0)
#define AuroraSnesCostProfilerScopeLeave(_bucket) ((void)0)
#define AuroraSnesCostProfilerPpuEnter(_bucket) ((void)0)
#define AuroraSnesCostProfilerPpuLeave(_bucket) ((void)0)
#define AuroraSnesCostProfilerPpuWork(_main,_sub,_map,_chr) ((void)0)
#define AuroraSnesCostProfilerDrawOverlay() ((void)0)

#endif

#ifdef __cplusplus
}
#endif

#define AURORA_SNES_COST_SCOPE_BEGIN(_bucket) \
    AuroraSnesCostProfilerScopeEnter((Int32)(_bucket))
#define AURORA_SNES_COST_SCOPE_END(_bucket) \
    AuroraSnesCostProfilerScopeLeave((Int32)(_bucket))

#define AURORA_SNES_PPU_DETAIL_BEGIN(_bucket) \
    AuroraSnesCostProfilerPpuEnter((Int32)(_bucket))
#define AURORA_SNES_PPU_DETAIL_END(_bucket) \
    AuroraSnesCostProfilerPpuLeave((Int32)(_bucket))

/* RAII is used only for scopes with early returns (Sync/RenderLine). */
#if AURORA_SNES_COST_PROFILER && defined(__cplusplus)
class AuroraSnesPpuDetailAutoScope
{
public:
    explicit AuroraSnesPpuDetailAutoScope(Int32 bucket)
        : m_bucket(bucket)
    {
        AuroraSnesCostProfilerPpuEnter(m_bucket);
    }
    ~AuroraSnesPpuDetailAutoScope()
    {
        AuroraSnesCostProfilerPpuLeave(m_bucket);
    }
private:
    Int32 m_bucket;
};
#define AURORA_SNES_JOIN2(_a,_b) _a##_b
#define AURORA_SNES_JOIN(_a,_b) AURORA_SNES_JOIN2(_a,_b)
#define AURORA_SNES_PPU_DETAIL_AUTO(_bucket) \
    AuroraSnesPpuDetailAutoScope \
        AURORA_SNES_JOIN(_auroraSnesPpuDetail_, __LINE__)((Int32)(_bucket))
#else
#define AURORA_SNES_PPU_DETAIL_AUTO(_bucket) ((void)0)
#endif

#endif
