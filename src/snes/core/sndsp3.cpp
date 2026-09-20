/* AURORA_DSP_SA1_FX_CX4_CPU_MEGA_ACCURACY_V6_20260916 */
#include "types.h"
#include "sndsp3.h"
#include "dsp3emu.h"
/* AURORA_DSP3_LAZY_WORK_V7_1_20260916: keep DSP-3 cold until SetRom() selects it and SnesSystem::Reset()
 * reaches this wrapper. */
SNDSP3::SNDSP3() : m_bReady(FALSE) {}

SNDSP3::~SNDSP3()
{
    DSP3ReleaseWork();
    m_bReady = FALSE;
}

void SNDSP3::ReleaseWork()
{
    /* AURORA_FDC52B8_AUDIT_DSP3_RELEASE_V1_20260917
     * OP1E scratch belongs only to an attached DSP-3 device. */
    DSP3ReleaseWork();
    m_bReady = FALSE;
}

void SNDSP3::Reset()
{
    m_bReady = DSP3EnsureWork() ? TRUE : FALSE;
    if (m_bReady) InitDSP3();
}

void SNDSP3::WriteData(Uint32 uAddr, Uint8 uData)
{
    if (!m_bReady) return;
    dsp3_address = (Uint16)(uAddr & 0xFFFFu);
    dsp3_byte = uData;
    DSP3SetByte();
}

Uint8 SNDSP3::ReadData(Uint32 uAddr)
{
    if (!m_bReady) return 0x00;
    dsp3_address = (Uint16)(uAddr & 0xFFFFu);
    DSP3GetByte();
    return dsp3_byte;
}

Uint8 SNDSP3::ReadStatus(Uint32 uAddr)
{
    if (!m_bReady) return 0x80;
    dsp3_address = (Uint16)(uAddr & 0xFFFFu);
    DSP3GetByte();
    return dsp3_byte;
}
