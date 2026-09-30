

#include <stdio.h>
#include <string.h>
#include "types.h"
#include "snio.h"

#define SNIO_VERSION_5A22 (0x02)

/* AURORA_CODEPATH_SIMPLIFY_V5_20260927
 * Exact 4-bit reversal table shared by the Aurora-side input bridges. */
const Uint8 SnesIOReverseNibble[16] =
{
	0, 8, 4, 12, 2, 10, 6, 14,
	1, 9, 5, 13, 3, 11, 7, 15
};

/* AURORA_SNES_TURBOFILE_V4_20260829
 *
 * One ASCII Turbo File Twin on SFC controller port 2.
 *
 * Physical backing store:
 *   $00000-$07FFF : four TFII-compatible banks (4 x 8 KiB)
 *   $08000-$27FFF : STF area (128 KiB)
 *
 * The real Twin has a physical selector for STF vs one of four TFII banks.
 * Aurora automatically selects STF/TFII by exact game CRC; TFII bank 1 is
 * the default physical switch position. The remaining three banks are kept
 * in the same persistent image for future UI selection without changing the
 * file format.
 *
 * Protocol:
 *   $4016 D0 = strobe
 *   $4017 D0/D1 = status/data
 *   WRIO $4201 D7 = CPU -> peripheral data line
 *
 * STF behavior follows the hardware-tested MesenCE implementation. TFII is
 * the Twin's backwards-compatible emulation of Turbo File II + SFC Adapter.
 */

enum
{
	SNES_TFII_BANK_BYTES = 0x2000,
	SNES_TFII_BANKS      = 4,
	SNES_TFII_BYTES      = SNES_TFII_BANK_BYTES * SNES_TFII_BANKS,
	SNES_STF_BYTES       = 128 * 1024,
	SNES_STF_BASE        = SNES_TFII_BYTES,
	SNES_TWIN_BYTES      = SNES_TFII_BYTES + SNES_STF_BYTES
};

static SnesTurboFileModeE s_SnesTurboFileMode = SNES_TURBOFILE_NONE;
static Uint8 s_SnesTurboFileTwin[SNES_TWIN_BYTES];
static Bool s_SnesTurboFileInitialized = FALSE;
static Bool s_SnesTurboFileDirty = FALSE;
/* Physical TFII selector: bank 0 == switch position 1. */
static Uint8 s_SnesTurboFileTfiiBank = 0;

static Bool s_SnesTurboFileStrobe = FALSE;

/* TFII compatibility state. */
static Uint16 s_SnesTfiiPosition = 0;
static Uint8 s_SnesTfiiUnlockCounter = 0;
static Bool s_SnesTfiiUnlocked = FALSE;
static Uint32 s_SnesTfiiStateBuffer = 0;

/* STF state. */
static Uint32 s_SnesStfPosition = 0;
static Uint8 s_SnesStfCurrentByte = 0;
static Uint8 s_SnesStfMostRecentByte = 0xFF;
static Bool s_SnesStfWriteMode = FALSE;
static Bool s_SnesStfReadMode = FALSE;
static Bool s_SnesStfFirstAccess = FALSE;
static Bool s_SnesStfDidReadWithStrobe = FALSE;
static Bool s_SnesStfDidWriteAnything = FALSE;
static Uint32 s_SnesStfNewCommand = 0;
static Uint32 s_SnesStfStateBuffer = 0;

static void _SnesTurboFileInit(void)
{
	if (!s_SnesTurboFileInitialized)
	{
		memset(s_SnesTurboFileTwin, 0xFF, sizeof(s_SnesTurboFileTwin));
		s_SnesTurboFileInitialized = TRUE;
	}
}

static Uint32 _SnesTfiiBankBase(void)
{
	return (Uint32)(s_SnesTurboFileTfiiBank & 3U) *
	       (Uint32)SNES_TFII_BANK_BYTES;
}

