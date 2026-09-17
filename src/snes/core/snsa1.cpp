/* AURORA_DSP_SA1_FX_CX4_CPU_MEGA_ACCURACY_V6_20260916: cumulative DSP V2 + SA-1 accuracy */
/* AURORA_V13_UNIFIED_GBC_AUDIO_32X_FRAMESKIP_20260910 */
#include <string.h>
#include "types.h"
#include "snsa1.h"
#include "snes.h"
#include "sncpudefs.h"

/* AURORA_SA1_V1_REFERENCE_LOGIC_20260902
 * Behavioural reference: reference emulator sa1.cpp / sa1cpu.cpp.
 * CPU execution itself is NOT copied: the Aurora SNCpuT core is instantiated
 * a second time.  One S-CPU master-clock slice gives SA-1 a 3x cycle budget,
 * matching reference emulator's SA1.Cycles < CPU.Cycles * 3 scheduler relationship.
 */

static Uint16 _SA1LE16(const Uint8 *p)
{
    return (Uint16)p[0] | ((Uint16)p[1] << 8);
}

/* AURORA_SA1_SAFE_PERF_V9_20260910
 * Bit-exact 8x8 transpose for both character-conversion engines.
 * Byte N is pixel N; result byte N is plane N, pixel 0 in bit 7. */
static unsigned long long _SA1Pack8Pixels(const Uint8 *p)
{
    return ((unsigned long long)p[0] << 0)  |
           ((unsigned long long)p[1] << 8)  |
           ((unsigned long long)p[2] << 16) |
           ((unsigned long long)p[3] << 24) |
           ((unsigned long long)p[4] << 32) |
           ((unsigned long long)p[5] << 40) |
           ((unsigned long long)p[6] << 48) |
           ((unsigned long long)p[7] << 56);
}

static unsigned long long _SA1ExpandPackedPixels(unsigned long long bits, Uint32 depth)
{
    if (depth == 8)
        return bits;
    if (depth == 4)
    {
        return ((bits >> 0)  & 0x0FULL) |
              (((bits >> 4)  & 0x0FULL) << 8)  |
              (((bits >> 8)  & 0x0FULL) << 16) |
              (((bits >> 12) & 0x0FULL) << 24) |
              (((bits >> 16) & 0x0FULL) << 32) |
              (((bits >> 20) & 0x0FULL) << 40) |
              (((bits >> 24) & 0x0FULL) << 48) |
              (((bits >> 28) & 0x0FULL) << 56);
    }
    return ((bits >> 0)  & 0x03ULL) |
          (((bits >> 2)  & 0x03ULL) << 8)  |
          (((bits >> 4)  & 0x03ULL) << 16) |
          (((bits >> 6)  & 0x03ULL) << 24) |
          (((bits >> 8)  & 0x03ULL) << 32) |
          (((bits >> 10) & 0x03ULL) << 40) |
          (((bits >> 12) & 0x03ULL) << 48) |
          (((bits >> 14) & 0x03ULL) << 56);
}

static unsigned long long _SA1Transpose8x8ToPlanes(unsigned long long x)
{
    unsigned long long t;
    t = (x ^ (x >> 7)) & 0x00AA00AA00AA00AAULL;
    x ^= t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000CCCC0000CCCCULL;
    x ^= t ^ (t << 14);
    t = (x ^ (x >> 28)) & 0x00000000F0F0F0F0ULL;
    x ^= t ^ (t << 28);

    x = ((x & 0x5555555555555555ULL) << 1) |
        ((x >> 1) & 0x5555555555555555ULL);
    x = ((x & 0x3333333333333333ULL) << 2) |
        ((x >> 2) & 0x3333333333333333ULL);
    x = ((x & 0x0F0F0F0F0F0F0F0FULL) << 4) |
        ((x >> 4) & 0x0F0F0F0F0F0F0F0FULL);
    return x;
}

SNSA1::SNSA1()
{
    memset(this, 0, sizeof(*this));
    SNCPUNew(&m_Cpu);
    m_Cpu.pUserData = this;
}

SNSA1::~SNSA1()
{
    SNCPUDelete(&m_Cpu);
}

Bool SNSA1::Attach(SnesSystem *pOwner,
                   const Uint8 *pRom, Uint32 nRomBytes,
                   Uint8 *pBWRAM, Uint32 nBWRAMBytes,
                   Bool bMapMainRom, Bool bDonorBWRAM,
                   Bool bTrackBWRAMDirty)
{
    if (!pOwner || !pRom || !nRomBytes)
        return FALSE;

    m_pOwner = pOwner;
    m_pRom = pRom;
    m_nRomBytes = nRomBytes;
    m_pBWRAM = pBWRAM;
    m_nBWRAMBytes = nBWRAMBytes;
    if (m_nBWRAMBytes > 0x40000u)
        m_nBWRAMBytes = 0x40000u;
    /* AURORA_SA1_PERF_V8_3_2_20260903
     * SA-1 SRAM/BW-RAM sizes are normally powers of two. Avoid MIPS div/mod
     * in byte-granular hot paths while retaining exact fallback semantics. */
    m_uBWRAMMask = (m_nBWRAMBytes > 1u &&
                     !(m_nBWRAMBytes & (m_nBWRAMBytes - 1u)))
        ? (m_nBWRAMBytes - 1u) : 0u;
    m_bMapMainRom = bMapMainRom;
    m_bDonorBWRAM = bDonorBWRAM;
    m_bTrackBWRAMDirty = bTrackBWRAMDirty;
    m_bActive = TRUE;

    /* AURORA_V12_SELF_AUDIT_GBC_FX1_20260910: Reset() already establishes the private SA-1 map. */
    Reset();
    return TRUE;
}

void SNSA1::Detach()
{
    if (m_pOwner)
        m_pOwner->SetSA1IRQ(FALSE);
    m_bActive = FALSE;
    m_bMapMainRom = FALSE;
    m_bDonorBWRAM = FALSE;
    m_bTrackBWRAMDirty = FALSE;
    m_pOwner = NULL;
    m_pRom = NULL;
    m_nRomBytes = 0;
    m_pBWRAM = NULL;
    m_nBWRAMBytes = 0;
    m_uBWRAMMask = 0;
    m_uMainBWRAMBase = 0;
    m_uSA1BWRAMBase = 0;
    m_uSA1BitmapBaseBytes = 0;
    m_uSA1BitmapPixelBase = 0;
    m_uBWRAMProtectLimit = 0;
    m_bSA1BWRAMBitmap = FALSE;
    m_bBWRAMProtectActive = FALSE;
    m_bDirectBWRAMWrite = FALSE;
    m_uCCDSA = m_uCCDDA = m_uCCBPL = 0;
    m_uTimerHMax = m_uTimerVMax = 0;
    m_uTimerHTarget = m_uTimerVTarget = 0;
    m_uCCBPP = m_uCCCharMask = m_uCCDmaSize = m_uCCTileXMask = m_uCCTileShift = 0;
}

void SNSA1::Reset()
{
    if (!m_bActive)
        return;

    memset(m_Reg, 0, sizeof(m_Reg));
    memset(m_IRAM, 0, sizeof(m_IRAM));
    memset(m_CharData, 0, sizeof(m_CharData));

    /* Same power-on values used by reference emulator SA-1 init. */
    m_Reg[0x00] = 0x20; /* CCNT: SA-1 held in reset */
    m_Reg[0x20] = 0x00;
    m_Reg[0x21] = 0x01;
    m_Reg[0x22] = 0x02;
    m_Reg[0x23] = 0x03;
    m_Reg[0x28] = 0x0F;

    m_uOp1 = m_uOp2 = 0;
    m_uArithmeticOp = 0;
    m_uSum = 0;
    m_bArithmeticOverflow = FALSE;
    m_uVariableBitPos = 0;
    m_uCharIndex = 0;
    m_bCharDMA = FALSE;
    m_uBitmapFormat = 4;
    m_uHCounter = m_uVCounter = m_uPrevHCounter = 0;
    m_uLatchedHCounter = m_uLatchedVCounter = 0;
    m_bTimerLastState = FALSE;
    RebuildFastState();
    /* AURORA_V12_SELF_AUDIT_GBC_FX1_20260910: full MapSA1CPU() below supersedes reset-time bRAM-only refresh. */

    SNCPUResetCounters(&m_Cpu);
    SNCPUResetRegs(&m_Cpu);
    m_Cpu.uSignal = 0;
    m_Cpu.uNmiDmaDelay = 0;
    m_Cpu.Regs.rE = 1;
    m_Cpu.Regs.rP = SNCPU_FLAG_M | SNCPU_FLAG_X | SNCPU_FLAG_I;
    m_Cpu.Regs.rS.w = 0x01FF;
    m_Cpu.Regs.rPC = 0;
    m_Cpu.uMDR = 0;

    /* AURORA_SA1_COMPAT_V12_RESET_MAP_20260910
     * Reset changes Super MMC/BMAP/BMAPS/BWPA architectural state. Aurora's
     * Bank[] page descriptors are materialized host state, so rebuild them
     * now instead of carrying a pre-reset map into the next SA-1 instruction. */
    MapSA1CPU();

    UpdateMainIRQ();
}

/* AURORA_SA1_PERF_STATE_V8_3_20260903 */
/* AURORA SA-1 state v2: CC2 line semantics are now hardware-accurate. */
static const Uint8 _SNSA1StateTag[8] =
    { 'A', 'U', 'S', 'A', '1', 'S', '2', 0 };

Bool SNSA1::SaveState(SNSA1StateT *pState) const
{
    Int32 i;

    if (!m_bActive || !pState)
        return FALSE;

    memset(pState, 0, sizeof(*pState));
    memcpy(pState->Tag, _SNSA1StateTag, sizeof(pState->Tag));
    pState->Version = 2;

    pState->CpuRegs = m_Cpu.Regs;
    pState->CpuCycles = m_Cpu.Cycles;
    for (i = 0; i < SNCPU_COUNTER_NUM; ++i)
        pState->CpuCounter[i] = m_Cpu.Counter[i];
    pState->CpuSignal = m_Cpu.uSignal;
    pState->CpuNmiDmaDelay = m_Cpu.uNmiDmaDelay;
    pState->CpuMDR = m_Cpu.uMDR;

    memcpy(pState->Reg, m_Reg, sizeof(m_Reg));
    memcpy(pState->IRAM, m_IRAM, sizeof(m_IRAM));
    memcpy(pState->CharData, m_CharData, sizeof(m_CharData));

    pState->Sum = m_uSum;
    pState->HCounter = m_uHCounter;
    pState->VCounter = m_uVCounter;
    pState->PrevHCounter = m_uPrevHCounter;
    pState->LatchedHCounter = m_uLatchedHCounter;
    pState->LatchedVCounter = m_uLatchedVCounter;
    pState->Op1 = m_uOp1;
    pState->Op2 = m_uOp2;
    pState->ArithmeticOp = m_uArithmeticOp;
    pState->ArithmeticOverflow = m_bArithmeticOverflow ? 1 : 0;
    pState->VariableBitPos = m_uVariableBitPos;
    pState->CharIndex = m_uCharIndex;
    pState->CharDMA = m_bCharDMA ? 1 : 0;
    pState->BitmapFormat = m_uBitmapFormat;
    pState->TimerLastState = m_bTimerLastState ? 1 : 0;

    return TRUE;
}

