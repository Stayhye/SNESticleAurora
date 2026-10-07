#ifndef _SNSPCTRACER_H
#define _SNSPCTRACER_H

#include "types.h"

#ifndef AURORA_SNES_TRACER
#define AURORA_SNES_TRACER 0
#endif

struct SNSpc_t;

#ifdef __cplusplus
extern "C" {
#endif

#if AURORA_SNES_TRACER
extern volatile Uint8 g_AuroraSnesTracerEnabled;
#define SNSPC_TRACER_FAST_ACTIVE() \
    (__builtin_expect(g_AuroraSnesTracerEnabled != 0u, 0))
void SNSPCTracerConfigureGame(Uint32 uRuntimeCRC, const char *pTitle);
Bool SNSPCTracerSetEnabled(Bool bEnabled);
Bool SNSPCTracerIsEnabled(void);
void SNSPCTracerRecordInstruction(
    struct SNSpc_t *pCpu, Uint16 pc, Uint8 opcode, Int32 nCycles,
    Uint32 fN, Uint32 fZ, Uint32 fC, Uint32 fHV);
void SNSPCTracerPortEvent(
    Uint8 kind, Uint8 port, Uint8 data, Uint8 prior,
    Uint32 frameCycle, Uint32 totalCycle, Uint16 pc);
void SNSPCTracerPeekPorts(
    struct SNSpc_t *pSpc, Uint8 *cpuToSpc, Uint8 *spcToCpu);
void SNSPCTracerDspEvent(
    Uint8 kind, Uint32 eventCycle, Uint32 applyCycle,
    Uint8 reg, Uint8 prior, Uint8 data);
void SNSPCTracerEchoRun(
    Uint32 mixCycle, Uint32 sampleRate,
    Uint32 base, Uint32 size, Uint32 posBefore, Uint32 posAfter,
    Uint32 nSamples, Uint8 flg, Uint8 esaReg, Uint8 edlReg,
    Uint8 edlLatch, Uint8 bWrite);
#else
#define SNSPC_TRACER_FAST_ACTIVE() 0
#define SNSPCTracerConfigureGame(...) ((void)0)
#define SNSPCTracerSetEnabled(_x) FALSE
#define SNSPCTracerIsEnabled() FALSE
#define SNSPCTracerRecordInstruction(...) ((void)0)
#define SNSPCTracerPortEvent(...) ((void)0)
#define SNSPCTracerPeekPorts(...) ((void)0)
#define SNSPCTracerDspEvent(...) ((void)0)
#define SNSPCTracerEchoRun(...) ((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif
