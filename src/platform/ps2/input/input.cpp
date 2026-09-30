#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <kernel.h>
#include <sifrpc.h>

#include "libpad.h"
#include "libxpad.h"
#include "libxmtap.h"
#include "types.h"
#include "input.h"
typedef struct _mouse_data { Int32 x, y, wheel; Uint32 buttons; } mouse_data;
enum { PS2MOUSE_READMODE_DIFF = 0, PS2MOUSE_READ = 0x1, PS2MOUSE_SETREADMODE = 0x2, PS2MOUSE_SETACCEL = 0x6, PS2MOUSE_RESET = 0xB, PS2MOUSE_ENUM = 0xC, PS2MOUSE_BIND_RPC_ID = 0x500C001, PS2MOUSE_AURORA_STATUS = 0x21 };
#include "hw.h"

/* On-screen log (defined in mainloop_ui.cpp, C linkage).  Used so the
   controller bring-up is visible on a real PS2 where plain printf/DLog
   output never reaches the screen (DLog goes to the EE SIO FIFO, which
   needs a serial cable to read). */
extern "C" void ScrPrintf(const char *pFormat, ...);
/* Defined by embedded_irx.cpp. Guarding the bind with this status prevents
   the stock libmouse-style endless wait when the IOP driver did not start. */
extern "C" int UsbMouseDriverAvailable(void);

static char _Input_PadBuf[INPUT_MAXPADS][256]
    __attribute__((aligned(64)))
    __attribute__((section(".bss")));

/* Each entry packs the four 8-bit analog axes reported by libpad in the
   order (rjoy_h, rjoy_v, ljoy_h, ljoy_v) starting from the LSB so that
   0x80808080 is centred. */
static Uint32 _Input_PadData[INPUT_MAXPADS];
static Uint32 _Input_PadAnalog[INPUT_MAXPADS];
/* AURORA_SAFE_HOTPATH_V4: derived once per fresh pad report, read many times/frame. */
static Uint32 _Input_PadAnalogDpad[INPUT_MAXPADS];
static int    _Input_bPadConnected[INPUT_MAXPADS];
static Bool   _Input_bInitialized = FALSE;
static Bool   _Input_bXPad = FALSE;
static Int32  _Input_nPads = 0;

/* AURORA_USB_SNES_MOUSE_INPUT_V1_5
 * Direct bounded RPC client for PS2SDK ps2mouse.irx. V1.5 explicitly
 * forces differential mode and 1.0 host acceleration, so raw X/Y counts
 * remain 1:1 and diagonals are not re-scaled. */
static SifRpcClientData_t _Input_MouseRpc __attribute__((aligned(64)));
static Uint8  _Input_MouseRpcBuffer[128] __attribute__((aligned(64)));
static Bool   _Input_bMouseRpcReady = FALSE;
static Bool   _Input_bMouseConnected = FALSE;
static Int32  _Input_MouseDeltaX = 0;
static Int32  _Input_MouseDeltaY = 0;
static Uint32 _Input_MouseButtons = 0;
static Uint32 _Input_MouseEnumCountdown = 0;
static Uint32 _Input_MouseAuroraStatus = 0;
static InputSnesMouseModeE _Input_SnesMouseMode = INPUT_SNES_MOUSE_OFF;

/* AURORA_COMPOSITE_MOUSE_V1 status bits from the project-local IOP driver. */
#define INPUT_MOUSE_STATUS_DRIVER     0x01U
#define INPUT_MOUSE_STATUS_HID_SEEN   0x02U
#define INPUT_MOUSE_STATUS_BOOT_SEEN  0x04U
#define INPUT_MOUSE_STATUS_CONNECTED  0x08U
#define INPUT_MOUSE_STATUS_BOOT_PROTO 0x10U

#define INPUT_MOUSE_ENUM_PERIOD       60
#define INPUT_MOUSE_BIND_TRIES       250
#define INPUT_MOUSE_BIND_SLEEP_US   2000

static Bool _Input_MouseBindRpc(void)
{
    int i;

    memset(&_Input_MouseRpc, 0, sizeof(_Input_MouseRpc));
    for (i = 0; i < INPUT_MOUSE_BIND_TRIES; i++)
    {
        if (sceSifBindRpc(&_Input_MouseRpc, PS2MOUSE_BIND_RPC_ID, 0) < 0)
            return FALSE;
        if (_Input_MouseRpc.server)
            return TRUE;
        usleep(INPUT_MOUSE_BIND_SLEEP_US);
    }
    return FALSE;
}

