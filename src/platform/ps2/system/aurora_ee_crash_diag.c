/*
 * Aurora EE Crash Diagnostics
 * ===========================
 *
 * Uses PS2SDK's eedebug Level-1 exception framework rather than replacing the
 * EE vectors by hand. eedebug saves the complete machine state into an
 * EE_RegFrame on a dedicated exception stack, and restores the original BIOS/
 * application handlers when removed.
 *
 * Important diagnostic rule:
 *   host breadcrumbs are RAM-only. Never call fopen/fwrite/fclose from normal
 *   breadcrumb publication. We are specifically trying to distinguish an
 *   emulator/runtime crash from trace-storage perturbation.
 */
/* AURORA_EE_CRASH_DIAG_DKC_V1_20260918 */
/* AURORA_EE_HANG_WATCHDOG_DKC_V2_20260918 */
/* AURORA_EE_WATCHDOG_SELFTEST_V1_20260918 */
#ifndef AURORA_EE_WATCHDOG_SELFTEST
#define AURORA_EE_WATCHDOG_SELFTEST 0
#endif

#include "platform/ps2/system/aurora_ee_crash_diag.h"

#if AURORA_RUNTIME_TRACE && AURORA_EE_CRASH_DIAG

#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <delaythread.h>
#include <ee_debug.h>
#include <debug.h>
#include <ps2_debug.h>

#include "platform/ps2/system/aurora_runtime_trace.h"

#define AED_RING_CAP 64u
#define AED_RING_MASK (AED_RING_CAP - 1u)
#define AED_WATCHDOG_POLL_USEC 250000u
#define AED_WATCHDOG_STALE_POLLS 16u
#define AED_WATCHDOG_STACK_BYTES 8192u

typedef struct
{
    Uint32 seq;
    Uint16 stage;
    Uint16 reserved;
    Uint32 a;
    Uint32 b;
    Uint32 sp;
    Uint32 ra;
    Uint32 gp;
} AuroraEEBreadcrumbT;

typedef char _aed_breadcrumb_28[
    (sizeof(AuroraEEBreadcrumbT) == 28) ? 1 : -1];

static volatile AuroraEEBreadcrumbT
    s_aed_ring[AED_RING_CAP] __attribute__((aligned(64)));
static volatile Uint32 s_aed_write = 0;
static volatile Uint32 s_aed_seq = 0;
static volatile Uint32 s_aed_installed = 0;
static volatile Uint32 s_aed_eedebug_installed = 0;
static volatile Uint32 s_aed_in_handler = 0;

static volatile Uint32 s_aed_watchdog_armed = 0;
static volatile Uint32 s_aed_watchdog_frame = 0;
static volatile Uint32 s_aed_watchdog_epoch = 0;
static volatile s32 s_aed_main_thread = -1;
static s32 s_aed_watchdog_thread = -1;
static unsigned char s_aed_watchdog_stack[AED_WATCHDOG_STACK_BYTES]
    __attribute__((aligned(64)));

static inline Uint32 aed_read_sp(void)
{
    Uint32 v;
    __asm__ __volatile__("move %0, $sp" : "=r"(v));
    return v;
}

static inline Uint32 aed_read_ra(void)
{
    Uint32 v;
    __asm__ __volatile__("move %0, $ra" : "=r"(v));
    return v;
}

static inline Uint32 aed_read_gp(void)
{
    Uint32 v;
    __asm__ __volatile__("move %0, $gp" : "=r"(v));
    return v;
}

static const char *aed_stage_name(Uint16 stage)
{
    switch (stage)
    {
        case AED_HOST_FRAME_CALL_ENTER:    return "host ExecuteFrame enter";
        case AED_HOST_FRAME_CALL_RETURN:   return "host ExecuteFrame returned";
        case AED_TRACE_FRAME_BEGIN_ENTER:  return "TraceFrameBegin enter";
        case AED_TRACE_FRAME_BEGIN_RETURN: return "TraceFrameBegin returned";
        case AED_TRACE_FRAME_END_ENTER:    return "TraceFrameEnd enter";
        case AED_TRACE_FRAME_END_RETURN:   return "TraceFrameEnd returned";
        case AED_SYNCSPC_ENTER:            return "SyncSPC enter";
        case AED_SYNCSPC_TIMING:         return "SyncSPC timing computed";
        case AED_SPC_EXEC_ENTER:         return "SNSPCExecute enter";
        case AED_SPC_EXEC_RETURN:        return "SNSPCExecute returned";
        case AED_APUIO_QUEUE_ENTER:      return "APUIO queue sync enter";
        case AED_APUIO_QUEUE_RETURN:     return "APUIO queue sync returned";
        case AED_SYNCSPC_RETURN:         return "SyncSPC return";
        case AED_TRACE_PHASE:            return "Aurora trace phase";
        default:                         return "unknown";
    }
}

