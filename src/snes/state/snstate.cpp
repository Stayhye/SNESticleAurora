
#include <string.h>
#include <stdio.h>
#include "types.h"
#include "console.h"
#include "snes.h"
#include "snstate.h"
#include "dsp4emu.h"

/* AURORA_SPEEDY_MDR_STATE_LAYOUT_V1
   SNStateCPUT was 44 bytes before MDR support. New fields only consume
   old padding, so existing state payload size must remain identical. */
typedef char SNStateCPUSizeMustRemain44[(sizeof(SNStateCPUT) == 44) ? 1 : -1];

/* AURORA_CX4_STATE_V7
 *
 * SnesStateT already carries a fixed 256 KiB SRAM image for compatibility.
 * Put a tagged CX4 snapshot at the END of its unused SRAM tail rather than
 * growing SnesStateT and invalidating every pre-existing normal SNES state.
 */
struct SNCX4StateEnvelopeT
{
    Uint8 Tag[8];
    Uint32 Version;
    SNCX4StateT CX4;
};

static const Uint8 _SNCX4StateTag[8] =
    { 'C', 'X', '4', 'S', 'T', '7', 0, 0 };

static SNCX4StateEnvelopeT *_SNCX4StateEnvelope(SnesStateT *pState)
{
    return (SNCX4StateEnvelopeT *)
        (pState->SRam + sizeof(pState->SRam) - sizeof(SNCX4StateEnvelopeT));
}

static const SNCX4StateEnvelopeT *_SNCX4StateEnvelope(
    const SnesStateT *pState)
{
    return (const SNCX4StateEnvelopeT *)
        (pState->SRam + sizeof(pState->SRam) - sizeof(SNCX4StateEnvelopeT));
}

Bool SnesSystem::CanSerializeCX4State()
{
    Uint32 nSramBytes;

    if (!m_pRom || !(m_pRom->m_Flags & SNROM_FLAG_CX4))
        return FALSE;

    nSramBytes = m_pRom->GetSRAMBytes();

    return nSramBytes <=
        (Uint32)(SNES_SRAMSIZE - sizeof(SNCX4StateEnvelopeT))
        ? TRUE : FALSE;
}

/* AURORA_SPECIAL_CHIP_STATE_V1
 *
 * Keep sizeof(SnesStateT) byte-for-byte compatible.  Like the existing CX4
 * support, implemented coprocessor snapshots live in the unused tail of the
 * fixed 256 KiB SRAM image.  The CX4 envelope stays at the exact old offset;
 * this envelope is placed immediately before it.
 *
 * The payload stores only runtime bytes with no vtable or live host pointers.
 * A byte-count guard makes a layout-changing future build reject an old
 * special-chip state instead of restoring a mismatched object layout.
 */
#define SNSPECIAL_STATE_PAYLOAD_BYTES (12 * 1024)
#define SNSPECIAL_STATE_SUPPORTED_FLAGS \
    (SNROM_FLAG_DSP1 | SNROM_FLAG_DSP2 | SNROM_FLAG_DSP4 | \
     SNROM_FLAG_OBC1 | SNROM_FLAG_SUPERFX | SNROM_FLAG_SDD1 | \
     SNROM_FLAG_SRTC | SNROM_FLAG_SA1) /* AURORA_SA1_SPECIAL_STATE_V8_3_20260903 */

struct SNSpecialStateEnvelopeT
{
    Uint8  Tag[8];
    Uint32 Version;
    Uint32 ChipFlag;
    Uint32 DataBytes;
    Uint8  Data[SNSPECIAL_STATE_PAYLOAD_BYTES];
};

static const Uint8 _SNSpecialStateTag[8] =
    { 'A', 'U', 'S', 'P', 'S', 'T', '1', 0 };

static Uint32 _SNSpecialStateFlag(Uint32 uFlags)
{
    Uint32 uSpecial = uFlags & SNSPECIAL_STATE_SUPPORTED_FLAGS;

    /* Commercial carts represented here use one of these coprocessors at a
       time.  Refuse an unexpected combination rather than guess an order. */
    if (!uSpecial || (uSpecial & (uSpecial - 1)))
        return 0;
    return uSpecial;
}

static SNSpecialStateEnvelopeT *_SNSpecialStateEnvelope(SnesStateT *pState)
{
    return (SNSpecialStateEnvelopeT *)
        (pState->SRam + sizeof(pState->SRam)
         - sizeof(SNCX4StateEnvelopeT)
         - sizeof(SNSpecialStateEnvelopeT));
}

static const SNSpecialStateEnvelopeT *_SNSpecialStateEnvelope(
    const SnesStateT *pState)
{
    return (const SNSpecialStateEnvelopeT *)
        (pState->SRam + sizeof(pState->SRam)
         - sizeof(SNCX4StateEnvelopeT)
         - sizeof(SNSpecialStateEnvelopeT));
}

typedef char SNSpecialStateEnvelopeMustFit[
    (sizeof(SNSpecialStateEnvelopeT) + sizeof(SNCX4StateEnvelopeT)
        < SNES_SRAMSIZE) ? 1 : -1];

