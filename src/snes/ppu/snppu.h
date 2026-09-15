
#ifndef _SNPPU_H
#define _SNPPU_H

#include "snesreg.h"
#include "snmask.h"
#include "snppurenderi.h"
#include "snqueue.h"
#include "sndbglog.h"

#define SNPPU_WRITEQUEUE (TRUE)

#define SNESPPU_VRAM_NUMWORDS	0x8000
#define SNESPPU_CGRAM_NUM			256
#define	SNESPPU_OBJ_NUM			128

/* AURORA_SETINI_DISPLAY_V1_PPUH_20260915
 * $2133 SETINI: bit 0 screen interlace, bit 1 OBJ interlace,
 * bit 2 overscan (239 visible lines), bit 3 pseudo-hires, bit 6 EXTBG.
 * Screen interlace and overscan are latched for a frame at V=0; the other
 * mode bits remain the live register image used by the renderer. */
#define SNESPPU_SETINI_INTERLACE       0x01u
#define SNESPPU_SETINI_OBJ_INTERLACE   0x02u
#define SNESPPU_SETINI_OVERSCAN        0x04u
#define SNESPPU_SETINI_PSEUDOHIR       0x08u
#define SNESPPU_SETINI_EXTBG           0x40u
#define SNESPPU_VISIBLE_LINES_NORMAL   224u
#define SNESPPU_VISIBLE_LINES_OVERSCAN 239u

/* AURORA_OBJ_STAT77_V2_PPUH_20260915
 * STAT77 $213e: bit 6 is Range Over (33rd OBJ candidate), bit 7 is
 * Time Over (35th fetched 8-pixel OBJ sliver). The low nibble remains
 * the real S-PPU1 version field. */
#define SNESPPU_STAT77_RANGE_OVER      0x40u
#define SNESPPU_STAT77_TIME_OVER       0x80u

/* AURORA_PPU_MEMORY_V3_PPUH_20260915
 * Queue-time bus phase. VRAM/OAM are owned by the PPU throughout the visible
 * vertical field (including V=0); CGRAM is contended only during visible
 * pixels, H=88..1095 and V=1..vdisp-1. Forced blank overrides both. */
#define SNESPPU_MEMBUS_VRAM_OAM_BUSY  0x01u
#define SNESPPU_MEMBUS_CGRAM_BUSY     0x02u
#define SNESPPU_CGRAM_ACTIVE_H_BEGIN  88u
#define SNESPPU_CGRAM_ACTIVE_H_END    1096u

/* AURORA_DOT_RASTER_V6_PPUH_20260915
 * PPU queue timestamps now carry both V and H without growing the 8-byte
 * SNQueueElementT. Twelve H bits cover every 1360/1364/1368-clock scanline;
 * the remaining bits identify the scanline. H=512 is the single low-cost
 * visual snapshot used by ares' generic performance PPU. This is deliberately
 * a dot-aware scanline renderer, not a per-dot renderer. */
#define SNESPPU_RASTER_H_BITS        12u
#define SNESPPU_RASTER_H_MASK        0x0fffu
#define SNESPPU_RASTER_SNAPSHOT_H    512u
#define SNESPPU_RASTER_H_FALLBACK    SNESPPU_RASTER_H_MASK

_INLINE Uint32 SnesPPUPackRasterTime(Uint32 uLine, Uint32 uHClock)
{
    if (uHClock > SNESPPU_RASTER_H_MASK)
        uHClock = SNESPPU_RASTER_H_MASK;
    return (uLine << SNESPPU_RASTER_H_BITS) | uHClock;
}

_INLINE Uint32 SnesPPURasterAfter(Uint32 uLine, Uint32 uHClock)
{
    if (uHClock >= SNESPPU_RASTER_H_MASK)
        return SnesPPUPackRasterTime(uLine + 1u, 0u);
    return SnesPPUPackRasterTime(uLine, uHClock + 1u);
}

enum SnesPPULayerE
{
	SNESPPU_LAYER_BG1 = 0,
	SNESPPU_LAYER_BG2 = 1,
	SNESPPU_LAYER_BG3 = 2,
	SNESPPU_LAYER_BG4 = 3,
	SNESPPU_LAYER_OBJ = 4,
	SNESPPU_LAYER_BACK = 5,
	