void AuroraEECrashDiagReset(void)
{
    memset((void *)s_aed_ring, 0, sizeof(s_aed_ring));
    s_aed_write = 0;
    s_aed_seq = 0;
    s_aed_in_handler = 0;
    s_aed_watchdog_armed = 0;
    s_aed_watchdog_frame = 0;
    s_aed_watchdog_epoch = 0;
    s_aed_main_thread = -1;
}

void AuroraEECrashDiagBreadcrumb(Uint16 stage, Uint32 a, Uint32 b)
{
    Uint32 w;
    volatile AuroraEEBreadcrumbT *e;

    if (!g_AuroraTraceEnabled || !s_aed_installed || s_aed_in_handler)
        return;

    w = s_aed_write++;
    e = &s_aed_ring[w & AED_RING_MASK];

    e->seq = ++s_aed_seq;
    e->stage = stage;
    e->reserved = 0;
    e->a = a;
    e->b = b;
    e->sp = aed_read_sp();
    e->ra = aed_read_ra();
    e->gp = aed_read_gp();

    /* Keep the breadcrumb visible before the next risky host operation. */
    __asm__ __volatile__("sync" ::: "memory");
}

static void aed_print_breadcrumb(
    const volatile AuroraEEBreadcrumbT *e, Uint32 ordinal)
{
    scr_printf(
        "%lu: st=%04X %s a=%08lX b=%08lX\n"
        "    sp=%08lX ra=%08lX gp=%08lX\n",
        (unsigned long)ordinal,
        (unsigned int)e->stage,
        aed_stage_name(e->stage),
        (unsigned long)e->a,
        (unsigned long)e->b,
        (unsigned long)e->sp,
        (unsigned long)e->ra,
        (unsigned long)e->gp);
}

static void aed_print_recent(Uint32 maxCount)
{
    Uint32 w = s_aed_write;
    Uint32 count = (w < maxCount) ? w : maxCount;
    Uint32 i;

    for (i = 0; i < count; ++i)
    {
        const volatile AuroraEEBreadcrumbT *e =
            &s_aed_ring[(w - 1u - i) & AED_RING_MASK];
        aed_print_breadcrumb(e, i);
    }
}

static void aed_hang_card(void)
{
    ee_thread_status_t st;
    int haveStatus = 0;

    if (s_aed_in_handler)
        return;

    s_aed_in_handler = 1;
    s_aed_watchdog_armed = 0;
    __asm__ __volatile__("sync" ::: "memory");

    if (s_aed_main_thread > 0)
    {
        if (ReferThreadStatus(s_aed_main_thread, &st) >= 0)
            haveStatus = 1;
        (void)SuspendThread(s_aed_main_thread);
    }

    init_scr();
    scr_clear();

    scr_printf("AURORA EE HANG WATCHDOG\n");
    scr_printf("=======================\n\n");
    scr_printf("No host breadcrumb progress for ~4 seconds.\n");
    scr_printf("This is a HANG report, not an EE exception.\n\n");
    scr_printf("Armed frame : %lu\n", (unsigned long)s_aed_watchdog_frame);
    scr_printf("Last seq    : %lu\n", (unsigned long)s_aed_seq);
    scr_printf("Arm epoch   : %lu\n", (unsigned long)s_aed_watchdog_epoch);

    if (haveStatus)
    {
        scr_printf("Main thread : %ld status=%08X wait=%lu pri=%d\n",
                   (long)s_aed_main_thread,
                   (unsigned int)st.status,
                   (unsigned long)st.waitType,
                   st.current_priority);
        scr_printf("waitId=%lu wakeups=%lu\n\n",
                   (unsigned long)st.waitId,
                   (unsigned long)st.wakeupCount);
    }
    else
    {
        scr_printf("Main thread status unavailable.\n\n");
    }

    scr_printf("Last Aurora host breadcrumbs (newest first):\n");
    aed_print_recent(8u);

    scr_printf("\nPhotograph this screen exactly as shown.\n");
    scr_printf("The EE scheduler/watchdog survived the hang.\n");

    for (;;)
        DelayThread(1000000);
}

static void aed_watchdog_main(void *arg)
{
    Uint32 seen = 0;
    Uint32 stale = 0;
    Uint32 selftest_polls = 0;
    (void)arg;

    for (;;)
    {
        DelayThread(AED_WATCHDOG_POLL_USEC);

        if (!s_aed_installed || !s_aed_watchdog_armed || s_aed_in_handler)
        {
            seen = s_aed_seq;
            stale = 0;
            selftest_polls = 0;
            continue;
        }

#if AURORA_EE_WATCHDOG_SELFTEST
        /*
         * Diagnostic of the diagnostic itself:
         * 8 x 250 ms ~= 2 seconds after arming, regardless of breadcrumb
         * progress. If this card appears during healthy execution, thread
         * scheduling, priority and libdebug screen takeover are proven.
         */
        ++selftest_polls;
        if (selftest_polls >= 8u)
            aed_hang_card();
#endif

        if (s_aed_seq != seen)
        {
            seen = s_aed_seq;
            stale = 0;
            continue;
        }

        ++stale;
        if (stale >= AED_WATCHDOG_STALE_POLLS)
            aed_hang_card();
    }
}

