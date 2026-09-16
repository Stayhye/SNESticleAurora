
#ifndef _SNPPURENDERI_H
#define _SNPPURENDERI_H

#define SNESPPURENDER_UPDATE_OBJ    (1<<0)
#define SNESPPURENDER_UPDATE_PAL    (1<<1)
#define SNESPPURENDER_UPDATE_BGSCR  (1<<2)
#define SNESPPURENDER_UPDATE_BGCHR  (1<<3)
#define SNESPPURENDER_UPDATE_OBJPRI (1<<4)


#define SNESPPURENDER_UPDATE_WINDOW   (1<<5)

#define SNESPPURENDER_UPDATE_ALL   (0xFFFFFFFF)

class CRenderSurface;
class SnesPPU;

class ISnesPPURender
{
protected:
	Uint32			m_UpdateFlags;
	SnesPPU			*m_pPPU;

public:
	void SetUpdateFlags(Uint32 uFlags) {m_UpdateFlags |= uFlags;}
	void SetPPU(SnesPPU *pPPU) {m_pPPU = pPPU;}

	void BeginFrame();
	void EndFrame();

	virtual void BeginRender(CRenderSurface *pTarget) = 0;
	virtual void EndRender() = 0;
	virtual void RenderLine(Int32 iLine) = 0;
	/* AURORA_SETINI_DISPLAY_V1_RENDERI_20260915
	 * Returns TRUE only when the line was actually cleared on a live target.
	 * This lets PPU frame geometry defer the 239->224 tail cleanup across a
	 * Safe-Frameskip frame without repeatedly clearing it forever. */
	virtual Bool ClearLine(Int32 iLine) { (void)iLine; return FALSE; }
	virtual void UpdateVRAM(Uint32 uVramAddr) {};
	virtual void UpdateVRAMRange(Uint32 uVramAddr, Uint32 nWords) {};
	virtual void UpdateCGRAM(Uint32 uAddr, Uint16 uData) {};
};


#endif