	SNESPPU_LAYER_NUM
};

#define SNESPPU_MASK_BG1            (1 << SNESPPU_LAYER_BG1)
#define SNESPPU_MASK_BG2            (1 << SNESPPU_LAYER_BG2)
#define SNESPPU_MASK_BG3            (1 << SNESPPU_LAYER_BG3)
#define SNESPPU_MASK_BG4            (1 << SNESPPU_LAYER_BG4)
#define SNESPPU_MASK_OBJ            (1 << SNESPPU_LAYER_OBJ)
#define SNESPPU_MASK_BACK           (1 << SNESPPU_LAYER_BACK)

//
//
//

typedef Uint16 SnesColor16T;

struct SnesPPUOBJT
{
	Uint8	uX;
	Uint8	uY;
	Uint8	uTile;
	Uint8	uAttrib;
};

struct SnesOAMT
{
	SnesPPUOBJT	Objs[SNESPPU_OBJ_NUM];
	Uint8		ObjEx[SNESPPU_OBJ_NUM / 4];
};

struct SnesPPUTile2T
{
	Uint8	uPlane01[8][2];
};

struct SnesPPUTile4T
{
	Uint8	uPlane01[8][2];
	Uint8	uPlane23[8][2];
};

struct SnesPPUTile8T
{
	Uint8	uPlane01[8][2];
	Uint8	uPlane23[8][2];
	Uint8	uPlane45[8][2];
	Uint8	uPlane67[8][2];
};


struct SnesPPUScreenT
{
	Uint16	uTile[32][32];
};




// ppu registers
struct SnesPPURegsT
{
	SnesReg8T   	        inidisp;
	SnesReg8T   	        obsel;
	SnesReg16T   	        oamaddr;
	SnesReg16T   	        oamaddrlatch;
	SnesReg16T   	        oampri;
	SnesReg8T   	        bgmode;
	SnesReg8T   	        mosaic;
	SnesReg8T   	        bg1sc;
	SnesReg8T   	        bg2sc;
	SnesReg8T   	        bg3sc;
	SnesReg8T   	        bg4sc;
	SnesReg8T   	        bg12nba;
	SnesReg8T   	        bg34nba;
	
	SnesReg8T		        bgofslo;
	SnesReg16T   	        bg1hofs;
	SnesReg16T   	        bg1vofs;
	SnesReg16T   	        bg2hofs;
	SnesReg16T   	        bg2vofs;
	SnesReg16T   	        bg3hofs;
	SnesReg16T   	        bg3vofs;
	SnesReg16T   	        bg4hofs;
	SnesReg16T   	        bg4vofs;

	SnesReg8T   	        vmain;
	SnesReg16T   	        vmaddr;
	SnesReg16T   	        vmreadlatch;
	Uint8			        vminc[2];

	SnesReg8T   	        m7sel;
	SnesReg16T   	        m7a;
	SnesReg16T   	        m7b;
	SnesReg16T   	        m7c;
	SnesReg16T   	        m7d;
	SnesReg16T   	        m7x;
	SnesReg16T   	        m7y;

	SnesReg8T   	        mpyl;
	SnesReg8T   	        mpym;
	SnesReg8T   	        mpyh;

	SnesReg16T   	        cgadd;
	
	SnesReg8T   	        w12sel;
	SnesReg8T   	        w34sel;
	SnesReg8T   	        wobjsel;
	SnesReg8T   	        wh0;
	SnesReg8T   	        wh1;
	SnesReg8T   	        wh2;
	SnesReg8T   	        wh3;
	SnesReg8T   	        wbglog;
	SnesReg8T   	        wobjlog;

	SnesReg8T   	        tm;
	SnesReg8T   	        ts;
	SnesReg8T   	        tmw;
	SnesReg8T   	        tsw;
	SnesReg8T   	        cgwsel;
	SnesReg8T   	        cgadsub;
	SnesColor16T   	        coldata;

	SnesReg8T   	        setini;

	SnesReg16FT		        ophct;
	SnesReg16FT		        opvct;
	SnesReg8T		        stat77;
	SnesReg8T		        stat78;

	/* Internal PPU write latches.  Keep these at the end so the offsets of
	   the long-standing public register image remain stable. */
	SnesReg8T              bghofslo;  // low 3 bits used only by BGxHOFS
	SnesReg8T              m7latch;   // shared by $210D/$210E and $211B-$2120
	SnesReg16T             m7hofs;
	SnesReg16T             m7vofs;
};

