
#include "types.h"
#include "snspctimer.h"

/* AURORA_SPC700_MEGA_ACCURACY_V1_20260916 */

//
//
//

void SNSpcTimerReset(SNSpcTimerT *pTimer, Uint32 uCyclesPerTick)
{
	pTimer->bEnabled = FALSE;
	pTimer->uUpCounter = 0;
	pTimer->uCompare = 0;
	pTimer->uCyclesPerTick = uCyclesPerTick;
	pTimer->iDivisor = pTimer->uCyclesPerTick * 0x100;
	pTimer->iCycleSync = 0;
	pTimer->nElapsedCycles = 0;
}

/* AURORA_TOPGEAR_ACCURACY_PERF_RECOVERY_V1_20260917
 * Exact S-SMP timer math with PS2-friendly constant-rate div/mod.
 * Timers 0/1 tick every 128 SPC cycles (2688 master clocks here), timer 2
 * every 16 SPC cycles (336).  Keep a generic fallback for tests/future use.
 * No timer state/layout or externally visible timing changes. */
static inline void _SNSpcTimerDivModRate(Uint32 v, Uint32 rate,
                                         Uint32 *q, Uint32 *r)
{
    if (rate == 336u)
    {
        *q = v / 336u;
        *r = v % 336u;
        return;
    }
    if (rate == 2688u)
    {
        *q = v / 2688u;
        *r = v % 2688u;
        return;
    }
    *q = rate ? (v / rate) : 0u;
    *r = rate ? (v % rate) : 0u;
}

static inline Uint32 _SNSpcTimerModRate(Uint32 v, Uint32 rate)
{
    Uint32 q, r;
    _SNSpcTimerDivModRate(v, rate, &q, &r);
    (void)q;
    return r;
}

void SNSpcTimerSync(SNSpcTimerT *pTimer, Int32 nCycles)
{
    Uint32 uRate = pTimer->uCyclesPerTick;
    Uint32 uDelta = (Uint32)nCycles - (Uint32)pTimer->iCycleSync;
    Uint32 uPacked, uStage2, uPhase, uTicks, uDeltaPhase;

    pTimer->iCycleSync = nCycles;
    if (!uRate)
    {
        pTimer->nElapsedCycles = 0;
        return;
    }

    uPacked = (Uint32)pTimer->nElapsedCycles;

    /* AURORA_TOPGEAR_ACCURACY_PERF_RECOVERY_V1_20260917: while disabled, hardware only needs the free-running base
     * divider phase.  Current-format disabled states already satisfy this
     * invariant; ModRate also keeps old/restored states safe. */
    if (!pTimer->bEnabled)
    {
        uPhase = _SNSpcTimerModRate(uPacked, uRate);
        uDeltaPhase = _SNSpcTimerModRate(uDelta, uRate);
        uPhase += uDeltaPhase;
        if (uPhase >= uRate)
            uPhase -= uRate;
        pTimer->nElapsedCycles = (Int32)uPhase;
        return;
    }

    _SNSpcTimerDivModRate(uPacked, uRate, &uStage2, &uPhase);
    uStage2 &= 0xFFu;
    _SNSpcTimerDivModRate(uDelta, uRate, &uTicks, &uDeltaPhase);
    uPhase += uDeltaPhase;
    if (uPhase >= uRate)
    {
        uPhase -= uRate;
        uTicks++;
    }

    if (uTicks)
    {
        Uint32 uTarget = pTimer->uCompare;
        Uint32 uDistance = (uTarget - uStage2) & 0xFFu;
        if (!uDistance)
            uDistance = 0x100u;

        if (uTicks < uDistance)
        {
            uStage2 = (uStage2 + uTicks) & 0xFFu;
        }
        else
        {
            Uint32 uRemain = uTicks - uDistance;
            Uint32 uPeriod = uTarget ? uTarget : 0x100u;
            Uint32 uEvents = 1u + uRemain / uPeriod;
            pTimer->uUpCounter =
                (Uint8)((pTimer->uUpCounter + uEvents) & 0x0Fu);
            uStage2 = uRemain % uPeriod;
        }
    }

    pTimer->nElapsedCycles = (Int32)(uStage2 * uRate + uPhase);
}

void SNSpcTimerSetEnable(SNSpcTimerT *pTimer, Int32 nCycles, Bool bEnable)
{
	if (bEnable != pTimer->bEnabled)
	{
		Uint32 uPhase;
		SNSpcTimerSync(pTimer, nCycles);
		uPhase = pTimer->uCyclesPerTick ? _SNSpcTimerModRate((Uint32)pTimer->nElapsedCycles, pTimer->uCyclesPerTick) : 0u;
		pTimer->nElapsedCycles = (Int32)uPhase;
		if (bEnable) pTimer->uUpCounter = 0;
		pTimer->bEnabled = bEnable;
	}
}

void SNSpcTimerSetTimer(SNSpcTimerT *pTimer, Uint8 uValue)
{
	pTimer->uCompare = uValue;
	pTimer->iDivisor = pTimer->uCyclesPerTick * (uValue ? (Int32)uValue : 0x100);
}

Uint8 SNSpcTimerGetCounter(SNSpcTimerT *pTimer, Int32 iCycle)
{
	Uint8 uValue;
	SNSpcTimerSync(pTimer, iCycle);
	uValue = (Uint8)(pTimer->uUpCounter & 0x0F);
	pTimer->uUpCounter = 0;
	return uValue;
}