/* AURORA_SA1_SPECIAL_STATE_V8_3_20260903 */
typedef char SNSA1StateMustFitSpecialPayload[
    (sizeof(SNSA1StateT) <= SNSPECIAL_STATE_PAYLOAD_BYTES) ? 1 : -1];

Bool SnesSystem::CanSerializeSpecialChipState()
{
    Uint32 uChip;
    Uint32 nStateBytes = 0;
    Uint32 nSramBytes;

    if (!m_pRom)
        return FALSE;

    uChip = _SNSpecialStateFlag(m_pRom->m_Flags);
    if (!uChip)
        return FALSE;

    switch (uChip)
    {
#if SNES_DSP1
        case SNROM_FLAG_DSP1:
            nStateBytes = (Uint32)
                (((const Uint8 *)&m_DSP1.m_Vz + sizeof(m_DSP1.m_Vz))
                 - (const Uint8 *)&m_DSP1.m_uSR);
            break;

        case SNROM_FLAG_DSP2:
            nStateBytes = (Uint32)
                (((const Uint8 *)&m_DSP2.m_Op0DOutLen + sizeof(m_DSP2.m_Op0DOutLen))
                 - (const Uint8 *)&m_DSP2.m_uCommand);
            break;

        case SNROM_FLAG_DSP4:
            nStateBytes = (Uint32)(sizeof(DSP4) + sizeof(DSP4_vars)
                + sizeof(dsp4_byte) + sizeof(dsp4_address));
            break;
#endif

        case SNROM_FLAG_OBC1:
            nStateBytes = (Uint32)
                (((const Uint8 *)&m_OBC1.m_Shift + sizeof(m_OBC1.m_Shift))
                 - (const Uint8 *)&m_OBC1.m_Ram[0]);
            break;

        case SNROM_FLAG_SUPERFX:
            /* All live GSU execution/cache/pixel state precedes m_pRom.
               m_pRom/m_pRam and their sizes belong to the currently loaded
               cartridge and must remain live pointers, never state bytes. */
            nStateBytes = (Uint32)
                ((const Uint8 *)&m_GSU.m_pRom
                 - (const Uint8 *)&m_GSU.m_R[0]);
            break;

        case SNROM_FLAG_SDD1:
            /* Decompression runs synchronously.  Between calls only the
               register/map prefix is persistent; the input pointer and
               arithmetic decoder scratch state are transient. */
            nStateBytes = (Uint32)
                (((const Uint8 *)&m_SDD1.m_bMapDirty + sizeof(m_SDD1.m_bMapDirty))
                 - (const Uint8 *)&m_SDD1.m_Reg[0]);
            break;

        case SNROM_FLAG_SRTC:
            /* Exclude the optional SNSRTC_TESTHOOK host function pointer. */
            nStateBytes = (Uint32)
                (((const Uint8 *)&m_SRTC.m_Index + sizeof(m_SRTC.m_Index))
                 - (const Uint8 *)&m_SRTC.m_Reg[0]);
            break;

        case SNROM_FLAG_SA1:
            nStateBytes = (Uint32)sizeof(SNSA1StateT);
            break;

        default:
            return FALSE;
    }

    if (!nStateBytes || nStateBytes > SNSPECIAL_STATE_PAYLOAD_BYTES)
        return FALSE;

    nSramBytes = m_pRom->GetSRAMBytes();
    return nSramBytes <=
        (Uint32)(SNES_SRAMSIZE
                 - sizeof(SNCX4StateEnvelopeT)
                 - sizeof(SNSpecialStateEnvelopeT))
        ? TRUE : FALSE;
}

/* AURORA_SGB_RUNTIME_V0_4_20260904: SGB also appends a private dynamic envelope. */
/* AURORA_SWC_FLOPPY_V4_20260831
 * Normal SNES keeps sizeof(SnesStateT); SWC appends its private envelope. */
void SnesSystem::SaveState(void *pState, Int32 nStateBytes)
{
    (void)SaveStateChecked(pState, nStateBytes);
}

void SnesSystem::RestoreState(void *pState, Int32 nStateBytes)
{
    (void)RestoreStateChecked(pState, nStateBytes);
}

