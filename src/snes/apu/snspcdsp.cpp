
#include <string.h>
#include "types.h"
#include "prof.h"
#include "snspcdsp.h"
#include "console.h"
#include "platform/ps2/system/aurora_runtime_trace.h"
#include "snspctracer.h" /* AURORA_SNES_GENERIC_TRACER_V36_20261002 */
/* AURORA_SNES_BINARY_TRACE_V6D_SPARSE_HIGHSIGNAL_20260918 */

#define SNSPCDSP_DETERMINISMSAFE (0)
#define SNSPCDSP_DEBUGPRINT (CODE_DEBUG && FALSE)


//
//
//


SNSpcDsp::SNSpcDsp()
{
	/* AURORA_SPC700_MEGA_ACCURACY_V1_20260916 */
	m_pMem = NULL;
	m_pShadowMem = NULL;
	m_pbRomEnable = NULL;
	m_pMixer[0] = NULL;
	m_pMixer[1] = NULL;
}

void SNSpcDsp::Reset()
{
	memset(m_Regs, 0, sizeof(m_Regs));
	memset(m_LiveRegs, 0, sizeof(m_LiveRegs));
	/* AURORA_HW_ACCURACY_SDSP_FLG_RESET_V1_20260916 */
	m_Regs[SNSPCDSP_REG_FLG] = 0xE0;
	m_LiveRegs[SNSPCDSP_REG_FLG] = 0xE0;
	m_Queue.Reset();
}

void SNSpcDsp::WriteLive8(Uint32 uAddr, Uint8 uData)
{
	uAddr &= 0x7Fu;
	m_LiveRegs[uAddr] = uData;
	/* Readback-visible register semantics only. Mixer/voice side effects are
	 * deliberately deferred to Write8() on the replay timeline. */
	if (uAddr == SNSPCDSP_REG_FLG && (uData & 0x80u))
	{
		m_LiveRegs[SNSPCDSP_REG_FLG] |= 0x60u;
		m_LiveRegs[SNSPCDSP_REG_KOFF] = 0;
		m_LiveRegs[SNSPCDSP_REG_KON] = 0;
		m_LiveRegs[SNSPCDSP_REG_ENDX] = 0;
	}
	else if (uAddr == SNSPCDSP_REG_KON)
	{
		m_LiveRegs[SNSPCDSP_REG_ENDX] &= (Uint8)~uData;
	}
	else if (uAddr == SNSPCDSP_REG_ENDX)
	{
		m_LiveRegs[SNSPCDSP_REG_ENDX] = 0;
	}
}

void SNSpcDsp::Write8(Uint32 uAddr, Uint8 uData)
{
	Int32 iChannel;

	uAddr &= 0x7F;
	/* AURORA_SNES_BINARY_TRACE_V6D_SPARSE_HIGHSIGNAL_20260918_DSPW
	 * AURORA_TRACE_OFF_PERF_V6_20260919: when Debugger is Off, avoid the seven-address diagnostic
	 * chain and the trace call. The register write below is untouched. */
#if AURORA_RUNTIME_TRACE
	if (g_AuroraTraceEnabled &&
	    (uAddr == 0x4C || uAddr == 0x5C || uAddr == 0x5D ||
	     uAddr == 0x6C || uAddr == 0x6D || uAddr == 0x7C ||
	     uAddr == 0x7D))
	{
		AuroraTraceRecord(
		    ATR_DSP_W, ATR_F_PRE, (Uint16)uAddr,
		    (Uint32)uData, (Uint32)m_Regs[uAddr], ATR_P_NONE);
	}
#endif

//	if (uAddr==SNSPCDSP_REG_FLG) // && uData != m_Regs[uAddr])
//		ConDebug("flg %02X\n", uData);

#if SNSPCDSP_DEBUGPRINT
	if (m_Regs[uAddr] != uData)
		ConDebug("dsp[%02X] %02X\n", uAddr, uData);
#endif

	m_Regs[uAddr] = uData;

	switch (uAddr)
	{
	case SNSPCDSP_REG_FLG:
		if (uData & 0x80)
		{
			/* Soft reset: mute + echo-write-disable, voices enter Release at zero. */
			m_Regs[SNSPCDSP_REG_FLG] |= 0x60;
			m_Regs[SNSPCDSP_REG_KOFF] = 0;
			m_Regs[SNSPCDSP_REG_KON] = 0;
			m_Regs[SNSPCDSP_REG_ENDX] = 0;
			if (m_pMixer[0]) m_pMixer[0]->SoftReset();
			if (m_pMixer[1]) m_pMixer[1]->SoftReset();
		}
		break;
    case SNSPCDSP_REG_KON:
		// if a channel has keyoff set, it can't be keyed on

		//uData &=  ~m_Regs[SNSPCDSP_REG_KOFF];
		if (uData != 0)
		{
#if SNSPCDSP_DEBUGPRINT
			ConDebug("kon: %02X\n", uData);
#endif

			for (iChannel=0; iChannel < SNSPCDSP_CHANNEL_NUM; iChannel++)
			{
				if ((uData>>iChannel)&1)
					KeyOn(iChannel);
			}
		}
		break;
    case SNSPCDSP_REG_ENDX:
        // reset endx
    	m_Regs[SNSPCDSP_REG_ENDX] = 0;
        break;
	case SNSPCDSP_REG_KOFF:
		//ConDebug("koff: %02X\n", uData);
		if (uData != 0)
		{
			for (iChannel=0; iChannel < SNSPCDSP_CHANNEL_NUM; iChannel++)
			{
				if ((uData>>iChannel)&1)
					KeyOff(iChannel);
			}
		}
		break;
	default:
		#if SNSPCDSP_DEBUGPRINT
		ConDebug("dsp[%02X] %02X\n", uAddr, uData);
		#endif
		break;
	}

}


