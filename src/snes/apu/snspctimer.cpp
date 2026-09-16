
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

void SNSpcTimerSync(SNSpcTimerT *pTimer, Int32 nCycles)
{
	Uint32 uRate = pTimer->uCyclesPerTick;
	Uint32 uDelta = (Uint32)nCycles - (Uint32)pTimer->iCycleSync;
	Uint32 uPacked, uStage2, uPhase, uTicks;
	pTimer->iCycleSync = nCycles;
	if (!uRate) { pTimer->nElapsedCycles = 0; return; }
	uPacked = (Uint32)pTimer->nElapsedCycles;
	uStage2 = (uPacked / uRate) & 0xFFu;
	uPhase = uPacked % uRate;
	uTicks = uDelta / uRate;
	uPhase += uDelta % uRate;
	if (uPhase >= uRate) { uPhase -= uRate; uTicks++; }
	if (!pTimer->bEnabled) { pTimer->nElapsedCycles = (Int32)uPhase; return; }
	if (uTicks)
	{
		Uint32 uTarget = pTimer->uCompare;
		Uint32 uDistance = (uTarget - uStage2) & 0xFFu;
		if (!uDistance) uDistance = 0x100u;
		if (uTicks < uDistance) uStage2 = (uStage2 + uTicks) & 0xFFu;
		else
		{
			Uint32 uRemain = uTicks - uDistance;
			Uint32 uPeriod = uTarget ? uTarget : 0x100u;
			Uint32 uEvents = 1u + uRemain / uPeriod;
			pTimer->uUpCounter = (Uint8)((pTimer->uUpCounter + uEvents) & 0x0Fu);
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
		uPhase = pTimer->uCyclesPerTick ? ((Uint32)pTimer->nElapsedCycles % pTimer->uCyclesPerTick) : 0u;
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
