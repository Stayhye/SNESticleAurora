/* AURORA_DSP_SA1_FX_CX4_CPU_MEGA_ACCURACY_V6_20260916 */
#ifndef _DSP3EMU_H
#define _DSP3EMU_H
#include <stdint.h>
extern uint8_t dsp3_byte;
extern uint16_t dsp3_address;
/* AURORA_DSP3_LAZY_WORK_V7_1_20260916: lazy 48 KiB OP1E workspace lifecycle. */
int DSP3EnsureWork(void);
void DSP3ReleaseWork(void);
void InitDSP3(void);
void DSP3SetByte(void);
void DSP3GetByte(void);
#endif