Bool SnesSystem::SaveStateChecked(void *pState, Int32 nStateBytes)
{
    if (!pState)
        return FALSE;

    if (!m_bSuperWildCard && !m_SGB.IsActive())
    {
        if (nStateBytes != (Int32)sizeof(SnesStateT))
            return FALSE;
        SaveState((SnesStateT *)pState);
        return TRUE;
    }

    if (m_SGB.IsActive())
    {
        Uint32 nSgbBytes;
        Uint32 nExpected;
        Uint8 *pBytes = (Uint8 *)pState;
        SyncSuperGameBoy();
        nSgbBytes = m_SGB.GetStateBytes();
        nExpected = (Uint32)sizeof(SnesStateT) + nSgbBytes;
        if (!nSgbBytes || nStateBytes <= 0 || (Uint32)nStateBytes != nExpected)
            return FALSE;
        SaveState((SnesStateT *)pBytes);
        if (!m_SGB.SaveState(pBytes + sizeof(SnesStateT), nSgbBytes))
        {
            memset(pBytes, 0, nExpected);
            return FALSE;
        }
        return TRUE;
    }

    {
        Uint32 nSwcBytes = m_SWC.GetStateBytes();
        Uint32 nExpected = (Uint32)sizeof(SnesStateT) + nSwcBytes;
        Uint8 *pBytes = (Uint8 *)pState;

        if (nStateBytes <= 0 || (Uint32)nStateBytes != nExpected)
            return FALSE;

        SaveState((SnesStateT *)pBytes);
        if (!m_SWC.SaveState(pBytes + sizeof(SnesStateT), nSwcBytes))
        {
            memset(pBytes, 0, nExpected);
            return FALSE;
        }
        return TRUE;
    }
}

Bool SnesSystem::RestoreStateChecked(void *pState, Int32 nStateBytes)
{
    if (!pState)
        return FALSE;

    if (!m_bSuperWildCard && !m_SGB.IsActive())
    {
        if (nStateBytes != (Int32)sizeof(SnesStateT))
            return FALSE;
        return RestoreState((SnesStateT *)pState);
    }

    if (m_SGB.IsActive())
    {
        Uint32 nSgbBytes = m_SGB.GetStateBytes();
        Uint32 nExpected = (Uint32)sizeof(SnesStateT) + nSgbBytes;
        Uint8 *pBytes = (Uint8 *)pState;
        SnesStateT *pBase = (SnesStateT *)pBytes;
        if (!nSgbBytes || nStateBytes <= 0 || (Uint32)nStateBytes != nExpected ||
            memcmp(pBase->Tag, "SNS", 4) != 0)
            return FALSE;
        if (!RestoreState(pBase)) return FALSE;
        MapSuperGameBoy();
        if (!m_SGB.RestoreState(pBytes + sizeof(SnesStateT), nSgbBytes)) return FALSE;
        m_uSGBSyncClock = (Uint32)SNCPUGetCounter(&m_Cpu, SNCPU_COUNTER_TOTAL);
        return TRUE;
    }

    {
        Uint32 nSwcBytes = m_SWC.GetStateBytes();
        Uint32 nExpected = (Uint32)sizeof(SnesStateT) + nSwcBytes;
        Uint8 *pBytes = (Uint8 *)pState;
        SnesStateT *pBase = (SnesStateT *)pBytes;

        if (nStateBytes <= 0 || (Uint32)nStateBytes != nExpected ||
            memcmp(pBase->Tag, "SNS", 4) != 0)
            return FALSE;

        if (!m_SWC.RestoreState(
                pBytes + sizeof(SnesStateT), nSwcBytes))
            return FALSE;

        if (!RestoreState(pBase))
            return FALSE;

        MapSuperWildCard();
        return TRUE;
    }
}

Int32 SnesSystem::GetStateSize()
{
    if (m_SGB.IsActive())
    {
        Uint32 n = (Uint32)sizeof(SnesStateT) + m_SGB.GetStateBytes();
        return n <= 0x7fffffffu ? (Int32)n : 0;
    }
    if (m_bSuperWildCard)
    {
        Uint32 n = (Uint32)sizeof(SnesStateT) + m_SWC.GetStateBytes();
        return n <= 0x7fffffffu ? (Int32)n : 0;
    }
    return sizeof(SnesStateT);
}



void SnesSystem::SaveState(SnesStateT *pState)
{
	/* The state is persisted as an opaque payload by the PS2 front-end.
	   Clear padding and currently-unused fields first so two equivalent
	   states have deterministic bytes (and therefore a deterministic
	   CRC), instead of leaking whatever happened to be in the buffer. */
	memset(pState, 0, sizeof(*pState));

	// set tag
	pState->Tag[0] = 'S';
	pState->Tag[1] = 'N';
	pState->Tag[2] = 'S';
	pState->Tag[3] = '\0';

	pState->uFrame = m_uFrame;
	pState->uLine  = m_uLine;

	// copy cpu state
	pState->CPU.Regs = m_Cpu.Regs;
	pState->CPU.Cycles = m_Cpu.Cycles;
	pState->CPU.Counter[0] = m_Cpu.Counter[0];
	pState->CPU.Counter[1] = m_Cpu.Counter[1];
	pState->CPU.Counter[2] = m_Cpu.Counter[2];
	pState->CPU.Counter[3] = m_Cpu.Counter[3];
	pState->CPU.uSignal    = m_Cpu.uSignal;
	/* AURORA_SPEEDY_MDR_STATE_RW_V1 */
	pState->CPU.uMDR       = m_Cpu.uMDR;
	pState->CPU.uMDRTag[0] = 'M';
	pState->CPU.uMDRTag[1] = 'D';

	pState->SPC.Regs = m_Spc.Regs;
	pState->SPC.Cycles = m_Spc.Cycles;
	pState->SPC.Counter[0] = m_Spc.Counter[0];
	pState->SPC.Counter[1] = m_Spc.Counter[1];
	/* AURORA_SPC700_HALT_STATE_TAG_V1 / AURORA_SPC700_ACCURACY_BATCH2_V1_20260914
	 * Old states always stored zero here; the nonzero tag says Regs.uPad
	 * contains a defined SLEEP/STOP state rather than legacy padding. */
	pState->SPC.uCycleShift = SNSPC_STATE_HALT_TAG;

	m_PPU.SaveState(&pState->PPU);
	m_DMAC.SaveState(&pState->DMAC);
	m_IO.SaveState(&pState->IO);
	m_SpcDsp.SaveState(&pState->SPCDSP);
	m_SpcDspMixer.SaveState(&pState->SPCDSP);
	m_SpcIO.SaveState(&pState->SPCIO);

	// save memory state
	memcpy(pState->Ram, m_Ram, sizeof(pState->Ram));
	memcpy(pState->SRam, m_SRam, sizeof(pState->SRam));

    /* AURORA_CX4_STATE_V7 */
    if (m_pRom && (m_pRom->m_Flags & SNROM_FLAG_CX4) &&
        CanSerializeCX4State())
    {
        SNCX4StateEnvelopeT *pCX4 = _SNCX4StateEnvelope(pState);
        memset(pCX4, 0, sizeof(*pCX4));
        memcpy(pCX4->Tag, _SNCX4StateTag, sizeof(pCX4->Tag));
        pCX4->Version = 1;
        m_CX4.SaveState(&pCX4->CX4);
    }

    /* AURORA_SPECIAL_CHIP_STATE_V1 */
    if (m_pRom && CanSerializeSpecialChipState())
    {
        SNSpecialStateEnvelopeT *pSpecial = _SNSpecialStateEnvelope(pState);
        Uint32 uChip = _SNSpecialStateFlag(m_pRom->m_Flags);
        Uint32 nStateBytes = 0;

        memset(pSpecial, 0, sizeof(*pSpecial));
        memcpy(pSpecial->Tag, _SNSpecialStateTag, sizeof(pSpecial->Tag));
        pSpecial->Version = 1;
        pSpecial->ChipFlag = uChip;

        switch (uChip)
        {
#if SNES_DSP1
            case SNROM_FLAG_DSP1:
            {
                const Uint8 *pBegin = (const Uint8 *)&m_DSP1.m_uSR;
                const Uint8 *pEnd =
                    (const Uint8 *)&m_DSP1.m_Vz + sizeof(m_DSP1.m_Vz);
                nStateBytes = (Uint32)(pEnd - pBegin);
                memcpy(pSpecial->Data, pBegin, nStateBytes);
                break;
            }

            case SNROM_FLAG_DSP2:
            {
                const Uint8 *pBegin = (const Uint8 *)&m_DSP2.m_uCommand;
                const Uint8 *pEnd = (const Uint8 *)&m_DSP2.m_Op0DOutLen
                    + sizeof(m_DSP2.m_Op0DOutLen);
                nStateBytes = (Uint32)(pEnd - pBegin);
                memcpy(pSpecial->Data, pBegin, nStateBytes);
                break;
            }

            case SNROM_FLAG_DSP4:
            {
                Uint32 uOffset = 0;
                memcpy(pSpecial->Data + uOffset, &DSP4, sizeof(DSP4));
                uOffset += sizeof(DSP4);
                memcpy(pSpecial->Data + uOffset, &DSP4_vars, sizeof(DSP4_vars));
                uOffset += sizeof(DSP4_vars);
                memcpy(pSpecial->Data + uOffset, &dsp4_byte, sizeof(dsp4_byte));
                uOffset += sizeof(dsp4_byte);
                memcpy(pSpecial->Data + uOffset, &dsp4_address, sizeof(dsp4_address));
                uOffset += sizeof(dsp4_address);
                nStateBytes = uOffset;
                break;
            }
#endif

            case SNROM_FLAG_OBC1:
            {
                const Uint8 *pBegin = (const Uint8 *)&m_OBC1.m_Ram[0];
                const Uint8 *pEnd =
                    (const Uint8 *)&m_OBC1.m_Shift + sizeof(m_OBC1.m_Shift);
                nStateBytes = (Uint32)(pEnd - pBegin);
                memcpy(pSpecial->Data, pBegin, nStateBytes);
                break;
            }

            case SNROM_FLAG_SUPERFX:
            {
                const Uint8 *pBegin = (const Uint8 *)&m_GSU.m_R[0];
                const Uint8 *pEnd = (const Uint8 *)&m_GSU.m_pRom;
                nStateBytes = (Uint32)(pEnd - pBegin);
                memcpy(pSpecial->Data, pBegin, nStateBytes);
                break;
            }

            case SNROM_FLAG_SDD1:
            {
                const Uint8 *pBegin = (const Uint8 *)&m_SDD1.m_Reg[0];
                const Uint8 *pEnd = (const Uint8 *)&m_SDD1.m_bMapDirty
                    + sizeof(m_SDD1.m_bMapDirty);
                nStateBytes = (Uint32)(pEnd - pBegin);
                memcpy(pSpecial->Data, pBegin, nStateBytes);
                break;
            }

            case SNROM_FLAG_SRTC:
            {
                const Uint8 *pBegin = (const Uint8 *)&m_SRTC.m_Reg[0];
                const Uint8 *pEnd =
                    (const Uint8 *)&m_SRTC.m_Index + sizeof(m_SRTC.m_Index);
                nStateBytes = (Uint32)(pEnd - pBegin);
                memcpy(pSpecial->Data, pBegin, nStateBytes);
                break;
            }

            case SNROM_FLAG_SA1:
            {
                SNSA1StateT *pSA1 = (SNSA1StateT *)pSpecial->Data;
                if (m_SA1.SaveState(pSA1))
                    nStateBytes = (Uint32)sizeof(*pSA1);
                break;
            }

            default:
                break;
        }

        pSpecial->DataBytes = nStateBytes;
    }

    // copy spc ram
    pState->SPC.bRomEnable = m_Spc.bRomEnable;

    SNSPCSetRomEnable(&m_Spc, FALSE);
	memcpy(pState->SpcRam, m_Spc.Mem, SNSPC_MEM_SIZE);
    SNSPCSetRomEnable(&m_Spc,  pState->SPC.bRomEnable);
}