Bool SNSA1::RestoreState(const SNSA1StateT *pState)
{
    Int32 i;

    if (!m_bActive || !pState ||
        memcmp(pState->Tag, _SNSA1StateTag, sizeof(pState->Tag)) != 0 ||
        pState->Version != 2)
        return FALSE;

    memcpy(m_Reg, pState->Reg, sizeof(m_Reg));
    memcpy(m_IRAM, pState->IRAM, sizeof(m_IRAM));
    memcpy(m_CharData, pState->CharData, sizeof(m_CharData));

    m_uSum = pState->Sum;
    m_uHCounter = pState->HCounter;
    m_uVCounter = pState->VCounter;
    m_uPrevHCounter = pState->PrevHCounter;
    m_uLatchedHCounter = pState->LatchedHCounter;
    m_uLatchedVCounter = pState->LatchedVCounter;
    m_uOp1 = pState->Op1;
    m_uOp2 = pState->Op2;
    m_uArithmeticOp = pState->ArithmeticOp;
    m_bArithmeticOverflow = pState->ArithmeticOverflow ? TRUE : FALSE;
    m_uVariableBitPos = pState->VariableBitPos;
    m_uCharIndex = pState->CharIndex;
    m_bCharDMA = pState->CharDMA ? TRUE : FALSE;
    m_uBitmapFormat = pState->BitmapFormat;
    m_bTimerLastState = pState->TimerLastState ? TRUE : FALSE;
    RebuildFastState();

    m_Cpu.Regs = pState->CpuRegs;
    m_Cpu.Cycles = pState->CpuCycles;
    for (i = 0; i < SNCPU_COUNTER_NUM; ++i)
        m_Cpu.Counter[i] = pState->CpuCounter[i];
    m_Cpu.nAbortCycles = 0;
    m_Cpu.bRunning = FALSE;
    m_Cpu.uSignal = pState->CpuSignal;
    m_Cpu.uNmiDmaDelay = pState->CpuNmiDmaDelay;
    m_Cpu.uMDR = pState->CpuMDR;
    m_Cpu.pUserData = this;

    /* Bank[] contains live host pointers and must be reconstructed from the
     * restored MMC/BW-RAM registers, never copied out of a state file. */
    MapSA1CPU();
    UpdateMainIRQ();
    return TRUE;
}

Uint32 SNSA1::MirrorRomOffset(Uint32 uPos) const
{
    Uint32 uMask;
    if (!m_nRomBytes || uPos < m_nRomBytes)
        return m_nRomBytes ? uPos : 0;

    uMask = 0x80000000u;
    while (uMask && !(uPos & uMask))
        uMask >>= 1;
    if (!uMask)
        return 0;
    if (m_nRomBytes <= (uPos & uMask))
        return MirrorRomOffset(uPos - uMask);

    /* Aurora's cartridge mirror rule, written iteratively for this object. */
    {
        Uint32 uBase = uMask;
        Uint32 uSize = m_nRomBytes - uMask;
        Uint32 uSub = uPos - uMask;
        while (uSize && uSub >= uSize)
        {
            Uint32 m = 0x80000000u;
            while (m && !(uSub & m)) m >>= 1;
            if (!m) { uSub = 0; break; }
            if (uSize <= (uSub & m)) uSub -= m;
            else { uBase += m; uSize -= m; uSub -= m; }
        }
        return uBase + uSub;
    }
}


/* AURORA_BSXSLOT_MEMORY_PACK_V1_20260906_SNSA1_CPP
 * Super MMC addresses segments 0-7 in 1 MiB units.  On a slotted SA-1 cart,
 * 0x400000+ is the Memory Pack device; a 1 MiB pack mirrors through segments
 * 4-7.  Low 32 KiB windows select CB/DB/EB/FB only when bit 7 enables bank
 * mode, while C0-FF always use the selected segment. */
Bool SNSA1::BSXMemoryOffset(Uint8 uBank, Uint16 uAddr, Uint32 *pOffset) const
{
    Uint32 uSegment, uWithin, uGroup, uIndex;
    Uint8 uReg;

    if (!pOffset || !m_pOwner || !m_pOwner->HasBSXMemoryPack())
        return FALSE;

    if (uBank >= 0xC0)
    {
        uGroup = (uBank - 0xC0) >> 4;
        uSegment = m_Reg[0x20 + uGroup] & 7;
        uWithin = ((Uint32)(uBank & 0x0F) << 16) | uAddr;
    }
    else if (uAddr >= 0x8000 &&
             (uBank <= 0x3F || (uBank >= 0x80 && uBank <= 0xBF)))
    {
        if (uBank <= 0x1F) { uGroup = 0; uIndex = uBank; }
        else if (uBank <= 0x3F) { uGroup = 1; uIndex = uBank - 0x20; }
        else if (uBank <= 0x9F) { uGroup = 2; uIndex = uBank - 0x80; }
        else { uGroup = 3; uIndex = uBank - 0xA0; }

        uReg = m_Reg[0x20 + uGroup];
        if (!(uReg & 0x80))
            return FALSE;
        uSegment = uReg & 7;
        uWithin = uIndex * 0x8000u + (uAddr & 0x7FFFu);
    }
    else
    {
        return FALSE;
    }

    if (uSegment < 4u)
        return FALSE;

    *pOffset = (((uSegment - 4u) << 20) + uWithin) &
               (SNES_BSX_MEMORY_PACK_BYTES - 1);
    return TRUE;
}

Uint32 SNSA1::RomOffset(Uint8 uBank, Uint16 uAddr) const
{
    Uint32 uSegment;
    Uint32 uWithin;
    Uint32 uGroup;

    if (uBank >= 0xC0)
    {
        uGroup = (uBank - 0xC0) >> 4;
        uSegment = m_Reg[0x20 + uGroup] & 7;
        uWithin = ((Uint32)(uBank & 0x0F) << 16) | uAddr;
        return MirrorRomOffset(uSegment * 0x100000u + uWithin);
    }

    if (uAddr >= 0x8000 &&
        (uBank <= 0x3F || (uBank >= 0x80 && uBank <= 0xBF)))
    {
        Uint32 uIndex;
        if (uBank <= 0x1F) { uGroup = 0; uIndex = uBank; }
        else if (uBank <= 0x3F) { uGroup = 1; uIndex = uBank - 0x20; }
        else if (uBank <= 0x9F) { uGroup = 2; uIndex = uBank - 0x80; }
        else { uGroup = 3; uIndex = uBank - 0xA0; }

        uSegment = (m_Reg[0x20 + uGroup] & 0x80)
            ? (m_Reg[0x20 + uGroup] & 7) : uGroup;
        uWithin = uIndex * 0x8000u + (uAddr & 0x7FFFu);
        return MirrorRomOffset(uSegment * 0x100000u + uWithin);
    }

    return MirrorRomOffset(((Uint32)(uBank & 0x7F) << 15) |
                           (uAddr & 0x7FFFu));
}

void SNSA1::MapRomPage(SNCpuT *pCpu, Uint32 uBus, Bool bMainCpu)
{
    Uint8 uBank = (Uint8)(uBus >> 16);
    Uint16 uAddr = (Uint16)uBus;
    Uint32 uPackOffset;
    if (BSXMemoryOffset(uBank, uAddr, &uPackOffset))
    {
        if (bMainCpu)
            SNCPUSetTrap(pCpu, uBus, 0x2000,
                         SnesSystem::ReadSA1ROM, SnesSystem::WriteSA1ROM);
        else
            SNCPUSetTrap(pCpu, uBus, 0x2000, ReadROMCPU, WriteROMCPU);
        SNCPUSetMemSpeed(pCpu, uBus, 0x2000, SNCPU_CYCLE_FAST);
        return;
    }
    Uint32 uOff = RomOffset(uBank, uAddr);
    Uint32 uEnd = RomOffset(uBank, (Uint16)(uAddr + 0x1FFF));

    if (m_pRom && m_nRomBytes && uEnd == uOff + 0x1FFFu &&
        uOff + 0x2000u <= m_nRomBytes)
    {
        SNCPUSetBank(pCpu, uBus, 0x2000, (Uint8 *)(m_pRom + uOff), FALSE);
    }
    else
    {
        if (bMainCpu)
            SNCPUSetTrap(pCpu, uBus, 0x2000,
                         SnesSystem::ReadSA1ROM, SnesSystem::WriteSA1ROM);
        else
            SNCPUSetTrap(pCpu, uBus, 0x2000, ReadROMCPU, WriteROMCPU);
    }

    /* SA-1 ROM fetches use the fast 10.74 MHz execution domain in this V1;
       BW-RAM separately receives the 2-cycle penalty used by reference emulator. */
    SNCPUSetMemSpeed(pCpu, uBus, 0x2000, SNCPU_CYCLE_FAST);
}

/* AURORA_SA1_PERF_V8_3_2_20260903 */
void SNSA1::MapRomGroup(SNCpuT *pCpu, Uint32 uGroup, Bool bMainCpu)
{
    Uint32 uBank, uAddr;
    Uint32 uLoStart;
    Uint32 uHiStart;

    if (!pCpu || uGroup > 3u)
        return;

    uLoStart = (uGroup < 2u)
        ? (uGroup << 5)
        : (0x80u + ((uGroup - 2u) << 5));
    for (uBank = uLoStart; uBank < uLoStart + 0x20u; ++uBank)
        for (uAddr = 0x8000; uAddr < 0x10000; uAddr += 0x2000)
            MapRomPage(pCpu, (uBank << 16) | uAddr, bMainCpu);

    uHiStart = 0xC0u + (uGroup << 4);
    for (uBank = uHiStart; uBank < uHiStart + 0x10u; ++uBank)
        for (uAddr = 0; uAddr < 0x10000; uAddr += 0x2000)
            MapRomPage(pCpu, (uBank << 16) | uAddr, bMainCpu);
}

void SNSA1::MapRomWindows(SNCpuT *pCpu, Bool bMainCpu)
{
    Uint32 uGroup;
    for (uGroup = 0; uGroup < 4u; ++uGroup)
        MapRomGroup(pCpu, uGroup, bMainCpu);
}