class SnesPPU
{
public:
	SnesPPU();

	void                    Reset();
	void                    SoftReset();
	void                    BeginFrame();
	void                    EndFrame();
	/* AURORA_SAFE_RASTER_V4_PPUH_20260915
	 * EndFrame() is the VBlank edge; field changes only when V wraps. */
	void                    AdvanceField();
void SetRegionPAL(Bool bPAL);
	void                    SetPPURender(ISnesPPURender *pPPURender)    {m_pRender=pPPURender;}

	const SnesPPURegsT *    GetRegs() const                             {return &m_Regs;}
	/* SETINI frame geometry.  Overscan/interlace are sampled by BeginFrame(),
	 * matching the hardware frame latch rather than changing VBlank halfway
	 * through an already-running frame. */
	Uint32                  GetFrameVisibleLineCount() const             {return m_uFrameVisibleLines;}
	Bool                    IsFrameInterlace() const                     {return m_bFrameInterlace;}
	Bool                    IsTimingInterlace() const                    {return m_bTimingInterlace;}
	void                    LatchTimingInterlace()                       {m_bTimingInterlace = (m_Regs.setini & SNESPPU_SETINI_INTERLACE) != 0;}
	Bool                    IsObjInterlace() const                       {return (m_Regs.setini & SNESPPU_SETINI_OBJ_INTERLACE) != 0;}
	Bool                    IsPseudoHires() const                        {return (m_Regs.setini & SNESPPU_SETINI_PSEUDOHIR) != 0;}
	Bool                    IsExtBG() const                              {return (m_Regs.setini & SNESPPU_SETINI_EXTBG) != 0;}
	Bool                    IsHires() const
	{
		const Uint8 uMode = (Uint8)(m_Regs.bgmode & 7);
		return IsPseudoHires() || uMode == 5 || uMode == 6;
	}
	Bool                    GetField() const                             {return (m_Regs.stat78 & 0x80) != 0;}
	void                    SetObjOverflow(Bool bRangeOver, Bool bTimeOver)
	{
		if (bRangeOver) m_Regs.stat77 |= SNESPPU_STAT77_RANGE_OVER;
		if (bTimeOver)  m_Regs.stat77 |= SNESPPU_STAT77_TIME_OVER;
	}
	SnesOAMT *              GetOAM()                                    {return &m_OAM;}
	Uint16 *                GetVramPtr(Uint32 uVramAddr)                {return &m_VRAM[uVramAddr & 0x7FFF];}
	/* AURORA_SNES_FORCEBLANK_V1: INIDISP bit 7 means forced blank. */
	Bool                    IsForceBlank() const                        {return (m_Regs.inidisp & 0x80) != 0;}
	Bool                    InVBlank() const                            {return m_bVBlank;}
	Uint32                  GetIntensity()  const                       {return m_Regs.inidisp & 0xF;}

	/* CPU/HDMA enqueue-time phase survives until the queued write is applied.
	 * This deliberately records only the two access windows V3 can model
	 * exactly without converting the renderer into a dot scheduler. */
	Uint8                   BuildMemoryAccessFlags(Uint32 uLine, Uint32 uHClock) const;
	void                    SetMemoryAccessFlags(Uint8 uFlags)          {m_uMemoryAccessFlags = uFlags;}
	Uint8                   GetMemoryAccessFlags() const                {return m_uMemoryAccessFlags;}

	#if SNPPU_WRITEQUEUE
	/* AURORA_REVIVE_005CEE_PPU_ENQUEUE_INLINE_20260829
	 * Hot path compartilhado por CPU writes e HDMA direto. */
	_INLINE Bool            EnqueueWrite(Uint32 uLine, Uint32 uAddr, Uint8 uData,
	                                    Bool bCountFailure = TRUE,
	                                    Uint8 uMemoryAccessFlags = 0,
	                                    Uint32 uHClock = SNESPPU_RASTER_H_FALLBACK)
	{
		Bool bQueued = m_Queue.Enqueue(
			SnesPPUPackRasterTime(uLine, uHClock),
			uAddr, uData, uMemoryAccessFlags);
#if SNDBG_LOG
		if (bQueued)
			g_DbgPPUQueuedWrites++;
		else if (bCountFailure)
			g_DbgPPUQueueFull++;
#else
		(void)bCountFailure;
#endif
		return bQueued;
	}
	#endif
	void                    Sync(Uint32 uLine, Uint32 uHClock = SNESPPU_RASTER_H_FALLBACK);