static int _Input_MouseRpcCall(Uint32 uCommand, void *pOut, Uint32 nOut)
{
    const Uint8 *pUncached;

    if (!_Input_bMouseRpcReady)
        return -1;
    if (nOut > sizeof(_Input_MouseRpcBuffer))
        return -1;

    memset(_Input_MouseRpcBuffer, 0, sizeof(_Input_MouseRpcBuffer));
    if (sceSifCallRpc(
            &_Input_MouseRpc,
            uCommand,
            0,
            _Input_MouseRpcBuffer,
            sizeof(_Input_MouseRpcBuffer),
            _Input_MouseRpcBuffer,
            sizeof(_Input_MouseRpcBuffer),
            NULL,
            NULL) < 0)
        return -1;

    pUncached = (const Uint8 *)UNCACHED_SEG(_Input_MouseRpcBuffer);
    if (pOut && nOut)
        memcpy(pOut, pUncached, nOut);
    return 0;
}

static int _Input_MouseRpcCallU32(Uint32 uCommand, Uint32 uValue)
{
    if (!_Input_bMouseRpcReady)
        return -1;

    memset(_Input_MouseRpcBuffer, 0, sizeof(_Input_MouseRpcBuffer));
    ((Uint32 *)_Input_MouseRpcBuffer)[0] = uValue;
    if (sceSifCallRpc(
            &_Input_MouseRpc,
            uCommand,
            0,
            _Input_MouseRpcBuffer,
            sizeof(_Input_MouseRpcBuffer),
            _Input_MouseRpcBuffer,
            sizeof(_Input_MouseRpcBuffer),
            NULL,
            NULL) < 0)
        return -1;
    return 0;
}

static void _Input_MouseInit(void)
{
    _Input_bMouseRpcReady = FALSE;
    _Input_bMouseConnected = FALSE;
    _Input_MouseDeltaX = 0;
    _Input_MouseDeltaY = 0;
    _Input_MouseButtons = 0;
    _Input_MouseEnumCountdown = 0;
    _Input_MouseAuroraStatus = 0;

    if (!UsbMouseDriverAvailable())
        return;

    if (!_Input_MouseBindRpc())
    {
        printf("Input: ps2mouse RPC bind timed out; mouse disabled\n");
        return;
    }

    _Input_bMouseRpcReady = TRUE;
    _Input_MouseRpcCall(PS2MOUSE_RESET, NULL, 0);
    /* Force the exact host contract rather than relying on driver defaults:
       differential relative counts and a 1.0 acceleration multiplier. */
    _Input_MouseRpcCallU32(PS2MOUSE_SETREADMODE, PS2MOUSE_READMODE_DIFF);
    _Input_MouseRpcCallU32(PS2MOUSE_SETACCEL, 0x00010000U);
    _Input_MouseRpcCall(PS2MOUSE_AURORA_STATUS,
                        &_Input_MouseAuroraStatus,
                        sizeof(_Input_MouseAuroraStatus));
    printf("Input: USB mouse RPC ready (diff, accel=1.0, status=%02x)\n",
           (unsigned)_Input_MouseAuroraStatus);
}

void InputMouseClearSnapshot(void)
{
    _Input_MouseDeltaX = 0;
    _Input_MouseDeltaY = 0;
    _Input_MouseButtons = 0;
}

void InputMousePollPostFrame(Bool bCaptureMotion)
{
    Uint32 nDevices = 0;
    mouse_data data;

    if (!_Input_bInitialized)
        return;

    InputMouseClearSnapshot();
    if (_Input_SnesMouseMode != INPUT_SNES_MOUSE_USB)
        return;

    if (!_Input_bMouseRpcReady)
    {
        _Input_bMouseConnected = FALSE;
        return;
    }

    if (!_Input_bMouseConnected)
    {
        if (_Input_MouseEnumCountdown > 0)
        {
            _Input_MouseEnumCountdown--;
            return;
        }
        if (_Input_MouseRpcCall(PS2MOUSE_ENUM, &nDevices, sizeof(nDevices)) == 0)
            _Input_bMouseConnected = nDevices > 0 ? TRUE : FALSE;
        _Input_MouseEnumCountdown = INPUT_MOUSE_ENUM_PERIOD;
        if (!_Input_bMouseConnected)
            return;
    }

    memset(&data, 0, sizeof(data));
    if (_Input_MouseRpcCall(PS2MOUSE_READ, &data, sizeof(data)) < 0)
    {
        _Input_bMouseConnected = FALSE;
        _Input_MouseEnumCountdown = 0;
        return;
    }

    if (bCaptureMotion)
    {
        _Input_MouseDeltaX = (Int32)data.x;
        _Input_MouseDeltaY = (Int32)data.y;
        /* AURORA_IRIXXXX_922543_MOUSE_BUTTONS_20260829
         * Preserve the four low HID button bits for PicoDrive's Mega Mouse.
         * Native SNES mouse still masks its own input back to the real 2 buttons. */
        _Input_MouseButtons = (Uint32)data.buttons & 0x0FU;
    }
}

