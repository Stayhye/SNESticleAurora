
#ifndef _SNIO_H
#define _SNIO_H

#include "emuinput.h"
#include "snesreg.h"
#include "snspctimer.h"

#define SNESIO_JOY_R		0x0010
#define SNESIO_JOY_L		0x0020
#define SNESIO_JOY_X		0x0040
#define SNESIO_JOY_A		0x0080
#define SNESIO_JOY_RIGHT	0x0100
#define SNESIO_JOY_LEFT		0x0200
#define SNESIO_JOY_DOWN		0x0400
#define SNESIO_JOY_UP		0x0800
#define SNESIO_JOY_START	0x1000
#define SNESIO_JOY_SELECT	0x2000
#define SNESIO_JOY_Y		0x4000
#define SNESIO_JOY_B		0x8000

/* AURORA_CODEPATH_SIMPLIFY_V5_20260927
 * The high byte of Aurora's legacy SNES carrier is, bit-for-bit, the
 * reverse of the ordinary NES A/B/Select/Start/Up/Down/Left/Right byte.
 * Keep one 16-byte nibble table in snio.cpp and inline only two tiny lookups
 * in the host bridges instead of repeating eight conditionals per mapper. */
extern const Uint8 SnesIOReverseNibble[16];

_INLINE Uint8 SnesIOPadToNes8(Uint16 pad)
{
	const Uint32 hi = (Uint32)pad >> 8;
	return (Uint8)(
		(Uint32)SnesIOReverseNibble[hi >> 4] |
		((Uint32)SnesIOReverseNibble[hi & 0x0FU] << 4));
}

/* Pairs are bits 4/5 and 6/7. 11 must become 00, all other combinations
 * stay unchanged. This is exactly the old two conditional clears. */
_INLINE Uint8 SnesIOFilterOppositeDirections8(Uint8 value)
{
	const Uint32 both =
		((Uint32)value & ((Uint32)value << 1)) & 0xA0U;
	return (Uint8)((Uint32)value & ~(both | (both >> 1)));
}

#define SNESIO_DEVICE_NUM 5

/* AURORA_SNES_TURBOFILE_V4_20260829
 * One physical ASCII Turbo File Twin on controller port 2.
 * The Twin can be switched between STF and backward-compatible TFII mode. */
typedef enum SnesTurboFileModeE
{
	SNES_TURBOFILE_NONE = 0,
	SNES_TURBOFILE_TWIN_TFII,
	SNES_TURBOFILE_TWIN_STF
} SnesTurboFileModeE;

void SnesTurboFileSelectForCRC(Uint32 crc);
void SnesTurboFileResetBus(void);
Bool SnesTurboFileEnabled(void);
SnesTurboFileModeE SnesTurboFileGetMode(void);
Int32 SnesTurboFileGetBytes(void);
Uint8 *SnesTurboFileGetData(void);
Bool SnesTurboFileDirty(void);
void SnesTurboFileClearDirty(void);

struct SnesIORegsT
{
	SnesReg16T  	wrdiv;
	SnesReg16T  	rdmpy;
	SnesReg16T  	rddiv;
	SnesReg8T  		wrmpya;
	SnesReg8T  		wrmpyb;
	SnesReg8T  		wrdivb;

	SnesReg8T  		nmitimen;
	SnesReg16T		vtime;
	SnesReg16T		htime;
	SnesReg8T		timeup;
	SnesReg8T		rdnmi;
	SnesReg8T		hvbjoy;
	SnesReg8T		memsel;
	SnesReg8T		wrio;
	SnesReg32T		wmadd;

	SnesReg8T		joydata;
	SnesReg16T		joy1;
	SnesReg16T		joy2;
	SnesReg16T		joy3;
	SnesReg16T		joy4;

	Uint16			joyserial[SNESIO_DEVICE_NUM];	// latched joypad data
};

class SnesIO
{
	Emu::SysInputT	m_Input;

	/* AURORA_SNES_MOUSE_V1_5
	 * Port-1 mouse state stays outside SysInputT so NES/netplay/movie layouts
	 * and the legacy save-state payload size remain untouched. */
	Bool			m_bMouse0Connected;
	Int32			m_nMousePendingX;
	Int32			m_nMousePendingY;
	Uint32			m_uMouseHostButtons;
	Uint32			m_uMousePacket;
	Uint8			m_uMouseSpeed;
	Uint8			m_uMouseReadIndex;

	Uint8			ReadSerialPad(Uint32 uPad);
	void			ShiftSerialPad(Uint32 uPad);
	Uint8			ReadSerialMouse0();
	void			CaptureMousePacket();
	void			UpdateMousePacketSpeed();
	Uint16			GetMouseAutoWord() const;

public:
	SnesIORegsT		m_Regs;

public:
	SnesIO();

	void	Reset();
	void	SaveState(struct SNStateIOT *pState);
	void	RestoreState(struct SNStateIOT *pState);

	void	LatchInput(Emu::SysInputT *pInput);
	void	SetMouseInput(Bool bConnected, Int32 nDeltaX, Int32 nDeltaY, Uint32 uButtons);
	void	WriteSerial(Uint8 uData);
	Uint8	ReadSerial0();
	Uint8	ReadSerial1();
	void	UpdateJoyPads();
};

#endif