static void _SnesTfiiRefreshState(void)
{
	/* Controller type $E, disambiguated as TFII by $FF. */
	s_SnesTfiiStateBuffer = 0x00FF7000U;
	if (s_SnesTfiiUnlocked)
		s_SnesTfiiStateBuffer |= 1U << 11;
}

static void _SnesStfRefreshState(void)
{
	/* Controller type $E, disambiguated as STF by $FE.
	 * Battery-present, write-protect off, capacity nibble 0 = 128 KiB. */
	s_SnesStfStateBuffer = 0x007F7000U;
	if (s_SnesStfWriteMode && !s_SnesStfDidWriteAnything)
		s_SnesStfStateBuffer |= 1U << 24;
	if (s_SnesStfReadMode)
		s_SnesStfStateBuffer |= 1U << 25;
}

void SnesTurboFileResetBus(void)
{
	s_SnesTurboFileStrobe = FALSE;

	s_SnesTfiiPosition = 0;
	s_SnesTfiiUnlockCounter = 0;
	s_SnesTfiiUnlocked = FALSE;
	_SnesTfiiRefreshState();

	s_SnesStfPosition = 0;
	s_SnesStfCurrentByte = 0;
	s_SnesStfMostRecentByte = 0xFF;
	s_SnesStfWriteMode = FALSE;
	s_SnesStfReadMode = FALSE;
	s_SnesStfFirstAccess = FALSE;
	s_SnesStfDidReadWithStrobe = FALSE;
	s_SnesStfDidWriteAnything = FALSE;
	s_SnesStfNewCommand = 0;
	_SnesStfRefreshState();
}

static SnesTurboFileModeE _SnesTurboFileModeForCRC(Uint32 crc)
{
	/* STF-capable titles. Games supporting both protocols live here,
	 * so STF has priority exactly as requested. */
	switch (crc)
	{
		case 0x75EEEB3FU: /* Derby Stallion III v1.0 */
		case 0x7E97AE6AU: /* Derby Stallion III v1.1 */
		case 0x5F3E5279U: /* Derby Stallion III v1.2 */
		case 0x36768B55U: /* Derby Stallion III v1.4 */
		case 0x19BDCB19U: /* Derby Stallion 96 */
		case 0x525FFB26U: /* Derby Stallion 98 */
		case 0x4CD45939U: /* Daisenryaku Expert WWII */
		case 0x125A0C22U: /* Dark Law */
		case 0xAF415C24U: /* Ganpuru / Gunman's Proof */
		case 0xB17E95D4U: /* Mini Yonku Shining Scorpion */
		case 0xC79C123FU: /* Ongaku Tsukuru Kanadeeru */
		case 0xE20870EEU: /* RPG Tsukuru: Super Dante */
		case 0xE0BD6C71U: /* RPG Tsukuru: Super Dante - translated */
		case 0x675B6382U: /* RPG Tsukuru 2 */
		case 0x916D9C92U: /* RPG Tsukuru 2 - translated */
		case 0x5EBF7246U: /* Sound Novel Tsukuru */
		case 0xDAF285A5U: /* Tactics Ogre v1.0 */
		case 0x271E1D07U: /* Tactics Ogre v1.1 */
		case 0x12F0A699U: /* Tactics Ogre v1.2 */
		case 0xC17ECBA5U: /* Wizardry VI */
			return SNES_TURBOFILE_TWIN_STF;
		default:
			break;
	}

	/* Older Adapter-compatible games run on the same Twin in TFII mode.
	 * The known Ardy Lightfoot Japanese beta is deliberately excluded. */
	switch (crc)
	{
		case 0x51A5F489U: /* Ardy Lightfoot USA */
		case 0x2AA7777BU: /* Ardy Lightfoot Japan retail */
		case 0xD239398FU: /* Ardy Lightfoot Europe */
		case 0x32E29079U: /* Derby Stallion II */
		case 0xF1110FADU: /* Down the World */
		case 0xBC1A265FU: /* Kakinoki Shogi */
		case 0x547CF7B9U: /* Wizardry V Japan */
			return SNES_TURBOFILE_TWIN_TFII;
		default:
			return SNES_TURBOFILE_NONE;
	}
}

void SnesTurboFileSelectForCRC(Uint32 crc)
{
	SnesTurboFileModeE mode = _SnesTurboFileModeForCRC(crc);

	s_SnesTurboFileMode = mode;
	if (mode != SNES_TURBOFILE_NONE)
		_SnesTurboFileInit();

	SnesTurboFileResetBus();

	if (mode == SNES_TURBOFILE_TWIN_STF)
		printf("[SNES/TurboFile] Twin STF enabled; CRC=%08X\n",
		       (unsigned)crc);
	else if (mode == SNES_TURBOFILE_TWIN_TFII)
		printf("[SNES/TurboFile] Twin TFII bank 1 enabled; CRC=%08X\n",
		       (unsigned)crc);
}

Bool SnesTurboFileEnabled(void)
{
	return s_SnesTurboFileMode != SNES_TURBOFILE_NONE ? TRUE : FALSE;
}

SnesTurboFileModeE SnesTurboFileGetMode(void)
{
	return s_SnesTurboFileMode;
}

Int32 SnesTurboFileGetBytes(void)
{
	return SnesTurboFileEnabled() ? SNES_TWIN_BYTES : 0;
}

Uint8 *SnesTurboFileGetData(void)
{
	if (!SnesTurboFileEnabled())
		return NULL;
	_SnesTurboFileInit();
	return s_SnesTurboFileTwin;
}

Bool SnesTurboFileDirty(void)
{
	return SnesTurboFileEnabled() && s_SnesTurboFileDirty ? TRUE : FALSE;
}

void SnesTurboFileClearDirty(void)
{
	s_SnesTurboFileDirty = FALSE;
}

static Uint8 _SnesTurboFileReadTfii(void)
{
	Uint8 output;
	Uint8 bit;
	Uint32 byteIndex;

	_SnesTurboFileInit();

	/* BaseControlDevice::StrobeProcessRead() refreshes while strobe is high,
	 * before this clock updates the unlock state. */
	if (s_SnesTurboFileStrobe)
		_SnesTfiiRefreshState();

	s_SnesTfiiUnlocked =
		(s_SnesTfiiUnlockCounter == 0x0F) ? TRUE : FALSE;
	if (s_SnesTurboFileStrobe)
		s_SnesTfiiUnlockCounter =
			(Uint8)((s_SnesTfiiUnlockCounter + 1) & 0x0F);
	if (!s_SnesTfiiUnlocked)
		s_SnesTfiiPosition = 0;

	output = (Uint8)(s_SnesTfiiStateBuffer & 1U);
	s_SnesTfiiStateBuffer >>= 1;
	s_SnesTfiiStateBuffer |= 1U << 23;

	byteIndex = _SnesTfiiBankBase() +
		((Uint32)s_SnesTfiiPosition >> 3);
	bit = (Uint8)((s_SnesTurboFileTwin[byteIndex] >>
		(s_SnesTfiiPosition & 7)) & 1U);
	output |= (Uint8)(bit << 1);
	return output;
}

static Uint8 _SnesTurboFileReadStf(Uint8 wrio)
{
	Uint8 output = 0;
	Uint8 ioBit = (wrio & 0x80U) ? 1U : 0U;

	_SnesTurboFileInit();

	if (s_SnesTurboFileStrobe)
	{
		s_SnesStfDidReadWithStrobe = TRUE;
		s_SnesStfReadMode = FALSE;
		if (s_SnesStfFirstAccess)
			s_SnesStfWriteMode = FALSE;
	}
	else
	{
		output = (Uint8)(s_SnesStfStateBuffer & 1U);
		s_SnesStfStateBuffer >>= 1;
		s_SnesStfStateBuffer |= 1U << 31;
	}

	s_SnesStfNewCommand =
		(s_SnesStfNewCommand >> 1) | ((Uint32)ioBit << 27);

	if (!(s_SnesStfCurrentByte & 0x01U))
		output |= 0x02U;
	s_SnesStfCurrentByte >>= 1;

	if (s_SnesStfWriteMode && s_SnesTurboFileStrobe && ioBit)
		s_SnesStfCurrentByte |= 0x80U;

	return output;
}

static Uint8 _SnesTurboFileReadPort2(Uint8 wrio)
{
	if (s_SnesTurboFileMode == SNES_TURBOFILE_TWIN_STF)
		return _SnesTurboFileReadStf(wrio);
	if (s_SnesTurboFileMode == SNES_TURBOFILE_TWIN_TFII)
		return _SnesTurboFileReadTfii();
	return 0;
}

static void _SnesTurboFileWriteTfii(Uint8 value, Uint8 wrio)
{
	Bool prevStrobe = s_SnesTurboFileStrobe;
	Bool nextStrobe = (value & 1U) ? TRUE : FALSE;

	s_SnesTurboFileStrobe = nextStrobe;

	if (prevStrobe && !nextStrobe)
	{
		_SnesTfiiRefreshState();

		if (s_SnesTfiiUnlocked)
		{
			Uint32 byteIndex = _SnesTfiiBankBase() +
				((Uint32)s_SnesTfiiPosition >> 3);
			Uint8 mask = (Uint8)(1U << (s_SnesTfiiPosition & 7));
			Uint8 oldValue = s_SnesTurboFileTwin[byteIndex];
			Uint8 newValue;

			if (wrio & 0x80U)
				newValue = (Uint8)(oldValue | mask);
			else
				newValue = (Uint8)(oldValue & (Uint8)~mask);

			if (newValue != oldValue)
			{
				s_SnesTurboFileTwin[byteIndex] = newValue;
				s_SnesTurboFileDirty = TRUE;
			}

			s_SnesTfiiPosition =
				(Uint16)((s_SnesTfiiPosition + 1U) &
				         (SNES_TFII_BANK_BYTES * 8 - 1U));
		}
	}
}

static void _SnesTurboFileWriteStf(Uint8 value)
{
	Bool prevStrobe = s_SnesTurboFileStrobe;
	Bool nextStrobe = (value & 1U) ? TRUE : FALSE;

	s_SnesTurboFileStrobe = nextStrobe;

	if (!prevStrobe && nextStrobe)
	{
		s_SnesStfDidReadWithStrobe = FALSE;
		_SnesStfRefreshState();
	}

	if (prevStrobe && !nextStrobe)
	{
		if (!s_SnesStfWriteMode && !s_SnesStfReadMode)
		{
			s_SnesStfPosition = s_SnesStfNewCommand >> 8;
			if ((s_SnesStfNewCommand & 0xFFU) == 0x24U)
				s_SnesStfReadMode = TRUE;
			else if ((s_SnesStfNewCommand & 0xFFU) == 0x75U)
				s_SnesStfWriteMode = TRUE;

			s_SnesStfFirstAccess = TRUE;
			s_SnesStfDidWriteAnything = FALSE;
			s_SnesStfCurrentByte = s_SnesStfMostRecentByte;
		}
		else
		{
			if (!s_SnesStfFirstAccess)
			{
				if (s_SnesStfWriteMode)
				{
					if (s_SnesStfDidReadWithStrobe)
					{
						if (s_SnesStfPosition < SNES_STF_BYTES)
						{
							Uint32 byteIndex =
								SNES_STF_BASE + s_SnesStfPosition;
							Uint8 oldValue =
								s_SnesTurboFileTwin[byteIndex];
							if (oldValue != s_SnesStfCurrentByte)
							{
								s_SnesTurboFileTwin[byteIndex] =
									s_SnesStfCurrentByte;
								s_SnesTurboFileDirty = TRUE;
							}
						}
						s_SnesStfMostRecentByte = s_SnesStfCurrentByte;
						s_SnesStfDidWriteAnything = TRUE;
					}
					else
						s_SnesStfWriteMode = FALSE;
				}
				else if (s_SnesStfReadMode)
				{
					if (s_SnesStfPosition < SNES_STF_BYTES)
						s_SnesStfMostRecentByte =
							s_SnesTurboFileTwin[
								SNES_STF_BASE + s_SnesStfPosition];
					s_SnesStfCurrentByte = s_SnesStfMostRecentByte;
				}

				s_SnesStfPosition =
					(s_SnesStfPosition + 1U) & 0x000FFFFFU;
			}
			s_SnesStfFirstAccess = FALSE;
		}
		s_SnesStfNewCommand = 0;
	}
}