/* Centred-stick value reported by libpad when the controller is digital
   only or when the analog stick is at rest. */
#define INPUT_ANALOG_CENTER  (0x80)

/* Half range of motion that we ignore around the centre. ~37% of the full
   half-range; matches the value used by InfinityStation/uLaunchELF style
   menus and feels comfortable on real DualShock pads, while still being
   loose enough that worn analog sticks register a deflection reliably. */
#define INPUT_ANALOG_DEADZONE (0x30)

/* A pure form of the old InputGetPadDpadFromAnalog calculation. Keeping it
   here lets InputPoll compute the result exactly once when axes actually
   change instead of every time frontend/gameplay asks for the same value. */
static _INLINE Uint32 _InputAnalogAxesToDpad(Uint8 ljoy_h, Uint8 ljoy_v)
{
    Uint32 dpad = 0;

    if ((int)ljoy_h < (INPUT_ANALOG_CENTER - INPUT_ANALOG_DEADZONE)) dpad |= PAD_LEFT;
    if ((int)ljoy_h > (INPUT_ANALOG_CENTER + INPUT_ANALOG_DEADZONE)) dpad |= PAD_RIGHT;
    if ((int)ljoy_v < (INPUT_ANALOG_CENTER - INPUT_ANALOG_DEADZONE)) dpad |= PAD_UP;
    if ((int)ljoy_v > (INPUT_ANALOG_CENTER + INPUT_ANALOG_DEADZONE)) dpad |= PAD_DOWN;

    return dpad;
}

static Uint8 _Input_PadPort[INPUT_MAXPADS][2] =
{
    {0, 0},
    {1, 0},
    {1, 1},
    {1, 2},
    {1, 3},
};

static int _Input_GetPadState(int port, int slot)
{
    if (_Input_bXPad)
        return xpadGetState(port, slot);

    return padGetState(port, slot);
}

static unsigned char _Input_GetReqState(int port, int slot)
{
    if (_Input_bXPad)
        return xpadGetReqState(port, slot);

    return padGetReqState(port, slot);
}

static int _Input_InfoMode(int port, int slot, int infoMode, int index)
{
    if (_Input_bXPad)
        return xpadInfoMode(port, slot, infoMode, index);

    return padInfoMode(port, slot, infoMode, index);
}

/* Wait for a previously-issued padSetMainMode / padEnterPressMode /
 * padSetActAlign / ... request to actually finish processing on the
 * IOP side.  Without this, calling code that immediately calls another
 * setter or reads padInfoMode can hit a race where the pad is still
 * in PAD_RSTAT_BUSY and the next op either no-ops or smashes the
 * pending one.
 *
 * On real PS2 hardware (PR feedback from a DualShock 2 retail user)
 * the symptom of skipping this wait is exactly what we see: the pad
 * is opened, the digital negotiation completes, but the analog mode
 * lock that we asked for in _Input_InitPad never lands -- the pad
 * stays in digital mode 0x4 instead of climbing to dualshock 0x7.
 *
 * Timeout is generous (~5 s) so that a misbehaving pad does not
 * freeze the whole boot, matching the existing _Input_WaitPadReady
 * policy.  PCSX2 / NetherSX2 complete the RPC in microseconds; real
 * PS2 hardware takes a frame or two, never seconds. */
static void _Input_WaitReqComplete(int port, int slot)
{
    int tm = 50; /* 50 * 100ms = 5s upper bound */
    unsigned char st;

    do
    {
        st = _Input_GetReqState(port, slot);
        if (st == PAD_RSTAT_COMPLETE || st == PAD_RSTAT_FAILED)
            break;
        usleep(100 * 1000);
    } while (--tm > 0);
}

