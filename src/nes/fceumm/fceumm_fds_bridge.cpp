/* AURORA_FCEUMM_FDS_V0_6_RUNTIME
 * Runtime bridge for the pinned, FDS-only FCEUmm libretro archive.
 *
 * v0_6 adds two host-only services while leaving FCEUmm's emulated state
 * authoritative:
 *   - libretro serialize/unserialize for Aurora save states;
 *   - non-blocking opposite-side swap: eject now, emulate 60 FDS frames,
 *     select A<->B of the SAME disk, then insert.
 *
 * The core itself serializes SelectDisk and InDisk. The small host envelope
 * additionally serializes the pending 60-frame swap countdown so a state made
 * while the disk is ejected resumes the same virtual drive operation.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>

#include "nes/fceumm/fceumm_fds_bridge.h"
#include "types.h"
#include "emuinput.h"
#include "rendersurface.h"
#include "pixelformat.h"
#include "mixbuffer.h"
#include "audmixbuffer.h"
#include "snio.h"
/* AURORA_FCEUMM_FDS_V9_INPUT_PALETTE_CPU_FASTPATH_20260827: exact QuickNES palette model. */
#include "Nes_Emu.h"
#include "nes_ntsc.h"

/* AURORA_FCEUMM_FDS_PERF_DIRECT_T8_V3_20260827: direct T8/CLUT upload, same GS primitive layer as QuickNES. */
extern "C" {
#include "gs.h"
#include "gpprim.h"
}

extern AudMixBuffer *_AudMix;

extern "C" {
#include "libretro.h"
}

extern "C" {
unsigned FDS_retro_api_version(void);
void FDS_retro_set_environment(retro_environment_t);
void FDS_retro_set_video_refresh(retro_video_refresh_t);
void FDS_retro_set_audio_sample(retro_audio_sample_t);
void FDS_retro_set_audio_sample_batch(retro_audio_sample_batch_t);
void FDS_retro_set_input_poll(retro_input_poll_t);
void FDS_retro_set_input_state(retro_input_state_t);
void FDS_retro_init(void);
void FDS_retro_deinit(void);
bool FDS_retro_load_game(const struct retro_game_info *);
void FDS_retro_unload_game(void);
void FDS_retro_reset(void);
void FDS_retro_run(void);
size_t FDS_retro_serialize_size(void);
bool FDS_retro_serialize(void *, size_t);
bool FDS_retro_unserialize(const void *, size_t);
void FDS_retro_get_system_av_info(struct retro_system_av_info *);
void FDS_aurora_fds_set_system_directory(const char *);
void FDS_aurora_fds_set_save_directory(const char *);
void FDS_aurora_fds_set_skip_video(int);
/* AURORA_FCEUMM_FDS_PERF_DIRECT_T8_V3_20260827: generated libretro native-video accessors. */
const uint8_t *FDS_aurora_fds_get_video_pixels(void);
const uint16_t *FDS_aurora_fds_get_video_palette(void);
/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827 */
uint32_t FDS_aurora_fds_get_palette_serial(void);
int FDS_aurora_fds_get_deemph(void);
/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827 */
void FDS_aurora_fds_set_pad_state(unsigned, unsigned);
void FDS_aurora_fds_set_palette(const uint8_t *);
const int32_t *FDS_aurora_fds_get_audio_mono(void);
int FDS_aurora_fds_get_audio_frames(void);
void FDS_FCEU_FDSEject(void);
void FDS_FCEU_FDSSelect(void);
void FDS_FCEU_FDSInsert(int);
void FDS_FCEU_SetMicrophoneDirect(int); /* AURORA_FAMICOM_MIC_CFG41_20260828 */
}

static bool s_Initialized = false;
static bool s_GameLoaded = false;
static bool s_SkipVideoNext = false;
/* AURORA_V3_SAFE_FDS_ONESHOT_SKIP_DECL_20260828 */
static Uint16 s_LastPadRaw[2] = { 0, 0 };
static bool s_LastPadRawValid = false;
static Emu::SysInputT *s_pInput = NULL;
static CRenderSurface *s_pTarget = NULL;
static CMixBuffer *s_pMix = NULL;
static unsigned s_SampleRate = 32050;
static int s_StateBytes = 0;