Bool SnesSystem::RestoreState(SnesStateT *pState)
{
	if (memcmp(pState->Tag, "SNS", 4))
	{
		return FALSE;
	}

    if (m_pRom && (m_pRom->m_Flags & SNROM_FLAG_CX4))
    {
        const SNCX4StateEnvelopeT *pCX4 = _SNCX4StateEnvelope(pState);
        if (!CanSerializeCX4State() ||
            memcmp(pCX4->Tag, _SNCX4StateTag, sizeof(pCX4->Tag)) != 0 ||
            pCX4->Version != 1)
        {
            return FALSE;
        }
    }

    /* AURORA_SPECIAL_CHIP_STATE_V1
     * Validate the coprocessor envelope before mutating any live machine
     * state. Old special-chip states were blocked by the frontend, so there
     * is no legacy special-chip payload to accept here. */
    if (m_pRom && _SNSpecialStateFlag(m_pRom->m_Flags))
    {
        const SNSpecialStateEnvelopeT *pSpecial =
            _SNSpecialStateEnvelope(pState);
        Uint32 uChip = _SNSpecialStateFlag(m_pRom->m_Flags);
        Uint32 nExpectedBytes = 0;

        if (!CanSerializeSpecialChipState() ||
            memcmp(pSpecial->Tag, _SNSpecialStateTag,
                   sizeof(pSpecial->Tag)) != 0 ||
            pSpecial->Version != 1 ||
            pSpecial->ChipFlag != uChip)
        {
            return FALSE;
        }

        switch (uChip)
        {
#if SNES_DSP1
            case SNROM_FLAG_DSP1:
                nExpectedBytes = (Uint32)
                    (((const Uint8 *)&m_DSP1.m_Vz + sizeof(m_DSP1.m_Vz))
                     - (const Uint8 *)&m_DSP1.m_uSR);
                break;
            case SNROM_FLAG_DSP2:
                nExpectedBytes = (Uint32)
                    (((const Uint8 *)&m_DSP2.m_Op0DOutLen + sizeof(m_DSP2.m_Op0DOutLen))
                     - (const Uint8 *)&m_DSP2.m_uCommand);
                break;
            case SNROM_FLAG_DSP4:
                nExpectedBytes = (Uint32)(sizeof(DSP4) + sizeof(DSP4_vars)
                    + sizeof(dsp4_byte) + sizeof(dsp4_address));
                break;
#endif
            case SNROM_FLAG_OBC1:
                nExpectedBytes = (Uint32)
                    (((const Uint8 *)&m_OBC1.m_Shift + sizeof(m_OBC1.m_Shift))
                     - (const Uint8 *)&m_OBC1.m_Ram[0]);
                break;
            case SNROM_FLAG_SUPERFX:
                nExpectedBytes = (Uint32)
                    ((const Uint8 *)&m_GSU.m_pRom
                     - (const Uint8 *)&m_GSU.m_R[0]);
                break;
            case SNROM_FLAG_SDD1:
                nExpectedBytes = (Uint32)
                    (((const Uint8 *)&m_SDD1.m_bMapDirty + sizeof(m_SDD1.m_bMapDirty))
                     - (const Uint8 *)&m_SDD1.m_Reg[0]);
                break;
            case SNROM_FLAG_SRTC:
                nExpectedBytes = (Uint32)
                    (((const Uint8 *)&m_SRTC.m_Index + sizeof(m_SRTC.m_Index))
                     - (const Uint8 *)&m_SRTC.m_Reg[0]);
                break;
            case SNROM_FLAG_SA1:
                nExpectedBytes = (Uint32)sizeof(SNSA1StateT);
                break;
            default:
                return FALSE;
        }

        if (!nExpectedBytes ||
            nExpectedBytes > SNSPECIAL_STATE_PAYLOAD_BYTES ||
            pSpecial->DataBytes != nExpectedBytes)
        {
            return FALSE;
        }
    }

	m_uFrame = pState->uFrame;
	m_uLine  = pState->uLine;

	// restore state
	m_Cpu.Regs = pState->CPU.Regs;
	m_Cpu.Cycles = pState->CPU.Cycles;
	m_Cpu.Counter[0] = pState->CPU.Counter[0];
	m_Cpu.Counter[1] = pState->CPU.Counter[1];
	m_Cpu.Counter[2] = pState->CPU.Counter[2];
	m_Cpu.Counter[3] = pState->CPU.Counter[3];
	m_Cpu.nAbortCycles = 0;
	m_Cpu.uSignal    = pState->CPU.uSignal;
	/* AURORA_SPEEDY_MDR_STATE_RW_V1
	   Old states used these bytes as padding; trust MDR only with tag MD. */
	if (pState->CPU.uMDRTag[0] == 'M' && pState->CPU.uMDRTag[1] == 'D')
		m_Cpu.uMDR = pState->CPU.uMDR;
	else
		m_Cpu.uMDR = (Uint8)(m_Cpu.Regs.rPC >> 8);
	m_Cpu.uNmiDmaDelay = 0;

	m_Spc.Regs = pState->SPC.Regs;
	if (pState->SPC.uCycleShift == SNSPC_STATE_HALT_TAG)
		m_Spc.Regs.uPad &= SNSPC_HALT_MASK;
	else
		m_Spc.Regs.uPad = SNSPC_HALT_NONE;
	m_Spc.Cycles = pState->SPC.Cycles;
	m_Spc.Counter[0] = pState->SPC.Counter[0];
	m_Spc.Counter[1] = pState->SPC.Counter[1];
	m_Spc.uPad = 0;


	m_PPU.RestoreState(&pState->PPU);
	m_DMAC.RestoreState(&pState->DMAC);
	m_IO.RestoreState(&pState->IO);
	/* The legacy state payload does not contain the DSP write queue,
	   echo history or noise generator.  Reset those transient pieces
	   before applying the serialized registers/channels so a load does
	   not inherit audio work from the future state it is replacing. */
	m_SpcDsp.Reset();
	m_SpcDspMixer.Reset();
	m_SpcDspSilentMixer.Reset();
	m_SpcDsp.RestoreState(&pState->SPCDSP);
	m_SpcDspMixer.RestoreState(&pState->SPCDSP);
	m_SpcIO.RestoreState(&pState->SPCIO);

	// restore memory state
	memcpy(m_Ram, pState->Ram, sizeof(m_Ram));
	memcpy(m_SRam, pState->SRam, sizeof(m_SRam));

    /* AURORA_CX4_STATE_V7 */
    if (m_pRom && (m_pRom->m_Flags & SNROM_FLAG_CX4))
    {
        const SNCX4StateEnvelopeT *pCX4 = _SNCX4StateEnvelope(pState);
        if (!m_CX4.RestoreState(&pCX4->CX4))
            return FALSE;
    }

    /* AURORA_SPECIAL_CHIP_STATE_V1 */
    if (m_pRom && _SNSpecialStateFlag(m_pRom->m_Flags))
    {
        const SNSpecialStateEnvelopeT *pSpecial =
            _SNSpecialStateEnvelope(pState);
        Uint32 uChip = _SNSpecialStateFlag(m_pRom->m_Flags);

        switch (uChip)
        {
#if SNES_DSP1
            case SNROM_FLAG_DSP1:
                memcpy(&m_DSP1.m_uSR, pSpecial->Data,
                       pSpecial->DataBytes);
                break;

            case SNROM_FLAG_DSP2:
                memcpy(&m_DSP2.m_uCommand, pSpecial->Data,
                       pSpecial->DataBytes);
                break;

            case SNROM_FLAG_DSP4:
            {
                Uint32 uOffset = 0;
                memcpy(&DSP4, pSpecial->Data + uOffset, sizeof(DSP4));
                uOffset += sizeof(DSP4);
                memcpy(&DSP4_vars, pSpecial->Data + uOffset, sizeof(DSP4_vars));
                uOffset += sizeof(DSP4_vars);
                memcpy(&dsp4_byte, pSpecial->Data + uOffset, sizeof(dsp4_byte));
                uOffset += sizeof(dsp4_byte);
                memcpy(&dsp4_address, pSpecial->Data + uOffset,
                       sizeof(dsp4_address));
                break;
            }
#endif

            case SNROM_FLAG_OBC1:
                memcpy(&m_OBC1.m_Ram[0], pSpecial->Data,
                       pSpecial->DataBytes);
                break;

            case SNROM_FLAG_SUPERFX:
                /* Preserve the current cartridge pointers/sizes; only the
                   pointer-free runtime prefix is restored. */
                memcpy(&m_GSU.m_R[0], pSpecial->Data,
                       pSpecial->DataBytes);
                break;

            case SNROM_FLAG_SDD1:
                /* Drop stale decoder scratch/pointer state, then restore the
                   persistent register/map prefix. */
                m_SDD1.Reset();
                memcpy(&m_SDD1.m_Reg[0], pSpecial->Data,
                       pSpecial->DataBytes);
                break;

            case SNROM_FLAG_SRTC:
                memcpy(&m_SRTC.m_Reg[0], pSpecial->Data,
                       pSpecial->DataBytes);
                break;

            case SNROM_FLAG_SA1:
                if (!m_SA1.RestoreState(
                        (const SNSA1StateT *)pSpecial->Data))
                    return FALSE;
                break;

            default:
                return FALSE;
        }
    }

    // copy spc ram
    SNSPCSetRomEnable(&m_Spc, FALSE);
	memcpy(m_Spc.Mem, pState->SpcRam, SNSPC_MEM_SIZE);
    SNSPCSetRomEnable(&m_Spc, pState->SPC.bRomEnable);


	// set fast or slow rom
	if (m_IO.m_Regs.memsel & 1)
	{
		SetFastRom();
	} else
	{
		SetSlowRom();
	}

    /* AURORA_SPECIAL_CHIP_STATE_V1
     * SetFastRom/SetSlowRom may touch CPU bank descriptors, so make the
     * restored S-DD1 $C0-$FF mapping the final word. */
    if (m_pRom && (m_pRom->m_Flags & SNROM_FLAG_SDD1))
    {
        RemapSDD1();
        m_SDD1.ClearMapDirty();
    }

    /* AURORA_SA1_SPECIAL_STATE_V8_3_20260903
     * SetFastRom/SetSlowRom runs after the special-state payload. Reassert
     * the restored SA-1 cartridge windows last, using live host pointers. */
    if (m_pRom && (m_pRom->m_Flags & SNROM_FLAG_SA1))
        m_SA1.MapMainCPU(&m_Cpu);

    return TRUE;
}


