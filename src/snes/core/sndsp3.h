/* AURORA_DSP_SA1_FX_CX4_CPU_MEGA_ACCURACY_V6_20260916 */
#ifndef _SNDSP3_H
#define _SNDSP3_H
#include "types.h"
#include "sndsp.h"
class SNDSP3 : public ISNDSP {
public:
    SNDSP3();
    ~SNDSP3(); /* AURORA_DSP3_LAZY_WORK_V7_1_20260916 */
    void Reset();
    void WriteData(Uint32 uAddr, Uint8 uData);
    Uint8 ReadData(Uint32 uAddr);
    Uint8 ReadStatus(Uint32 uAddr);
private:
    Bool m_bReady; /* AURORA_DSP3_LAZY_WORK_V7_1_20260916 */
};
#endif