Uint8 SNSpcDsp::Read8(Uint32 uAddr)
{
#if SNSPCDSP_DEBUGPRINT
	ConDebug("read_dsp[%02X]\n", uAddr);
#endif

	uAddr &= 0x7F;

#if SNSPCDSP_DETERMINISMSAFE
	// short-circuit registers that will violate determinism
	switch (uAddr)
	{
	case 0x00 + SNSPCDSP_REG_ENVX:
	case 0x10 + SNSPCDSP_REG_ENVX:
	case 0x20 + SNSPCDSP_REG_ENVX:
	case 0x30 + SNSPCDSP_REG_ENVX:
	case 0x40 + SNSPCDSP_REG_ENVX:
	case 0x50 + SNSPCDSP_REG_ENVX:
	case 0x60 + SNSPCDSP_REG_ENVX:
	case 0x70 + SNSPCDSP_REG_ENVX:
		return 0;

	case 0x00 + SNSPCDSP_REG_OUTX:
	case 0x10 + SNSPCDSP_REG_OUTX:
	case 0x20 + SNSPCDSP_REG_OUTX:
	case 0x30 + SNSPCDSP_REG_OUTX:
	case 0x40 + SNSPCDSP_REG_OUTX:
	case 0x50 + SNSPCDSP_REG_OUTX:
	case 0x60 + SNSPCDSP_REG_OUTX:
	case 0x70 + SNSPCDSP_REG_OUTX:
		return 0;

		// this violates determinism
    case SNSPCDSP_REG_KON:
	case SNSPCDSP_REG_KOFF:
		return 0;

		// this violates determinism
    case SNSPCDSP_REG_ENDX:
		return 0x0;
	}
#endif

	return m_LiveRegs[uAddr];
}

Uint16 SNSpcDsp::GetSampleDir(Uint8 uSrcN, Uint32 uOffset)
{
	Uint16 uSampleDir;
	Uint16 uData;

	// get sample dir address
	uSampleDir =  m_Regs[SNSPCDSP_REG_DIR] * 0x100 + uSrcN * 0x04;
	uSampleDir+= uOffset;

	// S-DSP reads physical APURAM; 16-bit address bus wraps naturally.
	uData = ReadRAM((Uint16)(uSampleDir + 0)) << 0;
	uData|= ReadRAM((Uint16)(uSampleDir + 1)) << 8;
	return uData;
}


void SNSpcDsp::KeyOn(Int32 iChannel)
{
	/* AURORA_SNES_BINARY_TRACE_V6_20260918_KON
	 * AURORA_TRACE_OFF_PERF_V6_20260919: KON trace is host-only; emulated ENDX/mixer work is below. */
#if AURORA_RUNTIME_TRACE
	if (g_AuroraTraceEnabled)
	{
		AuroraTraceRecord(
		    ATR_KON, ATR_F_PRE, (Uint16)iChannel,
		    (Uint32)m_Regs[SNSPCDSP_REG_ENDX], 0, ATR_P_FLUSH);
		AuroraTraceArmVoice(iChannel);
	}
#endif
	// clear endx
	m_Regs[SNSPCDSP_REG_ENDX] &=  ~(1 << iChannel);

	// tell mixer(s) to key on
	if (m_pMixer[0])
		m_pMixer[0]->KeyOn(iChannel);
	if (m_pMixer[1])
		m_pMixer[1]->KeyOn(iChannel);
}


