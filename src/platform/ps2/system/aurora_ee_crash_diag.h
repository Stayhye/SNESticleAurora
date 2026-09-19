#ifndef AURORA_EE_CRASH_DIAG_H
#define AURORA_EE_CRASH_DIAG_H

/*
 * Aurora EE Crash Diagnostics
 * ===========================
 *
 * Host-side PlayStation 2 diagnostics owned by Aurora Runtime Trace.
 *
 * This module is intentionally independent from SNES emulation semantics.
 * Client code only publishes cheap progress breadcrumbs. If the Emotion
 * Engine raises a catchable Level-1 exception, PS2SDK eedebug supplies a
 * complete EE_RegFrame on its own exception stack and this module renders a
 * crash card containing the fault registers plus the most recent host
 * breadcrumbs.
 *
 * Breadcrumbs perform NO filesystem I/O and NO durable trace close/reopen.
 */
/* AURORA_EE_CRASH_DIAG_DKC_V1_20260918 */
/* AURORA_EE_HANG_WATCHDOG_DKC_V2_20260918 */

#include "types.h"

#ifndef AURORA_RUNTIME_TRACE
#define AURORA_RUNTIME_TRACE 0
#endif

#ifndef AURORA_EE_CRASH_DIAG
#define AURORA_EE_CRASH_DIAG AURORA_RUNTIME_TRACE
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    AED_HOST_FRAME_CALL_ENTER    = 0x1001,
    AED_HOST_FRAME_CALL_RETURN   = 0x1002,
    AED_TRACE_FRAME_BEGIN_ENTER  = 0x1003,
    AED_TRACE_FRAME_BEGIN_RETURN = 0x1004,
    AED_TRACE_FRAME_END_ENTER    = 0x1005,
    AED_TRACE_FRAME_END_RETURN   = 0x1006,

    AED_SYNCSPC_ENTER            = 0x1101,
    AED_SYNCSPC_TIMING         = 0x1102,
    AED_SPC_EXEC_ENTER         = 0x1103,
    AED_SPC_EXEC_RETURN        = 0x1104,
    AED_APUIO_QUEUE_ENTER      = 0x1105,
    AED_APUIO_QUEUE_RETURN     = 0x1106,
    AED_SYNCSPC_RETURN         = 0x1107,
    AED_TRACE_PHASE            = 0x1201
};

#if AURORA_RUNTIME_TRACE && AURORA_EE_CRASH_DIAG

Bool AuroraEECrashDiagInstall(void);
void AuroraEECrashDiagRemove(void);
void AuroraEECrashDiagReset(void);

/*
 * Cheap host breadcrumb.
 *
 * a/b are deliberately generic: callers publish the values that best explain
 * their current boundary. The diagnostic module additionally snapshots the
 * EE $sp/$ra/$gp registers itself.
 */
void AuroraEECrashDiagBreadcrumb(Uint16 stage, Uint32 a, Uint32 b);
void AuroraEECrashDiagWatchdogArm(Uint32 frame);
void AuroraEECrashDiagWatchdogDisarm(void);

#else

#define AuroraEECrashDiagInstall() FALSE
#define AuroraEECrashDiagRemove() ((void)0)
#define AuroraEECrashDiagReset() ((void)0)
#define AuroraEECrashDiagBreadcrumb(...) ((void)0)
#define AuroraEECrashDiagWatchdogArm(...) ((void)0)
#define AuroraEECrashDiagWatchdogDisarm() ((void)0)

#endif

#ifdef __cplusplus
}
#endif
#endif
