/*
 * audio.h - EE-side audio output API (audsrv backend).
 *
 * A small Aud_* API over the PS2SDK **audsrv** service (SPU2 native:
 * 48000 Hz / 16-bit / stereo). This was historically the SjPCM API by
 * Nick Van Veen ("Sjeep", 2002), which talked to a custom SJPCM2.IRX
 * over SIF RPC - that IRX is gone. The implementation here
 * (audio_audsrv.c) is a from-scratch audsrv version; the API was
 * renamed from SjPCM_* to Aud_* so the name matches what it actually
 * does. MIT (this repo).
 */

#ifndef _AUDIO_H
#define _AUDIO_H

void Aud_Puts(char *format, ...);
int  Aud_Init(int sync, int buffersize, int maxenqueuesamples);

void Aud_Enqueue(short *left, short *right, int size, int wait);
void Aud_Play();
void Aud_Pause();
void Aud_Setvol(unsigned int volume);
void Aud_SetRuntimeTraceMute(int enabled);
void Aud_Clearbuff();
int  Aud_Available();
int  Aud_Buffered();
void Aud_Quit();

void Aud_BufferedAsyncStart();
/* <=0 restores the historical default catch-up limit. */
void Aud_SetAsyncBurstLimit(int samples);
int  Aud_BufferedAsyncGet();
void Aud_EnqueueAsync(short *left, short *right, int size);
/* AURORA_AUDIO_ASYNC_GAIN_COPY_V4_API_20260821 */
void Aud_EnqueueAsyncGain(short *left, short *right, int size, int gainPct);
void Aud_Wait();

int  Aud_IsInitialized();

/* AURORA_GAMEPLAY_HEADROOM_V1
 * Called once when host UI transitions into gameplay. It may append silence
 * only to restore a small safety cushion before cold renderer work begins. */
void Aud_PrepareGameplayHeadroom();

/* AURORA_AUDIO_ASYNC_FIFO_V2
 * Drop only EE-staged gameplay PCM when intentionally leaving gameplay. */
void Aud_AsyncDiscardPending();

/* AURORA_COMPAT_AUDIO_API */
void Aud_SetCompatSmallChunks(int enabled);
int  Aud_GetCompatSmallChunks(void);
void Aud_SetCompatDeepQueue(int enabled);
int  Aud_GetCompatDeepQueue(void);

#endif /* _AUDIO_H */