/* AURORA_FCEUMM_FDS_V0_6_DRIVE_STATE */
static unsigned s_TotalSides = 0;
static unsigned s_SelectedSide = 0;
static bool s_DiskInserted = false;
static unsigned s_SwapFramesRemaining = 0;
static unsigned s_SwapTargetSide = 0;
static bool s_DriveStateChanged = true; /* AURORA_FCEUMM_FDS_V12_3B_BRIDGE_HOTPATH_FIX_20260827 */
enum { FDS_SIDE_SWAP_DELAY_FRAMES = 60 };

enum { FDS_AUDIO_CHUNK = 1024 };
static Int16 s_AudioL[FDS_AUDIO_CHUNK];
static Int16 s_AudioR[FDS_AUDIO_CHUNK];
/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827 */
static Uint8 s_CustomPalette[64 * 3];
static bool s_CustomPaletteValid = false;
/* AURORA_FCEUMM_FDS_V9_INPUT_PALETTE_CPU_FASTPATH_20260827: QuickNES-compatible 512-color expansion. */
static Uint8 s_CustomExpandedPalette[Nes_Emu::color_table_size * 3];
static Uint32 s_HostPaletteSerial = 1;
static Int32 s_DirectLastDeemph = -1;
static Uint32 s_R5[32], s_G5[32], s_B5[32];
static bool s_ColorTablesReady = false;

/* AURORA_FCEUMM_FDS_PERF_DIRECT_T8_V3_20260827
 * Native FCEUmm framebuffer is 256x240 T8. XBuf in this old core is only
 * guaranteed 4-byte alignment, while the GS upload path is happiest with
 * DMA-friendly alignment. Use a static aligned fallback only when needed.
 */
enum
{
    FDS_GS_TEX_TBP_OFFSET  = 0x400,
    FDS_GS_CLUT_TBP_OFFSET = 0x580,
    FDS_GS_T8_TBW          = 256,
    FDS_VIDEO_W            = 256,
    FDS_VIDEO_H            = 240
};

static const Uint8 *s_DirectPixels = NULL;
static const Uint16 *s_DirectPalette = NULL;
/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827: V4 allocates XBuf at 64-byte alignment; no staging copy needed. */
static Uint32 s_DirectGsPalette[256] __attribute__((aligned(64)));
static Uint32 s_DirectPaletteSerial = 0;
static bool s_DirectPaletteValid = false;
static bool s_DirectClutResident = false;
static bool s_DirectReady = false;
static Uint32 s_DirectFrameSerial = 0;
static Uint32 s_DirectUploadSerial = 0;

static void fdsResetDirectVideo(void)
{
    s_DirectPixels = NULL;
    s_DirectPalette = NULL;
    s_DirectPaletteSerial = 0;
    s_DirectLastDeemph = -1;
    s_DirectPaletteValid = false;
    s_DirectClutResident = false;
    s_DirectReady = false;
    s_DirectFrameSerial = 0;
    s_DirectUploadSerial = 0;
}

static void fdsResetDriveHostState(void)
{
    s_TotalSides = 0;
    s_SelectedSide = 0;
    s_DiskInserted = false;
    s_SwapFramesRemaining = 0;
    s_SwapTargetSide = 0;
    s_DriveStateChanged = true;
}

static void fdsInitColorTables(void)
{
    if (s_ColorTablesReady) return;
    for (unsigned i = 0; i < 32; ++i)
    {
        Uint32 v = (i << 3) | (i >> 2);
        s_R5[i] = v;
        s_G5[i] = v << 8;
        s_B5[i] = v << 16;
    }
    s_ColorTablesReady = true;
}

static bool fdsEnvironment(unsigned cmd, void *data)
{
    switch (cmd)
    {
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            if (data) *(bool *)data = true;
            return true;
        case RETRO_ENVIRONMENT_SET_MESSAGE:
            return true;
        default:
            return false;
    }
}

static void fdsVideo(const void *data, unsigned width, unsigned height, size_t pitch)
{
    if (!data || !s_pTarget || !width || !height || pitch < width * 2)
        return;

    PixelFormatT *fmt = s_pTarget->GetFormat();
    if (!fmt || fmt->uBitDepth != 32)
        return;

    fdsInitColorTables();

    unsigned w = width;
    unsigned h = height;
    if (w > s_pTarget->GetWidth()) w = s_pTarget->GetWidth();
    if (h > s_pTarget->GetHeight()) h = s_pTarget->GetHeight();
    if (w > 256) w = 256;
    if (h > 240) h = 240;

    for (unsigned y = 0; y < h; ++y)
    {
        const Uint16 *src = (const Uint16 *)((const Uint8 *)data + (size_t)y * pitch);
        Uint32 *dst = (Uint32 *)s_pTarget->GetLinePtr((Int32)y);
        if (!dst) continue;
        for (unsigned x = 0; x < w; ++x)
        {
            Uint16 c = src[x];
            dst[x] = 0xff000000u |
                     s_R5[(c >> 10) & 31u] |
                     s_G5[(c >> 5) & 31u] |
                     s_B5[c & 31u];
        }
    }
}

