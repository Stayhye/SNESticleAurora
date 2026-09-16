
#ifndef _SNTIMING_H
#define _SNTIMING_H

/* AURORA_SAFE_RASTER_V4_TIMING_20260915
 * Most S-CPU scanlines are 1364 master clocks. Two hardware exceptions
 * matter to a scanline scheduler: NTSC non-interlace field 1 line 240 is
 * four clocks short, and PAL interlace field 1 line 311 is four clocks long.
 * Keep the legacy name as the nominal period for code that is not raster
 * position-sensitive; ExecuteLine() chooses the physical period per line. */
#define SNES_CYCLESPERLINE_NORMAL (1364)
#define SNES_CYCLESPERLINE_SHORT  (1360)
#define SNES_CYCLESPERLINE_LONG   (1368)
#define SNES_NTSC_BASE_LINES      (262u)
#define SNES_PAL_BASE_LINES       (312u)
#define SNES_CYCLESPERLINE        SNES_CYCLESPERLINE_NORMAL

/* AURORA_V7_HORIZONTAL_TIMING
 * Low-cost scanline scheduler positions, in S-CPU master clocks.
 * ares models HBlank at H=1096 and visible-line HDMA at H=1104.  For
 * revision-2 S-CPU timing, HDMA setup and DRAM refresh are phase-aligned
 * to the DMA divider: H=(12+phase) and H=(538-phase), respectively. */
#define SNES_HBLANK_WRAP_CYCLES      (3)
#define SNES_HDMA_SETUP_BASE_CYCLE   (12)
#define SNES_DRAM_REFRESH_BASE_CYCLE (538)
#define SNES_DRAM_REFRESH_CYCLES     (40)
#define SNES_HBLANK_START_CYCLE  (1096)
#define SNES_HDMA_START_CYCLE    (1104)

/* AURORA_CUMULATIVE_V5_TIMING_20260915
 * S-CPU automatic joypad polling is phase-aligned to the global 256-master-
 * clock divider. On the first VBlank line the aligned edge falls at H=130..384
 * and the busy interval lasts exactly 33*128 = 4224 master clocks. */
#define SNES_AUTOJOY_START_MIN_CYCLE  (130)
#define SNES_AUTOJOY_START_MAX_CYCLE  (384)
#define SNES_AUTOJOY_ALIGN_CYCLES     (256)
#define SNES_AUTOJOY_BUSY_CYCLES      (4224)

#define SNES_LINECYCLEDELAY SNES_DRAM_REFRESH_CYCLES
#define SNES_HBLANKCYCLES  (SNES_CYCLESPERLINE - SNES_HBLANK_START_CYCLE)

/* The S-CPU's H/V timer compare is not visible at H=HTIME*4 immediately.
   The counter reset/compare circuit and IRQ pipeline add 14 master clocks
   (reference emulator/bsnes timing); H=0 has the documented one-dot special case. */
#define SNES_IRQ_TRIGGER_CYCLES (14)
#define SNES_HIRQ_CYCLES(_htime) \
	((Int32)(_htime) * 4 + SNES_IRQ_TRIGGER_CYCLES - ((_htime) ? 0 : 4))
#define SNES_VIRQ_CYCLES (SNES_IRQ_TRIGGER_CYCLES - 4)
#define SNES_SPCMINCYCLES 0
/* Nominal non-interlaced NTSC field. Exact ExecuteFrame() duration is
 * variable because interlace adds one line on field 0 and the NTSC short
 * scanline removes four master clocks on field 1. */
#define SNES_CYCLESPERFRAME (SNES_CYCLESPERLINE_NORMAL * SNES_NTSC_BASE_LINES)


#endif