/* Wait until the pad finishes its libpad initialisation handshake.
 *
 * Original implementation used WaitForNextVRstart(1) between polls,
 * which depends on the iaddis hw.s INTC #3 (VBlank Start) handler
 * incrementing a counter. The gsKit migration installs its own INTC
 * #3 handler via gsKit_add_vsync_handler. Depending on the order in
 * which the two handlers end up registered on the actual chip, the
 * iaddis counter can stop being incremented and WaitForNextVRstart
 * spins forever -- which manifested as boot stalls and as the pad
 * "not responding" on real PS2 hardware while emulators (PCSX2 /
 * NetherSX2) booted fine because their INTC chaining happens to keep
 * both handlers happy.
 *
 * The picodrive PS2 port (irixxxx fork) avoids this whole class of
 * problems by polling padGetState() with a plain usleep() between
 * tries plus a hard timeout, which is independent of any vsync /
 * interrupt handler. Mirror that approach here.
 *
 * Exit conditions:
 *   - PAD_STATE_STABLE   : pad finished init, ready to read
 *   - PAD_STATE_DISCONN  : no pad on this port/slot, give up
 *   - timeout (~5s)      : pad never settled, give up rather than
 *                          freezing the whole boot
 *
 * Note: the previous code also exited on PAD_STATE_FINDCTP1, which is
 * an *intermediate* state ("finding controller type, pass 1") -- the
 * pad is still negotiating. Treating it as "ready" causes subsequent
 * padRead() calls to return zero buttons even with the pad connected,
 * which is consistent with the "menu shows up but controller does not
 * respond" symptom on CRT (Yamark).
 */
static void _Input_WaitPadReady(int port, int slot)
{
    int ret;
    int tm = 50; /* 50 * 100ms = 5s upper bound */

    do
    {
        ret = _Input_GetPadState(port, slot);
        /* Like OPL's waitPadReady(): FINDCTP1 is a legitimately
           readable state, not just STABLE.  Treating it as "still
           busy" made a pad that settles in FINDCTP1 burn the whole
           5 s timeout on real hardware. */
        if (ret == PAD_STATE_STABLE || ret == PAD_STATE_FINDCTP1 || ret == PAD_STATE_DISCONN)
            break;
        usleep(100 * 1000);
    } while (--tm > 0);
}

static int _Input_InitPad(int port, int slot, void *buffer)
{
    int ret;
    int mode;
    const char *mode_name;

    if (_Input_bXPad)
        ret = xpadPortOpen(port, slot, buffer);
    else
        ret = padPortOpen(port, slot, buffer);

    if (ret == 0)
    {
        printf("Input: failed to open pad port=%d slot=%d\n", port, slot);
        return -1;
    }

    /* Block until the initial digital-mode handshake finishes (or the
       pad reports definitively disconnected / we hit the 5s timeout). */
    _Input_WaitPadReady(port, slot);

    /* Ask the pad to enter DualShock mode and lock that mode so the
       user pressing the ANALOG button does not toggle us back to
       digital.  Pads that do not support DualShock silently stay in
       digital mode after this request; that is fine, we just don't
       get analog stick deflection for those.

       The previous incantation called xpadExitPressMode before the
       SetMainMode, which is a no-op on a freshly opened pad (press
       mode was never entered) and just slows boot.  Drop it. */
    /* Like OPL's initializePad(): only request DualShock if the pad
       actually advertises it in its mode table.  Digital-only pads
       (and many third-party pads / guns) have no DualShock entry, so
       locking DualShock on them just wastes boot time and can leave
       the pad in a confused state.  PAD_TYPE_DUALSHOCK == 0x7. */
    {
        int n_modes = _Input_InfoMode(port, slot, PAD_MODETABLE, -1);
        int has_ds  = 0;
        int i;

        for (i = 0; i < n_modes; i++)
        {
            if (_Input_InfoMode(port, slot, PAD_MODETABLE, i) == PAD_TYPE_DUALSHOCK)
            {
                has_ds = 1;
                break;
            }
        }

        if (has_ds)
        {
            if (_Input_bXPad)
                xpadSetMainMode(port, slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);
            else
                padSetMainMode(port, slot, PAD_MMODE_DUALSHOCK, PAD_MMODE_LOCK);

            /* CRITICAL on real PS2 hardware: padSetMainMode dispatches
               an RPC to the IOP and returns immediately.  The pad sits
               in PAD_RSTAT_BUSY for ~1-2 frames while the IOP
               renegotiates the link, then transitions to COMPLETE (or
               FAILED).  If we proceed without waiting, the
               _Input_WaitPadReady below sees the pad still STABLE
               (the busy transition hasn't happened yet) and exits
               immediately, leaving the pad in digital mode.

               PCSX2 / NetherSX2 complete the RPC synchronously so this
               race never fires under emulation -- invisible until
               someone tests on actual hardware. */
            _Input_WaitReqComplete(port, slot);

            /* Let the post-command state settle back to STABLE. */
            _Input_WaitPadReady(port, slot);
        }
    }

    /* Surface the negotiated mode in the boot log so users / their
       friends with retail PS2s can confirm at a glance whether we
       successfully went DualShock.  0x7 == DS2, 0x4 == digital,
       anything else (KONAMI gun, joystick, NAMCO gun) we still
       accept and forward digital bits from. */
    mode = _Input_InfoMode(port, slot, PAD_MODECURID, 0);
    switch (mode)
    {
        case 0x7: mode_name = "DS2/analog"; break;
        case 0x4: mode_name = "digital";    break;
        case 0x5: mode_name = "joystick";   break;
        case 0x3: mode_name = "KONAMI-gun"; break;
        case 0x6: mode_name = "NAMCO-gun";  break;
        default:  mode_name = "unknown";    break;
    }
    printf("Input: pad p=%d s=%d opened, mode=0x%X (%s)\n",
           port, slot, mode, mode_name);

    return 0;
}