static void fdsAudioSample(int16_t left, int16_t right)
{
    if (!s_pMix) return;
    Int16 l = (Int16)left, r = (Int16)right;
    s_pMix->OutputSamplesStereo(&l, &r, 1);
}

static size_t fdsAudioBatch(const int16_t *data, size_t frames)
{
    if (!data || !frames || !s_pMix)
        return frames;

    if (_AudMix && s_pMix == _AudMix && frames <= 0x7fffffffU)
    {
        size_t pos = 0;
        while (pos < frames)
        {
            size_t n = frames - pos;
            if (n > FDS_AUDIO_CHUNK) n = FDS_AUDIO_CHUNK;
            _AudMix->OutputLibretroInterleaved(
                (const Int16 *)(data + pos * 2), (Int32)n);
            pos += n;
        }
        return frames;
    }

    size_t pos = 0;
    while (pos < frames)
    {
        size_t n = frames - pos;
        if (n > FDS_AUDIO_CHUNK) n = FDS_AUDIO_CHUNK;
        for (size_t i = 0; i < n; ++i)
        {
            s_AudioL[i] = (Int16)data[(pos + i) * 2 + 0];
            s_AudioR[i] = (Int16)data[(pos + i) * 2 + 1];
        }
        s_pMix->OutputSamplesStereo(s_AudioL, s_AudioR, (Int32)n);
        pos += n;
    }
    return frames;
}

static void fdsInputPoll(void) {}

static bool fdsPadHas(unsigned port, Uint16 bit)
{
    if (!s_pInput || port >= 2)
        return false;
    Uint16 pad = s_pInput->uPad[port];
    return pad != EMUSYS_DEVICE_DISCONNECTED && (pad & bit) != 0;
}

/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827: map Aurora carrier bits once per frame. */
static Uint8 fdsMapPad(Uint16 pad)
{
    Uint8 nes = 0;
    if (pad == EMUSYS_DEVICE_DISCONNECTED) return 0;

    /* AURORA_FCEUMM_FDS_V9_INPUT_PALETTE_CPU_FASTPATH_20260827 */
    if (pad & SNESIO_JOY_B)      nes |= 0x01; /* Cross  -> A */
    if (pad & SNESIO_JOY_Y)      nes |= 0x02; /* Square -> B */
    if (pad & SNESIO_JOY_SELECT) nes |= 0x04;
    if (pad & SNESIO_JOY_START)  nes |= 0x08;
    if (pad & SNESIO_JOY_UP)     nes |= 0x10;
    if (pad & SNESIO_JOY_DOWN)   nes |= 0x20;
    if (pad & SNESIO_JOY_LEFT)   nes |= 0x40;
    if (pad & SNESIO_JOY_RIGHT)  nes |= 0x80;

    if ((nes & 0x10) && (nes & 0x20))
        nes &= (Uint8)~(0x10 | 0x20);
    if ((nes & 0x40) && (nes & 0x80))
        nes &= (Uint8)~(0x40 | 0x80);
    return nes;
}

static int16_t fdsInputState(unsigned port, unsigned device,
                             unsigned index, unsigned id)
{
    (void)index;
    if (device != RETRO_DEVICE_JOYPAD || port >= 2)
        return 0;

    switch (id)
    {
        case RETRO_DEVICE_ID_JOYPAD_A:      return fdsPadHas(port, SNESIO_JOY_B) ? 1 : 0; /* Cross */
        case RETRO_DEVICE_ID_JOYPAD_B:      return fdsPadHas(port, SNESIO_JOY_Y) ? 1 : 0; /* Square */
        case RETRO_DEVICE_ID_JOYPAD_SELECT: return fdsPadHas(port, SNESIO_JOY_SELECT) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_START:  return fdsPadHas(port, SNESIO_JOY_START) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_UP:     return fdsPadHas(port, SNESIO_JOY_UP) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_DOWN:   return fdsPadHas(port, SNESIO_JOY_DOWN) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_LEFT:   return fdsPadHas(port, SNESIO_JOY_LEFT) ? 1 : 0;
        case RETRO_DEVICE_ID_JOYPAD_RIGHT:  return fdsPadHas(port, SNESIO_JOY_RIGHT) ? 1 : 0;
        default: return 0;
    }
}