void AuroraEECrashDiagWatchdogArm(Uint32 frame)
{
    ee_thread_status_t st;

    if (!s_aed_installed || s_aed_in_handler)
        return;

    s_aed_main_thread = GetThreadId();
    s_aed_watchdog_frame = frame;
    s_aed_watchdog_epoch = s_aed_seq;
    s_aed_watchdog_armed = 1;

    if (s_aed_watchdog_thread > 0 &&
        ReferThreadStatus(s_aed_main_thread, &st) >= 0)
    {
        int p = st.current_priority;
        if (p > 1)
            --p;
        (void)ChangeThreadPriority(s_aed_watchdog_thread, p);
    }

    __asm__ __volatile__("sync" ::: "memory");
}

void AuroraEECrashDiagWatchdogDisarm(void)
{
    s_aed_watchdog_armed = 0;
    __asm__ __volatile__("sync" ::: "memory");
}

static int aed_exception_handler(EE_RegFrame *frame)
{
    Uint32 excode;
    Uint32 bd;
    Uint32 w;
    Uint32 count;
    Uint32 i;

    if (!frame)
        for (;;) __asm__ __volatile__("sync\n\tnop");

    /* A second exception while drawing the crash card must not recurse. */
    if (s_aed_in_handler)
        for (;;) __asm__ __volatile__("sync\n\tnop");

    s_aed_in_handler = 1;
    __asm__ __volatile__("sync" ::: "memory");

    excode = (frame->cause >> 2) & 0x1Fu;
    bd = (frame->cause >> 31) & 1u;

    /* The normal Aurora renderer is no longer trustworthy after an exception. */
    init_scr();
    scr_clear();

    scr_printf("AURORA EE CRASH DIAGNOSTICS\n");
    scr_printf("===========================\n\n");
    scr_printf("Exception code : %lu   BD=%lu\n",
               (unsigned long)excode, (unsigned long)bd);
    scr_printf("EPC            : %08lX\n", (unsigned long)frame->epc);
    scr_printf("BadVAddr       : %08lX\n", (unsigned long)frame->badvaddr);
    scr_printf("Cause          : %08lX\n", (unsigned long)frame->cause);
    scr_printf("Status         : %08lX\n", (unsigned long)frame->status);
    scr_printf("SP             : %08lX\n", (unsigned long)frame->sp[0]);
    scr_printf("RA             : %08lX\n", (unsigned long)frame->ra[0]);
    scr_printf("GP             : %08lX\n\n", (unsigned long)frame->gp[0]);

    /* Avoid stdio/RPC from exception context; the crash card is GS-only. */

    scr_printf("Last Aurora host breadcrumbs (newest first):\n");

    (void)w;
    (void)count;
    (void)i;
    aed_print_recent(6u);

    scr_printf("\nConsole intentionally halted. Photograph this screen.\n");
    scr_printf("Map EPC with mips64r5900el-ps2-elf-addr2line.\n");

    /* Never return to the faulting instruction and never touch USB here. */
    for (;;)
        __asm__ __volatile__("sync\n\tnop");

    return 0;
}

Bool AuroraEECrashDiagInstall(void)
{
    static const int causes[] = {
        1, 2, 3,
        4, 5,
        6, 7,
        10, 11, 12, 13
    };
    ee_thread_t th;
    Uint32 i;

    if (s_aed_installed)
        return TRUE;

    AuroraEECrashDiagReset();
    s_aed_installed = 1;

    memset(&th, 0, sizeof(th));
    th.func = (void *)aed_watchdog_main;
    th.stack = s_aed_watchdog_stack;
    th.stack_size = AED_WATCHDOG_STACK_BYTES;
    th.gp_reg = &_gp;
    th.initial_priority = 48;

    s_aed_watchdog_thread = CreateThread(&th);
    if (s_aed_watchdog_thread > 0)
    {
        if (StartThread(s_aed_watchdog_thread, NULL) < 0)
        {
            (void)DeleteThread(s_aed_watchdog_thread);
            s_aed_watchdog_thread = -1;
        }
    }

    if (ee_dbg_install(1) == 0)
    {
        for (i = 0; i < (Uint32)(sizeof(causes) / sizeof(causes[0])); ++i)
            (void)ee_dbg_set_level1_handler(causes[i], aed_exception_handler);
        s_aed_eedebug_installed = 1;
    }

    return TRUE;
}

void AuroraEECrashDiagRemove(void)
{
    s_aed_watchdog_armed = 0;

    if (s_aed_eedebug_installed)
    {
        s_aed_eedebug_installed = 0;
        (void)ee_dbg_remove(1);
    }

    if (s_aed_watchdog_thread > 0)
    {
        (void)TerminateThread(s_aed_watchdog_thread);
        (void)DeleteThread(s_aed_watchdog_thread);
        s_aed_watchdog_thread = -1;
    }

    s_aed_installed = 0;
    AuroraEECrashDiagReset();
}

#endif