Bool InputIsPadConnected(Uint32 uPad)
{
    if (uPad >= (Uint32)_Input_nPads)
        return FALSE;

    return _Input_bPadConnected[uPad] ? TRUE : FALSE;
}

Uint32 InputGetPadData(Uint32 uPad)
{
    if (!InputIsPadConnected(uPad))
        return 0;

    return _Input_PadData[uPad];
}

/* AURORA_CODEPATH_SIMPLIFY_V5_20260927
 * InputPoll is the sole writer during a frontend tick. Read both values after
 * one bounds/connection test instead of making MainLoopProcess cross this TU
 * repeatedly through three small getters for every pad. */
Bool InputGetPadSnapshot(Uint32 uPad, Uint32 *pData, Uint32 *pAnalogDpad)
{
    if (uPad >= (Uint32)_Input_nPads || !_Input_bPadConnected[uPad])
    {
        *pData = 0;
        *pAnalogDpad = 0;
        return FALSE;
    }

    *pData = _Input_PadData[uPad];
    *pAnalogDpad = _Input_PadAnalogDpad[uPad];
    return TRUE;
}

/* AURORA_QN_ARKANOID_ANALOG_V2_20260828 */
Uint32 InputGetPadAnalog(Uint32 uPad)
{
    if (!InputIsPadConnected(uPad))
        return 0x80808080U;

    return _Input_PadAnalog[uPad];
}

Bool InputIsMouseConnected(void)
{
    return _Input_bMouseConnected;
}

/* AURORA_CONTROLLER_SNES_MOUSE_V1
 * Convert P1 into relative SNES Mouse motion without adding persistent
 * emulation state. Analog motion is proportional and capped at four host
 * counts/frame; the d-pad is a precise two-count override. The SNES mouse
 * core still applies its authentic speed state afterwards. */
#define INPUT_PAD_MOUSE_DEADZONE    18
#define INPUT_PAD_MOUSE_DIVISOR     28
#define INPUT_PAD_MOUSE_DPAD_STEP    2

static Int32 _InputPadMouseAxisDelta(Int32 uAxis)
{
    Int32 d = uAxis - 0x80;
    Int32 mag;

    if (d > -INPUT_PAD_MOUSE_DEADZONE &&
        d <  INPUT_PAD_MOUSE_DEADZONE)
        return 0;

    if (d < 0)
    {
        mag = -d - INPUT_PAD_MOUSE_DEADZONE;
        mag = (mag + INPUT_PAD_MOUSE_DIVISOR - 1) /
              INPUT_PAD_MOUSE_DIVISOR;
        if (mag > 4) mag = 4;
        return -mag;
    }

    mag = d - INPUT_PAD_MOUSE_DEADZONE;
    mag = (mag + INPUT_PAD_MOUSE_DIVISOR - 1) /
          INPUT_PAD_MOUSE_DIVISOR;
    if (mag > 4) mag = 4;
    return mag;
}

