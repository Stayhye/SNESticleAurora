

#include <stdlib.h>
#include <string.h>
#include "types.h"
#include "console.h"
#include "snspcio.h"
#include "sntiming.h"
extern "C" {
#include "snspc.h"
};
#include "snspcdsp.h"
#include "platform/ps2/system/aurora_runtime_trace.h"
/* AURORA_SNES_BINARY_TRACE_V6D_SPARSE_HIGHSIGNAL_20260918 */

/* AURORA_CPU_SPC_HOST_WORK_REDUCTION_V2_20260920_IO */
#define SNES_DEBUGSPCIO (CODE_DEBUG && FALSE)

#if SNES_DEBUGSPCIO
Uint32  _SpcOp;
Uint32 _SpcAddr;
Uint32 _SpcData;

static void _SpcDebugRead(SNSpcT *pSpc, Uint32 uAddr, Uint32 uData)
{
	if (_SpcOp || _SpcAddr!=uAddr || _SpcData!=uData)
	{
		//ConDebug("%08d: spc read  %04X %02X\n", SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL, 0), uAddr, uData);
		_SpcOp = 0;
		_SpcAddr = uAddr;
		_SpcData = uData;
	}
}

static void _SpcDebugWrite(SNSpcT *pSpc, Uint32 uAddr, Uint32 uData)
{
	if (!_SpcOp || _SpcAddr!=uAddr || _SpcData!=uData)
	{
		ConDebug("%08d: spc write %04X %02X %02X %02X %02X %02X %08X\n", 
			SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL, 0), 
			uAddr, uData,
			pSpc->Regs.rA,
			pSpc->Regs.rX,
			pSpc->Regs.rY,
			pSpc->Regs.rSP,
			SNSPCMemChecksum(pSpc)
			);
		_SpcOp = 1;
		_SpcAddr = uAddr;
		_SpcData = uData;
	}
}
#endif

/* AURORA_ZENKI_APUIO_F1_COLLISION_V1_1_20260920
 * AURORA_APUIO_F1_EXACT_ORDER_FINAL_V1_20260922
 *
 * $F1 clears the CPU->APU data latch at the CONTROL write.  Preserve the
 * Zenki reverse-host-order protection for a truly coincident CPU APUIO write,
 * but do not extend that reset over the rest of the SPC700 instruction cycle:
 * a CPU write with a later TOTAL timestamp is a future bus event and must
 * survive.  Queue ordering itself remains in FRAME time. */
Bool SNSpcIO::EnqueueWrite(
	Uint32 uCycle, Uint32 uTotalCycle, Uint32 uAddr, Uint8 uData)
{
	/* V2 common path: almost every APUIO write has no live $F1 reset stamp. */
	if (m_uPortResetValid)
	{
		const Uint8 uGroup = (uAddr & 2u) ? 0x02u : 0x01u;

		if (m_uPortResetValid & uGroup)
		{
			const Uint32 uResetTotal = (uGroup == 0x01u)
				? m_uPortResetTotal01 : m_uPortResetTotal23;
			const Uint32 uDelta = uTotalCycle - uResetTotal;
			if (uDelta == 0u)
			{
				/* Exactly coincident: the $F1 reset wins. TRUE = handled. */
				return TRUE;
			}

			/* First non-coincident write is a later bus event; keep it. */
			m_uPortResetValid &= (Uint8)~uGroup;
		}
	}

	return m_Queue.Enqueue(uCycle, uAddr, uData);
}

void SNSpcIO::SyncQueueAll()
{
	SNQueueElementT *pElement;

	// dequeue all pending writes
	while ( (pElement=m_Queue.Dequeue()) != NULL)
	{
		// perform write
		m_Regs.apu_w[pElement->uAddr] = pElement->uData;
	}

	// empty queue
	m_Queue.Reset();
}