void SNSA1::RebuildFastState()
{
    /* AURORA_SA1_SAFE_PERF_V10_20260910 */
    Uint32 bmaps = (Uint32)m_Reg[0x25];
    Uint32 dmacb = (Uint32)(m_Reg[0x31] & 3u);
    Uint32 dmasize = (Uint32)((m_Reg[0x31] >> 2) & 7u);

    if (dmacb > 2u) dmacb = 2u;
    if (dmasize > 5u) dmasize = 5u;

    m_uMainBWRAMBase =
        WrapBWRAMOffset((Uint32)(m_Reg[0x24] & 0x1F) << 13);
    m_bSA1BWRAMBitmap = (bmaps & 0x80u) ? TRUE : FALSE;
    m_uSA1BWRAMBase = m_bSA1BWRAMBitmap
        ? 0u : WrapBWRAMOffset((bmaps & 0x1Fu) << 13);

    m_uSA1BitmapBaseBytes = (bmaps & 0x7Fu) << 11;
    m_uSA1BitmapPixelBase = (bmaps & 0x7Fu) <<
        ((m_uBitmapFormat == 2) ? 13 : 12);

    m_bBWRAMProtectActive =
        (((m_Reg[0x26] | m_Reg[0x27]) & 0x80u) == 0) ? TRUE : FALSE;
    m_uBWRAMProtectLimit = 0x100u << (m_Reg[0x28] & 0x0F);

    /* A direct native write is equivalent to WriteBWRAMLinear only when
     * no address can be rejected and no external per-write dirty callback
     * is requested. Normal SA-1 SRAM persistence is checksum-discovered. */
    m_bDirectBWRAMWrite =
        (!m_bBWRAMProtectActive && !m_bTrackBWRAMDirty) ? TRUE : FALSE;

    /* CC1/CC2 geometry changes only with $2231-$2236. */
    m_uCCBPP = (Uint8)(8u >> dmacb);
    m_uCCBPL = (8u << dmasize) >> dmacb;
    m_uCCCharMask = (Uint8)((1u << (6u - dmacb)) - 1u);
    m_uCCDmaSize = (Uint8)dmasize;
    m_uCCTileXMask = (Uint8)((1u << dmasize) - 1u);
    m_uCCTileShift = (Uint8)(6u - dmacb);
    m_uCCDSA = (Uint32)m_Reg[0x32] |
               ((Uint32)m_Reg[0x33] << 8) |
               ((Uint32)m_Reg[0x34] << 16);
    m_uCCDDA = (Uint32)m_Reg[0x35] | ((Uint32)m_Reg[0x36] << 8);

    /* Timer mode/targets are hot on every SA-1 scheduler entry but change
     * only through $2210/$2212-$2215. */
    {
        Uint32 scanlines = 262u;
        if (m_pOwner && m_pOwner->m_pRom &&
            m_pOwner->m_pRom->m_eVideoType == SNROM_VIDEO_PAL)
            scanlines = 312u;
        m_uTimerHMax = (m_Reg[0x10] & 0x80u) ? 0x800u : 1364u;
        m_uTimerVMax = (m_Reg[0x10] & 0x80u) ? 0x200u : scanlines;
    }
    m_uTimerHTarget = ((Uint32)m_Reg[0x12] |
                       ((Uint32)m_Reg[0x13] << 8)) << 2;
    m_uTimerVTarget = (Uint32)m_Reg[0x14] |
                      ((Uint32)m_Reg[0x15] << 8);
}

void SNSA1::RefreshBWRAMDirectWrites()
{
    /* AURORA_SA1_SAFE_PERF_V11_20260910
     * V10 made linear BW-RAM pages writable directly when protection is
     * architecturally disabled and no external dirty callback is needed.
     * Keep that fast path, but also keep the 24-bit overflow mirror coherent
     * and extend the same proven-safe write path to the S-CPU's SA-1 BW-RAM
     * mappings.  A page is touched on the S-CPU side only if its preserved
     * write trap still identifies it as an SA-1 BW-RAM descriptor. */
    Uint32 uBank, uAddr;
    Uint8 writable = m_bDirectBWRAMWrite ? 0xFFu : 0u;
    SNCpuT *pMainCpu = NULL;
    Bool bTouchedMain = FALSE;

    if (!m_bActive)
        return;

    /* SA-1 $40-$5F: contiguous linear BW-RAM pages already have pMem. */
    for (uBank = 0x40; uBank <= 0x5F; ++uBank)
    {
        for (uAddr = 0; uAddr < 0x10000u; uAddr += 0x2000u)
        {
            SNCpuBankT *pBank = &m_Cpu.Bank[((uBank << 16) | uAddr) >> SNCPU_BANK_SHIFT];
            pBank->bRAM = pBank->pMem ? writable : 0u;
        }
    }

    /* SA-1 $00-$3F/$80-$BF:$6000: direct only while BMAPS is linear. */
    for (uBank = 0; uBank <= 0x3F; ++uBank)
    {
        Uint32 a = (uBank << 16) | 0x6000u;
        Uint32 b = ((uBank | 0x80u) << 16) | 0x6000u;
        SNCpuBankT *pa = &m_Cpu.Bank[a >> SNCPU_BANK_SHIFT];
        SNCpuBankT *pb = &m_Cpu.Bank[b >> SNCPU_BANK_SHIFT];
        pa->bRAM = (!m_bSA1BWRAMBitmap && pa->pMem) ? writable : 0u;
        pb->bRAM = (!m_bSA1BWRAMBitmap && pb->pMem) ? writable : 0u;
    }

    /* V10 changed bRAM after the map's initial mirror pass.  Re-copy bank
     * $00 descriptors so an effective-address carry into $100:xxxx cannot
     * retain stale direct-write permission or stale trap state. */
    SNCPUMirror24BitBus(&m_Cpu);

    if (m_pOwner)
        pMainCpu = m_pOwner->GetCpu();
    if (!pMainCpu)
        return;

    /* S-CPU $40-$4F linear BW-RAM.  The trap-function identity prevents an
     * Attach-time refresh from ever marking unrelated ROM/RAM descriptors
     * writable before MapMainCPU() has installed the SA-1 map. */
    for (uBank = 0x40; uBank <= 0x4F; ++uBank)
    {
        for (uAddr = 0; uAddr < 0x10000u; uAddr += 0x2000u)
        {
            SNCpuBankT *pBank = &pMainCpu->Bank[((uBank << 16) | uAddr) >> SNCPU_BANK_SHIFT];
            if (pBank->pWriteTrapFunc == SnesSystem::WriteSA1BWRAM)
            {
                pBank->bRAM = pBank->pMem ? writable : 0u;
                bTouchedMain = TRUE;
            }
        }
    }

    /* S-CPU programmable $6000-$7FFF windows.  During CC1, pMem is NULL and
     * writes correctly stay trapped even though ordinary writes are linear. */
    for (uBank = 0; uBank <= 0x3F; ++uBank)
    {
        Uint32 a = (uBank << 16) | 0x6000u;
        Uint32 b = ((uBank | 0x80u) << 16) | 0x6000u;
        SNCpuBankT *pa = &pMainCpu->Bank[a >> SNCPU_BANK_SHIFT];
        SNCpuBankT *pb = &pMainCpu->Bank[b >> SNCPU_BANK_SHIFT];
        if (pa->pWriteTrapFunc == SnesSystem::WriteSA1BWRAM)
        {
            pa->bRAM = pa->pMem ? writable : 0u;
            bTouchedMain = TRUE;
        }
        if (pb->pWriteTrapFunc == SnesSystem::WriteSA1BWRAM)
        {
            pb->bRAM = pb->pMem ? writable : 0u;
            bTouchedMain = TRUE;
        }
    }

    if (bTouchedMain)
        SNCPUMirror24BitBus(pMainCpu);
}

void SNSA1::MapSA1BWRAMWindow()
{
    Uint32 uBank;
    Uint32 uOff = 0;
    Bool bDirect = FALSE;

    if (m_pBWRAM && m_nBWRAMBytes >= 0x2000u && !m_bSA1BWRAMBitmap)
    {
        uOff = m_uSA1BWRAMBase;
        bDirect = (uOff + 0x2000u <= m_nBWRAMBytes) ? TRUE : FALSE;
    }

    for (uBank = 0; uBank <= 0x3F; ++uBank)
    {
        Uint32 a = (uBank << 16) | 0x6000;
        Uint32 b = ((uBank | 0x80) << 16) | 0x6000;
        SNCPUSetTrap(&m_Cpu, a, 0x2000, ReadBWRAMWindowCPU, WriteBWRAMWindowCPU);
        SNCPUSetTrap(&m_Cpu, b, 0x2000, ReadBWRAMWindowCPU, WriteBWRAMWindowCPU);
        SNCPUSetMemSpeed(&m_Cpu, a, 0x2000, 12);
        SNCPUSetMemSpeed(&m_Cpu, b, 0x2000, 12);
        if (bDirect)
        {
            SNCPUSetBank(&m_Cpu, a, 0x2000, m_pBWRAM + uOff, m_bDirectBWRAMWrite);
            SNCPUSetBank(&m_Cpu, b, 0x2000, m_pBWRAM + uOff, m_bDirectBWRAMWrite);
        }
    }

    /* V11: BMAPS remaps bank $00:$6000 too; refresh the overflow mirror. */
    SNCPUMirror24BitBus(&m_Cpu);
}

void SNSA1::MapMainBWRAMWindow(SNCpuT *pMainCpu)
{
    Uint32 uBank;
    Uint32 uOff = 0;
    Bool bDirect = FALSE;

    if (!pMainCpu)
        return;
    if (!m_bCharDMA && m_pBWRAM && m_nBWRAMBytes >= 0x2000u)
    {
        uOff = m_uMainBWRAMBase;
        bDirect = (uOff + 0x2000u <= m_nBWRAMBytes) ? TRUE : FALSE;
    }

    for (uBank = 0; uBank <= 0x3F; ++uBank)
    {
        Uint32 a = (uBank << 16) | 0x6000;
        Uint32 b = ((uBank | 0x80) << 16) | 0x6000;
        SNCPUSetTrap(pMainCpu, a, 0x2000,
                     SnesSystem::ReadSA1BWRAM, SnesSystem::WriteSA1BWRAM);
        SNCPUSetTrap(pMainCpu, b, 0x2000,
                     SnesSystem::ReadSA1BWRAM, SnesSystem::WriteSA1BWRAM);
        SNCPUSetMemSpeed(pMainCpu, a, 0x2000, SNCPU_CYCLE_SLOW);
        SNCPUSetMemSpeed(pMainCpu, b, 0x2000, SNCPU_CYCLE_SLOW);
        if (bDirect)
        {
            SNCPUSetBank(pMainCpu, a, 0x2000, m_pBWRAM + uOff, m_bDirectBWRAMWrite);
            SNCPUSetBank(pMainCpu, b, 0x2000, m_pBWRAM + uOff, m_bDirectBWRAMWrite);
        }
    }

    /* V11: BMAP/CC1 changes bank $00:$6000 at runtime. */
    SNCPUMirror24BitBus(pMainCpu);
}

void SNSA1::MapSA1CPU()
{
    Uint32 uBank, uAddr;
    if (!m_bActive)
        return;

    SNCPUSetTrap(&m_Cpu, 0, SNCPU_MEM_SIZE, ReadCPU, WriteCPU);
    SNCPUSetMemSpeed(&m_Cpu, 0, SNCPU_MEM_SIZE, SNCPU_CYCLE_FAST);
    MapRomWindows(&m_Cpu, FALSE);

    for (uBank = 0; uBank <= 0x3F; ++uBank)
    {
        Uint32 a = uBank << 16;
        Uint32 b = (uBank | 0x80u) << 16;
        SNCPUSetTrap(&m_Cpu, a + 0x0000u, 0x2000, ReadLow0CPU, WriteLow0CPU);
        SNCPUSetTrap(&m_Cpu, a + 0x2000u, 0x2000, ReadLow1CPU, WriteLow1CPU);
        SNCPUSetTrap(&m_Cpu, b + 0x0000u, 0x2000, ReadLow0CPU, WriteLow0CPU);
        SNCPUSetTrap(&m_Cpu, b + 0x2000u, 0x2000, ReadLow1CPU, WriteLow1CPU);
    }

    for (uBank = 0x40; uBank <= 0x5F; ++uBank)
    {
        SNCPUSetTrap(&m_Cpu, uBank << 16, 0x10000,
                     ReadBWRAMLinearCPU, WriteBWRAMLinearCPU);
        SNCPUSetMemSpeed(&m_Cpu, uBank << 16, 0x10000, 12);
    }
    for (uBank = 0x60; uBank <= 0x7F; ++uBank)
    {
        SNCPUSetTrap(&m_Cpu, uBank << 16, 0x10000,
                     ReadBitmapCPU, WriteBitmapCPU);
        SNCPUSetMemSpeed(&m_Cpu, uBank << 16, 0x10000, 12);
    }
    MapSA1BWRAMWindow();

    /* Direct read pages retain the narrow write trap installed above. */
    if (m_pBWRAM && m_nBWRAMBytes >= 0x2000u)
    {
        for (uBank = 0x40; uBank <= 0x5F; ++uBank)
        {
            for (uAddr = 0; uAddr < 0x10000; uAddr += 0x2000)
            {
                Uint32 bus = (uBank << 16) | uAddr;
                Uint32 off = WrapBWRAMOffset((((Uint32)uBank & 3u) << 16) | uAddr);
                if (off + 0x2000u <= m_nBWRAMBytes)
                    SNCPUSetBank(&m_Cpu, bus, 0x2000, m_pBWRAM + off, m_bDirectBWRAMWrite);
            }
        }
    }

    SNCPUMirror24BitBus(&m_Cpu);
}