static void _InputGetControllerMouseData(Int32 *pDeltaX,
                                         Int32 *pDeltaY,
                                         Uint32 *pButtons)
{
    Uint32 packed = _Input_PadAnalog[0];
    Uint32 pad = InputGetPadData(0);
    Int32 dx = _InputPadMouseAxisDelta(
        (Int32)((packed >> 16) & 0xff));
    Int32 dy = _InputPadMouseAxisDelta(
        (Int32)((packed >> 24) & 0xff));
    Uint32 buttons = 0;

    if (pad & PAD_LEFT)
        dx = -INPUT_PAD_MOUSE_DPAD_STEP;
    else if (pad & PAD_RIGHT)
        dx = INPUT_PAD_MOUSE_DPAD_STEP;

    if (pad & PAD_UP)
        dy = -INPUT_PAD_MOUSE_DPAD_STEP;
    else if (pad & PAD_DOWN)
        dy = INPUT_PAD_MOUSE_DPAD_STEP;

    if (pad & PAD_CROSS)
        buttons |= 0x01U;
    if (pad & PAD_CIRCLE)
        buttons |= 0x02U;

    if (pDeltaX) *pDeltaX = dx;
    if (pDeltaY) *pDeltaY = dy;
    if (pButtons) *pButtons = buttons;
}

void InputGetMouseData(Int32 *pDeltaX, Int32 *pDeltaY, Uint32 *pButtons)
{
    if (_Input_SnesMouseMode == INPUT_SNES_MOUSE_CONTROLLER)
    {
        _InputGetControllerMouseData(pDeltaX, pDeltaY, pButtons);
        return;
    }

    if (pDeltaX) *pDeltaX = _Input_MouseDeltaX;
    if (pDeltaY) *pDeltaY = _Input_MouseDeltaY;
    if (pButtons) *pButtons = _Input_MouseButtons;
}

void InputSnesMouseSetMode(InputSnesMouseModeE eMode)
{
    if (eMode < INPUT_SNES_MOUSE_OFF ||
        eMode >= INPUT_SNES_MOUSE_MODE_NUM)
        eMode = INPUT_SNES_MOUSE_OFF;

    _Input_SnesMouseMode = eMode;
    InputMouseClearSnapshot();
    if (eMode == INPUT_SNES_MOUSE_USB && !_Input_bMouseRpcReady)
        _Input_MouseInit();
    if (eMode != INPUT_SNES_MOUSE_USB)
        _Input_MouseEnumCountdown = 0;
}

InputSnesMouseModeE InputSnesMouseGetMode(void)
{
    return _Input_SnesMouseMode;
}

void InputSnesMouseCycleModeDir(Int32 dir)
{
    Int32 mode;

    if (dir == 0)
        return;

    mode = (Int32)_Input_SnesMouseMode + (dir < 0 ? -1 : 1);
    if (mode < INPUT_SNES_MOUSE_OFF)
        mode = INPUT_SNES_MOUSE_MODE_NUM - 1;
    if (mode >= INPUT_SNES_MOUSE_MODE_NUM)
        mode = INPUT_SNES_MOUSE_OFF;

    InputSnesMouseSetMode((InputSnesMouseModeE)mode);
}

void InputSnesMouseCycleMode(void)
{
    InputSnesMouseCycleModeDir(1);
}

Bool InputSnesMouseShouldUse(void)
{
    if (_Input_SnesMouseMode == INPUT_SNES_MOUSE_CONTROLLER)
        return InputIsPadConnected(0);
    if (_Input_SnesMouseMode == INPUT_SNES_MOUSE_USB)
        return _Input_bMouseRpcReady && _Input_bMouseConnected;
    return FALSE;
}

const char *InputSnesMouseGetModeName(void)
{
    if (_Input_SnesMouseMode == INPUT_SNES_MOUSE_CONTROLLER)
        return InputIsPadConnected(0) ? "Controller" : "Controller (no P1)";
    if (_Input_SnesMouseMode == INPUT_SNES_MOUSE_USB)
    {
        if (!_Input_bMouseRpcReady) return "USB (driver off)";
        return _Input_bMouseConnected ? "USB" : "USB (no mouse)";
    }
    return "Off";
}


Uint32 InputGetPadDpadFromAnalog(Uint32 uPad)
{
    if (!InputIsPadConnected(uPad))
        return 0;

    return _Input_PadAnalogDpad[uPad];
}