bool FceummFdsBridge_Init(void)
{
    if (s_Initialized)
        return true;

    FDS_retro_set_environment(fdsEnvironment);
    FDS_retro_set_video_refresh(fdsVideo);
    FDS_retro_set_audio_sample(fdsAudioSample);
    FDS_retro_set_audio_sample_batch(fdsAudioBatch);
    FDS_retro_set_input_poll(fdsInputPoll);
    FDS_retro_set_input_state(fdsInputState);

    if (FDS_retro_api_version() != RETRO_API_VERSION)
    {
        printf("[FCEUmm/FDS] libretro API mismatch\n");
        return false;
    }

    FDS_retro_init();
    s_Initialized = true;
    return true;
}

void FceummFdsBridge_UnloadGame(void)
{
    if (s_Initialized && s_GameLoaded)
        FDS_retro_unload_game();
    s_GameLoaded = false;
    s_SkipVideoNext = false;
    s_StateBytes = 0;
    s_pInput = NULL;
    s_pTarget = NULL;
    s_pMix = NULL;
    fdsResetDriveHostState();
    fdsResetDirectVideo();
}

void FceummFdsBridge_Shutdown(void)
{
    if (!s_Initialized)
        return;
    FceummFdsBridge_UnloadGame();
    FDS_retro_deinit();
    s_Initialized = false;
}

/* AURORA_FDS_SAVE_GPSP_BUILD_FIX_V4_20260913_FDS_NV
 * FCEUmm's FDS driver writes modified diskdata[] through FCEUMKF_FDS on
 * GI_CLOSE. Redirect only that save image to SNESticle/NES.
 */
static bool fdsConfigureSaveDirectory(const char *systemPath)
{
    char root[PATH_MAX];
    char save[PATH_MAX];
    struct stat st;
    size_t n;
    int chars;

    if (!systemPath || !*systemPath)
        return false;

    chars = snprintf(root, sizeof(root), "%s", systemPath);
    if (chars < 0 || chars >= (int)sizeof(root))
        return false;

    n = strlen(root);
    while (n > 0 && (root[n - 1] == '/' || root[n - 1] == '\\'))
        root[--n] = 0;

    if (n < 7 || strcmp(root + n - 7, "/SYSTEM"))
        return false;
    root[n - 7] = 0;

    chars = snprintf(save, sizeof(save), "%s/NES", root);
    if (chars < 0 || chars >= (int)sizeof(save))
        return false;

    if (mkdir(save, 0777) != 0)
    {
        if (stat(save, &st) != 0 || !S_ISDIR(st.st_mode))
            return false;
    }

    FDS_aurora_fds_set_save_directory(save);
    printf("[FCEUmm/FDS] save directory: %s\n", save);
    return true;
}

bool FceummFdsBridge_LoadDisk(const char *path, const char *systemPath,
                              unsigned totalSides)
{
    if (!path || !*path || !systemPath || !*systemPath ||
        totalSides < 1 || totalSides > 8)
        return false;

    if (s_GameLoaded)
        FceummFdsBridge_UnloadGame();

    FDS_aurora_fds_set_system_directory(systemPath);
    if (!fdsConfigureSaveDirectory(systemPath))
        return false;
    if (!FceummFdsBridge_Init())
        return false;

    struct retro_game_info info;
    memset(&info, 0, sizeof(info));
    info.path = path;
    info.data = NULL;
    info.size = 0;
    info.meta = NULL;

    if (!FDS_retro_load_game(&info))
    {
        printf("[FCEUmm/FDS] load failed: %s (SYSTEM=%s)\n", path, systemPath);
        return false;
    }

    s_GameLoaded = true;
    s_StateBytes = 0;
    if (s_CustomPaletteValid) FDS_aurora_fds_set_palette(s_CustomPalette);
    s_TotalSides = totalSides;
    s_SelectedSide = 0;
    s_DiskInserted = true;
    s_SwapFramesRemaining = 0;
    s_SwapTargetSide = 0;
    s_DriveStateChanged = true;
    s_SkipVideoNext = false;
    FDS_aurora_fds_set_skip_video(0);
    /* AURORA_V3_SAFE_FDS_ONESHOT_SKIP_LOAD_20260828 */
    s_LastPadRawValid = false;

    struct retro_system_av_info av;
    memset(&av, 0, sizeof(av));
    FDS_retro_get_system_av_info(&av);
    if (av.timing.sample_rate >= 8000.0 && av.timing.sample_rate <= 96000.0)
        s_SampleRate = (unsigned)(av.timing.sample_rate + 0.5);

    printf("[FCEUmm/FDS] LOAD OK: %s; sides=%u; audio=%u Hz; SYSTEM=%s\n",
           path, s_TotalSides, s_SampleRate, systemPath);
    return true;
}