void SNSA1::MapMainCPU(SNCpuT *pMainCpu)
{
    Uint32 uBank, uAddr;
    if (!m_bActive || !pMainCpu)
        return;

    if (m_bMapMainRom)
        MapRomWindows(pMainCpu, TRUE);

    /* V8.3.2: direct reads when BMAP is linear and CC1 is idle;
     * MapMainBWRAMWindow restores traps immediately for character DMA. */
    MapMainBWRAMWindow(pMainCpu);

    /* Linear BW-RAM $40-$4F. Keep writes trapped for protection/dirty state. */
    for (uBank = 0x40; uBank <= 0x4F; ++uBank)
    {
        for (uAddr = 0; uAddr < 0x10000; uAddr += 0x2000)
        {
            Uint32 bus = (uBank << 16) | uAddr;
            Uint32 off = ((uBank & 3u) << 16) | uAddr;
            Uint32 wrapped = WrapBWRAMOffset(off); /* AURORA_V6_RUNTIME_EFFECT_ALL5_20260908 */
            SNCPUSetTrap(pMainCpu, bus, 0x2000,
                         SnesSystem::ReadSA1BWRAM, SnesSystem::WriteSA1BWRAM);
            if (m_pBWRAM && m_nBWRAMBytes >= 0x2000 &&
                wrapped + 0x2000 <= m_nBWRAMBytes)
                SNCPUSetBank(pMainCpu, bus, 0x2000,
                             m_pBWRAM + wrapped, m_bDirectBWRAMWrite);
            SNCPUSetMemSpeed(pMainCpu, bus, 0x2000, SNCPU_CYCLE_SLOW);
        }
    }

    SNCPUMirror24BitBus(pMainCpu);
}

Uint32 SNSA1::MainBWRAMOffset(Uint32 uAddr, Bool *pOK) const
{
    Uint8 bank = (Uint8)(uAddr >> 16);
    Uint16 addr = (Uint16)uAddr;
    if (pOK) *pOK = FALSE;
    if (!m_nBWRAMBytes)
        return 0;

    if ((bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) &&
        addr >= 0x6000 && addr <= 0x7FFF)
    {
        if (pOK) *pOK = TRUE;
        return WrapBWRAMOffset(m_uMainBWRAMBase + (addr - 0x6000u));
    }
    if (bank >= 0x40 && bank <= 0x4F)
    {
        if (pOK) *pOK = TRUE;
        return WrapBWRAMOffset(((Uint32)(bank & 3) << 16) | addr);
    }
    return 0;
}

Uint32 SNSA1::SA1BWRAMOffset(Uint32 uAddr, Bool *pOK, Bool *pBitmap) const
{
    Uint8 bank = (Uint8)(uAddr >> 16);
    Uint16 addr = (Uint16)uAddr;
    if (pOK) *pOK = FALSE;
    if (pBitmap) *pBitmap = FALSE;
    if (!m_nBWRAMBytes)
        return 0;

    if ((bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF)) &&
        addr >= 0x6000 && addr <= 0x7FFF)
    {
        Uint32 local = addr - 0x6000u;
        if (m_bSA1BWRAMBitmap)
        {
            if (pOK) *pOK = TRUE;
            if (pBitmap) *pBitmap = TRUE;
            return m_uSA1BitmapBaseBytes + local;
        }
        if (pOK) *pOK = TRUE;
        return WrapBWRAMOffset(m_uSA1BWRAMBase + local);
    }

    if (bank >= 0x40 && bank <= 0x5F)
    {
        if (pOK) *pOK = TRUE;
        return WrapBWRAMOffset((((Uint32)bank & 3u) << 16) | addr);
    }
    if (bank >= 0x60 && bank <= 0x7F)
    {
        if (pOK) *pOK = TRUE;
        if (pBitmap) *pBitmap = TRUE;
        return (uAddr & 0x1FFFFFu) - 0x600000u;
    }
    return 0;
}

Uint8 SNSA1::ReadBWRAMLinear(Uint32 uOffset, Uint8 uOpenBus) const
{
    if (!m_pBWRAM || !m_nBWRAMBytes)
        return uOpenBus;
    return m_pBWRAM[WrapBWRAMOffset(uOffset)];
}

Bool SNSA1::BWRAMWriteProtected(Uint32 uOffset) const
{
    if (!m_bBWRAMProtectActive)
        return FALSE;
    return ((uOffset & 0x3FFFFu) < m_uBWRAMProtectLimit) ? TRUE : FALSE;
}

void SNSA1::MarkBWRAMDirty()
{
    /* AURORA_SA1_PERF_V8_3_2_20260903
     * Normal SA-1 .srm persistence is discovered by the existing menu
     * checksum/flush path, so it does not need the copier external-cart
     * dirty callback on every byte write. SWC attach paths opt in. */
    if (m_bTrackBWRAMDirty && m_pOwner)
        m_pOwner->MarkSA1BWRAMDirty();
}

void SNSA1::WriteBWRAMLinear(Uint32 uOffset, Uint8 uData)
{
    if (!m_pBWRAM || !m_nBWRAMBytes)
        return;
    uOffset = WrapBWRAMOffset(uOffset);
    if (BWRAMWriteProtected(uOffset))
        return;
    m_pBWRAM[uOffset] = uData;
    MarkBWRAMDirty();
}

Uint8 SNSA1::ReadBitmap(Uint32 uPixelAddr, Uint8 uOpenBus) const
{
    Uint32 byteOff, shift;
    if (!m_pBWRAM || !m_nBWRAMBytes)
        return uOpenBus;
    if (m_uBitmapFormat == 2)
    {
        byteOff = WrapBWRAMOffset(uPixelAddr >> 2);
        shift = (uPixelAddr & 3) << 1;
        return (m_pBWRAM[byteOff] >> shift) & 3;
    }
    byteOff = WrapBWRAMOffset(uPixelAddr >> 1);
    shift = (uPixelAddr & 1) << 2;
    return (m_pBWRAM[byteOff] >> shift) & 15;
}

void SNSA1::WriteBitmap(Uint32 uPixelAddr, Uint8 uData)
{
    Uint32 byteOff, shift;
    Uint8 mask;
    if (!m_pBWRAM || !m_nBWRAMBytes)
        return;
    if (m_uBitmapFormat == 2)
    {
        byteOff = WrapBWRAMOffset(uPixelAddr >> 2);
        shift = (uPixelAddr & 3) << 1;
        mask = (Uint8)(3u << shift);
        m_pBWRAM[byteOff] = (Uint8)((m_pBWRAM[byteOff] & ~mask) |
                              ((uData & 3u) << shift));
    }
    else
    {
        byteOff = WrapBWRAMOffset(uPixelAddr >> 1);
        shift = (uPixelAddr & 1) << 2;
        mask = (Uint8)(15u << shift);
        m_pBWRAM[byteOff] = (Uint8)((m_pBWRAM[byteOff] & ~mask) |
                              ((uData & 15u) << shift));
    }
    /* reference emulator/ares protection applies to linear BW-RAM, not bitmap writes. */
    MarkBWRAMDirty();
}

Bool SNSA1::IRAMWriteAllowed(Bool bSA1, Uint32 uOffset) const
{
    Uint8 reg = m_Reg[bSA1 ? 0x2A : 0x29];
    Uint8 bit = (Uint8)(1u << ((uOffset & 0x7FFu) >> 8));
    return (reg & bit) ? TRUE : FALSE;
}

Uint8 SNSA1::ReadMainIRAM(Uint16 uAddr, Uint8 uOpenBus) const
{
    if (!m_bActive || uAddr < 0x3000 || uAddr > 0x37FF)
        return uOpenBus;
    return m_IRAM[uAddr & 0x7FF];
}

void SNSA1::WriteMainIRAM(Uint16 uAddr, Uint8 uData)
{
    Uint32 off;
    if (!m_bActive || uAddr < 0x3000 || uAddr > 0x37FF)
        return;
    off = uAddr & 0x7FF;
    if (IRAMWriteAllowed(FALSE, off))
        m_IRAM[off] = uData;
}

Uint8 SNSA1::ReadCC1(Uint32 bwoffset)
{
    Uint32 tile, ty, tx, bwaddr;
    Uint32 y;
    Uint32 bpp = (Uint32)m_uCCBPP;
    Uint32 charmask = (Uint32)m_uCCCharMask;

    if ((bwoffset & charmask) == 0 && m_nBWRAMBytes)
    {
        tile = WrapBWRAMOffset(bwoffset - m_uCCDSA) >> m_uCCTileShift;
        ty = tile >> m_uCCDmaSize;
        tx = tile & m_uCCTileXMask;
        bwaddr = m_uCCDSA + ty * 8u * m_uCCBPL + tx * bpp;

        for (y = 0; y < 8; ++y)
        {
            unsigned long long bits = 0;
            unsigned long long pixels, planes;
            Uint32 rowOff = WrapBWRAMOffset(bwaddr);

            if (m_pBWRAM && rowOff + bpp <= m_nBWRAMBytes)
            {
                const Uint8 *q = m_pBWRAM + rowOff;
                bits = (unsigned long long)q[0] | ((unsigned long long)q[1] << 8);
                if (bpp >= 4)
                    bits |= ((unsigned long long)q[2] << 16) | ((unsigned long long)q[3] << 24);
                if (bpp >= 8)
                    bits |= ((unsigned long long)q[4] << 32) | ((unsigned long long)q[5] << 40) |
                            ((unsigned long long)q[6] << 48) | ((unsigned long long)q[7] << 56);
            }
            else
            {
                Uint32 byte;
                for (byte = 0; byte < bpp; ++byte)
                    bits |= (unsigned long long)ReadBWRAMLinear(bwaddr + byte, 0) << (byte << 3);
            }

            bwaddr += m_uCCBPL;
            pixels = _SA1ExpandPackedPixels(bits, bpp);
            planes = _SA1Transpose8x8ToPlanes(pixels);
            {
                Uint32 p = m_uCCDDA + (y << 1);
                m_IRAM[(p + 0) & 0x7FF] = (Uint8)(planes >> 0);
                m_IRAM[(p + 1) & 0x7FF] = (Uint8)(planes >> 8);
                if (bpp >= 4)
                {
                    m_IRAM[(p + 16) & 0x7FF] = (Uint8)(planes >> 16);
                    m_IRAM[(p + 17) & 0x7FF] = (Uint8)(planes >> 24);
                }
                if (bpp >= 8)
                {
                    m_IRAM[(p + 32) & 0x7FF] = (Uint8)(planes >> 32);
                    m_IRAM[(p + 33) & 0x7FF] = (Uint8)(planes >> 40);
                    m_IRAM[(p + 48) & 0x7FF] = (Uint8)(planes >> 48);
                    m_IRAM[(p + 49) & 0x7FF] = (Uint8)(planes >> 56);
                }
            }
        }
    }
    return m_IRAM[(m_uCCDDA + (bwoffset & charmask)) & 0x7FF];
}

Uint8 SNSA1::ReadMainBWRAM(Uint32 uAddr, Uint8 uOpenBus)
{
    Bool ok = FALSE;
    Uint32 off = MainBWRAMOffset(uAddr, &ok);
    Uint16 a = (Uint16)uAddr;
    if (!ok)
        return uOpenBus;
    if (m_bCharDMA && a >= 0x6000 && a <= 0x7FFF)
        return ReadCC1(off);
    return ReadBWRAMLinear(off, uOpenBus);
}

void SNSA1::WriteMainBWRAM(Uint32 uAddr, Uint8 uData)
{
    Bool ok = FALSE;
    Uint32 off = MainBWRAMOffset(uAddr, &ok);
    if (ok)
        WriteBWRAMLinear(off, uData);
}

Uint8 SNSA1::ReadMainROM(Uint32 uAddr, Uint8 uOpenBus) const
{
    Uint32 uPackOffset;
    if (!m_bActive)
        return uOpenBus;
    if (BSXMemoryOffset((Uint8)(uAddr >> 16), (Uint16)uAddr, &uPackOffset))
        return m_pOwner->ReadBSXMemoryPack(uPackOffset);
    if (!m_pRom || !m_nRomBytes)
        return uOpenBus;
    return m_pRom[RomOffset((Uint8)(uAddr >> 16), (Uint16)uAddr)];
}

void SNSA1::WriteMainROM(Uint32 uAddr, Uint8 uData)
{
    Uint32 uPackOffset;
    if (m_bActive && m_pOwner &&
        BSXMemoryOffset((Uint8)(uAddr >> 16), (Uint16)uAddr, &uPackOffset))
        m_pOwner->WriteBSXMemoryPack(uPackOffset, uData);
}

Uint8 SNCPU_TRAPFUNC SNSA1::ReadCPU(SNCpuT *pCpu, Uint32 uAddr)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    Uint8 v = p ? p->ReadBus(uAddr, pCpu->uMDR) : pCpu->uMDR;
    pCpu->uMDR = v;
    return v;
}

void SNCPU_TRAPFUNC SNSA1::WriteCPU(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    pCpu->uMDR = uData;
    if (p) p->WriteBus(uAddr, uData);
}

Uint8 SNCPU_TRAPFUNC SNSA1::ReadLow0CPU(SNCpuT *pCpu, Uint32 uAddr)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    Uint16 a = (Uint16)uAddr;
    Uint8 v = pCpu->uMDR;
    if (p && a <= 0x07FFu) v = p->m_IRAM[a & 0x07FFu];
    pCpu->uMDR = v;
    return v;
}

void SNCPU_TRAPFUNC SNSA1::WriteLow0CPU(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    Uint16 a = (Uint16)uAddr;
    pCpu->uMDR = uData;
    if (p && a <= 0x07FFu &&
        (p->m_Reg[0x2A] & (Uint8)(1u << ((a & 0x07FFu) >> 8))))
        p->m_IRAM[a & 0x07FFu] = uData;
}

Uint8 SNCPU_TRAPFUNC SNSA1::ReadLow1CPU(SNCpuT *pCpu, Uint32 uAddr)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    Uint16 a = (Uint16)uAddr;
    Uint8 v = pCpu->uMDR;
    if (p)
    {
        if (a >= 0x2301u && a <= 0x230Du) v = p->ReadRegister(a, v);
        else if (a >= 0x3000u && a <= 0x37FFu) v = p->m_IRAM[a & 0x07FFu];
    }
    pCpu->uMDR = v;
    return v;
}

void SNCPU_TRAPFUNC SNSA1::WriteLow1CPU(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    Uint16 a = (Uint16)uAddr;
    pCpu->uMDR = uData;
    if (!p) return;
    if (a >= 0x2200u && a <= 0x23FFu)
    {
        if (SA1CanWriteRegister(a)) p->WriteRegister(a, uData);
        return;
    }
    if (a >= 0x3000u && a <= 0x37FFu)
    {
        Uint32 off = a & 0x07FFu;
        if (p->m_Reg[0x2A] & (Uint8)(1u << (off >> 8))) p->m_IRAM[off] = uData;
    }
}

Uint8 SNCPU_TRAPFUNC SNSA1::ReadBWRAMWindowCPU(SNCpuT *pCpu, Uint32 uAddr)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    Uint8 v = pCpu->uMDR;
    if (p)
    {
        Uint32 local = (Uint32)((Uint16)uAddr - 0x6000u);
        v = p->m_bSA1BWRAMBitmap
            ? p->ReadBitmap(p->m_uSA1BitmapPixelBase + local, v)
            : p->ReadBWRAMLinear(p->m_uSA1BWRAMBase + local, v);
    }
    pCpu->uMDR = v;
    return v;
}

void SNCPU_TRAPFUNC SNSA1::WriteBWRAMWindowCPU(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    pCpu->uMDR = uData;
    if (!p) return;
    Uint32 local = (Uint32)((Uint16)uAddr - 0x6000u);
    if (p->m_bSA1BWRAMBitmap) p->WriteBitmap(p->m_uSA1BitmapPixelBase + local, uData);
    else p->WriteBWRAMLinear(p->m_uSA1BWRAMBase + local, uData);
}

Uint8 SNCPU_TRAPFUNC SNSA1::ReadBWRAMLinearCPU(SNCpuT *pCpu, Uint32 uAddr)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    Uint8 v = pCpu->uMDR;
    if (p) v = p->ReadBWRAMLinear((((uAddr >> 16) & 3u) << 16) | (uAddr & 0xFFFFu), v);
    pCpu->uMDR = v;
    return v;
}

void SNCPU_TRAPFUNC SNSA1::WriteBWRAMLinearCPU(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    pCpu->uMDR = uData;
    if (p) p->WriteBWRAMLinear((((uAddr >> 16) & 3u) << 16) | (uAddr & 0xFFFFu), uData);
}

Uint8 SNCPU_TRAPFUNC SNSA1::ReadBitmapCPU(SNCpuT *pCpu, Uint32 uAddr)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    Uint8 v = pCpu->uMDR;
    if (p) v = p->ReadBitmap((uAddr & 0x1FFFFFu) - 0x600000u, v);
    pCpu->uMDR = v;
    return v;
}

void SNCPU_TRAPFUNC SNSA1::WriteBitmapCPU(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    pCpu->uMDR = uData;
    if (p) p->WriteBitmap((uAddr & 0x1FFFFFu) - 0x600000u, uData);
}

Uint8 SNCPU_TRAPFUNC SNSA1::ReadROMCPU(SNCpuT *pCpu, Uint32 uAddr)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    Uint8 v = p ? p->ReadMainROM(uAddr, pCpu->uMDR) : pCpu->uMDR;
    pCpu->uMDR = v;
    return v;
}

void SNCPU_TRAPFUNC SNSA1::WriteROMCPU(SNCpuT *pCpu, Uint32 uAddr, Uint8 uData)
{
    SNSA1 *p = (SNSA1 *)pCpu->pUserData;
    pCpu->uMDR = uData;
    if (p) p->WriteMainROM(uAddr, uData);
}

Uint8 SNSA1::ReadBus(Uint32 uAddr, Uint8 uOpenBus)
{
    Uint8 bank = (Uint8)(uAddr >> 16);
    Uint16 addr = (Uint16)uAddr;
    Bool ok = FALSE, bitmap = FALSE;
    Uint32 off;

    if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF))
    {
        if (addr <= 0x07FF) return m_IRAM[addr & 0x7FF];
        if (addr >= 0x2200 && addr <= 0x23FF)
        {
            if (addr >= 0x2301 && addr <= 0x230D)
                return ReadRegister(addr, uOpenBus);
            return uOpenBus;
        }
        if (addr >= 0x3000 && addr <= 0x37FF) return m_IRAM[addr & 0x7FF];
        if (addr >= 0x6000 && addr <= 0x7FFF)
        {
            off = SA1BWRAMOffset(uAddr, &ok, &bitmap);
            if (!ok) return uOpenBus;
            if (bitmap)
            {
                Uint32 pixel =
                    (off - m_uSA1BitmapBaseBytes) + m_uSA1BitmapPixelBase;
                return ReadBitmap(pixel, uOpenBus);
            }
            return ReadBWRAMLinear(off, uOpenBus);
        }
        if (addr >= 0x8000)
        {
            Uint32 uPackOffset;
            if (BSXMemoryOffset(bank, addr, &uPackOffset))
                return m_pOwner->ReadBSXMemoryPack(uPackOffset);
            if (m_pRom)
                return m_pRom[RomOffset(bank, addr)];
        }
        return uOpenBus;
    }

    if (bank >= 0x40 && bank <= 0x5F)
    {
        off = SA1BWRAMOffset(uAddr, &ok, &bitmap);
        return ok ? ReadBWRAMLinear(off, uOpenBus) : uOpenBus;
    }
    if (bank >= 0x60 && bank <= 0x7F)
    {
        off = SA1BWRAMOffset(uAddr, &ok, &bitmap);
        return ok ? ReadBitmap(off, uOpenBus) : uOpenBus;
    }
    if (bank >= 0xC0)
    {
        Uint32 uPackOffset;
        if (BSXMemoryOffset(bank, addr, &uPackOffset))
            return m_pOwner->ReadBSXMemoryPack(uPackOffset);
        if (m_pRom)
            return m_pRom[RomOffset(bank, addr)];
    }
    return uOpenBus;
}

void SNSA1::WriteBus(Uint32 uAddr, Uint8 uData)
{
    Uint8 bank = (Uint8)(uAddr >> 16);
    Uint16 addr = (Uint16)uAddr;
    Bool ok = FALSE, bitmap = FALSE;
    Uint32 off;

    if (bank <= 0x3F || (bank >= 0x80 && bank <= 0xBF))
    {
        if (addr <= 0x07FF)
        {
            if (IRAMWriteAllowed(TRUE, addr)) m_IRAM[addr & 0x7FF] = uData;
            return;
        }
        if (addr >= 0x2200 && addr <= 0x23FF)
        {
            if (SA1CanWriteRegister(addr)) WriteRegister(addr, uData);
            return;
        }
        if (addr >= 0x3000 && addr <= 0x37FF)
        {
            off = addr & 0x7FF;
            if (IRAMWriteAllowed(TRUE, off)) m_IRAM[off] = uData;
            return;
        }
        if (addr >= 0x6000 && addr <= 0x7FFF)
        {
            off = SA1BWRAMOffset(uAddr, &ok, &bitmap);
            if (!ok) return;
            if (bitmap)
            {
                Uint32 pixel =
                    (off - m_uSA1BitmapBaseBytes) + m_uSA1BitmapPixelBase;
                WriteBitmap(pixel, uData);
            }
            else WriteBWRAMLinear(off, uData);
            return;
        }
        if (addr >= 0x8000)
        {
            Uint32 uPackOffset;
            if (BSXMemoryOffset(bank, addr, &uPackOffset) && m_pOwner)
                m_pOwner->WriteBSXMemoryPack(uPackOffset, uData);
        }
        return;
    }

    if (bank >= 0x40 && bank <= 0x5F)
    {
        off = SA1BWRAMOffset(uAddr, &ok, &bitmap);
        if (ok) WriteBWRAMLinear(off, uData);
        return;
    }
    if (bank >= 0x60 && bank <= 0x7F)
    {
        off = SA1BWRAMOffset(uAddr, &ok, &bitmap);
        if (ok) WriteBitmap(off, uData);
        return;
    }
    if (bank >= 0xC0)
    {
        Uint32 uPackOffset;
        if (BSXMemoryOffset(bank, addr, &uPackOffset) && m_pOwner)
            m_pOwner->WriteBSXMemoryPack(uPackOffset, uData);
    }
}