static void _SnesTurboFileWritePort(Uint8 value, Uint8 wrio)
{
	if (s_SnesTurboFileMode == SNES_TURBOFILE_TWIN_STF)
		_SnesTurboFileWriteStf(value);
	else if (s_SnesTurboFileMode == SNES_TURBOFILE_TWIN_TFII)
		_SnesTurboFileWriteTfii(value, wrio);
}

/* AURORA_SNES_MOUSE_V1_5_PROTOCOL
 * 32-bit serial format: 8x0, R, L, speed[1:0], 0001,
 * Y(sign+7-bit magnitude), X(sign+7-bit magnitude). */
static Int32 _SnesMouseClampRaw(Int32 v)
{
	if (v > 127) return 127;
	if (v < -127) return -127;
	return v;
}

static Uint32 _SnesMouseScaleMagnitude(Int32 raw, Uint8 speed)
{
	Uint32 mag = (Uint32)(raw < 0 ? -raw : raw);

	if (speed == 1)
		mag = (mag * 3U) / 2U;
	else if (speed == 2)
		mag *= 2U;

	if (mag > 127U)
		mag = 127U;
	return mag;
}

void SnesIO::UpdateMousePacketSpeed()
{
	m_uMousePacket &= ~(3U << 20);
	m_uMousePacket |= ((Uint32)(m_uMouseSpeed & 3U)) << 20;
}

void SnesIO::CaptureMousePacket()
{
	Int32 rawX = _SnesMouseClampRaw(m_nMousePendingX);
	Int32 rawY = _SnesMouseClampRaw(m_nMousePendingY);
	Uint32 magX;
	Uint32 magY;

	m_nMousePendingX -= rawX;
	m_nMousePendingY -= rawY;

	magX = _SnesMouseScaleMagnitude(rawX, m_uMouseSpeed);
	magY = _SnesMouseScaleMagnitude(rawY, m_uMouseSpeed);

	m_uMousePacket = 0x00010000U;
	if (m_uMouseHostButtons & 0x02U) m_uMousePacket |= 1U << 23;
	if (m_uMouseHostButtons & 0x01U) m_uMousePacket |= 1U << 22;
	UpdateMousePacketSpeed();

	if (rawY < 0) m_uMousePacket |= 1U << 15;
	m_uMousePacket |= (magY & 0x7fU) << 8;
	if (rawX < 0) m_uMousePacket |= 1U << 7;
	m_uMousePacket |= (magX & 0x7fU);

	m_uMouseReadIndex = 0;
}

Uint8 SnesIO::ReadSerialMouse0()
{
	Uint8 bit;

	if (m_Regs.joydata & 1)
	{
		m_uMouseSpeed = (Uint8)((m_uMouseSpeed + 1) % 3);
		UpdateMousePacketSpeed();
		return 0;
	}

	if (m_uMouseReadIndex >= 32)
		return 1;

	bit = (m_uMousePacket & (0x80000000U >> m_uMouseReadIndex)) ? 1 : 0;
	m_uMouseReadIndex++;
	return bit;
}

Uint16 SnesIO::GetMouseAutoWord() const
{
	Uint16 v = 0x0001;
	if (m_uMouseHostButtons & 0x01U) v |= 0x0040;
	if (m_uMouseHostButtons & 0x02U) v |= 0x0080;
	v |= (Uint16)((m_uMouseSpeed & 3U) << 4);
	return v;
}

