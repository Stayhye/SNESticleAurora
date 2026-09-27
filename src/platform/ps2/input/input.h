#ifndef _INPUT_H
#define _INPUT_H

#include "types.h"

#define INPUT_MAXPADS (5)

typedef enum InputDeviceE
{
    INPUT_DEVICE_NULL,
    INPUT_DEVICE_MOUSE,
    INPUT_DEVICE_KEYBOARD0,
    INPUT_DEVICE_KEYBOARD1,
    INPUT_DEVICE_KEYBOARD2,
    INPUT_DEVICE_KEYBOARD3,
    INPUT_DEVICE_JOYSTICK0,
    INPUT_DEVICE_JOYSTICK1,
    INPUT_DEVICE_JOYSTICK2,
    INPUT_DEVICE_JOYSTICK3,
    INPUT_DEVICE_NUM
} InputDeviceE;

#ifdef __cplusplus
#include "inputdevice.h"
extern "C" {
#endif

void   InputInit(Bool bXLib);
void   InputShutdown(void);
void   InputPoll(void);
Uint32 InputGetPadData(Uint32 uPad);
Bool   InputIsPadConnected(Uint32 uPad);

/* AURORA_CODEPATH_SIMPLIFY_V5_20260927
 * One cross-TU call returns the immutable post-InputPoll digital + analog-dpad
 * snapshot used repeatedly by MainLoopProcess. Output pointers must be valid. */
Bool   InputGetPadSnapshot(Uint32 uPad, Uint32 *pData, Uint32 *pAnalogDpad);

/* AURORA_MOUSE_EXPLICIT_V3
 * No automatic USB probing. Off/Controller perform no mouse SIF RPC.
 * USB is explicit and lazy-initialized. */
Bool   InputIsMouseConnected(void);
void   InputGetMouseData(Int32 *pDeltaX, Int32 *pDeltaY, Uint32 *pButtons);
void   InputMouseClearSnapshot(void);
void   InputMousePollPostFrame(Bool bCaptureMotion);


typedef enum InputSnesMouseModeE
{
    INPUT_SNES_MOUSE_OFF = 0,
    INPUT_SNES_MOUSE_CONTROLLER,
    INPUT_SNES_MOUSE_USB,
    INPUT_SNES_MOUSE_MODE_NUM
} InputSnesMouseModeE;

void        InputSnesMouseCycleMode(void);
void        InputSnesMouseCycleModeDir(Int32 dir);
InputSnesMouseModeE InputSnesMouseGetMode(void);
void        InputSnesMouseSetMode(InputSnesMouseModeE eMode);
const char *InputSnesMouseGetModeName(void);
Bool        InputSnesMouseShouldUse(void);

/* Returns digital d-pad bits (PAD_LEFT/RIGHT/UP/DOWN) synthesised from the
   pad's left analog stick deflection. Returns 0 when the stick is inside
   the dead zone, when the pad is disconnected, or when the controller is
   not running in dualshock mode. Callers OR these bits into the digital
   pad data so the analog stick can drive both menu navigation and the
   in-game SNES d-pad. */
Uint32 InputGetPadDpadFromAnalog(Uint32 uPad);

/* AURORA_QN_ARKANOID_ANALOG_V2_20260828
 * Raw packed axes: rjoy_h, rjoy_v, ljoy_h, ljoy_v from LSB to MSB. */
Uint32 InputGetPadAnalog(Uint32 uPad);

#ifdef __cplusplus
}
#endif

#endif