/* AURORA_SA1_ACCURACY_REVIEW_V2_20260902
 * Real SA-1 I/O is asymmetric. S-CPU and C-CPU own different write ports;
 * only $2231-$2237 are shared. Keeping this in two predicates prevents a
 * write from the wrong CPU from changing live SA-1 state. */
Bool SNSA1::MainCanWriteRegister(Uint16 a)
{
    return ((a >= 0x2200 && a <= 0x2208) ||
            (a >= 0x2220 && a <= 0x2224) ||
            a == 0x2226 || a == 0x2228 || a == 0x2229 ||
            (a >= 0x2231 && a <= 0x2237)) ? TRUE : FALSE;
}

Bool SNSA1::SA1CanWriteRegister(Uint16 a)
{
    return ((a >= 0x2209 && a <= 0x2215) ||
            a == 0x2225 || a == 0x2227 || a == 0x222A ||
            (a >= 0x2230 && a <= 0x2239) || a == 0x223F ||
            (a >= 0x2240 && a <= 0x224F) ||
            (a >= 0x2250 && a <= 0x2254) ||
            (a >= 0x2258 && a <= 0x225B)) ? TRUE : FALSE;
}

Uint8 SNSA1::ReadMainRegister(Uint16 uAddr, Uint8 uOpenBus)
{
    if (!m_bActive)
        return uOpenBus;
    if (uAddr == 0x2300)
        return ReadRegister(uAddr, uOpenBus);
    return uOpenBus;
}

void SNSA1::WriteMainRegister(Uint16 uAddr, Uint8 uData)
{
    if (m_bActive && MainCanWriteRegister(uAddr))
        WriteRegister(uAddr, uData);
}

Uint8 SNSA1::ReadRegister(Uint16 uAddr, Uint8 uOpenBus)
{
    Uint16 i;
    if (uAddr < 0x2200 || uAddr > 0x23FF)
        return uOpenBus;
    i = (Uint16)(uAddr - 0x2200);

    switch (uAddr)
    {
        case 0x2300:
            return (Uint8)((m_Reg[0x09] & 0x5F) | (m_Reg[0x100] & 0xA0));
        case 0x2301:
            return (Uint8)((m_Reg[0x00] & 0x0F) | (m_Reg[0x101] & 0xF0));
        case 0x2302:
            m_uLatchedHCounter = (Uint16)(m_uHCounter / 4u);
            m_uLatchedVCounter = (Uint16)m_uVCounter;
            return (Uint8)m_uLatchedHCounter;
        case 0x2303: return (Uint8)(m_uLatchedHCounter >> 8);
        case 0x2304: return (Uint8)m_uLatchedVCounter;
        case 0x2305: return (Uint8)(m_uLatchedVCounter >> 8);
        case 0x2306: return (Uint8)(m_uSum >> 0);
        case 0x2307: return (Uint8)(m_uSum >> 8);
        case 0x2308: return (Uint8)(m_uSum >> 16);
        case 0x2309: return (Uint8)(m_uSum >> 24);
        case 0x230A: return (Uint8)(m_uSum >> 32);
        case 0x230B: return m_bArithmeticOverflow ? 0x80 : 0;
        case 0x230C: return m_Reg[0x10C];
        case 0x230D:
        {
            Uint8 v = m_Reg[0x10D];
            if (m_Reg[0x58] & 0x80) ReadVariableLength(TRUE, FALSE);
            return v;
        }
        case 0x230E:
            return uOpenBus; /* real carts: version register is not decoded */
        default:
            return m_Reg[i];
    }
}

void SNSA1::DoArithmetic()
{
    switch (m_uArithmeticOp)
    {
        case 0:
            m_uSum = (unsigned long long)(Int32)((Int16)m_uOp1 * (Int16)m_uOp2);
            m_uOp2 = 0;
            break;
        case 1:
        {
            Int16 dividend = (Int16)m_uOp1;
            Uint16 divisor = m_uOp2;
            if (!divisor)
            {
                Uint16 remainder = (dividend < 0)
                    ? (Uint16)(-(Int32)dividend) : (Uint16)dividend;
                Uint16 quotient = (dividend < 0) ? 0x0001u : 0xFFFFu;
                m_uSum = ((Uint32)remainder << 16) | quotient;
            }
            else
            {
                Uint32 ext = (Uint32)((Int32)dividend + (Uint32)divisor * 65536u);
                Uint16 rem = (Uint16)(ext % divisor);
                Uint16 quo = (Uint16)(ext / divisor);
                m_uSum = ((Uint32)rem << 16) | quo;
            }
            m_uOp1 = m_uOp2 = 0;
            break;
        }
        default:
        case 2:
            m_uSum += (unsigned long long)((Int16)m_uOp1 * (Int16)m_uOp2);
            m_bArithmeticOverflow = (m_uSum >= (1ULL << 40)) ? TRUE : FALSE;
            m_uSum &= ((1ULL << 40) - 1ULL);
            m_uOp2 = 0;
            break;
    }
}

void SNSA1::ReadVariableLength(Bool bInc, Bool bNoShift)
{
    Uint32 addr = (Uint32)m_Reg[0x59] | ((Uint32)m_Reg[0x5A] << 8) |
                  ((Uint32)m_Reg[0x5B] << 16);
    Uint8 shift = m_Reg[0x58] & 15;
    Uint8 s;
    Uint32 data = 0;
    Bool bDirect = FALSE;

    if (bNoShift) shift = 0;
    else if (!shift) shift = 16;
    s = (Uint8)(shift + m_uVariableBitPos);
    if (s >= 16)
    {
        addr += (Uint32)(s >> 4) << 1;
        s &= 15;
    }

    {
        Uint32 a = addr & 0xFFFFFFu;
        Uint32 pageOff = a & SNCPU_BANK_MASK;
        Uint32 bankOff = a & 0xFFFFu;
        if (pageOff <= (SNCPU_BANK_MASK - 3u) && bankOff <= 0xFFFCu)
        {
            SNCpuBankT *pBank = &m_Cpu.Bank[a >> SNCPU_BANK_SHIFT];
            if (pBank->pMem)
            {
                const Uint8 *q = pBank->pMem + a;
                data = (Uint32)q[0] | ((Uint32)q[1] << 8) |
                       ((Uint32)q[2] << 16) | ((Uint32)q[3] << 24);
                bDirect = TRUE;
            }
        }
    }

    if (!bDirect)
    {
        data = (Uint32)ReadBus(addr, 0) |
               ((Uint32)ReadBus((addr & 0xFF0000u) | ((addr + 1) & 0xFFFFu), 0) << 8) |
               ((Uint32)ReadBus((addr + 2) & 0xFFFFFFu, 0) << 16) |
               ((Uint32)ReadBus((addr + 3) & 0xFFFFFFu, 0) << 24);
    }
    data >>= s;
    m_Reg[0x10C] = (Uint8)data;
    m_Reg[0x10D] = (Uint8)(data >> 8);

    if (bInc)
    {
        m_uVariableBitPos = (Uint8)((m_uVariableBitPos + shift) & 15);
        m_Reg[0x59] = (Uint8)addr;
        m_Reg[0x5A] = (Uint8)(addr >> 8);
        m_Reg[0x5B] = (Uint8)(addr >> 16);
    }
}

void SNSA1::DoCC2()
{
    /* AURORA_DSP_SA1_FX_CX4_CPU_MEGA_ACCURACY_V6_20260916
     * Hardware Type-2 CC commits one row at a time. m_uCharIndex is the
     * 4-bit DMA line counter; bit0 selects BRF0-7 vs BRF8-15. */
    const Uint8 *brf = &m_Reg[0x40 + ((m_uCharIndex & 1u) << 3)];
    Uint32 bpp = (Uint32)m_uCCBPP;
    Uint32 address = m_uCCDDA & 0x07FFu;
    Uint32 alignMask = ((Uint32)m_uCCCharMask << 1) | 1u;
    Uint32 byte;
    unsigned long long planes;

    address &= ~alignMask;
    address += (Uint32)(m_uCharIndex & 8u) * bpp;
    address += (Uint32)(m_uCharIndex & 7u) * 2u;

    planes = _SA1Transpose8x8ToPlanes(_SA1Pack8Pixels(brf));
    for (byte = 0; byte < bpp; ++byte)
    {
        Uint32 q = address + ((byte & 6u) << 3) + (byte & 1u);
        m_IRAM[q & 0x07FFu] = (Uint8)(planes >> (byte << 3));
    }

    m_uCharIndex = (Uint8)((m_uCharIndex + 1u) & 15u);
}