void SNSpcDsp::KeyOff(Int32 iChannel)
{
	/* AURORA_SNES_BINARY_TRACE_V6_20260918_KOFF
	 * AURORA_TRACE_OFF_PERF_V6_20260919: KOFF trace is host-only. */
#if AURORA_RUNTIME_TRACE
	if (g_AuroraTraceEnabled)
	{
		AuroraTraceRecord(
		    ATR_KOFF, ATR_F_PRE, (Uint16)iChannel,
		    0, 0, ATR_P_FLUSH);
	}
#endif
	// tell mixer(s) to key off
	if (m_pMixer[0])
		m_pMixer[0]->KeyOff(iChannel);
	if (m_pMixer[1])
		m_pMixer[1]->KeyOff(iChannel);
}


Bool SNSpcDsp::EnqueueWrite(Uint32 uCycle, Uint32 uAddr, Uint8 uData)
{
	const Uint8 r = (Uint8)(uAddr & 0x7Fu);
	if (!m_Queue.Enqueue(uCycle, r, uData))
		return FALSE;
#if AURORA_SNES_TRACER
	if (SNSPC_TRACER_FAST_ACTIVE())
		SNSPCTracerDspEvent('Q', uCycle, 0xFFFFFFFFu, r, m_LiveRegs[r], uData);
#endif
	WriteLive8(r, uData);
	return TRUE;
}

void SNSpcDsp::Sync(Uint32 uCycle)
{
	SNQueueElementT *pElement;
	/* AURORA_SDSP_DUAL_TIMELINE_V36_20261002: a DSP write stamped exactly
	 * at the first cycle of this mixer chunk is already visible to it. */
	while ( (pElement=m_Queue.DequeueAtOrBefore(uCycle)) != NULL)
	{
#if AURORA_SNES_TRACER
		if (SNSPC_TRACER_FAST_ACTIVE())
		{
			const Uint8 r=(Uint8)(pElement->uAddr&0x7Fu);
			SNSPCTracerDspEvent('T',pElement->uCycle,uCycle,r,m_Regs[r],pElement->uData);
		}
#endif
		Write8(pElement->uAddr, pElement->uData);
	}
}

void SNSpcDsp::Sync(void)
{
	SNQueueElementT *pElement;
	while ( (pElement=m_Queue.Dequeue()) != NULL)
	{
#if AURORA_SNES_TRACER
		if (SNSPC_TRACER_FAST_ACTIVE())
		{
			const Uint8 r=(Uint8)(pElement->uAddr&0x7Fu);
			SNSPCTracerDspEvent('F',pElement->uCycle,0xFFFFFFFFu,r,m_Regs[r],pElement->uData);
		}
#endif
		Write8(pElement->uAddr, pElement->uData);
	}
	m_Queue.Reset();
}

void SNSpcDsp::UpdateFlags(ISNSpcDspMix *pMixer)
{
	Int32 iChannel;
	
	for (iChannel=0; iChannel < SNSPCDSP_CHANNEL_NUM; iChannel++)
	{
		SNSpcVoiceRegsT *pRegs = (SNSpcVoiceRegsT *)GetVoiceRegs(iChannel);

		if (pMixer)
		{
			// query mixer to get state of channel
			// the mixer provides feedback to the DSP so it can be a source of determinism problems
			if (pMixer->GetChannelState(iChannel, &pRegs->envx, &pRegs->outx))
			{
				// channel just turned off
				m_Regs[SNSPCDSP_REG_ENDX] |=   1 << iChannel;
				// clear key on/off for channel
				m_Regs[SNSPCDSP_REG_KON]  &= ~(1 << iChannel);
				m_Regs[SNSPCDSP_REG_KOFF] &= ~(1 << iChannel);
			}
			
		} else
		{
			pRegs->envx = 0;
			pRegs->outx = 0;
		}
	}
	/* End-of-frame replay has consumed every queued write. Reconcile dynamic
	 * ENVX/OUTX/ENDX and all register side effects for next frame readback. */
	memcpy(m_LiveRegs, m_Regs, sizeof(m_Regs));
}