/* AURORA_BLIZZARD_APUIO_QUEUE_ORDER_V1_20260914
 * SyncSPC's ordered APUIO-read path calls this from snes.cpp too;
 * keep a normal externally linkable definition rather than an inline
 * definition hidden in this translation unit. */
void SNSpcIO::SyncQueue(Uint32 uCycle)
{
	SNQueueElementT *pElement;

	/* AURORA_CPU_SPC_HOST_WORK_REDUCTION_V2_20260920_QUEUE_EMPTY */
	if (m_Queue.IsEmpty())
		return;

	/* AURORA_BLACKTHORNE_APUIO_SAMECYCLE_V1_SYNC_20260919
	 * A CPU->SPC port write is due when the SPC has REACHED its timestamp,
	 * not only one master clock later.  This inclusive rule is deliberately
	 * local to SNSpcIO; SNPPUQueue keeps the legacy strict Dequeue(). */
	while ( (pElement=m_Queue.DequeueAtOrBefore(uCycle)) != NULL)
	{
		// perform write
		m_Regs.apu_w[pElement->uAddr] = pElement->uData;
	}
}


void SNSpcIO::Reset()
{
	memset(&m_Regs, 0, sizeof(m_Regs));

	m_Queue.Reset();

	/* AURORA_ZENKI_APUIO_F1_COLLISION_V1_1_20260920_RESET */
	m_uPortResetTotal01 = 0;
	m_uPortResetTotal23 = 0;
	m_uPortResetValid = 0;

	SNSpcTimerReset(&m_Regs.spc_timer[0], 128 * SNSPC_CYCLE); //SNES_MASTERCLOCKRATE / 8000);
	SNSpcTimerReset(&m_Regs.spc_timer[1], 128 * SNSPC_CYCLE); //SNES_MASTERCLOCKRATE / 8000);
	SNSpcTimerReset(&m_Regs.spc_timer[2], 16  * SNSPC_CYCLE); //SNES_MASTERCLOCKRATE / 64000);
}



#if SNES_STATEDEBUG
extern "C" Bool g_bStateDebug;
#endif

Uint8 SNSpcIO::Read8Trap(SNSpcT *pSpc, Uint32 uAddr)
{
	SNSpcIO *pIO = (SNSpcIO *)pSpc->pUserData;
	/* AURORA_SNES_BINARY_TRACE_V7_VISUAL_BREADCRUMBS_20260918_SPCIO_READ: no USB event; SPC ring is sampled by frame/critical breadcrumbs. */

#if SNES_STATEDEBUG
	if (g_bStateDebug)
		ConDebug("read_spc[%04X] %d %d %04X\n", uAddr, pSpc->Cycles, pIO->m_Regs.spc_timer[0].nElapsedCycles, pSpc->Regs.rPC);
#endif
	switch (uAddr)
	{
	/* AURORA_SPC700_MEGA_ACCURACY_V1_20260916 */
	case 0xF0:
	case 0xF1:
	case 0xFA:
	case 0xFB:
	case 0xFC:
		return 0x00;

	case 0xF2:
		/* AURORA_SPC_DSPADDR_F2_READBACK_FIX_V1_20260919
		 * DSPADDR is an 8-bit S-SMP latch.  Bit 7 controls whether
		 * a write through DSPDATA ($F3) is accepted, but it must not
		 * be discarded from the DSPADDR value stored/read at $F2.
		 * The S-DSP register index itself remains 7-bit at $F3.
		 */
		return pSpc->Mem[uAddr];

	case 0xF3:
		/* AURORA_HW_ACCURACY_DSPDATA_ORDER_V1_20260916
		 * Do not make future S-DSP writes visible to an $F3 read. */
		pIO->m_pSpcDsp->Sync(
			SNSPCGetCounter(pSpc, SNSPC_COUNTER_FRAME));
		return pIO->m_pSpcDsp->Read8(pSpc->Mem[0xF2]);
	case 0xF4: // port 0-4
	case 0xF5:
	case 0xF6:
	case 0xF7:
		#if SNES_DEBUGSPCIO
	//	_SpcDebugRead(pSpc, uAddr, pIO->m_Regs.apu_w[uAddr & 3]);
		#endif

		#if SNSPCIO_WRITEQUEUE
		if (!pIO->m_Queue.IsEmpty())
			pIO->SyncQueue(SNSPCGetCounter(pSpc, SNSPC_COUNTER_FRAME));
		#endif

		return pIO->m_Regs.apu_w[uAddr & 3];

	case 0xFD:	// counter0
		return SNSpcTimerGetCounter(&pIO->m_Regs.spc_timer[0], SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL));
	case 0xFE:	// counter1
		return SNSpcTimerGetCounter(&pIO->m_Regs.spc_timer[1], SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL));
	case 0xFF:	// counter2
		return SNSpcTimerGetCounter(&pIO->m_Regs.spc_timer[2], SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL));
	default:
#if SNES_DEBUGPRINT
		ConDebug("read_spc[%04X]\n", uAddr);
#endif
		return 	pSpc->Mem[uAddr];
	}
}


void SNSpcIO::Write8Trap(SNSpcT *pSpc, Uint32 uAddr, Uint8 uData)
{
	SNSpcIO *pIO = (SNSpcIO *)pSpc->pUserData;
	/* AURORA_SNES_BINARY_TRACE_V6D_SPARSE_HIGHSIGNAL_20260918_SPCIO_W */
	if (uAddr >= 0xF4 && uAddr <= 0xF7)
	{
		AuroraTraceRecord(
		    ATR_SPCIO_W, ATR_F_PRE, (Uint16)uAddr,
		    (Uint32)uData, (Uint32)pSpc->Regs.rPC,
		    ATR_P_NONE);
	}/* AURORA_TOPGEAR_SPCIO_TIMESTAMP_V3_20260917
	 * TOTAL time is observable here only by timer-control/target writes.
	 * DSP and CPU/APU port writes do not consume this timestamp. */

#if SNES_STATEDEBUG
	if (g_bStateDebug)
		ConDebug("write_spc[%04X]=%02X %d %d %04X\n", uAddr, uData, pSpc->Cycles, pIO->m_Regs.spc_timer[0].nElapsedCycles, pSpc->Regs.rPC);
#endif
	switch (uAddr)
	{
	case 0xF1:	// control
		{
			const Bool bTimer0 = (uData & 0x01u) ? TRUE : FALSE;
			const Bool bTimer1 = (uData & 0x02u) ? TRUE : FALSE;
			const Bool bTimer2 = (uData & 0x04u) ? TRUE : FALSE;
			const Bool bTimer0Change =
				(bTimer0 != pIO->m_Regs.spc_timer[0].bEnabled);
			const Bool bTimer1Change =
				(bTimer1 != pIO->m_Regs.spc_timer[1].bEnabled);
			const Bool bTimer2Change =
				(bTimer2 != pIO->m_Regs.spc_timer[2].bEnabled);
			const Bool bResetPorts = (uData & 0x30u) ? TRUE : FALSE;
			const Bool bNeedTotalCycle =
				bResetPorts || bTimer0Change || bTimer1Change || bTimer2Change;
			Int32 iCycle = 0;

			if (bNeedTotalCycle)
				iCycle = SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL);

			#if SNSPCIO_WRITEQUEUE
			if (bResetPorts && !pIO->m_Queue.IsEmpty())
				pIO->SyncQueue(SNSPCGetCounter(pSpc, SNSPC_COUNTER_FRAME));
			#endif

			if (uData & 0x10u)
			{
				pIO->m_Regs.apu_w[0] = 0;
				pIO->m_Regs.apu_w[1] = 0;
				/* AURORA_ZENKI_APUIO_F1_COLLISION_V1_1_20260920_F1 */
				pIO->m_uPortResetTotal01 = (Uint32)iCycle;
				pIO->m_uPortResetValid |= 0x01u;
			}
			if (uData & 0x20u)
			{
				pIO->m_Regs.apu_w[2] = 0;
				pIO->m_Regs.apu_w[3] = 0;
				pIO->m_uPortResetTotal23 = (Uint32)iCycle;
				pIO->m_uPortResetValid |= 0x02u;
			}

			if (bTimer0Change)
				SNSpcTimerSetEnable(&pIO->m_Regs.spc_timer[0], iCycle, bTimer0);
			if (bTimer1Change)
				SNSpcTimerSetEnable(&pIO->m_Regs.spc_timer[1], iCycle, bTimer1);
			if (bTimer2Change)
				SNSpcTimerSetEnable(&pIO->m_Regs.spc_timer[2], iCycle, bTimer2);

			{
				const Bool bRomEnable = (uData & 0x80u) ? TRUE : FALSE;
				if (bRomEnable != pSpc->bRomEnable)
					SNSPCSetRomEnable(pSpc, bRomEnable);
			}
		}
		break;
	case 0xF2:	// dsp addr
		break;
	case 0xF3:  // dsp data
		if (!(pSpc->Mem[0xF2] & 0x80))
		{
			Uint32 uDspCycle = (Uint32)SNSPCGetCounter(pSpc, SNSPC_COUNTER_FRAME);
			/* AURORA_SNES_BINARY_TRACE_V6D_SPARSE_HIGHSIGNAL_20260918_DSPQ */
			{
				const Uint8 r = (Uint8)(pSpc->Mem[0xF2] & 0x7Fu);
				if (r == 0x4C || r == 0x5C || r == 0x5D ||
				    r == 0x6C || r == 0x6D || r == 0x7C ||
				    r == 0x7D)
				{
					AuroraTraceRecord(
					    ATR_DSP_Q, ATR_F_PRE, (Uint16)r,
					    (Uint32)uData,
					    ((Uint32)(Uint16)pSpc->Regs.rPC << 16) |
					        (uDspCycle & 0xFFFFu),
					    ATR_P_NONE);
				}
			}
			while (!pIO->m_pSpcDsp->EnqueueWrite(uDspCycle, pSpc->Mem[0xF2] & 0x7F, uData))
				/* AURORA_SPC_DSP_QUEUE_FULL_DEADLOCK_FIX_V2_20260918: queue-full retry must guarantee progress. */
				pIO->m_pSpcDsp->Sync();
		}
		break;

	case 0xF4:
	case 0xF5:
	case 0xF6:
	case 0xF7:
		pIO->m_Regs.apu_r[uAddr & 3] = uData;
		#if SNES_DEBUGSPCIO
		_SpcDebugWrite(pSpc, uAddr, uData);
		#endif
		break;

	case 0xFA:	// timer0
		{
			const Int32 iCycle = SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL);
			SNSpcTimerSync(&pIO->m_Regs.spc_timer[0], iCycle);
			SNSpcTimerSetTimer(&pIO->m_Regs.spc_timer[0], uData);
		}
		break;
	case 0xFB:	// timer1
		{
			const Int32 iCycle = SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL);
			SNSpcTimerSync(&pIO->m_Regs.spc_timer[1], iCycle);
			SNSpcTimerSetTimer(&pIO->m_Regs.spc_timer[1], uData);
		}
		break;
	case 0xFC:	// timer2
		{
			const Int32 iCycle = SNSPCGetCounter(pSpc, SNSPC_COUNTER_TOTAL);
			SNSpcTimerSync(&pIO->m_Regs.spc_timer[2], iCycle);
			SNSpcTimerSetTimer(&pIO->m_Regs.spc_timer[2], uData);
		}
		break;

	default:
#if SNES_DEBUGPRINT
		ConDebug("write_spc[%04X]=%02X\n", uAddr, uData);
#endif
		break;
	}

	// store to memory
	pSpc->Mem[uAddr] = uData;
}