void SNSA1::DoDMA()
{
    Uint32 src = (Uint32)m_Reg[0x32] | ((Uint32)m_Reg[0x33] << 8) |
                 ((Uint32)m_Reg[0x34] << 16);
    Uint32 dst = (Uint32)m_Reg[0x35] | ((Uint32)m_Reg[0x36] << 8) |
                 ((Uint32)m_Reg[0x37] << 16);
    Uint32 len = (Uint32)m_Reg[0x38] | ((Uint32)m_Reg[0x39] << 8);
    Uint32 n = 0;
    Uint8 uSrcType = (Uint8)(m_Reg[0x30] & 3);
    Bool bDstBWRAM = (m_Reg[0x30] & 4) ? TRUE : FALSE;
    Bool bBulkDone = FALSE;

    if (len && uSrcType == 0 && (!bDstBWRAM || (m_pBWRAM && m_nBWRAMBytes)))
    {
        while (n < len)
        {
            Uint32 sAddr = (src + n) & 0xFFFFFFu;
            Uint8 sBank = (Uint8)(sAddr >> 16);
            Uint16 sOff16 = (Uint16)sAddr;
            SNCpuBankT *pBank = &m_Cpu.Bank[sAddr >> SNCPU_BANK_SHIFT];
            const Uint8 *pSrc = pBank->pMem ? pBank->pMem + sAddr : NULL;
            Bool bSourceBWRAM =
                ((sBank >= 0x40 && sBank <= 0x5F) ||
                 ((sBank <= 0x3F || (sBank >= 0x80 && sBank <= 0xBF)) &&
                  sOff16 >= 0x6000 && sOff16 <= 0x7FFF && !m_bSA1BWRAMBitmap))
                ? TRUE : FALSE;

            if (pSrc && !(bDstBWRAM && bSourceBWRAM))
            {
                Uint32 chunk = len - n;
                Uint32 pageBytes = SNCPU_BANK_SIZE - (sAddr & SNCPU_BANK_MASK);
                if (chunk > pageBytes) chunk = pageBytes;
                if (bDstBWRAM)
                {
                    Uint32 dOff = WrapBWRAMOffset(dst + n);
                    Uint32 dBytes = m_nBWRAMBytes - dOff;
                    if (chunk > dBytes) chunk = dBytes;
                    memcpy(m_pBWRAM + dOff, pSrc, chunk);
                }
                else
                {
                    Uint32 dOff = (dst + n) & 0x7FFu;
                    Uint32 dBytes = 0x800u - dOff;
                    if (chunk > dBytes) chunk = dBytes;
                    memcpy(m_IRAM + dOff, pSrc, chunk);
                }
                n += chunk;
                continue;
            }

            Uint8 v = ReadBus(sAddr, 0);
            if (bDstBWRAM) m_pBWRAM[WrapBWRAMOffset(dst + n)] = v;
            else m_IRAM[(dst + n) & 0x7FFu] = v;
            ++n;
        }
        bBulkDone = TRUE;
    }

    if (!bBulkDone && len && m_pBWRAM && m_nBWRAMBytes)
    {
        if (uSrcType == 1 && !bDstBWRAM)
        {
            while (n < len)
            {
                Uint32 sOff = WrapBWRAMOffset(src + n);
                Uint32 dOff = (dst + n) & 0x7FFu;
                Uint32 chunk = len - n;
                Uint32 a = m_nBWRAMBytes - sOff;
                Uint32 b = 0x800u - dOff;
                if (chunk > a) chunk = a;
                if (chunk > b) chunk = b;
                memcpy(m_IRAM + dOff, m_pBWRAM + sOff, chunk);
                n += chunk;
            }
            bBulkDone = TRUE;
        }
        else if ((uSrcType == 2 || uSrcType == 3) && bDstBWRAM)
        {
            while (n < len)
            {
                Uint32 sOff = (src + n) & 0x7FFu;
                Uint32 dOff = WrapBWRAMOffset(dst + n);
                Uint32 chunk = len - n;
                Uint32 a = 0x800u - sOff;
                Uint32 b = m_nBWRAMBytes - dOff;
                if (chunk > a) chunk = a;
                if (chunk > b) chunk = b;
                memcpy(m_pBWRAM + dOff, m_IRAM + sOff, chunk);
                n += chunk;
            }
            bBulkDone = TRUE;
        }
        else if (uSrcType == 1 && bDstBWRAM && len >= 16u)
        {
            /* AURORA_SA1_V11_SAME_SPACE_DMA
             * Preserve the old byte-forward semantics whenever the physical
             * ranges overlap or either logical range wraps.  Only the proven
             * non-overlapping contiguous case is collapsed into memcpy. */
            Uint32 sOff = WrapBWRAMOffset(src);
            Uint32 dOff = WrapBWRAMOffset(dst);
            if (sOff + len <= m_nBWRAMBytes &&
                dOff + len <= m_nBWRAMBytes &&
                (sOff + len <= dOff || dOff + len <= sOff))
            {
                memcpy(m_pBWRAM + dOff, m_pBWRAM + sOff, len);
                n = len;
                bBulkDone = TRUE;
            }
        }
    }

    if (!bBulkDone && len >= 16u &&
        (uSrcType == 2 || uSrcType == 3) && !bDstBWRAM)
    {
        Uint32 sOff = src & 0x7FFu;
        Uint32 dOff = dst & 0x7FFu;
        if (sOff + len <= 0x800u &&
            dOff + len <= 0x800u &&
            (sOff + len <= dOff || dOff + len <= sOff))
        {
            memcpy(m_IRAM + dOff, m_IRAM + sOff, len);
            n = len;
            bBulkDone = TRUE;
        }
    }

    if (!bBulkDone)
    {
        for (n = 0; n < len; ++n)
        {
            Uint8 v = 0;
            switch (uSrcType)
            {
                case 0: v = ReadBus((src + n) & 0xFFFFFFu, 0); break;
                case 1: v = ReadBWRAMLinear(src + n, 0); break;
                default:
                case 2: v = m_IRAM[(src + n) & 0x7FF]; break;
            }
            if (bDstBWRAM)
            {
                if (m_pBWRAM && m_nBWRAMBytes)
                    m_pBWRAM[WrapBWRAMOffset(dst + n)] = v;
            }
            else
                m_IRAM[(dst + n) & 0x7FF] = v;
        }
    }

    if (bDstBWRAM && len) MarkBWRAMDirty();
    m_Reg[0x101] |= 0x20;
    if (m_Reg[0x0A] & 0x20) m_Reg[0x0B] &= (Uint8)~0x20;
}

void SNSA1::WriteRegister(Uint16 uAddr, Uint8 uData)
{
    Uint16 i;
    Uint8 old;
    if (uAddr < 0x2200 || uAddr > 0x22FF)
        return;
    i = (Uint16)(uAddr - 0x2200);
    old = m_Reg[i];

    switch (uAddr)
    {
        case 0x2200:
            m_Reg[0x00] = uData;
            if ((old & 0x20) && !(uData & 0x20))
            {
                m_Reg[0x2A] = 0; /* CIWP is cleared on reset release. */
                ResetCPUToVector((Uint16)m_Reg[0x03] | ((Uint16)m_Reg[0x04] << 8));
            }
            if (uData & 0x80)
            {
                m_Reg[0x101] |= 0x80;
                if (m_Reg[0x0A] & 0x80) m_Reg[0x0B] &= (Uint8)~0x80;
            }
            if (uData & 0x10)
            {
                m_Reg[0x101] |= 0x10;
                if (m_Reg[0x0A] & 0x10) m_Reg[0x0B] &= (Uint8)~0x10;
            }
            return;
        case 0x2201:
            m_Reg[0x01] = uData;
            if (((old ^ uData) & 0x80) && (m_Reg[0x100] & uData & 0x80))
                m_Reg[0x02] &= (Uint8)~0x80;
            if (((old ^ uData) & 0x20) && (m_Reg[0x100] & uData & 0x20))
                m_Reg[0x02] &= (Uint8)~0x20;
            UpdateMainIRQ();
            return;
        case 0x2202:
            m_Reg[0x100] &= (Uint8)~uData;
            m_Reg[0x02] = uData;
            UpdateMainIRQ();
            return;
        case 0x2209:
            m_Reg[0x09] = uData;
            if (uData & 0x80)
            {
                m_Reg[0x100] |= 0x80;
                if (m_Reg[0x01] & 0x80) m_Reg[0x02] &= (Uint8)~0x80;
            }
            UpdateMainIRQ();
            return;
        case 0x220A:
            if (((old ^ uData) & 0x80) && (m_Reg[0x101] & uData & 0x80))
                m_Reg[0x0B] &= (Uint8)~0x80;
            if (((old ^ uData) & 0x40) && (m_Reg[0x101] & uData & 0x40))
                m_Reg[0x0B] &= (Uint8)~0x40;
            if (((old ^ uData) & 0x20) && (m_Reg[0x101] & uData & 0x20))
                m_Reg[0x0B] &= (Uint8)~0x20;
            if (((old ^ uData) & 0x10) && (m_Reg[0x101] & uData & 0x10))
                m_Reg[0x0B] &= (Uint8)~0x10;
            m_Reg[0x0A] = uData;
            return;
        case 0x220B:
            m_Reg[0x101] &= (Uint8)~uData;
            m_Reg[0x0B] = uData;
            return;
        case 0x2210:
        case 0x2212: case 0x2213: case 0x2214: case 0x2215:
            m_Reg[i] = uData;
            RebuildFastState();
            return;
        case 0x2211:
            m_uHCounter = m_uVCounter = m_uPrevHCounter = 0;
            m_bTimerLastState = FALSE;
            m_Reg[i] = uData;
            return;
        case 0x2220: case 0x2221: case 0x2222: case 0x2223:
            /* AURORA_SA1_SUPERMMC_FETCH_ABORT_V5_20260903
             *
             * CXB/DB/EB/FB change the physical ROM decode immediately.
             * The PS2 MIPS 65816 executor caches the current direct-fetch
             * bank while it is inside SNCPUExecute(). Rebuilding Bank[]
             * from this S-CPU MMIO trap is therefore not sufficient: after
             * returning from the trap the same execution slice could keep
             * fetching through the pre-write host pointer.
             *
             * Skip a no-op register write. On a real mapping change, install
             * both SA-1 and S-CPU maps first, then abort only the current
             * S-CPU slice. SNCPUExecute() restores the unspent cycle budget;
             * its next entry resolves PC using the new map. */
            if (old == uData)
                return;

            m_Reg[i] = uData;
            MapRomGroup(&m_Cpu, (Uint32)(i - 0x20), FALSE);

            /* V8.3: if CXB/DB/EB/FB was written by the SA-1 itself, the
             * second native 65816 has the same cached-fetch hazard as the
             * S-CPU. SNCPUAbort is a no-op when that CPU is not executing. */
            SNCPUAbort(&m_Cpu);

            if (m_bMapMainRom && m_pOwner)
            {
                SNCpuT *pMainCpu = m_pOwner->GetCpu();
                MapRomGroup(pMainCpu, (Uint32)(i - 0x20), TRUE);
                SNCPUAbort(pMainCpu);
            }
            return;
        case 0x2224: /* BMAP: S-CPU $6000-$7FFF */
            if (old == uData)
                return;
            m_Reg[i] = uData;
            RebuildFastState();
            if (m_pOwner)
            {
                SNCpuT *pMainCpu = m_pOwner->GetCpu();
                MapMainBWRAMWindow(pMainCpu);
                SNCPUAbort(pMainCpu);
            }
            return;
        case 0x2225: /* BMAPS: SA-1 $6000-$7FFF / bitmap select */
            if (old == uData)
                return;
            m_Reg[i] = uData;
            RebuildFastState();
            MapSA1BWRAMWindow();
            SNCPUAbort(&m_Cpu);
            return;
        case 0x2226: case 0x2227: case 0x2228:
            m_Reg[i] = uData;
            RebuildFastState();
            RefreshBWRAMDirectWrites();
            return;
        case 0x2229: case 0x222A:
            m_Reg[i] = uData;
            return;
        case 0x2230:
            m_Reg[i] = uData;
            if (!(uData & 0x80u)) m_uCharIndex = 0; /* AURORA_DSP_SA1_FX_CX4_CPU_MEGA_ACCURACY_V6_20260916 */
            return;
        case 0x2231:
            m_Reg[i] = uData;
            RebuildFastState();
            if (uData & 0x80)
            {
                Bool bWasCharDMA = m_bCharDMA;
                m_bCharDMA = FALSE;
                if (bWasCharDMA && m_pOwner)
                {
                    SNCpuT *pMainCpu = m_pOwner->GetCpu();
                    MapMainBWRAMWindow(pMainCpu);
                    SNCPUAbort(pMainCpu);
                }
            }
            return;
        case 0x2232: case 0x2233: case 0x2234:
        case 0x2235:
            m_Reg[i] = uData;
            RebuildFastState();
            return;
        case 0x2236:
            m_Reg[i] = uData;
            RebuildFastState();
            if ((m_Reg[0x30] & 0xA4) == 0x80)
                DoDMA();
            else if ((m_Reg[0x30] & 0xB0) == 0xB0)
            {
                Bool bWasCharDMA = m_bCharDMA;
                m_Reg[0x100] |= 0x20;
                m_bCharDMA = TRUE;
                if (!bWasCharDMA && m_pOwner)
                {
                    SNCpuT *pMainCpu = m_pOwner->GetCpu();
                    MapMainBWRAMWindow(pMainCpu);
                    SNCPUAbort(pMainCpu);
                }
                UpdateMainIRQ();
            }
            return;
        case 0x2237:
            m_Reg[i] = uData;
            if ((m_Reg[0x30] & 0xA4) == 0x84)
                DoDMA();
            return;
        case 0x2238: case 0x2239:
            m_Reg[i] = uData;
            return;
        case 0x223F:
            m_Reg[i] = uData;
            m_uBitmapFormat = (uData & 0x80) ? 2 : 4;
            RebuildFastState();
            return;
        case 0x2240: case 0x2241: case 0x2242: case 0x2243:
        case 0x2244: case 0x2245: case 0x2246:
            m_Reg[i] = uData;
            return;
        case 0x2247:
            m_Reg[i] = uData;
            if ((m_Reg[0x30] & 0xB0) == 0xA0) DoCC2();
            return;
        case 0x2248: case 0x2249: case 0x224A: case 0x224B:
        case 0x224C: case 0x224D: case 0x224E:
            m_Reg[i] = uData;
            return;
        case 0x224F:
            m_Reg[i] = uData;
            if ((m_Reg[0x30] & 0xB0) == 0xA0) DoCC2();
            return;
        case 0x2250:
            if (uData & 2) m_uSum = 0;
            m_uArithmeticOp = uData & 3;
            m_Reg[i] = uData;
            return;
        case 0x2251:
            m_uOp1 = (Uint16)((m_uOp1 & 0xFF00) | uData); m_Reg[i] = uData; return;
        case 0x2252:
            m_uOp1 = (Uint16)((m_uOp1 & 0x00FF) | ((Uint16)uData << 8)); m_Reg[i] = uData; return;
        case 0x2253:
            m_uOp2 = (Uint16)((m_uOp2 & 0xFF00) | uData); m_Reg[i] = uData; return;
        case 0x2254:
            m_uOp2 = (Uint16)((m_uOp2 & 0x00FF) | ((Uint16)uData << 8));
            m_Reg[i] = uData; DoArithmetic(); return;
        case 0x2258:
            m_Reg[i] = uData; ReadVariableLength(TRUE, FALSE); return;
        case 0x2259: case 0x225A: case 0x225B:
            m_Reg[i] = uData;
            m_uVariableBitPos = 0;
            ReadVariableLength(FALSE, TRUE);
            return;
        default:
            m_Reg[i] = uData;
            return;
    }
}