void SnesIO::SaveState(struct SNStateIOT *pState)
{
	pState->Input = m_Input;
	pState->Regs = m_Regs;
}
void SnesIO::RestoreState(struct SNStateIOT *pState)
{
	m_Input = pState->Input;
	m_Regs = pState->Regs;
	/* External battery memory is not part of the legacy state payload. */
	SnesTurboFileResetBus();
}

void SNSpcIO::SaveState(struct SNStateSPCIOT *pState)
{
	pState->Regs = m_Regs;
}
void SNSpcIO::RestoreState(struct SNStateSPCIOT *pState)
{
	m_Regs = pState->Regs;
}

void SnesDMAC::SaveState(struct SNStateDMACT *pState)
{
	pState->m_HDMAEnable = m_HDMAEnable;
	pState->m_HDMAEnded = m_HDMAEnded;
	pState->m_HDMADoTransfer = m_HDMADoTransfer;
	pState->m_MDMAEnable = m_MDMAEnable;
	memcpy(pState->m_Channels, m_Channels, sizeof(pState->m_Channels));
}

void SnesDMAC::RestoreState(struct SNStateDMACT *pState)
{
	m_HDMAEnable = pState->m_HDMAEnable;
	m_HDMAEnded = pState->m_HDMAEnded;
	m_HDMADoTransfer = pState->m_HDMADoTransfer;
	m_MDMAEnable = pState->m_MDMAEnable;
	memcpy(m_Channels, pState->m_Channels, sizeof(m_Channels));
	/* AURORA_V82_MDMA_PHASE_STATE_RESTORE
	 * Phase/startup are transient scheduler state and are intentionally not
	 * added to the legacy opaque save-state payload. */
	memset(m_MDMAPhase, 0, sizeof(m_MDMAPhase));
	m_MDMAChannelStartup = 0;
	m_MDMAStartupPending = 0;
	m_uRasterLine = 0;
}