void InputInit(Bool bXLib)
{
    int iPad;

    _Input_bXPad = bXLib;
    _Input_nPads = bXLib ? INPUT_MAXPADS : 2;

    memset(_Input_PadData, 0, sizeof(_Input_PadData));
    memset(_Input_PadAnalogDpad, 0, sizeof(_Input_PadAnalogDpad));
    memset(_Input_bPadConnected, 0, sizeof(_Input_bPadConnected));
    memset(_Input_PadBuf, 0, sizeof(_Input_PadBuf));
    for (iPad = 0; iPad < INPUT_MAXPADS; iPad++)
    {
        _Input_PadAnalog[iPad] = 0x80808080U; /* both sticks centred */
    }

    for (iPad = 0; iPad < _Input_nPads; iPad++)
    {
        _Input_InitPad(_Input_PadPort[iPad][0],
                       _Input_PadPort[iPad][1],
                       _Input_PadBuf[iPad]);
    }

    /* AURORA_MOUSE_EXPLICIT_V3: Off/Controller do no mouse RPC.
       Re-init USB only if it was already an explicit persisted runtime mode. */
    _Input_bInitialized = TRUE;
    if (_Input_SnesMouseMode == INPUT_SNES_MOUSE_USB)
        _Input_MouseInit();
}

void InputShutdown(void)
{
    int iPad;

    if (!_Input_bInitialized)
        return;

    for (iPad = 0; iPad < _Input_nPads; iPad++)
    {
        if (_Input_bXPad)
            xpadPortClose(_Input_PadPort[iPad][0], _Input_PadPort[iPad][1]);
        else
            padPortClose(_Input_PadPort[iPad][0], _Input_PadPort[iPad][1]);
    }

    memset(_Input_PadData, 0, sizeof(_Input_PadData));
    memset(_Input_PadAnalogDpad, 0, sizeof(_Input_PadAnalogDpad));
    memset(_Input_bPadConnected, 0, sizeof(_Input_bPadConnected));
    for (iPad = 0; iPad < INPUT_MAXPADS; iPad++)
    {
        _Input_PadAnalog[iPad] = 0x80808080U;
    }

    _Input_nPads = 0;
    _Input_bMouseRpcReady = FALSE;
    _Input_bMouseConnected = FALSE;
    _Input_MouseDeltaX = _Input_MouseDeltaY = 0;
    _Input_MouseButtons = 0;
    _Input_MouseAuroraStatus = 0;
    memset(&_Input_MouseRpc, 0, sizeof(_Input_MouseRpc));
    _Input_bInitialized = FALSE;
}