	void                    WriteCGDATA(Uint8 uData);
	void                    WriteOAMDATA(Uint8 uData);
	void                    WriteOAMBlock(const Uint8 *pData, Int32 nBytes);
	void                    WriteVMDATAL(Uint8 uData);
	void                    WriteVMDATAH(Uint8 uData);
	void                    WriteVMDATALH(Uint8 uDataL, Uint8 uDataH);
	void                    WriteVMDATABlock(const Uint8 *pData, Int32 nBytes);
	void                    Write8(Uint32 uAddr, Uint8 uData);
	Uint8                   Read8(Uint32 uAddr);
	Uint8                   ReadOAMDATA();
	Uint8                   ReadCGDATA();
	Uint8                   ReadVMDATAL();
	Uint8                   ReadVMDATAH();

	/* AURORA_MEGA_V2_PPU_MDR
	 * Separate data-bus latches for the two S-PPU chips. These are
	 * internal open-bus state, deliberately kept outside SnesPPURegsT
	 * so the existing save-state/register layout stays unchanged. */
	Uint8                   GetPPU1MDR() const { return m_PPU1MDR; }
	Uint8                   GetPPU2MDR() const { return m_PPU2MDR; }
	void                    SetPPU1MDR(Uint8 uData) { m_PPU1MDR = uData; }
	void                    SetPPU2MDR(Uint8 uData) { m_PPU2MDR = uData; }

	/* AURORA_V9_MODE7_MOSAIC_LATCH_20260915 */
	Uint16                  GetMode7LineHofs() const { return m_Mode7LineHofs; }
	Uint16                  GetMode7LineVofs() const { return m_Mode7LineVofs; }
	Int32                   GetMode7MosaicSourceLine(Int32 iLine) const;


	SnesColor16T            GetCG(Uint32 uEntry)  const                       {return m_CGRAM[uEntry];}
	SnesColor16T *          GetCGData()                                       {return m_CGRAM;}

	void                    SaveState(struct SNStatePPUT *pState);
	void                    RestoreState(struct SNStatePPUT *pState);

	static Char *           GetRegName(Uint32 uAddr);

private:
    friend class SnesDMAC;

    Uint32			        m_uLine;
    Bool                    m_bVBlank;
    Bool                    m_bRasterLineRendered;
    Uint32                  m_uFrameVisibleLines;
    Bool                    m_bFrameInterlace;
    Bool                    m_bTimingInterlace;
    Bool                    m_bInactiveTailClearPending;

    SnesPPURegsT	        m_Regs;
    SnesColor16T	        m_CGRAM[SNESPPU_CGRAM_NUM] _ALIGN(16);			// 16-bit palette
    Uint16			        m_VRAM[SNESPPU_VRAM_NUMWORDS] _ALIGN(16);
    SnesOAMT		        m_OAM;
	Uint8                   m_OAMLatch;
	/* AURORA_ACCURACY_PPU_LATCHES_V1 */
	Uint8                   m_CGRAMLatch;
	Uint8                   m_PPU1MDR;
	Uint8                   m_PPU2MDR;
	Uint8                   m_uMemoryAccessFlags;

	/* AURORA_V9_MODE7_MOSAIC_LATCH_20260915 */
	Uint16                  m_Mode7LineHofs;
	Uint16                  m_Mode7LineVofs;
	Uint32                  m_uMosaicStartLine;

    ISnesPPURender *        m_pRender;

#if SNPPU_WRITEQUEUE
    SNPPUQueue			m_Queue;	// raster write queue
#endif

    void                    UpdateMatMul();
	void                    UpdateVRAMReadBuffer();
	void                    UpdateOAMPriority();
	Bool                    IsVRAMAccessAllowed() const;
	Bool                    IsOAMAccessAllowed() const;
	Bool                    IsCGRAMAccessAllowed() const;
	void                    ApplyQueuedWritesBefore(Uint32 uRasterTime);
};


#endif