void SnesPPU::SaveState(struct SNStatePPUT *pState)
{
	pState->Regs  = m_Regs;
	memcpy(pState->m_CGRAM,   m_CGRAM, sizeof(pState->m_CGRAM));
	memcpy(pState->m_VRAM,    m_VRAM, sizeof(pState->m_VRAM));
	pState->m_OAM = m_OAM;
}

void SnesPPU::RestoreState(struct SNStatePPUT *pState)
{
	m_Regs = pState->Regs;
	/* AURORA_OBJ_STAT77_V2_STATE_20260915
	 * V1 geometry is transient/derived and intentionally absent from the
	 * legacy save-state payload. Reconstruct it from restored SETINI and make
	 * the inactive 239-line tail retire on the next presented normal frame. */
	m_uFrameVisibleLines = (m_Regs.setini & SNESPPU_SETINI_OVERSCAN)
		? SNESPPU_VISIBLE_LINES_OVERSCAN
		: SNESPPU_VISIBLE_LINES_NORMAL;
	m_bFrameInterlace = (m_Regs.setini & SNESPPU_SETINI_INTERLACE) != 0;
	/* AURORA_SAFE_RASTER_V4_STATE_20260915
	 * Timing interlace is transient counter state. Save states are restored at
	 * Aurora's scheduler boundary, so seed it from the restored SETINI image;
	 * the normal V=128 capture will refresh it during the next field. */
	m_bTimingInterlace = m_bFrameInterlace;
	m_bInactiveTailClearPending =
		(m_uFrameVisibleLines == SNESPPU_VISIBLE_LINES_NORMAL);
	memcpy(m_CGRAM,   pState->m_CGRAM, sizeof(m_CGRAM));
	memcpy(m_VRAM,    pState->m_VRAM,  sizeof(m_VRAM));
	m_OAM = pState->m_OAM;
	/* AURORA_PPU_MEMORY_V3_STATE_20260915
	 * These are transient bus/latch states and are deliberately not appended
	 * to the legacy opaque state payload. Never inherit them from the timeline
	 * that is being replaced. Re-seed Mode 7's derived scanline latches too. */
	m_OAMLatch = 0;
	m_CGRAMLatch = 0;
	m_PPU1MDR = 0;
	m_PPU2MDR = 0;
	m_uMemoryAccessFlags = 0;
	/* AURORA_DOT_RASTER_V6_STATE_20260915 */
	m_bRasterLineRendered = FALSE;
	m_Mode7LineHofs = m_Regs.m7hofs.w;
	m_Mode7LineVofs = m_Regs.m7vofs.w;
	m_uMosaicStartLine = 1;
#if SNPPU_WRITEQUEUE
	/* AURORA_PPU_STATE_QUEUE_RESET_V6_2_20260829
	 * m_Queue is transient scheduler state and is intentionally absent from
	 * SNStatePPUT. Never let pending writes from the timeline being replaced
	 * survive a state load. Keep the legacy state payload byte-for-byte
	 * unchanged, matching the existing policy for transient DMAC phases. */
	m_Queue.Reset();
#endif
	m_pRender->UpdateVRAMRange(0, SNESPPU_VRAM_NUMWORDS);
	/* OAM bytes, SETINI.1 and priority rotation all came from the state.
	 * Force one complete derived rebuild instead of relying on whether the
	 * restored first-sprite index happens to differ from the old one. */
	m_pRender->SetUpdateFlags(SNESPPURENDER_UPDATE_ALL);
	UpdateOAMPriority();
}