/* AURORA_FDS_V4_ZIP_BRIDGE_IMPL_20260828 */
bool FceummFdsBridge_LoadDiskMemory(const void *data, Uint32 bytes,
                                    const char *contentName,
                                    const char *systemPath,
                                    unsigned totalSides)
{
    struct retro_game_info info;
    struct retro_system_av_info av;

    if (!data || bytes < 65500U || bytes > 524016U ||
        !contentName || !*contentName ||
        !systemPath || !*systemPath ||
        totalSides < 1 || totalSides > 8)
        return false;

    if (s_GameLoaded)
        FceummFdsBridge_UnloadGame();

    FDS_aurora_fds_set_system_directory(systemPath);
    if (!fdsConfigureSaveDirectory(systemPath))
        return false;
    if (!FceummFdsBridge_Init())
        return false;

    memset(&info, 0, sizeof(info));
    info.path = contentName;
    info.data = data;
    info.size = (size_t)bytes;
    info.meta = NULL;

    if (!FDS_retro_load_game(&info))
        return false;

    s_GameLoaded = true;
    s_StateBytes = 0;
    if (s_CustomPaletteValid) FDS_aurora_fds_set_palette(s_CustomPalette);
    s_TotalSides = totalSides;
    s_SelectedSide = 0;
    s_DiskInserted = true;
    s_SwapFramesRemaining = 0;
    s_SwapTargetSide = 0;
    s_DriveStateChanged = true;
    s_SkipVideoNext = false;
    FDS_aurora_fds_set_skip_video(0);
    s_LastPadRawValid = false;

    memset(&av, 0, sizeof(av));
    FDS_retro_get_system_av_info(&av);
    if (av.timing.sample_rate >= 8000.0 && av.timing.sample_rate <= 96000.0)
        s_SampleRate = (unsigned)(av.timing.sample_rate + 0.5);

    return true;
}

void FceummFdsBridge_Reset(void)
{
    if (s_GameLoaded) FDS_retro_reset();
    fdsResetDirectVideo();
}

void FceummFdsBridge_SoftReset(void)
{
    if (s_GameLoaded) FDS_retro_reset();
    fdsResetDirectVideo();
}

void FceummFdsBridge_SetSkipVideo(bool skip)
{
    s_SkipVideoNext = skip;
}

/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827: live/shared raw 64xRGB palette. */
bool FceummFdsBridge_SetPalette(const Uint8 *rgb192)
{
    nes_ntsc_setup_t setup;

    if (!rgb192)
        return false;

    memcpy(s_CustomPalette, rgb192, sizeof(s_CustomPalette));

    /* Same 64-base -> 512-emphasis expansion used by QuickNES. */
    setup = nes_ntsc_rgb;
    setup.palette = NULL;
    setup.base_palette = s_CustomPalette;
    setup.palette_out = s_CustomExpandedPalette;
    nes_ntsc_init(NULL, &setup);

    s_CustomPaletteValid = true;
    if (++s_HostPaletteSerial == 0)
        s_HostPaletteSerial = 1;

    if (s_Initialized)
        FDS_aurora_fds_set_palette(s_CustomPalette);

    s_DirectPaletteValid = false;
    s_DirectClutResident = false;
    s_DirectLastDeemph = -1;
    return true;
}

/* AURORA_FCEUMM_FDS_PERF_DIRECT_T8_V3_20260827: a GS reinit invalidates VRAM addresses/residency, not core pixels. */
void FceummFdsBridge_InvalidateGsResources(void)
{
    s_DirectUploadSerial = 0;
    s_DirectClutResident = false;
}

bool FceummFdsBridge_CanDirectGsVideo(void)
{
    return s_GameLoaded && s_DirectReady &&
           s_DirectPixels && s_DirectPalette &&
           s_DirectFrameSerial != 0;
}