void SNSA1::UpdateMainIRQ()
{
    Bool b = ((m_Reg[0x100] & m_Reg[0x01] & 0xA0) != 0) ? TRUE : FALSE;
    if (m_pOwner)
        m_pOwner->SetSA1IRQ(b);
}

void SNSA1::ResetCPUToVector(Uint16 uVector)
{
    SNCPUResetCounters(&m_Cpu);
    SNCPUResetRegs(&m_Cpu);
    m_Cpu.uSignal = 0;
    m_Cpu.Regs.rE = 1;
    m_Cpu.Regs.rP = SNCPU_FLAG_M | SNCPU_FLAG_X | SNCPU_FLAG_I;
    m_Cpu.Regs.rS.w = 0x01FF;
    m_Cpu.Regs.rPC = uVector;
    m_Cpu.uMDR = (Uint8)(uVector >> 8);
}

Bool SNSA1::EnterInterrupt(SNCpuT *pCpu, Uint16 uVector, Bool bNMI)
{
    if (!pCpu) return FALSE;

    if (pCpu->uSignal & SNCPU_SIGNAL_WAI)
        pCpu->uSignal &= (Uint8)~SNCPU_SIGNAL_WAI;

    if (!bNMI && (pCpu->Regs.rP & SNCPU_FLAG_I))
        return TRUE;

    if (pCpu->Regs.rE)
    {
        SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 8));
        SNCPUPush8(pCpu, (Uint8)pCpu->Regs.rPC);
        SNCPUPush8(pCpu, bNMI ? (pCpu->Regs.rP & (Uint8)~SNCPU_FLAG_B)
                              : (pCpu->Regs.rP & (Uint8)~SNCPU_FLAG_B));
    }
    else
    {
        SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 16));
        SNCPUPush8(pCpu, (Uint8)(pCpu->Regs.rPC >> 8));
        SNCPUPush8(pCpu, (Uint8)pCpu->Regs.rPC);
        SNCPUPush8(pCpu, pCpu->Regs.rP);
    }
    pCpu->Regs.rPC = uVector;
    pCpu->Regs.rP &= (Uint8)~SNCPU_FLAG_D;
    pCpu->Regs.rP |= SNCPU_FLAG_I;
    SNCPUConsumeCycles(pCpu,
        SNCPU_CYCLE_SLOW * (pCpu->Regs.rE ? 5 : 6) + SNCPU_CYCLE_FAST * 2);
    return TRUE;
}

Bool SNSA1::EnterMainIRQOverride(SNCpuT *pMainCpu)
{
    if (!m_bActive || !(m_Reg[0x09] & 0x40))
        return FALSE;
    EnterInterrupt(pMainCpu,
        (Uint16)m_Reg[0x0E] | ((Uint16)m_Reg[0x0F] << 8), FALSE);
    return TRUE;
}

Bool SNSA1::EnterMainNMIOverride(SNCpuT *pMainCpu)
{
    if (!m_bActive || !(m_Reg[0x09] & 0x10))
        return FALSE;
    EnterInterrupt(pMainCpu,
        (Uint16)m_Reg[0x0C] | ((Uint16)m_Reg[0x0D] << 8), TRUE);
    return TRUE;
}

void SNSA1::ServiceInterrupts()
{
    /* AURORA_SA1_CCNT_INTERRUPT_HANDSHAKE_V6_20260903
     * S-CPU -> SA-1 NMI/IRQ are live CCNT request lines. CIC is their
     * acknowledge/clear latch; CFR reports occurrence. Timer/DMA remain
     * CIE-qualified pending sources. */
    if ((m_Reg[0x00] & 0x10) && !(m_Reg[0x0B] & 0x10))
    {
        m_Reg[0x101] |= 0x10;
        m_Reg[0x0B] |= 0x10;
        EnterInterrupt(&m_Cpu,
            (Uint16)m_Reg[0x05] | ((Uint16)m_Reg[0x06] << 8), TRUE);
        return;
    }

    if (!(m_Cpu.Regs.rP & SNCPU_FLAG_I))
    {
        if ((m_Reg[0x0A] & 0x40) &&
            (m_Reg[0x101] & 0x40) &&
            !(m_Reg[0x0B] & 0x40))
        {
            EnterInterrupt(&m_Cpu,
                (Uint16)m_Reg[0x07] | ((Uint16)m_Reg[0x08] << 8), FALSE);
        }
        else if ((m_Reg[0x0A] & 0x20) &&
                 (m_Reg[0x101] & 0x20) &&
                 !(m_Reg[0x0B] & 0x20))
        {
            EnterInterrupt(&m_Cpu,
                (Uint16)m_Reg[0x07] | ((Uint16)m_Reg[0x08] << 8), FALSE);
        }
        else if ((m_Reg[0x00] & 0x80) &&
                 !(m_Reg[0x0B] & 0x80))
        {
            m_Reg[0x101] |= 0x80;
            EnterInterrupt(&m_Cpu,
                (Uint16)m_Reg[0x07] | ((Uint16)m_Reg[0x08] << 8), FALSE);
        }
    }
}

void SNSA1::UpdateTimer(Uint32 nSA1Cycles)
{
    /* AURORA_DSP_SA1_FX_CX4_CPU_MEGA_ACCURACY_V6_20260916: AURORA SA-1 timer event accuracy.
     * The hardware compares H/V at timer clock positions, not merely at the
     * final counter value of a scheduler batch. Walk only horizontal-wrap
     * segments so this stays cheap while preserving exact event crossings. */
    Uint8 timer = m_Reg[0x10];
    Bool hEnable = (timer & 1u) ? TRUE : FALSE;
    Bool vEnable = (timer & 2u) ? TRUE : FALSE;
    Bool hit = FALSE;
    Uint32 remaining = nSA1Cycles;

    if (!m_uTimerHMax || !m_uTimerVMax)
        return;

    if (m_uHCounter >= m_uTimerHMax)
        m_uHCounter %= m_uTimerHMax;
    if (m_uVCounter >= m_uTimerVMax)
        m_uVCounter %= m_uTimerVMax;

    m_uPrevHCounter = m_uHCounter;

    while (remaining)
    {
        Uint32 toWrap = m_uTimerHMax - m_uHCounter;
        Uint32 step = (remaining < toWrap) ? remaining : toWrap;
        Uint32 endH = m_uHCounter + step;

        /* Non-zero HCNT belongs to the current scanline. */
        if (hEnable && m_uTimerHTarget != 0 &&
            m_uTimerHTarget > m_uHCounter &&
            m_uTimerHTarget < m_uTimerHMax &&
            m_uTimerHTarget <= endH &&
            (!vEnable || m_uVCounter == m_uTimerVTarget))
            hit = TRUE;

        m_uHCounter = endH;
        remaining -= step;

        if (m_uHCounter >= m_uTimerHMax)
        {
            m_uHCounter = 0;
            if (++m_uVCounter >= m_uTimerVMax) m_uVCounter = 0;

            /* V-only compares at H=0. HCNT=0 also compares here. */
            if ((!hEnable && vEnable && m_uVCounter == m_uTimerVTarget) ||
                (hEnable && m_uTimerHTarget == 0 &&
                 (!vEnable || m_uVCounter == m_uTimerVTarget)))
                hit = TRUE;
        }
    }

    if (!(timer & 3u))
    {
        m_bTimerLastState = FALSE;
        return;
    }

    if (hit)
    {
        m_Reg[0x101] |= 0x40;
        if (m_Reg[0x0A] & 0x40) m_Reg[0x0B] &= (Uint8)~0x40;
    }
    m_bTimerLastState = hit;
}

void SNSA1::Run(Int32 nMainMasterCycles)
{
    Int32 nSA1;
    Uint8 oldInternal, oldSlow;
    if (!m_bActive || nMainMasterCycles <= 0)
        return;

    nSA1 = nMainMasterCycles * 3;
    UpdateTimer((Uint32)nSA1);

    if (m_Reg[0x00] & 0x60) /* reset or wait */
        return;

    ServiceInterrupts();
    if (m_Cpu.uSignal & (SNCPU_SIGNAL_WAI | SNCPU_SIGNAL_STP))
        return;

    /* CPU overclock is an S-CPU user option; do not leak it into SA-1. */
    oldInternal = g_SnesCpuInternalCycle;
    oldSlow = g_SnesCpuSlowCycle;
    /* AURORA_SA1_PERF_V8_3_2_20260903 */
    if (oldInternal != SNCPU_CYCLE_FAST)
        g_SnesCpuInternalCycle = SNCPU_CYCLE_FAST;
    if (oldSlow != SNCPU_CYCLE_SLOW)
        g_SnesCpuSlowCycle = SNCPU_CYCLE_SLOW;

    SNCPUAddCycles(&m_Cpu, nSA1);

    /* AURORA_SA1_BOUNDED_CHUNK_EXECUTOR_V7_20260903
     * Keep the scheduled SA-1 budget, but return to the scheduler after a
     * small native group so MMIO/remap/WAI transitions become visible. */
    while (m_Cpu.Cycles > 0)
    {
        Int32 before = m_Cpu.Cycles;

        ServiceInterrupts();
        if (m_Cpu.uSignal & (SNCPU_SIGNAL_WAI | SNCPU_SIGNAL_STP))
        {
            m_Cpu.Cycles = 0;
            break;
        }

        (void)SNCPUExecuteBounded(&m_Cpu, CPU_EXEC_QUANTUM);

        if (m_Cpu.Cycles >= before)
            SNCPUConsumeCycles(&m_Cpu, SNCPU_CYCLE_FAST);
    }

    if (g_SnesCpuInternalCycle != oldInternal)
        g_SnesCpuInternalCycle = oldInternal;
    if (g_SnesCpuSlowCycle != oldSlow)
        g_SnesCpuSlowCycle = oldSlow;
}