void SnesIO::SetMouseInput(Bool bConnected, Int32 nDeltaX, Int32 nDeltaY, Uint32 uButtons)
{
	Bool bWasConnected = m_bMouse0Connected;

	m_bMouse0Connected = bConnected ? TRUE : FALSE;
	if (!m_bMouse0Connected)
	{
		m_nMousePendingX = 0;
		m_nMousePendingY = 0;
		m_uMouseHostButtons = 0;
		m_uMousePacket = 0;
		m_uMouseReadIndex = 0;
		if (bWasConnected)
			m_uMouseSpeed = 0;
		return;
	}

	if (!bWasConnected)
	{
		m_uMouseSpeed = 0;
		m_uMousePacket = 0;
		m_uMouseReadIndex = 0;
		m_nMousePendingX = 0;
		m_nMousePendingY = 0;
	}

	if (nDeltaX > 32767) nDeltaX = 32767;
	if (nDeltaX < -32767) nDeltaX = -32767;
	if (nDeltaY > 32767) nDeltaY = 32767;
	if (nDeltaY < -32767) nDeltaY = -32767;

	if (nDeltaX > 0 && m_nMousePendingX > 32767 - nDeltaX)
		m_nMousePendingX = 32767;
	else if (nDeltaX < 0 && m_nMousePendingX < -32767 - nDeltaX)
		m_nMousePendingX = -32767;
	else
		m_nMousePendingX += nDeltaX;

	if (nDeltaY > 0 && m_nMousePendingY > 32767 - nDeltaY)
		m_nMousePendingY = 32767;
	else if (nDeltaY < 0 && m_nMousePendingY < -32767 - nDeltaY)
		m_nMousePendingY = -32767;
	else
		m_nMousePendingY += nDeltaY;

	m_uMouseHostButtons = uButtons & 0x03U;
}

Uint8 SnesIO::ReadSerialPad(Uint32 uPad)
{
	// read top-most joypad bit
	return (m_Regs.joyserial[uPad] >> 15) & 1;
}

void SnesIO::ShiftSerialPad(Uint32 uPad)
{
	// shift pad data
	m_Regs.joyserial[uPad] <<= 1;

	// if joystick connected
	if (m_Input.uPad[uPad]!=EMUSYS_DEVICE_DISCONNECTED)
	{
		// set connected status
		m_Regs.joyserial[uPad] |= 1;
	}
}

Uint8 SnesIO::ReadSerial0()
{
	Uint32 uData;

	if (m_bMouse0Connected)
		return ReadSerialMouse0();

	uData  = ReadSerialPad(0) << 0;

	// confirmed:
	// if strobe is left on, then bitposition never shifts
	// all bits returned are button B
	if (!(m_Regs.joydata&1))
	{
		ShiftSerialPad(0);
	}

	return uData;
}

Uint8 SnesIO::ReadSerial1()
{
	Uint32 uData;

	/* AURORA_SNES_TURBOFILE_V4_20260829
	 * Turbo File occupies controller port 2 and drives D0/D1 itself. */
	if (SnesTurboFileEnabled())
		return (Uint8)(_SnesTurboFileReadPort2(m_Regs.wrio) | 0x1C);

	// if joypads 2,3,4 are all disconnected then assume no multitap is installed
	if (
		m_Input.uPad[2]==EMUSYS_DEVICE_DISCONNECTED && 
		m_Input.uPad[3]==EMUSYS_DEVICE_DISCONNECTED && 
		m_Input.uPad[4]==EMUSYS_DEVICE_DISCONNECTED
		)
	{
		// no multitap!

		// read serial bit
		uData  = ReadSerialPad(1) << 0;

		// confirmed:
		// if strobe is left on, then bitposition never shifts
		// all bits returned are button B
		if (!(m_Regs.joydata&1))
		{
			ShiftSerialPad(1);
		}

	} else
	{
		// multitap

		// confirmed:
		// if stobe is left on, then bit is returned if multitap is connected
		if (m_Regs.joydata&1)
		{
			// signal presence of multitap
			uData = 0x02;
		}
		else
		{
			// multitap port enabled?
			if (m_Regs.wrio & 0x80)
			{
				// use controllers 2 and 3
				uData  = ReadSerialPad(1) << 0;
				uData |= ReadSerialPad(2) << 1; 

				ShiftSerialPad(1);
				ShiftSerialPad(2);
			} else
			{
				// use controllers 4 and 5
				uData  = ReadSerialPad(3) << 0;
				uData |= ReadSerialPad(4) << 1; 

				ShiftSerialPad(3);
				ShiftSerialPad(4);
			}
		}
	}

	// confirmed:
	// this port always returns with 1C bits on
	// havent tested with multitap yet though
	return uData | 0x1C;
}