static void fdsRefreshDirectPalette(void)
{
    Int32 deemph;
    bool fullRefresh;
    unsigned first, last;

    if (!s_DirectPalette)
        return;

    deemph = (Int32)FDS_aurora_fds_get_deemph();
    if (deemph < 0 || deemph > 7)
        deemph = 0;

    fullRefresh =
        !s_DirectPaletteValid ||
        s_DirectPaletteSerial != s_HostPaletteSerial;

    if (!fullRefresh && s_DirectLastDeemph == deemph)
        return;

    /* AURORA_V3_SAFE_FDS_PARTIAL_CLUT_20260828:
     * only bank 0x40 depends on the current de-emphasis. */
    first = fullRefresh ? 0u : 0x40u;
    last  = fullRefresh ? 256u : 0x80u;

    for (unsigned i = first; i < last; ++i)
    {
        const unsigned base = i & 0x3fU;
        const unsigned bank = i & 0xc0U;
        unsigned tint = 0;
        unsigned ci;
        unsigned dest = i;
        const unsigned block = i & 0x3fU;
        Uint32 r, g, b;

        if (bank == 0x40U)
            tint = (unsigned)deemph;
        else if (bank == 0xC0U)
            tint = 7U;

        ci = base | (tint << 6);

        if (s_CustomPaletteValid)
        {
            const Uint8 *rgb = &s_CustomExpandedPalette[ci * 3U];
            r = rgb[0];
            g = rgb[1];
            b = rgb[2];
        }
        else
        {
            const Nes_Emu::rgb_t &rgb = Nes_Emu::nes_colors[ci];
            r = rgb.red;
            g = rgb.green;
            b = rgb.blue;
        }

        if ((block & 0x18U) == 0x08U)
            dest += 8U;
        else if ((block & 0x18U) == 0x10U)
            dest -= 8U;

        s_DirectGsPalette[dest] =
            0x80000000u | (b << 16) | (g << 8) | r;
    }

    s_DirectPaletteSerial = s_HostPaletteSerial;
    s_DirectLastDeemph = deemph;
    s_DirectPaletteValid = true;
    s_DirectClutResident = false;
}

bool FceummFdsBridge_DrawDirectGs(Uint32 auroraOutBaseTBP,
                                  Int32 logicalY,
                                  Float32 intensity)
{
    Uint32 texTBP, clutTBP, mod, modColor;
    const Uint8 *uploadPixels;

    if (!auroraOutBaseTBP || !FceummFdsBridge_CanDirectGsVideo())
        return false;

    texTBP = auroraOutBaseTBP + FDS_GS_TEX_TBP_OFFSET;
    clutTBP = auroraOutBaseTBP + FDS_GS_CLUT_TBP_OFFSET;

    if (s_DirectUploadSerial != s_DirectFrameSerial)
    {
        /* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827: XBuf is 64-byte aligned by the generated V4 video core. */
        GPPrimUploadTexture(
            (int)texTBP, FDS_GS_T8_TBW,
            0, 0, GS_PSMT8,
            (void *)s_DirectPixels, FDS_VIDEO_W, FDS_VIDEO_H);
        s_DirectUploadSerial = s_DirectFrameSerial;
    }

    fdsRefreshDirectPalette();
    if (!s_DirectClutResident)
    {
        GPPrimUploadTexture(
            (int)clutTBP, 64,
            0, 0, GS_PSMCT32,
            s_DirectGsPalette, 16, 16);
        s_DirectClutResident = true;
    }

    GPPrimSetTex(
        texTBP, FDS_GS_T8_TBW, 8, 8, GS_PSMT8,
        clutTBP, 64, GS_PSMCT32, 0);

    if (intensity < 0.0f) intensity = 0.0f;
    if (intensity > 1.0f) intensity = 1.0f;
    mod = (Uint32)(128.0f * intensity + 0.5f);
    if (mod > 128u) mod = 128u;
    modColor = 0x80000000u | (mod << 16) | (mod << 8) | mod;

    GPPrimTexRect(
        0, (Uint32)logicalY << 4, 8, 8,
        256u << 4, (Uint32)(logicalY + 240) << 4,
        (256u << 4) + 8u, (240u << 4) + 8u,
        10u << 4, modColor, 0);
    return true;
}