void InputPoll(void)
{
    int iPad;

    if (!_Input_bInitialized)
        return;

    for (iPad = 0; iPad < _Input_nPads; iPad++)
    {
        int state;
        int rd;
        Uint32 uData = 0;
        struct padButtonStatus padStatus;

        state = _Input_GetPadState(_Input_PadPort[iPad][0],
                                   _Input_PadPort[iPad][1]);

        /* Treat "definitely gone" as disconnected; everything else is
           a candidate for padRead() and the read itself is the ground
           truth.

           PR #91 tightened the original "PAD_STATE_STABLE ||
           PAD_STATE_FINDCTP1" check to STABLE-only on the theory that
           FINDCTP1 returns garbage. That is true at boot, but a real
           DualShock 2 can transiently dip into PAD_STATE_EXECCMD /
           PAD_STATE_FINDPAD during normal use -- e.g. after we issued
           the analog-mode / press-mode init request from
           _Input_InitPad. On Yamark's CRT setup the pad lived in one
           of those intermediate states for the whole session and never
           climbed back to STABLE, so the strict gate produced the
           "menu shows up but controller does not respond" symptom.

           picodrive's PS2 port (irixxxx fork, platform/ps2/in_ps2.c
           in_ps2_update) sidesteps this whole class of bug by calling
           padRead() unconditionally and using the return value as the
           ground-truth signal: padRead returns 0 only when the pad has
           no fresh data, so we can use that as the connected-but-idle
           indicator without involving padGetState at all. Mirror that
           pattern here. */
        if (state == PAD_STATE_DISCONN)
        {
            if (_Input_bPadConnected[iPad] == 1)
                printf("Input: Pad %d removed!\n", iPad + 1);

            _Input_bPadConnected[iPad] = 0;
            _Input_PadData[iPad] = 0;
            _Input_PadAnalog[iPad] = 0x80808080U;
            _Input_PadAnalogDpad[iPad] = 0;
            continue;
        }

        memset(&padStatus, 0, sizeof(padStatus));

        if (_Input_bXPad)
            rd = xpadRead(_Input_PadPort[iPad][0], _Input_PadPort[iPad][1], &padStatus);
        else
            rd = padRead(_Input_PadPort[iPad][0], _Input_PadPort[iPad][1], &padStatus);

        /* padRead returns 0 when the pad is connected but has no fresh
           data this frame (still negotiating, transient bus glitch,
           rumble busy, ...). Hold the previous frame's button state
           and skip the redraw rather than zeroing it out, so a pad
           that briefly drops out of STABLE does not lose a button
           press that the game already observed. Same approach
           picodrive uses. */
        if (rd == 0)
        {
            _Input_bPadConnected[iPad] = 1;
            continue;
        }

        /* Reject "ghost-pressed" reads.
         *
         * libpad reports buttons in inverted logic: each bit is 1 when
         * the button is released and 0 when pressed.  So padStatus.btns
         * == 0 would mean "every single button -- including UP, DOWN,
         * LEFT and RIGHT simultaneously -- is held this frame", which is
         * physically impossible on the D-pad and only ever surfaces in
         * one specific situation: the libpad DMA buffer is still in its
         * zero-initialised state and padRead handed us stale bytes.
         *
         * The PS2SDK libpad has a hard fast-path where padRead returns
         * non-zero as soon as padPortOpen + STABLE complete, even if the
         * IOP RPC hasn't actually pushed real button data into the
         * 256-byte ring yet.  When that happens, the still-zero btns
         * field round-trips into uData = 0xffff ^ 0 = 0xffff (== "every
         * button held"), the menu state machine sees one frame of
         * spurious PAD_DOWN, and the cursor that should land on the
         * default option (e.g. "1P GAME" in Super Mario Kart) advances
         * once before the user has touched anything.  Other emulators
         * never observe this because they keep their pad layer in
         * "ignore" mode until they get a real RPC response, which the
         * SCEI ref driver does implicitly.
         *
         * Detection is unambiguous: an honest "no buttons pressed" read
         * sets btns to 0xffff, so btns == 0 cannot be confused with any
         * legitimate input.  Treat it the same way we treat an rd == 0
         * read above: keep the previous frame's PadData/PadAnalog
         * (which start out zeroed in InputInit) and try again next
         * frame.  By the time the SNES core has booted the cartridge
         * far enough to draw a menu (~tens of frames in), libpad has
         * filled the buffer for real and we start propagating input
         * normally.
         *
         * padStatus.mode == 0 is the same situation in field form: a
         * properly initialised libpad fills it with 0x4 (digital) /
         * 0x7 (DualShock 2) / etc., never 0.  Belt-and-braces. */
        if ((Uint16)padStatus.btns == 0 || padStatus.mode == 0)
        {
            _Input_bPadConnected[iPad] = 1;
            continue;
        }

        if (_Input_bPadConnected[iPad] == 0)
        {
            printf("Input: Pad %d inserted!\n", iPad + 1);
        }
        _Input_bPadConnected[iPad] = 1;

#ifdef _EE
        uData = 0xffffU ^ (Uint32)padStatus.btns;
#else
        uData = 0;
#endif

        /* Keep _Input_PadData strictly digital so SNES/NES emulation
           never sees synthesised d-pad bits from the analog stick. The
           analog deflection is exposed separately via
           InputGetPadDpadFromAnalog so the menu/UI layer can opt in. */
        _Input_PadData[iPad] = uData;

        /* Only trust the analog axes when the pad is actually in an
           analog mode.  padStatus.mode packs the controller terminal
           id in its high nibble (0x7 == DualShock 2 / analog).  A
           digital-only pad -- or a DualShock that has not finished
           negotiating analog yet -- returns a short report where
           padRead does NOT fill the ljoy/rjoy bytes, so they stay 0
           from the memset above.  0 reads as a full UP+LEFT deflection,
           which used to inject a permanent phantom UP+LEFT into the
           menu on real PS2 (emulators always present an analog-centred
           pad, which is exactly why the bug was invisible under
           emulation).  Centre the axes for non-analog pads so the
           synthesiser stays neutral. */
        if ((padStatus.mode >> 4) == 0x7)
        {
            _Input_PadAnalog[iPad] =
                  ((Uint32)padStatus.ljoy_v << 24)
                | ((Uint32)padStatus.ljoy_h << 16)
                | ((Uint32)padStatus.rjoy_v << 8)
                | ((Uint32)padStatus.rjoy_h);
            _Input_PadAnalogDpad[iPad] =
                _InputAnalogAxesToDpad(padStatus.ljoy_h, padStatus.ljoy_v);
        }
        else
        {
            _Input_PadAnalog[iPad] = 0x80808080U; /* centred -> no phantom dpad */
            _Input_PadAnalogDpad[iPad] = 0;
        }
    }
}