void SnesIO::WriteSerial(Uint8 uData)
{
	Bool bOldStrobe = (m_Regs.joydata & 1) ? TRUE : FALSE;
	Bool bNewStrobe = (uData & 1) ? TRUE : FALSE;

	if (SnesTurboFileEnabled())
		_SnesTurboFileWritePort(uData, m_Regs.wrio);

	if (bNewStrobe && !bOldStrobe)
	{
		int iPad;

		for (iPad=0; iPad < SNESIO_DEVICE_NUM; iPad++)
		{
			if (iPad == 0 && m_bMouse0Connected)
			{
				m_Regs.joyserial[iPad] = 0;
			}
			else if (m_Input.uPad[iPad] != EMUSYS_DEVICE_DISCONNECTED)
			{
				m_Regs.joyserial[iPad] = m_Input.uPad[iPad] & 0xFFF0;
			} else
			{
				m_Regs.joyserial[iPad] = 0;
			}
		}
		if (m_bMouse0Connected)
			m_uMouseReadIndex = 0;
	}

	/* Capture on falling edge so sensitivity-cycle reads while strobe is high
	   affect this packet, matching bsnes' transition-based sampling. */
	if (!bNewStrobe && bOldStrobe && m_bMouse0Connected)
		CaptureMousePacket();

	m_Regs.joydata = uData;
}

// this function gets called about 3 scanlines after vblank, it performs reads from the serial
// port and loads them into each register
void SnesIO::UpdateJoyPads()
{

	// strobe joypads
	WriteSerial(0);
	WriteSerial(1);
	WriteSerial(0);

	if (m_bMouse0Connected)
		m_Regs.joy1.w = GetMouseAutoWord();
	else
		m_Regs.joy1.w = m_Regs.joyserial[0];
	m_Regs.joy2.w = m_Regs.joyserial[1];

	// multitap enabled?
	if (m_Regs.wrio & 0x80)
	{
		// ??
		m_Regs.joy3.w = m_Regs.joyserial[1];
		m_Regs.joy4.w = m_Regs.joyserial[2];
	} else
	{
		// ??
		m_Regs.joy3.w = m_Regs.joyserial[3];
		m_Regs.joy4.w = m_Regs.joyserial[4];
	}

	// perform dummy reads
	for (int i=0; i<16; i++)
	{
		ReadSerial0();
		ReadSerial1();
	}
}


SnesIO::SnesIO()
{
	Reset();
}

void SnesIO::Reset()
{
	memset(this, 0, sizeof(*this));
	SnesTurboFileResetBus();
	m_Regs.rdnmi  =  SNIO_VERSION_5A22;
	/* AURORA_SNES_WRIO_POWERON_V4_20260822
	 * WRIO ($4201) powers on at $FF. Bit 7 enables the PPU /EXTLATCH
	 * sampled by reads of $2137. */
	m_Regs.wrio = 0xFF;
}

void SnesIO::LatchInput(Emu::SysInputT  *pInput)
{
	if (pInput)
	{
		m_Input = *pInput;
	} else
	{
		// not connected
		m_Input.uPad[0] = EMUSYS_DEVICE_DISCONNECTED;
		m_Input.uPad[1] = EMUSYS_DEVICE_DISCONNECTED;
		m_Input.uPad[2] = EMUSYS_DEVICE_DISCONNECTED;
		m_Input.uPad[3] = EMUSYS_DEVICE_DISCONNECTED;
		m_Input.uPad[4] = EMUSYS_DEVICE_DISCONNECTED;
	}
}