static bool fdsAdvanceSelectionTo(unsigned target)
{
    unsigned simulated;
    unsigned steps;

    if (!s_GameLoaded || s_DiskInserted || !s_TotalSides ||
        target >= s_TotalSides)
        return false;

    simulated = s_SelectedSide;
    steps = 0;
    while (simulated != target && steps < 16)
    {
        /* Match the pinned FCEUmm selector exactly. Its historical &3 keeps
         * the selectable drive positions in the four ordinary A/B slots. */
        simulated = ((simulated + 1u) % s_TotalSides) & 3u;
        ++steps;
    }
    if (simulated != target)
        return false;

    while (steps--)
        FDS_FCEU_FDSSelect();
    s_SelectedSide = target;
    return true;
}

bool FceummFdsBridge_BeginSideSwap(void)
{
    unsigned target;

    if (!s_GameLoaded || !s_DiskInserted || s_SwapFramesRemaining ||
        s_TotalSides < 2)
        return false;

    target = s_SelectedSide ^ 1u;
    if (target >= s_TotalSides)
        return false;

    FDS_FCEU_FDSEject();
    s_DiskInserted = false;
    s_DriveStateChanged = true;
    s_SwapTargetSide = target;
    s_SwapFramesRemaining = FDS_SIDE_SWAP_DELAY_FRAMES;
    return true;
}

bool FceummFdsBridge_IsSideSwapPending(void)
{
    return s_GameLoaded && s_SwapFramesRemaining != 0;
}

unsigned FceummFdsBridge_GetSelectedSide(void)
{
    return s_SelectedSide;
}

bool FceummFdsBridge_IsDiskInserted(void)
{
    return s_GameLoaded && s_DiskInserted;
}

void FceummFdsBridge_GetDriveState(unsigned *selectedSide,
                                   bool *inserted,
                                   unsigned *swapFramesRemaining,
                                   unsigned *swapTargetSide)
{
    if (selectedSide) *selectedSide = s_SelectedSide;
    if (inserted) *inserted = s_DiskInserted;
    if (swapFramesRemaining) *swapFramesRemaining = s_SwapFramesRemaining;
    if (swapTargetSide) *swapTargetSide = s_SwapTargetSide;
}

bool FceummFdsBridge_ConsumeDriveStateChange(unsigned *selectedSide,
                                              bool *inserted)
{
    if (!s_DriveStateChanged)
        return false;
    s_DriveStateChanged = false;
    if (selectedSide) *selectedSide = s_SelectedSide;
    if (inserted) *inserted = s_GameLoaded && s_DiskInserted;
    return true;
} /* AURORA_FCEUMM_FDS_V12_3B_BRIDGE_HOTPATH_FIX_20260827 */

bool FceummFdsBridge_SetDriveState(unsigned selectedSide,
                                   bool inserted,
                                   unsigned swapFramesRemaining,
                                   unsigned swapTargetSide)
{
    if (!s_GameLoaded || !s_TotalSides || selectedSide >= s_TotalSides ||
        swapTargetSide >= s_TotalSides ||
        swapFramesRemaining > FDS_SIDE_SWAP_DELAY_FRAMES ||
        (swapFramesRemaining && inserted))
        return false;

    s_SelectedSide = selectedSide;
    s_DiskInserted = inserted;
    s_SwapFramesRemaining = swapFramesRemaining;
    s_SwapTargetSide = swapTargetSide;
    s_DriveStateChanged = true;
    return true;
}

int FceummFdsBridge_GetStateSize(void)
{
    size_t n;
    if (!s_GameLoaded)
        return 0;
    if (s_StateBytes > 0)
        return s_StateBytes;
    n = FDS_retro_serialize_size();
    if (!n || n > (size_t)INT_MAX)
        return 0;
    s_StateBytes = (int)n;
    return s_StateBytes;
}

int FceummFdsBridge_SaveState(void *data, int bytes)
{
    int n = FceummFdsBridge_GetStateSize();
    if (!data || n <= 0 || bytes < n)
        return 0;
    return FDS_retro_serialize(data, (size_t)n) ? n : 0;
}

bool FceummFdsBridge_LoadState(const void *data, int bytes)
{
    int n = FceummFdsBridge_GetStateSize();
    if (!data || n <= 0 || bytes != n)
        return false;
    s_SwapFramesRemaining = 0;
    s_SwapTargetSide = s_SelectedSide;
    return FDS_retro_unserialize(data, (size_t)n);
}