void SNSpcDsp::SaveState(struct SNStateSPCDSPT *pState)
{
	memcpy(pState->m_Regs, m_Regs, sizeof(m_Regs));
}

void SNSpcDsp::RestoreState(struct SNStateSPCDSPT *pState)
{
	memcpy(m_Regs, pState->m_Regs, sizeof(m_Regs));
}

void SNSpcDspMix::SaveState(struct SNStateSPCDSPT *pState)
{
	memcpy(pState->m_Channels, m_Channels, sizeof(m_Channels));
}

void SNSpcDspMix::RestoreState(struct SNStateSPCDSPT *pState)
{
	memcpy(m_Channels, pState->m_Channels, sizeof(m_Channels));
}


void _SNStateMemDiff(const char *pTag, Uint8 *pA, Uint8 *pB, Int32 nBytes)
{
    Int32 iOffset;

    for (iOffset=0; iOffset < nBytes; iOffset++)
    {
        if (pA[iOffset]!=pB[iOffset])
        {
            ConDebug("%s: %04X %02X %02X\n", pTag, iOffset, pA[iOffset], pB[iOffset]);
        }
    }

}

void SNStateCompare(SnesStateT *pStateA, SnesStateT *pStateB)
{
    _SNStateMemDiff("Mem", pStateA->Ram, pStateB->Ram, SNES_RAMSIZE);
    _SNStateMemDiff("SpcMem", pStateA->SpcRam, pStateB->SpcRam, SNSPC_RAM_SIZE);
    _SNStateMemDiff("SRM", pStateA->SRam, pStateB->SRam, SNES_SRAMSIZE);
    _SNStateMemDiff("Cpu", (Uint8 *)&pStateA->CPU, (Uint8 *)&pStateB->CPU, sizeof(pStateA->CPU));
    _SNStateMemDiff("SPC", (Uint8 *)&pStateA->SPC, (Uint8 *)&pStateB->SPC, sizeof(pStateA->SPC));
	_SNStateMemDiff("SPCIO", (Uint8 *)&pStateA->SPCIO, (Uint8 *)&pStateB->SPCIO, sizeof(pStateA->SPCIO));
    _SNStateMemDiff("DSP", (Uint8 *)&pStateA->SPCDSP, (Uint8 *)&pStateB->SPCDSP, sizeof(pStateA->SPCDSP));
    _SNStateMemDiff("IO", (Uint8 *)&pStateA->IO, (Uint8 *)&pStateB->IO, sizeof(pStateA->IO));
    _SNStateMemDiff("PPU", (Uint8 *)&pStateA->PPU, (Uint8 *)&pStateB->PPU, sizeof(pStateA->PPU));
    _SNStateMemDiff("DMAC", (Uint8 *)&pStateA->DMAC, (Uint8 *)&pStateB->DMAC, sizeof(pStateA->DMAC));

}