/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827: keep FCEUmm mono until Aurora's mono mixer. */
static void fdsOutputNativeMono(void)
{
    const int32_t *src;
    int frames, pos = 0;

    if (!s_pMix) return;
    src = FDS_aurora_fds_get_audio_mono();
    frames = FDS_aurora_fds_get_audio_frames();
    if (!src || frames <= 0) return;

    /* AURORA_FCEUMM_FDS_V14_INT32_MONO_FASTPATH_20260827
     * Fuse the temporary Int16 conversion with the existing FDS resampler.
     * If mixer state/rate differs from the proven case, retain the old path. */
    if (_AudMix && s_pMix == _AudMix &&
        _AudMix->OutputFceummMonoInt32((const Int32 *)src, frames))
        return;

    while (pos < frames)
    {
        int n = frames - pos;
        if (n > FDS_AUDIO_CHUNK) n = FDS_AUDIO_CHUNK;
        for (int i = 0; i < n; ++i)
            s_AudioL[i] = (Int16)src[pos + i];
        s_pMix->OutputSamplesMono(s_AudioL, n);
        pos += n;
    }
}

void FceummFdsBridge_RunFrame(Emu::SysInputT *input,
                              CRenderSurface *target,
                              CMixBuffer *mix)
{
    if (!s_GameLoaded)
        return;

    const bool skipVideo = s_SkipVideoNext;
    s_SkipVideoNext = false;
    s_pInput = input;
    s_pTarget = target;
    s_pMix = mix;

    /* AURORA_FAMICOM_MIC_CFG41_20260828:
     * carrier comes from physical pad 1, emulated signal is controller-II
     * microphone data on $4016 D2. */
    FDS_FCEU_SetMicrophoneDirect(
        (input &&
         input->uPad[0] != EMUSYS_DEVICE_DISCONNECTED &&
         (input->uPad[0] & SNESIO_JOY_L)) ? 1 : 0);

    /* AURORA_FCEUMM_FDS_V12_3B_BRIDGE_HOTPATH_FIX_20260827: unchanged input and skip state need no cross-archive setter. */
    {
        const Uint16 raw0 = input ? input->uPad[0] : 0;
        const Uint16 raw1 = input ? input->uPad[1] : 0;
        if (!s_LastPadRawValid || raw0 != s_LastPadRaw[0] || raw1 != s_LastPadRaw[1])
        {
            FDS_aurora_fds_set_pad_state(fdsMapPad(raw0), fdsMapPad(raw1));
            s_LastPadRaw[0] = raw0;
            s_LastPadRaw[1] = raw1;
            s_LastPadRawValid = true;
        }
    }
    /* AURORA_V3_SAFE_FDS_ONESHOT_SKIP_RUN_20260828 */
    if (skipVideo)
        FDS_aurora_fds_set_skip_video(1);
    FDS_retro_run();
    fdsOutputNativeMono();

    /* AURORA_FCEUMM_FDS_V12_3B_BRIDGE_HOTPATH_FIX_20260827: XBuf and libretro palette storage are stable after load.
     * Resolve their addresses only until direct T8 becomes ready. */
    if (!skipVideo)
    {
        if (!s_DirectReady)
        {
            s_DirectPixels = FDS_aurora_fds_get_video_pixels();
            s_DirectPalette = FDS_aurora_fds_get_video_palette();
            s_DirectReady = (s_DirectPixels && s_DirectPalette);
        }
        if (s_DirectReady)
        {
            if (++s_DirectFrameSerial == 0)
            {
                s_DirectFrameSerial = 1;
                s_DirectUploadSerial = 0;
            }
        }
    }

    if (s_SwapFramesRemaining)
    {
        --s_SwapFramesRemaining;
        if (!s_SwapFramesRemaining)
        {
            if (!fdsAdvanceSelectionTo(s_SwapTargetSide))
            {
                /* Selection should be reachable for every valid A/B pair.
                 * Fail safe by reinserting the original selected side. */
                s_SwapTargetSide = s_SelectedSide;
            }
            FDS_FCEU_FDSInsert(0);
            s_DiskInserted = true;
            s_DriveStateChanged = true; /* AURORA_FCEUMM_FDS_V12_3B_BRIDGE_HOTPATH_FIX_20260827 */
        }
    }

    if (s_pMix)
        s_pMix->Flush();
    s_pMix = NULL;
    s_pTarget = NULL;
    s_pInput = NULL;
}

unsigned FceummFdsBridge_GetSampleRate(void)
{
    return s_SampleRate;
}

