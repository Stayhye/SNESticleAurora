#!/usr/bin/env python3
# AURORA_FCEUMM_FDS_V0_6
# Generate lean FDS-only/two-pad build overlays without modifying FCEUmm itself.

from __future__ import annotations

import argparse
from pathlib import Path
import os
import tempfile


def fail(msg: str) -> None:
    raise SystemExit("prepare_fceumm_fds_sources.py: " + msg)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8").replace("\r\n", "\n")


# AURORA_FCEUMM_FDS_GENERATOR_COMPAT_V8_1_20260827
def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        fail(f"{label}: expected anchor not found")
    return text.replace(old, new, 1)


def replace_between_once(text: str, start: str, end: str, new: str, label: str) -> str:
    if text.count(start) != 1 or text.count(end) != 1:
        fail(
            f"{label}: expected unique span anchors; "
            f"start={text.count(start)} end={text.count(end)}"
        )
    i = text.index(start)
    j = text.index(end, i)
    if j <= i:
        fail(f"{label}: invalid span order")
    return text[:i] + new + text[j:]


def write_atomic(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, tmp = tempfile.mkstemp(prefix=path.name + ".", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        os.replace(tmp, path)
    finally:
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass


def patch_fceu(text: str) -> str:
    # These headers only feed features deliberately removed by this FDS build.
    for inc, label in (
        ('#include  "netplay.h"\n', "netplay include"),
        ('#include  "cheat.h"\n', "cheat include"),
        ('#include  "movie.h"\n', "movie include"),
        ('#include  "vsuni.h"\n', "VS include"),
    ):
        text = replace_once(text, inc, "", "src/fceu.c " + label)

    loader = (
        "\tif (iNESLoad(name, fp))\n"
        "\t\tgoto endlseq;\n"
        "\tif (NSFLoad(fp))\n"
        "\t\tgoto endlseq;\n"
        "\tif (UNIFLoad(name, fp))\n"
        "\t\tgoto endlseq;\n"
        "\tif (FDSLoad(name, fp))\n"
        "\t\tgoto endlseq;\n"
    )
    fds_only = (
        "\t/* AURORA_FCEUMM_FDS_V0_6_FDS_ONLY_LOADER */\n"
        "\tif (FDSLoad(name, fp))\n"
        "\t\tgoto endlseq;\n"
    )
    text = replace_once(text, loader, fds_only, "src/fceu.c loader chain")

    text = replace_between_once(
        text,
        "int CopyFamiLoad(void);\n",
        "int FCEUI_Initialize(void)",
        "/* AURORA_FCEUMM_FDS_V0_6_NO_COPYFAMI */\n"
        "FCEUGI *FCEUI_CopyFamiStart(void) {\n"
        "\treturn NULL;\n"
        "}\n\n",
        "src/fceu.c CopyFamicom span",
    )

    text = replace_once(
        text,
        "\t\tif (FCEUnetplay)\n\t\t\tFCEUD_NetworkClose();\n",
        "\t\t/* AURORA_FCEUMM_FDS_V0_6_NO_NETPLAY_CLOSE */\n",
        "src/fceu.c netplay close",
    )
    text = replace_once(
        text,
        "\t\tif (GameInfo->type != GIT_NSF)\n\t\t\tFCEU_FlushGameCheats(0, 0);\n",
        "\t\t/* AURORA_FCEUMM_FDS_V0_6_NO_CHEAT_FLUSH */\n",
        "src/fceu.c cheat flush",
    )
    text = replace_once(
        text,
        "\t\tFCEU_CloseGenie();\n",
        "\t\t/* AURORA_FCEUMM_FDS_V0_6_NO_GAME_GENIE_CLOSE */\n",
        "src/fceu.c Game Genie close",
    )

    load_extras = (
        "\tFCEU_ResetVidSys();\n"
        "\tif (GameInfo->type != GIT_NSF)\n"
        "\t\tif (FSettings.GameGenie)\n"
        "\t\t\tFCEU_OpenGenie();\n\n"
        "\tPowerNES();\n"
        "\tFCEUSS_CheckStates();\n"
        "\tFCEUMOV_CheckMovies();\n\n"
        "\tif (GameInfo->type != GIT_NSF) {\n"
        "\t\tFCEU_LoadGamePalette();\n"
        "\t\tFCEU_LoadGameCheats(0);\n"
        "\t}\n"
    )
    load_lean = (
        "\tFCEU_ResetVidSys();\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_LEAN_LOAD: FDS has no movie/cheat/Genie frontend. */\n"
        "\tPowerNES();\n"
        "\tFCEUSS_CheckStates();\n"
        "\tFCEU_LoadGamePalette();\n"
    )
    text = replace_once(text, load_extras, load_lean, "src/fceu.c post-load extras")

    text = replace_once(
        text,
        "void FCEUI_Kill(void) {\n\tFCEU_KillVirtualVideo();\n\tFCEU_KillGenie();\n}\n",
        "void FCEUI_Kill(void) {\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_GAME_GENIE_KILL */\n"
        "\tFCEU_KillVirtualVideo();\n"
        "}\n",
        "src/fceu.c kill Game Genie",
    )
    text = replace_once(
        text,
        "\tif (geniestage != 1) FCEU_ApplyPeriodicCheats();\n",
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_PERIODIC_CHEATS */\n",
        "src/fceu.c per-frame cheats",
    )
    text = replace_once(
        text,
        "void ResetNES(void) {\n\tFCEUMOV_AddCommand(FCEUNPCMD_RESET);\n",
        "void ResetNES(void) {\n\t/* AURORA_FCEUMM_FDS_V0_6_NO_MOVIE_RESET */\n",
        "src/fceu.c reset movie command",
    )
    text = replace_once(
        text,
        "void PowerNES(void) {\n"
        "\tFCEUMOV_AddCommand(FCEUNPCMD_POWER);\n"
        "\tif (!GameInfo) return;\n\n"
        "\tFCEU_CheatResetRAM();\n"
        "\tFCEU_CheatAddRAM(2, 0, RAM);\n\n"
        "\tFCEU_GeniePower();\n",
        "void PowerNES(void) {\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_LEAN_POWER */\n"
        "\tif (!GameInfo) return;\n",
        "src/fceu.c power cheat/movie/Genie hooks",
    )
    text = replace_once(
        text,
        "\tGameInterface(GI_POWER);\n"
        "\tif (GameInfo->type == GIT_VSUNI)\n"
        "\t\tFCEU_VSUniPower();\n",
        "\tGameInterface(GI_POWER);\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_VSUNI_RUNTIME */\n",
        "src/fceu.c VS power hook",
    )
    text = replace_once(
        text,
        "\tX6502_Power();\n\tFCEU_PowerCheats();\n",
        "\tX6502_Power();\n\t/* AURORA_FCEUMM_FDS_V0_6_NO_POWER_CHEATS */\n",
        "src/fceu.c power cheats",
    )

    # AURORA_FCEUMM_FDS_V0_6_15_PRUNED_LINK_STUBS
    # ppu.c retains declarations/calls for MMC5 and NSF even in an FDS-only
    # build.  Those paths are unreachable here: MMC5Hack remains false and
    # the loader can never create GIT_NSF.  Keep the pruned core self-contained
    # with minimal ABI-compatible definitions instead of linking mmc5.c/nsf.c.
    text = replace_once(
        text,
        "uint64 timestampbase;\n",
        "uint64 timestampbase;\n\n"
        "/* AURORA_FCEUMM_FDS_V0_6_15_PRUNED_LINK_STUBS */\n"
        "uint8 mmc5ABMode = 0;\n"
        "void MMC5_hb(int scanline) { (void)scanline; }\n"
        "void DoNSFFrame(void) { }\n\n",
        "src/fceu.c pruned MMC5/NSF linker stubs",
    )

    # AURORA_FCEUMM_FDS_LOADER_FIX_V2_20260826
    text = replace_once(
        text,
        "FCEUGI *GameInfo = NULL;\n",
        "FCEUGI *GameInfo = NULL;\n"
        "extern void aurora_fds_trace(const char *stage);\n",
        "src/fceu.c Aurora FDS trace extern",
    )
    text = replace_once(
        text,
        "\tGameInfo = malloc(sizeof(FCEUGI));\n"
        "\tmemset(GameInfo, 0, sizeof(FCEUGI));\n",
        "\taurora_fds_trace(\"core:FCEUI_LoadGame begin\");\n"
        "\tGameInfo = malloc(sizeof(FCEUGI));\n"
        "\tif (!GameInfo) {\n"
        "\t\taurora_fds_trace(\"core:GameInfo malloc FAILED\");\n"
        "\t\treturn 0;\n"
        "\t}\n"
        "\tmemset(GameInfo, 0, sizeof(FCEUGI));\n",
        "src/fceu.c GameInfo allocation guard",
    )
    text = replace_once(
        text,
        "\tfp = FCEU_fopen(name, ipsfn, \"rb\", 0);\n"
        "\tfree(ipsfn);\n\n"
        "\tif (!fp) {\n",
        "\taurora_fds_trace(\"core:opening FDS content\");\n"
        "\tfp = FCEU_fopen(name, ipsfn, \"rb\", 0);\n"
        "\tfree(ipsfn);\n\n"
        "\tif (!fp) {\n"
        "\t\taurora_fds_trace(\"core:FCEU_fopen content FAILED\");\n",
        "src/fceu.c content fopen trace",
    )
    text = replace_once(
        text,
        "\tFCEU_ResetVidSys();\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_LEAN_LOAD: FDS has no movie/cheat/Genie frontend. */\n"
        "\tPowerNES();\n"
        "\tFCEUSS_CheckStates();\n",
        "\taurora_fds_trace(\"core:FDS loader returned success\");\n"
        "\tFCEU_ResetVidSys();\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_LEAN_LOAD: FDS has no movie/cheat/Genie frontend. */\n"
        "\taurora_fds_trace(\"core:PowerNES begin\");\n"
        "\tPowerNES();\n"
        "\taurora_fds_trace(\"core:PowerNES done\");\n"
        "\tFCEUSS_CheckStates();\n",
        "src/fceu.c post-load/PowerNES trace",
    )

    for marker in (
        "AURORA_FCEUMM_FDS_V0_6_FDS_ONLY_LOADER",
        "AURORA_FCEUMM_FDS_V0_6_NO_COPYFAMI",
        "AURORA_FCEUMM_FDS_V0_6_LEAN_LOAD",
        "AURORA_FCEUMM_FDS_V0_6_NO_PERIODIC_CHEATS",
        "AURORA_FCEUMM_FDS_V0_6_LEAN_POWER",
        "AURORA_FCEUMM_FDS_V0_6_NO_NETPLAY_CLOSE",
    ):
        if marker not in text:
            fail(f"src/fceu.c marker missing after patch: {marker}")
    return text


def patch_state(text: str) -> str:
    # AURORA_FCEUMM_FDS_V0_6_14_STATE_MEMSTREAM_ASPRINTF_FIX
    # state.c historically includes endian.h, whose prototypes are FILE *.
    # The libretro state path uses MEM_TYPE=memstream_t, while the pinned
    # core already provides fceu-endian.h with the correct MEM_TYPE API.
    text = replace_once(
        text,
        '#include "endian.h"\n',
        '#include "fceu-endian.h"\n',
        "src/state.c MEM_TYPE endian header",
    )
    # Aurora owns state slots/files.  FCEUmm only serializes to the supplied
    # memstream, so its FILE-based state-directory probe is both unnecessary
    # and type-invalid under HAVE_MEMSTREAM.
    text = replace_between_once(
        text,
        "void FCEUSS_CheckStates(void) {\n",
        "void ResetExState(void (*PreSave)(void), void (*PostSave)(void)) {",
        "void FCEUSS_CheckStates(void) {\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_14_NO_FILE_STATE_SCAN */\n"
        "\tmemset(SaveStateStatus, 0, sizeof(SaveStateStatus));\n"
        "\tCurrentState = 0;\n"
        "\tStateShow = 0;\n"
        "}\n\n",
        "src/state.c FILE-based state scan",
    )
    text = replace_once(text, '#include "movie.h"\n', "", "src/state.c movie include")
    text = replace_once(text, '#include "netplay.h"\n', "", "src/state.c netplay include")
    text = replace_once(text, "extern int geniestage;\n", "", "src/state.c geniestage extern")

    genie_save = (
        "\tif (geniestage == 1) {\n"
        "\t\tFCEU_DispMessage(\"Cannot save FCS in GG screen.\");\n"
        "\t\treturn;\n"
        "\t}\n\n"
    )
    text = replace_once(
        text, genie_save,
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_GENIE_STATE_GUARD */\n",
        "src/state.c save Game Genie guard",
    )
    genie_load = (
        "\tif (geniestage == 1) {\n"
        "\t\tFCEU_DispMessage(\"Cannot load FCS in GG screen.\");\n"
        "\t\treturn(0);\n"
        "\t}\n\n"
    )
    text = replace_once(
        text, genie_load,
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_GENIE_STATE_GUARD */\n",
        "src/state.c load Game Genie guard",
    )
    text = replace_once(
        text,
        "\tFCEUI_SelectMovie(-1);\n\n",
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_MOVIE_STATE_SELECTOR */\n\n",
        "src/state.c movie selector",
    )
    text = replace_between_once(
        text,
        "void FCEUI_LoadState(char *fname) {\n",
        "void FCEU_DrawSaveStates(uint8 *XBuf) {",
        "void FCEUI_LoadState(char *fname) {\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_LOCAL_STATE_LOAD */\n"
        "\tStateShow = 0;\n"
        "\t(void)FCEUSS_Load(fname);\n"
        "}\n\n",
        "src/state.c movie/netplay load-state span",
    )
    for bad in ("FCEUMOV_", "FCEUnetplay", "FCEUNET_"):
        if bad in text:
            fail(f"src/state.c retained forbidden runtime reference: {bad}")
    return text



def patch_file(text: str) -> str:
    # AURORA_FCEUMM_FDS_LOADER_FIX_V2_20260826
    text = replace_once(
        text,
        "\tif (strchr(mode, 'r'))\n"
        "\t\tipsfile = FCEUD_UTF8fopen(ipsfn, \"rb\");\n",
        "\t/* AURORA_FCEUMM_FDS_LOADER_FIX_V2_20260826_NULL_IPS */\n"
        "\tif (strchr(mode, 'r') && ipsfn && ipsfn[0])\n"
        "\t\tipsfile = FCEUD_UTF8fopen(ipsfn, \"rb\");\n",
        "src/file.c NULL IPS fopen guard",
    )
    text = replace_once(
        text,
        "\tfceufp = (FCEUFILE*)malloc(sizeof(FCEUFILE));\n\n",
        "\tfceufp = (FCEUFILE*)malloc(sizeof(FCEUFILE));\n"
        "\tif (!fceufp)\n"
        "\t\treturn 0;\n\n",
        "src/file.c FCEUFILE allocation guard",
    )
    return text


def patch_fds(text: str) -> str:
    # AURORA_FCEUMM_FDS_LOADER_FIX_V2_20260826
    text = replace_once(
        text,
        '#include "netplay.h"\n',
        '#include "netplay.h"\n\n'
        'extern void aurora_fds_trace(const char *stage);\n',
        "src/fds.c trace extern",
    )
    text = replace_once(
        text,
        "int FDSLoad(const char *name, FCEUFILE *fp) {\n"
        "\tFILE *zp;\n",
        "int FDSLoad(const char *name, FCEUFILE *fp) {\n"
        "\tFILE *zp;\n"
        "\taurora_fds_trace(\"core:FDSLoad begin\");\n",
        "src/fds.c load begin trace",
    )
    text = replace_once(
        text,
        '\tif (!(zp = FCEUD_UTF8fopen(fn, "rb"))) {\n',
        '\taurora_fds_trace("core:FDS BIOS fopen begin");\n'
        '\tif (!(zp = FCEUD_UTF8fopen(fn, "rb"))) {\n'
        '\t\taurora_fds_trace("core:FDS BIOS fopen FAILED");\n',
        "src/fds.c BIOS fopen trace",
    )
    text = replace_once(
        text,
        "\tFDSBIOSsize = 8192;\n"
        "\tFDSBIOS = (uint8*)FCEU_gmalloc(FDSBIOSsize);\n"
        "\tSetupCartPRGMapping(0, FDSBIOS, FDSBIOSsize, 0);\n",
        "\tFDSBIOSsize = 8192;\n"
        "\tFDSBIOS = (uint8*)FCEU_malloc(FDSBIOSsize);\n"
        "\tif (!FDSBIOS) {\n"
        "\t\taurora_fds_trace(\"core:FDS BIOS malloc FAILED\");\n"
        "\t\tfclose(zp);\n"
        "\t\treturn 0;\n"
        "\t}\n"
        "\tSetupCartPRGMapping(0, FDSBIOS, FDSBIOSsize, 0);\n",
        "src/fds.c BIOS allocation guard",
    )
    text = replace_once(
        text,
        "\tfclose(zp);\n\n"
        "\tFCEU_fseek(fp, 0, SEEK_SET);\n",
        "\tfclose(zp);\n"
        "\taurora_fds_trace(\"core:FDS BIOS read OK\");\n\n"
        "\tFCEU_fseek(fp, 0, SEEK_SET);\n",
        "src/fds.c BIOS read trace",
    )
    text = replace_once(
        text,
        "\tif (!SubLoad(fp)) {\n",
        "\taurora_fds_trace(\"core:FDS image parse begin\");\n"
        "\tif (!SubLoad(fp)) {\n"
        "\t\taurora_fds_trace(\"core:FDS image parse FAILED\");\n",
        "src/fds.c SubLoad trace",
    )

    old_backups = (
        "\t\tint x;\n"
        "\t\tfor (x = 0; x < TotalSides; x++) {\n"
        "\t\t\tdiskdatao[x] = (uint8*)FCEU_malloc(65500);\n"
        "\t\t\tmemcpy(diskdatao[x], diskdata[x], 65500);\n"
        "\t\t}\n\n"
        "\t\tif ((tp = FCEU_fopen(fn, 0, \"rb\", 0))) {\n"
    )
    new_backups = (
        "\t\tint x;\n"
        "\t\taurora_fds_trace(\"core:FDS image parse OK; backup alloc begin\");\n"
        "\t\tfor (x = 0; x < TotalSides; x++) {\n"
        "\t\t\tdiskdatao[x] = (uint8*)FCEU_malloc(65500);\n"
        "\t\t\tif (!diskdatao[x]) {\n"
        "\t\t\t\tint y;\n"
        "\t\t\t\taurora_fds_trace(\"core:FDS side backup malloc FAILED\");\n"
        "\t\t\t\tfor (y = 0; y < x; y++) { free(diskdatao[y]); diskdatao[y] = 0; }\n"
        "\t\t\t\tFreeFDSMemory();\n"
        "\t\t\t\tif (FDSBIOS) free(FDSBIOS);\n"
        "\t\t\t\tFDSBIOS = NULL;\n"
        "\t\t\t\tfree(fn);\n"
        "\t\t\t\treturn 0;\n"
        "\t\t\t}\n"
        "\t\t\tmemcpy(diskdatao[x], diskdata[x], 65500);\n"
        "\t\t}\n"
        "\t\taurora_fds_trace(\"core:FDS side backups OK; aux probe begin\");\n\n"
        "\t\tif ((tp = FCEU_fopen(fn, 0, \"rb\", 0))) {\n"
    )
    text = replace_once(text, old_backups, new_backups, "src/fds.c diskdatao guard")

    text = replace_once(
        text,
        "\t\tfree(fn);\n"
        "\t}\n\n"
        "\tGameInfo->type = GIT_FDS;\n",
        "\t\tfree(fn);\n"
        "\t}\n"
        "\taurora_fds_trace(\"core:FDS aux probe done\");\n\n"
        "\tGameInfo->type = GIT_FDS;\n",
        "src/fds.c aux probe trace",
    )

    text = replace_once(
        text,
        "\tCHRRAMSize = 8192;\n"
        "\tCHRRAM = (uint8*)FCEU_gmalloc(CHRRAMSize);\n"
        "\tmemset(CHRRAM, 0, CHRRAMSize);\n",
        "\tCHRRAMSize = 8192;\n"
        "\tCHRRAM = (uint8*)FCEU_malloc(CHRRAMSize);\n"
        "\tif (!CHRRAM) {\n"
        "\t\taurora_fds_trace(\"core:FDS CHRRAM malloc FAILED\");\n"
        "\t\tfor (x = 0; x < TotalSides; x++) if (diskdatao[x]) { free(diskdatao[x]); diskdatao[x] = 0; }\n"
        "\t\tFreeFDSMemory();\n"
        "\t\tif (FDSBIOS) free(FDSBIOS);\n"
        "\t\tFDSBIOS = NULL;\n"
        "\t\treturn 0;\n"
        "\t}\n"
        "\tmemset(CHRRAM, 0, CHRRAMSize);\n",
        "src/fds.c CHRRAM guard",
    )
    text = replace_once(
        text,
        "\tFDSRAMSize = 32768;\n"
        "\tFDSRAM = (uint8*)FCEU_gmalloc(FDSRAMSize);\n"
        "\tmemset(FDSRAM, 0, FDSRAMSize);\n",
        "\tFDSRAMSize = 32768;\n"
        "\tFDSRAM = (uint8*)FCEU_malloc(FDSRAMSize);\n"
        "\tif (!FDSRAM) {\n"
        "\t\taurora_fds_trace(\"core:FDS FDSRAM malloc FAILED\");\n"
        "\t\tif (CHRRAM) free(CHRRAM);\n"
        "\t\tCHRRAM = NULL;\n"
        "\t\tfor (x = 0; x < TotalSides; x++) if (diskdatao[x]) { free(diskdatao[x]); diskdatao[x] = 0; }\n"
        "\t\tFreeFDSMemory();\n"
        "\t\tif (FDSBIOS) free(FDSBIOS);\n"
        "\t\tFDSBIOS = NULL;\n"
        "\t\treturn 0;\n"
        "\t}\n"
        "\tmemset(FDSRAM, 0, FDSRAMSize);\n",
        "src/fds.c FDSRAM guard",
    )
    text = replace_once(
        text,
        '\tFCEU_printf(" Sides: %d\\n\\n", TotalSides);\n\treturn 1;\n',
        '\tFCEU_printf(" Sides: %d\\n\\n", TotalSides);\n'
        '\taurora_fds_trace("core:FDSLoad success");\n'
        '\treturn 1;\n',
        "src/fds.c success trace",
    )

    # FDSClose leaked all FDS allocations if DiskWritten == 0.
    start = text.index("void FDSClose(void) {\n")
    end = len(text)
    old_close = text[start:end]
    new_close = """void FDSClose(void) {
\tFILE *fp = NULL;
\tint x;
\tchar *fn = NULL;

\taurora_fds_trace("core:FDSClose begin");

\tif (DiskWritten) {
\t\tfn = FCEU_MakeFName(FCEUMKF_FDS, 0, 0);
\t\tif (fn)
\t\t\tfp = FCEUD_UTF8fopen(fn, "wb");
\t\tif (fp) {
\t\t\tFCEU_printf("FDS Save \\"%s\\"\\n", fn);
\t\t\tfor (x = 0; x < TotalSides; x++) {
\t\t\t\tif (fwrite(diskdata[x], 1, 65500, fp) != 65500) {
\t\t\t\t\tFCEU_PrintError("Error saving FDS image!");
\t\t\t\t\tbreak;
\t\t\t\t}
\t\t\t}
\t\t\tfclose(fp);
\t\t}
\t\tif (fn) free(fn);
\t}

\tfor (x = 0; x < 8; x++) {
\t\tif (diskdatao[x]) {
\t\t\tfree(diskdatao[x]);
\t\t\tdiskdatao[x] = 0;
\t\t}
\t}
\tFreeFDSMemory();
\tif (FDSBIOS) free(FDSBIOS);
\tFDSBIOS = NULL;
\tif (FDSRAM) free(FDSRAM);
\tFDSRAM = NULL;
\tif (CHRRAM) free(CHRRAM);
\tCHRRAM = NULL;
\tDiskWritten = 0;
\taurora_fds_trace("core:FDSClose done");
}
"""
    text = text[:start] + new_close + text[end:]
    return text


def patch_general(text: str) -> str:
    # AURORA_FCEUMM_FDS_V0_6_14_STATE_MEMSTREAM_ASPRINTF_FIX_ASPRINTF
    # The pinned source already carries a bounded local asprintf fallback.
    # Force that fallback in this generated TU instead of depending on the
    # PS2 libc declaration selected by the global HAVE_ASPRINTF define.
    text = replace_once(
        text,
        "#ifndef HAVE_ASPRINTF\n",
        "/* AURORA_FCEUMM_FDS_V0_6_14_LOCAL_ASPRINTF */\n"
        "#undef HAVE_ASPRINTF\n"
        "#ifndef HAVE_ASPRINTF\n",
        "src/general.c local asprintf fallback",
    )
    text = replace_once(text, '#include "movie.h"\n', "", "src/general.c movie include")
    old_override = (
        "\tif (GameInfo) {\t/* Rebuild cache of present states/movies. */\n"
        "\t\tif (which == FCEUIOD_STATE)\n"
        "\t\t\tFCEUSS_CheckStates();\n"
        "\t\telse if (which == FCEUIOD_MOVIE)\n"
        "\t\t\tFCEUMOV_CheckMovies();\n"
        "\t}\n"
    )
    new_override = (
        "\t/* AURORA_FCEUMM_FDS_V0_6_STATE_ONLY_DIR_OVERRIDE */\n"
        "\tif (GameInfo && which == FCEUIOD_STATE)\n"
        "\t\tFCEUSS_CheckStates();\n"
    )
    text = replace_once(text, old_override, new_override, "src/general.c dir override")

    text = replace_between_once(
        text,
        '\tcase FCEUMKF_NPTEMP: asprintf(&ret, "%s"PSS "m590plqd94fo.tmp", BaseDirectory); break;\n',
        "\tcase FCEUMKF_STATE:\n",
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_MOVIE_NETPLAY_FILENAMES */\n",
        "src/general.c movie/netplay filename cases",
    )
    cheat_case = (
        "\tcase FCEUMKF_CHEAT:\n"
        "\t\tif (odirs[FCEUIOD_CHEATS])\n"
        "\t\t\tasprintf(&ret, \"%s\"PSS \"%s.cht\", odirs[FCEUIOD_CHEATS], FileBase);\n"
        "\t\telse\n"
        "\t\t\tasprintf(&ret, \"%s\"PSS \"cheats\"PSS \"%s.cht\", BaseDirectory, FileBase);\n"
        "\t\tbreak;\n"
    )
    text = replace_once(
        text, cheat_case,
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_CHEAT_FILENAME */\n",
        "src/general.c cheat filename case",
    )
    text = replace_once(
        text,
        '\tcase FCEUMKF_GGROM: asprintf(&ret, "%s"PSS "gg.rom", BaseDirectory); break;\n',
        "\t/* AURORA_FCEUMM_FDS_V0_6_NO_GAME_GENIE_FILENAME */\n",
        "src/general.c Game Genie filename case",
    )
    if "FCEUMOV_" in text:
        fail("src/general.c retained movie reference")
    return text


def patch_libretro(text: str) -> str:
    for inc, label in (
        ('#include "cheat.h"\n', "cheat include"),
        ('#include "ines.h"\n', "iNES include"),
        ('#include "unif.h"\n', "UNIF include"),
    ):
        text = replace_once(text, inc, "", "libretro " + label)
    text = replace_once(text, "extern CartInfo iNESCart;\n", "", "libretro iNESCart extern")
    text = replace_once(text, "extern CartInfo UNIFCart;\n", "", "libretro UNIFCart extern")

    text = replace_once(
        text,
        '\tinfo->valid_extensions = "fds|FDS|zip|ZIP|nes|NES|unif|UNIF";\n',
        '\t/* AURORA_FCEUMM_FDS_V0_6_FDS_ONLY_EXTENSIONS */\n'
        '\tinfo->valid_extensions = "fds|FDS";\n',
        "libretro valid_extensions",
    )

    old_data = """void *retro_get_memory_data(unsigned id) {
\tif (id != RETRO_MEMORY_SAVE_RAM)
\t\treturn NULL;

\tif (iNESCart.battery)
\t\treturn iNESCart.SaveGame[0];
\tif (UNIFCart.battery)
\t\treturn UNIFCart.SaveGame[0];

\treturn 0;
}
"""
    new_data = """void *retro_get_memory_data(unsigned id) {
\t/* AURORA_FCEUMM_FDS_V0_6_NO_CART_SRAM */
\t(void)id;
\treturn NULL;
}
"""
    text = replace_once(text, old_data, new_data, "libretro cartridge SRAM data")

    old_size = """size_t retro_get_memory_size(unsigned id) {
\tif (id != RETRO_MEMORY_SAVE_RAM)
\t\treturn 0;

\tif (iNESCart.battery)
\t\treturn iNESCart.SaveGameLen[0];
\tif (UNIFCart.battery)
\t\treturn UNIFCart.SaveGameLen[0];

\treturn 0;
}
"""
    new_size = """size_t retro_get_memory_size(unsigned id) {
\t/* AURORA_FCEUMM_FDS_V0_6_NO_CART_SRAM */
\t(void)id;
\treturn 0;
}
"""
    if old_size not in text and old_size.endswith("\n"):
        old_size = old_size[:-1]
    text = replace_once(text, old_size, new_size, "libretro cartridge SRAM size")

    fceu_init_anchor = """static void fceu_init(const char * full_path) {
\tFCEUI_Initialize();

"""
    fceu_init_new = """/* AURORA_FCEUMM_FDS_V0_6_SYSTEM_BIOS
 * Aurora passes its already-existing SNESticle/SYSTEM directory here.
 * FCEU_MakeFName(FCEUMKF_FDSROM) then resolves exactly:
 *     <SYSTEM>/disksys.rom
 * Keep a relative SYSTEM fallback for isolated-core tests.
 */
static char aurora_system_directory[PATH_MAX] = "SYSTEM";

void aurora_fds_set_system_directory(const char *dir) {
\tif (!dir || !dir[0]) {
\t\tstrcpy(aurora_system_directory, "SYSTEM");
\t\treturn;
\t}
\tstrncpy(aurora_system_directory, dir, sizeof(aurora_system_directory) - 1);
\taurora_system_directory[sizeof(aurora_system_directory) - 1] = 0;
}

static void fceu_init(const char * full_path) {
\tFCEUI_Initialize();
\tFCEUI_SetBaseDirectory(aurora_system_directory);

"""
    text = replace_once(
        text, fceu_init_anchor, fceu_init_new, "libretro SYSTEM/disksys.rom setup"
    )

    text = replace_once(
        text,
        "static void fceu_init(const char * full_path) {\n"
        "\tFCEUI_Initialize();\n"
        "\tFCEUI_SetBaseDirectory(aurora_system_directory);\n\n",
        "/* AURORA_FCEUMM_FDS_LOADER_FIX_V2_20260826_TRACE */\n"
        "/* AURORA_FCEUMM_FDS_PERF_DIRECT_T8_V3_20260827: persistent trace retired after loader boot was proven. */\n"
        "void aurora_fds_trace(const char *stage) {\n"
        "\t(void)stage;\n"
        "}\n\n"
        "static void fceu_init(const char * full_path) {\n"
        "\taurora_fds_trace(\"core:fceu_init begin\");\n"
        "\tif (!FCEUI_Initialize()) {\n"
        "\t\tGameInfo = NULL;\n"
        "\t\taurora_fds_trace(\"core:FCEUI_Initialize FAILED\");\n"
        "\t\treturn;\n"
        "\t}\n"
        "\taurora_fds_trace(\"core:FCEUI_Initialize OK\");\n"
        "\tFCEUI_SetBaseDirectory(aurora_system_directory);\n\n",
        "libretro initialize guard + trace",
    )
    text = replace_once(
        text,
        "\tFCEUI_SetSoundVolume(256);\n"
        "\tFCEUI_Sound(32050);\n\n"
        "\tGameInfo = FCEUI_LoadGame(full_path);\n",
        "\tFCEUI_SetSoundVolume(256);\n"
        "\tFCEUI_Sound(32050);\n"
        "\taurora_fds_trace(\"core:sound configured\");\n\n"
        "\taurora_fds_trace(\"core:FCEUI_LoadGame call\");\n"
        "\tGameInfo = FCEUI_LoadGame(full_path);\n"
        "\taurora_fds_trace(GameInfo ? \"core:FCEUI_LoadGame returned OK\" : \"core:FCEUI_LoadGame returned FAIL\");\n",
        "libretro load trace",
    )

    # AURORA_FCEUMM_FDS_LOADER_FIX_V2_1_20260827
    # V2 already inserts a trace line between FCEUI_LoadGame() and
    # emulator_set_input(), so match the instrumented form here.
    text = replace_once(
        text,
        "\tGameInfo = FCEUI_LoadGame(full_path);\n"
        "\taurora_fds_trace(GameInfo ? \"core:FCEUI_LoadGame returned OK\" : "
        "\"core:FCEUI_LoadGame returned FAIL\");\n"
        "\temulator_set_input();\n",
        "\tGameInfo = FCEUI_LoadGame(full_path);\n"
        "\taurora_fds_trace(GameInfo ? \"core:FCEUI_LoadGame returned OK\" : "
        "\"core:FCEUI_LoadGame returned FAIL\");\n"
        "\tif (!GameInfo)\n"
        "\t\treturn;\n"
        "\temulator_set_input();\n",
        "libretro failed FDS load guard",
    )

    text = replace_once(
        text,
        "\tinfo->timing.sample_rate = 32040.5;\n",
        "\t/* AURORA_FCEUMM_FDS_V0_6_EXACT_AUDIO_RATE */\n"
        "\tinfo->timing.sample_rate = 32050.0;\n",
        "libretro FDS audio rate",
    )

    old_run = """void retro_run(void) {
\tunsigned y, x;
\tuint8_t *gfx;
\tstatic uint16_t video_out[256 * 240];
\tint32 ssize = 0;

\tupdate_input();

\tFCEUI_Emulate(&gfx, &sound, &ssize, 0);

\tgfx = XBuf;
\tfor (y = 0; y < 240; y++)
\t\tfor (x = 0; x < 256; x++, gfx++)
\t\t\tvideo_out[y * 256 + x] = palette[*gfx];

\tvideo_cb(video_out, 256, 240, 512);

\tfor (y = 0; y < ssize; y++)
\t\tsound[y] = (sound[y] << 16) | (sound[y] & 0xffff);

\taudio_batch_cb((const int16_t*)sound, ssize);
}
"""
    new_run = """/* AURORA_FCEUMM_FDS_V0_6_SAFE_SKIP
 * One-shot frontend video skip. CPU/FDS/APU and audio always advance.
 */
static int aurora_skip_video = 0;

void aurora_fds_set_skip_video(int skip) {
	aurora_skip_video = skip ? 1 : 0;
}

/* AURORA_FCEUMM_FDS_PERF_DIRECT_T8_V3_20260827
 * PS2/Aurora consumes FCEUmm's native 8-bit indexed XBuf and the existing
 * 256-entry RGB555 palette directly. The GS performs palette lookup through
 * a T8 texture + CLUT. This removes both software full-frame colour
 * conversions from the normal FDS path.
 */
const uint8_t *aurora_fds_get_video_pixels(void) {
	return XBuf;
}

const uint16_t *aurora_fds_get_video_palette(void) {
	return palette;
}

void retro_run(void) {
	unsigned y;
	uint8_t *gfx;
	int32 ssize = 0;
	int skip_video = aurora_skip_video;
	aurora_skip_video = 0;

	update_input();

	FCEUI_Emulate(&gfx, &sound, &ssize, skip_video ? 1 : 0);

	/* No 8->16 software blit and no video_cb in the Aurora direct path.
	 * XBuf/palette remain owned by FCEUmm and are read through the accessors
	 * above after retro_run returns. */

	for (y = 0; y < ssize; y++)
		sound[y] = (sound[y] << 16) | (sound[y] & 0xffff);

	audio_batch_cb((const int16_t*)sound, ssize);
}
"""
    text = replace_once(text, old_run, new_run, "libretro one-shot FDS video skip")

    # AURORA_FCEUMM_FDS_V0_6_STATE_SIZE_SAFETY
    # The old frontend probes state size through a temporary 1 MB memstream.
    # Keep the established format, but fail cleanly if that one-time allocation
    # cannot be satisfied on the 32 MB EE heap.
    text = replace_once(
        text,
        "\t\tuint8_t *buffer = (uint8_t*)malloc(1000000);\n"
        "\t\tmemstream_set_buffer(buffer, 1000000);\n",
        "\t\tuint8_t *buffer = (uint8_t*)malloc(1000000);\n"
        "\t\tif (!buffer)\n"
        "\t\t\treturn 0;\n"
        "\t\tmemstream_set_buffer(buffer, 1000000);\n",
        "libretro serialize-size allocation guard",
    )

    old_load = """bool retro_load_game(const struct retro_game_info *game) {
\tfceu_init(game->path);

\treturn TRUE;
}
"""
    new_load = """bool retro_load_game(const struct retro_game_info *game) {
\t/* AURORA_FCEUMM_FDS_V0_6_LOAD_RESULT */
\tif (!game || !game->path || !game->path[0])
\t\treturn false;
\t/* AURORA_FCEUMM_FDS_V0_6_STATE_SIZE_PER_GAME */
\tserialize_size = 0;
\tfceu_init(game->path);
\treturn GameInfo != NULL;
}
"""
    text = replace_once(text, old_load, new_load, "libretro FDS load result")

    text = replace_once(
        text,
        "void retro_unload_game(void) {\n"
        "\tFCEUI_CloseGame();\n"
        "}\n",
        "void retro_unload_game(void) {\n"
        "\tFCEUI_CloseGame();\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_STATE_SIZE_PER_GAME */\n"
        "\tserialize_size = 0;\n"
        "}\n",
        "libretro per-game serialize-size reset",
    )

    for marker in (
        "AURORA_FCEUMM_FDS_V0_6_FDS_ONLY_EXTENSIONS",
        "AURORA_FCEUMM_FDS_V0_6_NO_CART_SRAM",
        "AURORA_FCEUMM_FDS_V0_6_SYSTEM_BIOS",
        "AURORA_FCEUMM_FDS_V0_6_SAFE_SKIP",
        "AURORA_FCEUMM_FDS_PERF_DIRECT_T8_V3_20260827",
        "AURORA_FCEUMM_FDS_V0_6_LOAD_RESULT",
        "AURORA_FCEUMM_FDS_V0_6_EXACT_AUDIO_RATE",
        "AURORA_FCEUMM_FDS_V0_6_STATE_SIZE_PER_GAME",
    ):
        if marker not in text:
            fail(f"libretro marker missing: {marker}")
    # AURORA_FDS_SAVE_GPSP_BUILD_FIX_V4_20260913_FDS_NV
    # FCEUmm already persists FDS diskdata[] via FCEUMKF_FDS. Keep that
    # separate from the source image and point its NV directory at NES.
    if "AURORA_FDS_SAVE_GPSP_BUILD_FIX_V4_20260913_FDS_NV" not in text:
        _a = 'static char aurora_system_directory[PATH_MAX] = "SYSTEM";\n'
        _d = (
            '/* AURORA_FDS_SAVE_GPSP_BUILD_FIX_V4_20260913_FDS_NV: separate writable FDS save directory. */\n'
            'static char aurora_save_directory[PATH_MAX] = {0};\n\n'
            'void aurora_fds_set_save_directory(const char *dir) {\n'
            '\tif (!dir || !dir[0]) {\n'
            '\t\taurora_save_directory[0] = 0;\n'
            '\t\treturn;\n'
            '\t}\n'
            '\tstrncpy(aurora_save_directory, dir, '
            'sizeof(aurora_save_directory) - 1);\n'
            '\taurora_save_directory['
            'sizeof(aurora_save_directory) - 1] = 0;\n'
            '}\n\n'
        )
        if _a not in text:
            fail("libretro FDS save: SYSTEM anchor missing")
        text = text.replace(_a, _a + _d, 1)

        _base = '\tFCEUI_SetBaseDirectory(aurora_system_directory);\n'
        _with_nv = (
            _base +
            '\tFCEUI_SetDirOverride(FCEUIOD_NV, '
            'aurora_save_directory[0] ? '
            'aurora_save_directory : NULL);\n'
        )
        if _with_nv not in text:
            if _base not in text:
                fail("libretro FDS save: base-directory anchor missing")
            text = text.replace(_base, _with_nv, 1)

    return text


def patch_video(text: str) -> str:
    for inc, label in (
        ('#include "movie.h"\n', "movie include"),
        ('#include "nsf.h"\n', "NSF include"),
        ('#include "vsuni.h"\n', "VS include"),
    ):
        text = replace_once(text, inc, "", "src/video.c " + label)

    old_dummy = (
        "void FCEU_PutImageDummy(void) {\n"
        "\t#ifdef SHOWFPS\n"
        "\tShowFPS();\n"
        "\t#endif\n"
        "\tif (GameInfo->type != GIT_NSF) {\n"
        "\t\tFCEU_DrawNTSCControlBars(XBuf);\n"
        "\t\tFCEU_DrawSaveStates(XBuf);\n"
        "\t\tFCEU_DrawMovies(XBuf);\n"
        "\t}\n"
        "\tif (howlong) howlong--;\t/* DrawMessage() */\n"
        "}\n"
    )
    new_dummy = (
        "void FCEU_PutImageDummy(void) {\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_LEAN_VIDEO_DUMMY */\n"
        "\t#ifdef SHOWFPS\n"
        "\tShowFPS();\n"
        "\t#endif\n"
        "\tFCEU_DrawNTSCControlBars(XBuf);\n"
        "\tFCEU_DrawSaveStates(XBuf);\n"
        "\tif (howlong) howlong--;\t/* DrawMessage() */\n"
        "}\n"
    )
    text = replace_once(text, old_dummy, new_dummy, "src/video.c frameskip draw path")

    text = replace_between_once(
        text,
        "void FCEU_PutImage(void) {\n",
        "void FCEU_DispMessage(char *format, ...) {",
        "void FCEU_PutImage(void) {\n"
        "\t/* AURORA_FCEUMM_FDS_V0_6_LEAN_VIDEO */\n"
        "\t#ifdef SHOWFPS\n"
        "\tShowFPS();\n"
        "\t#endif\n"
        "\tFCEU_DrawSaveStates(XBuf);\n"
        "\tFCEU_DrawNTSCControlBars(XBuf);\n"
        "\tDrawMessage();\n"
        "\tFCEU_DrawInput(XBuf);\n"
        "}\n\n",
        "src/video.c FDS-only image presentation",
    )
    for bad in ("FCEU_DrawMovies(", "FCEU_VSUniDraw(", "DrawNSF("):
        if bad in text:
            fail(f"src/video.c retained forbidden frontend draw reference: {bad}")
    return text


def audit_sources(fceu: str, libretro: str, input_text: str, pads_text: str,
                  state: str, general: str, video: str) -> None:
    required = {
        "src/fceu.c": (
            "FCEU_ApplyPeriodicCheats();", "FCEUMOV_CheckMovies();",
            "FCEU_FlushGameCheats(0, 0);", "FCEUnetplay",
            "FCEU_GeniePower();",
        ),
        "libretro.c": (
            '#include "cheat.h"', "extern CartInfo iNESCart;",
            "void retro_cheat_reset(void)",
        ),
        "src/state.c": (
            '#include "movie.h"', '#include "netplay.h"',
            "FCEUMOV_Stop();", "FCEUnetplay",
        ),
        "src/general.c": (
            '#include "movie.h"', "FCEUMOV_CheckMovies();",
            "case FCEUMKF_CHEAT:", "case FCEUMKF_GGROM:",
        ),
        "src/video.c": (
            '#include "movie.h"', '#include "nsf.h"', '#include "vsuni.h"',
            "FCEU_DrawMovies(XBuf);", "FCEU_VSUniDraw(XBuf);", "DrawNSF(XBuf);",
        ),
        "src/input.c": (
            "extern INPUTC *FCEU_InitJoyPad(int w);",
            "extern INPUTCFC *FCEU_InitBarcodeWorld(void);",
            "void FCEUI_SetInputFC(int type, void *ptr, int attrib)",
        ),
        "src/input/pads.c": (
            "FCEUMOV_AddJoy(joy);", "FCEU_VSUniSwap(&joy[0], &joy[1]);",
            "INPUTC *FCEU_InitJoyPad(int w)",
        ),
    }
    texts = {
        "src/fceu.c": fceu, "libretro.c": libretro,
        "src/state.c": state, "src/general.c": general, "src/video.c": video,
        "src/input.c": input_text, "src/input/pads.c": pads_text,
    }
    for name, anchors in required.items():
        for anchor in anchors:
            if anchor not in texts[name]:
                fail(f"{name} audit anchor missing: {anchor}")


INPUT_FDS_C = """/* AURORA_FCEUMM_FDS_V0_6_TWO_PAD_INPUT
 * Minimal FDS input dispatcher derived from the pinned FCEUmm input.c.
 * FDS integration exposes only the two built-in standard joypads.
 */
#include "fceu-types.h"
#include "x6502.h"
#include "fceu.h"
#include "input.h"
#include "driver.h"

extern INPUTC *FCEU_InitJoyPad(int w);

static void *InputDataPtr[2] = { 0, 0 };
uint8 LastStrobe = 0;

static int JPAttrib[2] = { 0, 0 };
static int JPType[2] = { SI_NONE, SI_NONE };
static INPUTC DummyJPort = { 0, 0, 0, 0, 0, 0 };
static INPUTC *JPorts[2] = { &DummyJPort, &DummyJPort };

/* AURORA_FAMICOM_MIC_CFG41_20260828 */
static uint8 AuroraFamicomMic = 0;
static uint8 AuroraFamicomMicPhase = 0;

void FCEU_SetMicrophoneDirect(int active) {
\tconst uint8 next = active ? 1 : 0;
\tif (next != AuroraFamicomMic)
\t\tAuroraFamicomMicPhase = 0;
\tAuroraFamicomMic = next;
}

void (*InputScanlineHook)(uint8 *bg, uint8 *spr, uint32 linets, int final) = 0;

static DECLFR(JPRead) {
\tuint8 ret = 0;
\tif (JPorts[A & 1]->Read)
\t\tret |= JPorts[A & 1]->Read(A & 1);

\t/* AURORA_FAMICOM_MIC_CFG41_20260828
\t * Famicom controller II microphone: $4016 D2 only. Alternate the
\t * one-bit level while held so transition-detecting games respond. */
\tif (!(A & 1) && AuroraFamicomMic) {
\t\tAuroraFamicomMicPhase ^= 1;
\t\tif (AuroraFamicomMicPhase)
\t\t\tret |= 0x04;
\t}

\tret |= X.DB & 0xC0;
\treturn ret;
}

static DECLFW(B4016) {
\tif (JPorts[0]->Write)
\t\tJPorts[0]->Write(V & 1);
\tif (JPorts[1]->Write)
\t\tJPorts[1]->Write(V & 1);

\tif ((LastStrobe & 1) && (!(V & 1))) {
\t\tif (JPorts[0]->Strobe)
\t\t\tJPorts[0]->Strobe(0);
\t\tif (JPorts[1]->Strobe)
\t\t\tJPorts[1]->Strobe(1);
\t}
\tLastStrobe = V & 1;
}

void FCEU_DrawInput(uint8 *buf) {
\tint x;
\tfor (x = 0; x < 2; x++)
\t\tif (JPorts[x]->Draw)
\t\t\tJPorts[x]->Draw(x, buf, JPAttrib[x]);
}

void FCEU_UpdateInput(void) {
\tint x;
\tfor (x = 0; x < 2; x++)
\t\tif (JPorts[x]->Update)
\t\t\tJPorts[x]->Update(x, InputDataPtr[x], JPAttrib[x]);
}

static void SetInputStuff(int x) {
\tif (JPType[x] == SI_GAMEPAD)
\t\tJPorts[x] = FCEU_InitJoyPad(x);
\telse
\t\tJPorts[x] = &DummyJPort;

\tInputScanlineHook = 0;
}

void InitializeInput(void) {
\tLastStrobe = 0;
\tSetReadHandler(0x4016, 0x4017, JPRead);
\tSetWriteHandler(0x4016, 0x4016, B4016);
\tSetInputStuff(0);
\tSetInputStuff(1);
}

void FCEUI_SetInput(int port, int type, void *ptr, int attrib) {
\tif (port < 0 || port > 1)
\t\treturn;
\tJPAttrib[port] = attrib;
\tJPType[port] = (type == SI_GAMEPAD) ? SI_GAMEPAD : SI_NONE;
\tInputDataPtr[port] = ptr;
\tSetInputStuff(port);
}

void FCEUI_SetInputFC(int type, void *ptr, int attrib) {
\t(void)type;
\t(void)ptr;
\t(void)attrib;
}
"""

PADS_FDS_C = """/* AURORA_FCEUMM_FDS_V0_6_TWO_PAD_PADS
 * Minimal two-pad implementation derived from pinned FCEUmm input/pads.c.
 * Movie/netplay/VS/FourScore hooks are deliberately absent.
 */
#include "../fceu-types.h"
#include "../input.h"
#include "../fceu.h"
#include "../state.h"

static uint8 joy_readbit[2] = { 0, 0 };
static uint8 joy[4] = { 0, 0, 0, 0 };

extern uint8 LastStrobe;

SFORMAT FCEUCTRL_STATEINFO[] = {
\t{ joy_readbit, 2, "JYRB" },
\t{ joy, 4, "JOYS" },
\t{ &LastStrobe, 1, "LSTS" },
\t{ 0 }
};

void FCEUI_DisableFourScore(int s) {
\t(void)s;
}

uint8 FCEU_GetJoyJoy(void) {
\treturn joy[0] | joy[1] | joy[2] | joy[3];
}

static void FP_FASTAPASS(1) StrobeGP(int w) {
\tjoy_readbit[w] = 0;
}

static uint8 FP_FASTAPASS(1) ReadGP(int w) {
\tuint8 ret = 1;
\tif (joy_readbit[w] < 8)
\t\tret = (joy[w] >> joy_readbit[w]) & 1;
\tif (joy_readbit[w] != 0xFF)
\t\tjoy_readbit[w]++;
\treturn ret;
}

static void FP_FASTAPASS(3) UpdateGP(int w, void *data, int arg) {
\tconst uint32 *ptr = (const uint32 *)data;
\t(void)arg;
\tif (w < 0 || w > 1)
\t\treturn;
\tjoy[w] = ptr ? (uint8)(*ptr & 0xFF) : 0;
\tjoy[w + 2] = 0;
}

static INPUTC GPC = { ReadGP, 0, StrobeGP, UpdateGP, 0, 0 };

INPUTC *FCEU_InitJoyPad(int w) {
\tjoy_readbit[w] = 0;
\tjoy[w] = 0;
\tjoy[w + 2] = 0;
\treturn &GPC;
}
"""



# AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827
# Final pass over the generated FDS-only C overlays.
def patch_v4_generated_libretro(text: str) -> str:
    text = replace_once(
        text,
        "static uint32 JSReturn[2];\n",
        "static uint32 JSReturn[2];\n"
        "static uint8 aurora_direct_pad[2] = { 0, 0 };\n"
        "static int32 aurora_audio_frames = 0;\n"
        "extern void FCEUI_SetPaletteArray(uint8 *pal);\n",
        "V4 direct pad/audio state",
    )
    old_update = """static void update_input(void) {
\tunsigned i;
\tunsigned char pad[2];

\tpad[0] = 0;
\tpad[1] = 0;

\tpoll_cb();

\tfor (i = 0; i < 8; i++) {
\t\tpad[0] |= input_cb(0, RETRO_DEVICE_JOYPAD, 0, bindmap[i].retro) ? bindmap[i].nes : 0;
\t\tpad[1] |= input_cb(1, RETRO_DEVICE_JOYPAD, 0, bindmap[i].retro) ? bindmap[i].nes : 0;
\t}

\t// This shouldn't matter. Why? Something very weird is going on.
#if defined(__CELLOS_LV2__) || defined(_XBOX360) || defined(GEKKO) // <-- big endian
\tJSReturn[0] = pad[0] | (pad[1] << 8);
#else
\tJSReturn[0] = pad[0];
\tJSReturn[1] = pad[1];
#endif
}
"""
    new_update = """static void update_input(void) {
\t/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827 */
#if defined(__CELLOS_LV2__) || defined(_XBOX360) || defined(GEKKO)
\tJSReturn[0] = aurora_direct_pad[0] | (aurora_direct_pad[1] << 8);
#else
\tJSReturn[0] = aurora_direct_pad[0];
\tJSReturn[1] = aurora_direct_pad[1];
#endif
}
"""
    text = replace_once(text, old_update, new_update, "V4 direct input")
    anchor = """void aurora_fds_set_skip_video(int skip) {
\taurora_skip_video = skip ? 1 : 0;
}

"""
    extra = """void aurora_fds_set_skip_video(int skip) {
\taurora_skip_video = skip ? 1 : 0;
}

void aurora_fds_set_pad_state(unsigned pad0, unsigned pad1) {
\taurora_direct_pad[0] = (uint8)pad0;
\taurora_direct_pad[1] = (uint8)pad1;
}

void aurora_fds_set_palette(const uint8 *rgb192) {
\tFCEUI_SetPaletteArray((uint8 *)rgb192);
}

const int32 *aurora_fds_get_audio_mono(void) { return sound; }
int aurora_fds_get_audio_frames(void) { return aurora_audio_frames; }

"""
    text = replace_once(text, anchor, extra, "V4 accessors")
    text = replace_once(text,
        "void retro_run(void) {\n\tunsigned y;\n\tuint8_t *gfx;\n",
        "void retro_run(void) {\n\tuint8_t *gfx;\n",
        "V4 retro_run index")
    text = replace_once(text,
        "\tfor (y = 0; y < ssize; y++)\n\t\tsound[y] = (sound[y] << 16) | (sound[y] & 0xffff);\n\n\taudio_batch_cb((const int16_t*)sound, ssize);\n}\n",
        "\t/* Native mono WaveFinal is consumed directly by Aurora. */\n\taurora_audio_frames = ssize;\n}\n",
        "V4 native mono audio")
    return text


def patch_v4_generated_video(text: str) -> str:
    old = """int FCEU_InitVirtualVideo(void) {
\tif (!XBuf)\t// Some driver code may allocate XBuf externally.
\t\t\t\t// 256 bytes per scanline, * 240 scanline maximum, +8 for alignment,
\t\tif (!(XBuf = (uint8*)(FCEU_malloc(256 * 256 + 8))))
\t\t\treturn 0;
\txbsave = XBuf;

\tif (sizeof(uint8*) == 4) {
\t\tuint32 m;
\t\tm = (uint32)XBuf;
\t\tm = (4 - m) & 3;
\t\tXBuf += m;
\t}
\tmemset(XBuf, 128, 256 * 256);
\treturn 1;
}
"""
    new = """int FCEU_InitVirtualVideo(void) {
\tif (!XBuf)
\t\t/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827: DMA-aligned T8 source. */
\t\tif (!(XBuf = (uint8*)(FCEU_malloc(256 * 256 + 64))))
\t\t\treturn 0;
\txbsave = XBuf;
\t{
\t\tuint32 m = (uint32)XBuf;
\t\tm = (64 - (m & 63)) & 63;
\t\tXBuf += m;
\t}
\tmemset(XBuf, 128, 256 * 256);
\treturn 1;
}
"""
    text = replace_once(text, old, new, "V4 XBuf alignment")
    text = replace_once(text,
        """void FCEU_PutImageDummy(void) {
\t/* AURORA_FCEUMM_FDS_V0_6_LEAN_VIDEO_DUMMY */
\t#ifdef SHOWFPS
\tShowFPS();
\t#endif
\tFCEU_DrawNTSCControlBars(XBuf);
\tFCEU_DrawSaveStates(XBuf);
\tif (howlong) howlong--;\t/* DrawMessage() */
}
""",
        """void FCEU_PutImageDummy(void) {
\t/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827: Aurora owns the OSD. */
\tif (howlong) howlong--;
}
""", "V4 dummy OSD")
    text = replace_once(text,
        """void FCEU_PutImage(void) {
\t/* AURORA_FCEUMM_FDS_V0_6_LEAN_VIDEO */
\t#ifdef SHOWFPS
\tShowFPS();
\t#endif
\tFCEU_DrawSaveStates(XBuf);
\tFCEU_DrawNTSCControlBars(XBuf);
\tDrawMessage();
\tFCEU_DrawInput(XBuf);
}

""",
        """void FCEU_PutImage(void) {
\t/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827: no embedded core OSD. */
\tif (howlong) howlong--;
}

""", "V4 image OSD")
    return text


# AURORA_FCEUMM_FDS_V5_CI_SMB_UPSTREAM_PERF_20260827
# Behaviour-preserving hot paths adapted from current libretro-fceumm.
def patch_v5_pputile(text: str) -> str:
    old = """if (X1 >= 2) {
\tuint8 *S = PALRAM;
\tuint32 pixdata;

\tpixdata = ppulut1[(pshift[0] >> (8 - XOffset)) & 0xFF] | ppulut2[(pshift[1] >> (8 - XOffset)) & 0xFF];

\tpixdata |= ppulut3[XOffset | (atlatch << 3)];

\tP[0] = S[pixdata & 0xF];
\tpixdata >>= 4;
\tP[1] = S[pixdata & 0xF];
\tpixdata >>= 4;
\tP[2] = S[pixdata & 0xF];
\tpixdata >>= 4;
\tP[3] = S[pixdata & 0xF];
\tpixdata >>= 4;
\tP[4] = S[pixdata & 0xF];
\tpixdata >>= 4;
\tP[5] = S[pixdata & 0xF];
\tpixdata >>= 4;
\tP[6] = S[pixdata & 0xF];
\tpixdata >>= 4;
\tP[7] = S[pixdata & 0xF];
\tP += 8;
}
"""
    new = """if (X1 >= 2) {
\tuint32 pixdata;
\tuint32 pair01, pair23;

\tpixdata = ppulut1[(pshift[0] >> (8 - XOffset)) & 0xFF] | ppulut2[(pshift[1] >> (8 - XOffset)) & 0xFF];
\tpixdata |= ppulut3[XOffset | (atlatch << 3)];

\t/* AURORA_FCEUMM_FDS_V5_CI_SMB_UPSTREAM_PERF_20260827
\t * Upstream a502ceaf: four 2-pixel LUT reads replace eight PALRAM
\t * nibble gathers.  PS2 uses two aligned 32-bit stores instead of the
\t * upstream unaligned 64-bit store, avoiding an R5900 alignment hazard. */
\tpair01 = (uint32)fceu_bg_pair_lut[pixdata & 0xFF] |
\t         ((uint32)fceu_bg_pair_lut[(pixdata >> 8) & 0xFF] << 16);
\tpair23 = (uint32)fceu_bg_pair_lut[(pixdata >> 16) & 0xFF] |
\t         ((uint32)fceu_bg_pair_lut[(pixdata >> 24) & 0xFF] << 16);
\t*(uint32 *)(P + 0) = pair01;
\t*(uint32 *)(P + 4) = pair23;
\tP += 8;
}
"""
    return replace_once(text, old, new, "V5 pputile pair LUT gather")


def patch_v5_ppu(text: str) -> str:
    count = text.count('#include "pputile.h"')
    if count != 9:
        fail(f"src/ppu.c pputile include count changed: {count}")
    text = text.replace('#include "pputile.h"', '#include "pputile_fds.h"')

    text = replace_once(
        text,
        "#define MMC5SPRVRAMADR(V)   &MMC5SPRVPage[(V) >> 10][(V)]\n",
        "/* AURORA_FCEUMM_FDS_V5_CI_SMB_UPSTREAM_PERF_20260827\n"
        " * Upstream a502ceaf/6911042c: 2-pixel palette-pair LUT. */\n"
        "static uint16 fceu_bg_pair_lut[256];\n"
        "static void FCEU_BuildBgPairLUT(void) {\n"
        "\tint b;\n"
        "\tfor (b = 0; b < 256; b++)\n"
        "\t\tfceu_bg_pair_lut[b] = (uint16)PALRAM[b & 0x0F] |\n"
        "\t\t\t((uint16)PALRAM[b >> 4] << 8);\n"
        "}\n\n"
        "#define MMC5SPRVRAMADR(V)   &MMC5SPRVPage[(V) >> 10][(V)]\n",
        "V5 ppu pair LUT definition",
    )

    # 6911042c is essential: build only AFTER priority bit 0x40 is ORed into
    # the universal-background palette entries.
    text = replace_once(
        text,
        "\tPal[0] |= 64;\n"
        "\tPal[4] |= 64;\n"
        "\tPal[8] |= 64;\n"
        "\tPal[0xC] |= 64;\n\n",
        "\tPal[0] |= 64;\n"
        "\tPal[4] |= 64;\n"
        "\tPal[8] |= 64;\n"
        "\tPal[0xC] |= 64;\n"
        "\t/* AURORA_FCEUMM_FDS_V5_CI_SMB_UPSTREAM_PERF_20260827:\n"
        "\t * priority-aware position matching upstream 6911042c. */\n"
        "\tFCEU_BuildBgPairLUT();\n\n",
        "V5 ppu priority-aware LUT rebuild",
    )

    text = replace_once(
        text,
        "\t\t\t\tif ((PPUViewer) && (scanline == PPUViewScanline)) UpdatePPUView(1);\n",
        "\t\t\t\t/* AURORA_FCEUMM_FDS_V5_CI_SMB_UPSTREAM_PERF_20260827:\n"
        "\t\t\t\t * embedded FDS-only core has no PPU viewer. */\n",
        "V5 remove unused PPU Viewer branch",
    )
    return text


def patch_v5_sound(text: str) -> str:
    old = """\tif (!inie[0] && !inie[1]) {
\t\tfor (V = start; V < end; V++)
\t\t\tWave[V >> 4] += totalout;
\t} else
"""
    new = """\tif (!inie[0] && !inie[1]) {
\t\t/* AURORA_FCEUMM_FDS_V5_CI_SMB_UPSTREAM_PERF_20260827
\t\t * Current upstream 9f6b84cf: amp[0]=amp[1]=0 here, therefore
\t\t * totalout == wlookup1[0] == 0. The old loop only added zero. */
\t} else
"""
    return replace_once(text, old, new, "V5 LQ silent-square no-op loop")


# AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827
# Final specialization pass over the generated FDS-only C files.
# All removed alternatives are unreachable in this build by construction:
# loader=FDS only; input=two ordinary pads; ResetCartMapping clears mapper PPU
# hooks; FDS never installs MMC5/NSF/PPU/HBlank hooks.
def patch_v6_pads(text: str) -> str:
    return replace_once(
        text,
        "extern uint8 LastStrobe;\n\n",
        "extern uint8 LastStrobe;\n\n"
        "/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827\n"
        " * Aurora has already converted both host pads to final NES bits before\n"
        " * retro_run.  Updating joy[] here avoids the generic two-port callback\n"
        " * walk without changing the strobe/read-bit state. */\n"
        "void FCEU_SetJoyDirect(uint8 p0, uint8 p1) {\n"
        "\tjoy[0] = p0;\n"
        "\tjoy[1] = p1;\n"
        "\tjoy[2] = 0;\n"
        "\tjoy[3] = 0;\n"
        "}\n\n",
        "V6 direct two-pad setter",
    )


def patch_v6_libretro(text: str) -> str:
    text = replace_once(
        text,
        "static uint8 aurora_direct_pad[2] = { 0, 0 };\n"
        "static int32 aurora_audio_frames = 0;\n"
        "extern void FCEUI_SetPaletteArray(uint8 *pal);\n",
        "static int32 aurora_audio_frames = 0;\n"
        "extern void FCEUI_SetPaletteArray(uint8 *pal);\n"
        "/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827 */\n"
        "extern void FCEU_SetJoyDirect(uint8 p0, uint8 p1);\n",
        "V6 libretro direct-pad state removal",
    )

    old_update = """static void update_input(void) {
\t/* AURORA_FCEUMM_FDS_V4_TURBO_PAL_PERF_20260827 */
#if defined(__CELLOS_LV2__) || defined(_XBOX360) || defined(GEKKO)
\tJSReturn[0] = aurora_direct_pad[0] | (aurora_direct_pad[1] << 8);
#else
\tJSReturn[0] = aurora_direct_pad[0];
\tJSReturn[1] = aurora_direct_pad[1];
#endif
}
"""
    text = replace_once(
        text,
        old_update,
        "/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827:\n"
        " * generic libretro input polling removed; Aurora pushes pad bytes. */\n",
        "V6 remove generated update_input",
    )

    text = replace_once(
        text,
        "void aurora_fds_set_pad_state(unsigned pad0, unsigned pad1) {\n"
        "\taurora_direct_pad[0] = (uint8)pad0;\n"
        "\taurora_direct_pad[1] = (uint8)pad1;\n"
        "}\n",
        "void aurora_fds_set_pad_state(unsigned pad0, unsigned pad1) {\n"
        "\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827 */\n"
        "\tFCEU_SetJoyDirect((uint8)pad0, (uint8)pad1);\n"
        "}\n",
        "V6 direct pad setter call",
    )

    text = replace_once(
        text,
        "\tupdate_input();\n\n\tFCEUI_Emulate(&gfx, &sound, &ssize, skip_video ? 1 : 0);\n",
        "\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827:\n"
        "\t * pad bytes were pushed by aurora_fds_set_pad_state(). */\n"
        "\tFCEUI_Emulate(&gfx, &sound, &ssize, skip_video ? 1 : 0);\n",
        "V6 remove retro_run input callback",
    )
    return text


def patch_v6_fceu(text: str) -> str:
    text = replace_once(
        text,
        "void FCEUI_Emulate(uint8 **pXBuf, int32 **SoundBuf, int32 *SoundBufSize, int skip) {\n"
        "\tint r, ssize;\n\n"
        "\tFCEU_UpdateInput();\n",
        "void FCEUI_Emulate(uint8 **pXBuf, int32 **SoundBuf, int32 *SoundBufSize, int skip) {\n"
        "\tint ssize;\n\n"
        "\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827:\n"
        "\t * the retained two-pad backend already contains this frame's bytes. */\n",
        "V6 FCEUI_Emulate generic input removal",
    )
    text = replace_once(
        text,
        "\tr = FCEUPPU_Loop(skip);\n",
        "\tFCEUPPU_Loop(skip);\n",
        "V6 unused PPU return removal",
    )
    return text


def patch_v6_ppu(text: str) -> str:
    # ResetRL's scanline input callback can never be installed by INPUT_FDS_C.
    text = replace_once(
        text,
        "\tif (InputScanlineHook)\n\t\tInputScanlineHook(0, 0, 0, 0);\n",
        "\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827:\n"
        "\t * no zapper/peripheral scanline hook in the two-pad FDS build. */\n",
        "V6 ResetRL scanline hook removal",
    )

    text = replace_once(
        text,
        "\t\tif (InputScanlineHook && (lastpixel - 16) >= 0) {\n"
        "\t\t\tInputScanlineHook(Plinef, spork ? sprlinebuf : 0, linestartts, lasttile * 8 - 16);\n"
        "\t\t}\n",
        "\t\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827: no scanline input hook. */\n",
        "V6 RefreshLine early scanline hook",
    )
    text = replace_once(
        text,
        "\tif (InputScanlineHook && (lastpixel - 16) >= 0) {\n"
        "\t\tInputScanlineHook(Plinef, spork ? sprlinebuf : 0, linestartts, lasttile * 8 - 16);\n"
        "\t}\n",
        "\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827: no scanline input hook. */\n",
        "V6 RefreshLine final scanline hook",
    )

    text = replace_once(
        text,
        "\tif(PEC586Hack)\n"
        "\t\tvofs = ((RefreshAddr & 0x200) << 3) | ((RefreshAddr >> 12) & 7);\n"
        "\telse\n"
        "\t\tvofs = ((PPU[0] & 0x10) << 8) | ((RefreshAddr >> 12) & 7);\n",
        "\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827: PEC586 is impossible in FDS. */\n"
        "\tvofs = ((PPU[0] & 0x10) << 8) | ((RefreshAddr >> 12) & 7);\n",
        "V6 RefreshLine PEC586 branch removal",
    )

    # Collapse MMC5 / mapper-hook / PEC586 alternatives to the ordinary FDS tile
    # loop.  Keep LUT placement, palette priority bits and every tile iteration.
    start = "#define PPUT_MMC5\n"
    end = "\n#undef vofs\n"
    if text.count(start) != 1 or text.count(end) != 1:
        fail("V6 RefreshLine mapper-branch span mudou")
    i = text.index(start)
    j = text.index(end, i)
    text = (
        text[:i]
        + "/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827\n"
          " * FDS installs no MMC5/PEC586/PPU mapper hook: execute the exact\n"
          " * ordinary tile body directly. */\n"
          "\tfor (X1 = firsttile; X1 < lasttile; X1++) {\n"
          "\t\t#include \"pputile_fds.h\"\n"
          "\t}\n"
        + text[j:]
    )

    text = replace_once(
        text,
        "\tif (MMC5Hack && (ScreenON || SpriteON)) MMC5_hb(scanline);\n\n"
        "\tX6502_Run(256);\n",
        "\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827: no MMC5 HBlank hook. */\n"
        "\tX6502_Run(256);\n",
        "V6 DoLine MMC5 hook removal",
    )

    irq_block = """\tif (GameHBIRQHook && (ScreenON || SpriteON) && ((PPU[0] & 0x38) != 0x18)) {
\t\tX6502_Run(6);
\t\tFixit2();
\t\tX6502_Run(4);
\t\tGameHBIRQHook();
\t\tX6502_Run(85 - 16 - 10);
\t} else {
\t\tX6502_Run(6);\t// Tried 65, caused problems with Slalom(maybe others)
\t\tFixit2();
\t\tX6502_Run(85 - 6 - 16);

\t\t// A semi-hack for Star Trek: 25th Anniversary
\t\tif (GameHBIRQHook && (ScreenON || SpriteON) && ((PPU[0] & 0x38) != 0x18))
\t\t\tGameHBIRQHook();
\t}
"""
    irq_direct = """\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827
\t * FDS has no GameHBIRQHook. This is exactly the old hook-null branch. */
\tX6502_Run(6);
\tFixit2();
\tX6502_Run(85 - 6 - 16);
"""
    text = replace_once(text, irq_block, irq_direct, "V6 DoLine HBlank branch collapse")
    text = replace_once(
        text,
        "\tif (GameHBIRQHook2 && (ScreenON || SpriteON))\n\t\tGameHBIRQHook2();\n",
        "\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827: no secondary HBlank hook. */\n",
        "V6 DoLine secondary HBlank removal",
    )

    fetch_start = "static void FetchSpriteData(void) {\n"
    fetch_end = "static void RefreshSprites(void) {\n"
    if text.count(fetch_start) != 1 or text.count(fetch_end) != 1:
        fail("V6 FetchSpriteData span mudou")
    i = text.index(fetch_start)
    j = text.index(fetch_end, i)
    fetch_direct = """static void FetchSpriteData(void) {
\tuint8 ns, sb;
\tSPR *spr;
\tuint8 H;
\tint n;
\tint vofs;
\tuint8 P0 = PPU[0];

\tspr = (SPR*)SPRAM;
\tH = 8;
\tns = sb = 0;

\tvofs = (uint32)(P0 & 0x8 & (((P0 & 0x20) ^ 0x20) >> 2)) << 9;
\tH += (P0 & 0x20) >> 2;

\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827
\t * FDS has no PPU mapper hook and no MMC5 CHR path. This is the exact
\t * old !PPU_hook / !MMC5 branch, without testing those globals per line. */
\tfor (n = 63; n >= 0; n--, spr++) {
\t\tif ((uint32)(scanline - spr->y) >= H) continue;
\t\tif (ns < maxsprites) {
\t\t\tif (n == 63) sb = 1;

\t\t\t{
\t\t\t\tSPRB dst;
\t\t\t\tuint8 *C;
\t\t\t\tint t;
\t\t\t\tuint32 vadr;

\t\t\t\tt = (int)scanline - (spr->y);

\t\t\t\tif (Sprite16)
\t\t\t\t\tvadr = ((spr->no & 1) << 12) + ((spr->no & 0xFE) << 4);
\t\t\t\telse
\t\t\t\t\tvadr = (spr->no << 4) + vofs;

\t\t\t\tif (spr->atr & V_FLIP) {
\t\t\t\t\tvadr += 7;
\t\t\t\t\tvadr -= t;
\t\t\t\t\tvadr += (P0 & 0x20) >> 1;
\t\t\t\t\tvadr -= t & 8;
\t\t\t\t} else {
\t\t\t\t\tvadr += t;
\t\t\t\t\tvadr += t & 8;
\t\t\t\t}

\t\t\t\tC = VRAMADR(vadr);
\t\t\t\tdst.ca[0] = C[0];
\t\t\t\tdst.ca[1] = C[8];
\t\t\t\tdst.x = spr->x;
\t\t\t\tdst.atr = spr->atr;
\t\t\t\t*(uint32*)&SPRBUF[ns << 2] = *(uint32*)&dst;
\t\t\t}

\t\t\tns++;
\t\t} else {
\t\t\tPPU_status |= 0x20;
\t\t\tbreak;
\t\t}
\t}

\tif (ns > 8) PPU_status |= 0x20;
\tnumsprites = ns;
\tSpriteBlurp = sb;
}

"""
    text = text[:i] + fetch_direct + text[j:]

    text = replace_once(
        text,
        "\t\tif (GameInfo->type == GIT_NSF)\n"
        "\t\t\tDoNSFFrame();\n"
        "\t\telse {\n"
        "\t\t\tif (VBlankON)\n"
        "\t\t\t\tTriggerNMI();\n"
        "\t\t}\n",
        "\t\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827: FDS-only, never NSF. */\n"
        "\t\tif (VBlankON)\n"
        "\t\t\tTriggerNMI();\n",
        "V6 PPU NSF vblank branch removal",
    )

    pre_render = """\t\t{
\t\t\tint x;

\t\t\tif (ScreenON || SpriteON) {
\t\t\t\tif (GameHBIRQHook && ((PPU[0] & 0x38) != 0x18))
\t\t\t\t\tGameHBIRQHook();
\t\t\t\tif (PPU_hook)
\t\t\t\t\tfor (x = 0; x < 42; x++) {
\t\t\t\t\t\tPPU_hook(0x2000); PPU_hook(0);
\t\t\t\t\t}
\t\t\t\tif (GameHBIRQHook2)
\t\t\t\t\tGameHBIRQHook2();
\t\t\t}
\t\t\tX6502_Run(85 - 16);
\t\t\tif (ScreenON || SpriteON) {
\t\t\t\tRefreshAddr = TempAddr;
\t\t\t\tif (PPU_hook) PPU_hook(RefreshAddr & 0x3fff);
\t\t\t}

\t\t\t//Clean this stuff up later.
\t\t\tspork = numsprites = 0;
\t\t\tResetRL(XBuf);

\t\t\tX6502_Run(16 - kook);
\t\t\tkook ^= 1;
\t\t}
"""
    pre_render_direct = """\t\t{
\t\t\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827:
\t\t\t * mapper HBlank/PPU hooks are null in FDS. Preserve cycle order. */
\t\t\tX6502_Run(85 - 16);
\t\t\tif (ScreenON || SpriteON)
\t\t\t\tRefreshAddr = TempAddr;

\t\t\tspork = numsprites = 0;
\t\t\tResetRL(XBuf);

\t\t\tX6502_Run(16 - kook);
\t\t\tkook ^= 1;
\t\t}
"""
    text = replace_once(text, pre_render, pre_render_direct, "V6 PPU pre-render hook collapse")

    text = replace_once(
        text,
        "\t\tif (GameInfo->type == GIT_NSF)\n"
        "\t\t\tX6502_Run((256 + 85) * 240);\n"
        "\t\t#ifdef FRAMESKIP\n"
        "\t\telse if (skip) {\n",
        "\t\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827: no NSF visible-frame branch. */\n"
        "\t\t#ifdef FRAMESKIP\n"
        "\t\tif (skip) {\n",
        "V6 PPU NSF visible branch removal",
    )
    text = replace_once(
        text,
        "\t\t#endif\n\t\telse {\n",
        "\t\telse\n"
        "\t\t#endif\n"
        "\t\t{\n",
        "V6 PPU normal-frame block",
    )
    text = replace_once(
        text,
        "\t\t\tif (MMC5Hack && (ScreenON || SpriteON)) MMC5_hb(scanline);\n",
        "\t\t\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827: no MMC5 end-of-frame hook. */\n",
        "V6 PPU end-frame MMC5 hook removal",
    )
    return text


def patch_v6_sound(text: str) -> str:
    text = replace_once(
        text,
        '#include "wave.h"\n',
        '/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827: embedded core has no WAV recorder. */\n',
        "V6 remove wave recorder header",
    )
    text = replace_once(
        text,
        "\tFCEU_WriteWaveData(WaveFinal, end);\t/* This function will just return\n"
        "\t\t\t\t\t\t\t\t\t\tif sound recording is off. */\n",
        "\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827:\n"
        "\t * sound recording is not exposed by Aurora; avoid the per-frame call/VLA path. */\n",
        "V6 remove disabled WAV call",
    )
    old = """\t} else {
\t\tfor (V = start; V < end; V++)
\t\t\tWave[V >> 4] += totalout;
\t}
}


static void RDoNoise(void) {
"""
    new = """\t} else if (totalout) {
\t\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827:
\t\t * if tri+noise are inactive and the held DMC output also maps to 0,
\t\t * the old loop only added zero. Preserve it when DMC contributes. */
\t\tfor (V = start; V < end; V++)
\t\t\tWave[V >> 4] += totalout;
\t}
}


static void RDoNoise(void) {
"""
    text = replace_once(text, old, new, "V6 silent tri/noise/DMC zero loop")
    return text


def patch_v6_fds(text: str) -> str:
    helper = """/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827
 * Old FDS APU master-volume divisor is one of 2,3,4,5 on every generated
 * sample.  cwave<=63 and amplitude<=32, so n=(cwave*amp)*4 is 0..8064.
 * The reciprocal forms below are EXACT for every integer in that complete
 * reachable range; they remove a MIPS integer divide from the sample loop. */
static INLINE int32 AuroraFDSScaleMaster(uint32 sample, uint8 master) {
\tuint32 n = sample << 2;
\tswitch (master & 3) {
\tcase 0: return (int32)(n >> 1);                    /* /2 */
\tcase 1: return (int32)((n * 2731U) >> 13);         /* /3 exact for n<=8064 */
\tcase 2: return (int32)(n >> 2);                    /* /4 */
\tdefault:return (int32)((n * 3277U) >> 14);         /* /5 exact for n<=8064 */
\t}
}

"""
    text = replace_once(
        text,
        "static INLINE int32 FDSDoSound(void) {\n",
        helper + "static INLINE int32 FDSDoSound(void) {\n",
        "V6 FDS exact master-volume helper",
    )
    text = replace_once(
        text,
        "\t\treturn (fdso.cwave[b24latch68 >> 19] * k) * 4 / ((SPSG[0x9] & 0x3) + 2);\n",
        "\t\treturn AuroraFDSScaleMaster(\n"
        "\t\t\t(uint32)fdso.cwave[b24latch68 >> 19] * (uint32)k, SPSG[0x9]);\n",
        "V6 FDS per-sample divide removal",
    )

    text = replace_once(
        text,
        "\tif (FSettings.SndRate) {\n"
        "\t\tif (FSettings.soundq >= 1)\n"
        "\t\t\tRenderSoundHQ();\n"
        "\t\telse\n"
        "\t\t\tRenderSound();\n"
        "\t}\n"
        "\tA -= 0x4080;\n",
        "\tif (FSettings.SndRate)\n"
        "\t\t/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827:\n"
        "\t\t * this embedded target is permanently SOUND_QUALITY=0. */\n"
        "\t\tRenderSound();\n"
        "\tA -= 0x4080;\n",
        "V6 FDS register-write LQ branch",
    )
    return text


# AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827
# Post-pass over the already-specialized V6 generated FDS-only files.

def patch_v7_libretro(text: str) -> str:
    text = replace_once(
        text,
        "static uint16_t palette[256];\n",
        "static uint16_t palette[256];\n"
        "/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827:\n"
        " * host CLUT invalidation without scanning all 256 RGB555 entries. */\n"
        "static uint32 aurora_palette_serial = 1;\n",
        "V7 palette serial state",
    )

    old_set = """void FCEUD_SetPalette(unsigned char index, unsigned char r, unsigned char g, unsigned char b) {
\tr >>= 3;
\tg >>= 3;
\tb >>= 3;
\tpalette[index] = (r << 10) | (g << 5) | (b << 0);
}
"""
    new_set = """void FCEUD_SetPalette(unsigned char index, unsigned char r, unsigned char g, unsigned char b) {
\tuint16 value;
\tr >>= 3;
\tg >>= 3;
\tb >>= 3;
\tvalue = (uint16)((r << 10) | (g << 5) | (b << 0));
\tif (palette[index] != value) {
\t\tpalette[index] = value;
\t\tif (++aurora_palette_serial == 0)
\t\t\taurora_palette_serial = 1;
\t}
}
"""
    text = replace_once(text, old_set, new_set, "V7 FCEUD_SetPalette serial")

    text = replace_once(
        text,
        """const uint16_t *aurora_fds_get_video_palette(void) {
\treturn palette;
}

""",
        """const uint16_t *aurora_fds_get_video_palette(void) {
\treturn palette;
}

uint32 aurora_fds_get_palette_serial(void) {
\treturn aurora_palette_serial;
}

""",
        "V7 palette serial accessor",
    )
    return text


def patch_v7_ppu(text: str) -> str:
    old_lut = """static uint16 fceu_bg_pair_lut[256];
static void FCEU_BuildBgPairLUT(void) {
\tint b;
\tfor (b = 0; b < 256; b++)
\t\tfceu_bg_pair_lut[b] = (uint16)PALRAM[b & 0x0F] |
\t\t\t((uint16)PALRAM[b >> 4] << 8);
}
"""
    new_lut = """static uint16 fceu_bg_pair_lut[256];
static uint8 fceu_bg_pair_lut_pal[16];
static uint8 fceu_bg_pair_lut_valid = 0;

static void FCEU_BuildBgPairLUT(void) {
\tint b;

\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827
\t * RefreshLine may run several times in one scanline after PPU writes.
\t * Rebuild the 256 pairs only when the 16 BG palette bytes changed. */
\tif (fceu_bg_pair_lut_valid &&
\t    memcmp(fceu_bg_pair_lut_pal, PALRAM, sizeof(fceu_bg_pair_lut_pal)) == 0)
\t\treturn;

\tmemcpy(fceu_bg_pair_lut_pal, PALRAM, sizeof(fceu_bg_pair_lut_pal));
\tfor (b = 0; b < 256; b++)
\t\tfceu_bg_pair_lut[b] = (uint16)PALRAM[b & 0x0F] |
\t\t\t((uint16)PALRAM[b >> 4] << 8);
\tfceu_bg_pair_lut_valid = 1;
}
"""
    text = replace_once(text, old_lut, new_lut, "V7 cached PPU pair LUT")

    text = replace_once(
        text,
        "\tif (norecurse) return;\n",
        "\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827: PPU_hook recursion path absent. */\n",
        "V7 remove dead RefreshLine norecurse branch",
    )

    old_bg_disable = """\tif (rendis & 2) {// User asked to not display background data.
\t\tuint32 tem;
\t\ttem = Pal[0] | (Pal[0] << 8) | (Pal[0] << 16) | (Pal[0] << 24);
\t\ttem |= 0x40404040;
\t\tFCEU_dwmemset(target, tem, 256);
\t}

"""
    text = replace_once(
        text, old_bg_disable,
        "\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827: embedded frontend never disables BG rendering. */\n\n",
        "V7 remove dead BG render-disable branch",
    )
    text = replace_once(
        text,
        "\tif (rendis & 1) return;\t//User asked to not display sprites.\n",
        "\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827: embedded frontend never disables sprite rendering. */\n",
        "V7 remove dead sprite render-disable branch",
    )

    old = "\t\t\tif (PPU_hook) PPU_hook(tmp);\n"
    if text.count(old) != 2:
        fail(f"V7 A2007 PPU_hook count changed: {text.count(old)}")
    text = text.replace(
        old,
        "\t\t\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827: no FDS PPU mapper hook. */\n",
    )
    text = replace_once(
        text,
        "\t\tif (PPU_hook) PPU_hook(RefreshAddr & 0x3fff);\n",
        "\t\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827: no FDS PPU mapper hook. */\n",
        "V7 A2007 post-read PPU hook",
    )
    text = replace_once(
        text,
        "\t\tif (PPU_hook)\n\t\t\tPPU_hook(RefreshAddr);\n",
        "\t\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827: no FDS PPU mapper hook. */\n",
        "V7 B2006 PPU hook",
    )
    text = replace_once(
        text,
        "\tif (PPU_hook)\n\t\tPPU_hook(RefreshAddr & 0x3fff);\n",
        "\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827: no FDS PPU mapper hook. */\n",
        "V7 B2007 PPU hook",
    )

    start = "\t\t#ifdef FRAMESKIP\n\t\tif (skip) {\n"
    end = "\t\telse\n\t\t#endif\n\t\t{\n"
    if text.count(start) != 1 or text.count(end) != 1:
        fail(
            "V7 PPU frameskip span mudou: "
            f"start={text.count(start)} end={text.count(end)}"
        )
    i = text.index(start)
    j = text.index(end, i)
    skip = """\t\t#ifdef FRAMESKIP
\t\tif (skip) {
\t\t\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827
\t\t\t * Pinned FCEUmm FRAMESKIP path, specialized to FDS-only.
\t\t\t * CPU/FDS timers/APU still advance through X6502_Run(). */
\t\t\tint y = (int)SPRAM[0] + 1;

\t\t\tPPU_status |= 0x20;
\t\t\tif (y < 240) {
\t\t\t\tX6502_Run((256 + 85) * y);
\t\t\t\tif (SpriteON)
\t\t\t\t\tPPU_status |= 0x40;
\t\t\t\tX6502_Run((256 + 85) * (240 - y));
\t\t\t} else {
\t\t\t\tX6502_Run((256 + 85) * 240);
\t\t\t}
\t\t}
"""
    text = text[:i] + skip + text[j:]
    return text


def patch_v7_fds(text: str) -> str:
    old_decl = "static void FP_FASTAPASS(1) FDSFix(int a);"
    if text.count(old_decl) != 1:
        fail(f"V7 FDSFix declaration count changed: {text.count(old_decl)}")
    text = text.replace(
        old_decl,
        "/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827 */\n"
        "void FP_FASTAPASS(1) FDSFix(int a);",
        1,
    )
    old_def = "static void FP_FASTAPASS(1) FDSFix(int a) {"
    if text.count(old_def) != 1:
        fail(f"V7 FDSFix definition count changed: {text.count(old_def)}")
    text = text.replace(old_def, "void FP_FASTAPASS(1) FDSFix(int a) {", 1)

    old_scale = """/* AURORA_FCEUMM_FDS_V6_FDS_ONLY_HOTPATHS_SMB_STATIC_20260827
 * Old FDS APU master-volume divisor is one of 2,3,4,5 on every generated
 * sample.  cwave<=63 and amplitude<=32, so n=(cwave*amp)*4 is 0..8064.
 * The reciprocal forms below are EXACT for every integer in that complete
 * reachable range; they remove a MIPS integer divide from the sample loop. */
static INLINE int32 AuroraFDSScaleMaster(uint32 sample, uint8 master) {
\tuint32 n = sample << 2;
\tswitch (master & 3) {
\tcase 0: return (int32)(n >> 1);                    /* /2 */
\tcase 1: return (int32)((n * 2731U) >> 13);         /* /3 exact for n<=8064 */
\tcase 2: return (int32)(n >> 2);                    /* /4 */
\tdefault:return (int32)((n * 3277U) >> 14);         /* /5 exact for n<=8064 */
\t}
}

"""
    new_scale = """/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827
 * Cache all 64 carrier results for the current clamped amplitude/master.
 * The exact V6 reciprocal formulas are now paid only when that key changes. */
static uint16 AuroraFDSMixTable[64];
static uint8 AuroraFDSMixKey = 0xFF;

static INLINE int32 AuroraFDSScaleExact(uint32 sample, uint8 master) {
\tuint32 n = sample << 2;
\tswitch (master & 3) {
\tcase 0: return (int32)(n >> 1);
\tcase 1: return (int32)((n * 2731U) >> 13);
\tcase 2: return (int32)(n >> 2);
\tdefault:return (int32)((n * 3277U) >> 14);
\t}
}

static INLINE void AuroraFDSRefreshMixTable(void) {
\tuint32 k = amplitude[0];
\tuint8 master = SPSG[0x9] & 3;
\tuint8 key;
\tint i;

\tif (k > 0x20)
\t\tk = 0x20;
\tkey = (uint8)((k << 2) | master);
\tif (key == AuroraFDSMixKey)
\t\treturn;

\tfor (i = 0; i < 64; ++i)
\t\tAuroraFDSMixTable[i] =
\t\t\t(uint16)AuroraFDSScaleExact((uint32)i * k, master);
\tAuroraFDSMixKey = key;
}

"""
    text = replace_once(text, old_scale, new_scale, "V7 cached FDS master-volume table")

    old_return = """\t// Might need to emulate applying the amplitude to the waveform a bit better...
\t{
\t\tint k = amplitude[0];
\t\tif (k > 0x20) k = 0x20;
\t\treturn AuroraFDSScaleMaster(
\t\t\t(uint32)fdso.cwave[b24latch68 >> 19] * (uint32)k, SPSG[0x9]);
\t}
"""
    new_return = """\t// Might need to emulate applying the amplitude to the waveform a bit better...
\tAuroraFDSRefreshMixTable();
\treturn (int32)AuroraFDSMixTable[fdso.cwave[b24latch68 >> 19] & 0x3F];
"""
    text = replace_once(text, old_return, new_return, "V7 FDS cached sample lookup")

    text = replace_once(
        text,
        "static int ta;\n",
        "/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827: dead ta counter removed. */\n",
        "V7 dead FDS ta declaration",
    )
    text = replace_once(
        text,
        "\t\tta++;\n",
        "\t\t/* V7: old ta++ debug counter removed. */\n",
        "V7 dead FDS ta increment",
    )

    old_render = """static void RenderSound(void) {
\tint32 end, start;
\tint32 x;

\tstart = FBC;
\tend = (SOUNDTS << 16) / soundtsinc;
\tif (end <= start)
\t\treturn;
\tFBC = end;

\tif (!(SPSG[0x9] & 0x80))
\t\tfor (x = start; x < end; x++) {
\t\t\tuint32 t = FDSDoSound();
\t\t\tt += t >> 1;
\t\t\tt >>= 4;
\t\t\tWave[x >> 4] += t;\t//(t>>2)-(t>>3); //>>3;
\t\t}
}
"""
    new_render = """static void RenderSound(void) {
\tint32 end, start;
\tint32 x;
\tint32 waveIndex;
\tint32 accum;

\tstart = FBC;
\tend = (SOUNDTS << 16) / soundtsinc;
\tif (end <= start)
\t\treturn;
\tFBC = end;

\tif (SPSG[0x9] & 0x80)
\t\treturn;

\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827
\t * x>>4 maps up to 16 consecutive FDS sub-samples to one Wave[] cell.
\t * FDSDoSound still executes once for every original x, in order. */
\twaveIndex = start >> 4;
\taccum = 0;
\tfor (x = start; x < end; x++) {
\t\tint32 t = FDSDoSound();
\t\tint32 idx = x >> 4;

\t\tt += t >> 1;
\t\tt >>= 4;
\t\tif (idx != waveIndex) {
\t\t\tWave[waveIndex] += accum;
\t\t\twaveIndex = idx;
\t\t\taccum = 0;
\t\t}
\t\taccum += t;
\t}
\tif (accum)
\t\tWave[waveIndex] += accum;
}
"""
    text = replace_once(text, old_render, new_render, "V7 grouped FDS expansion Wave writes")
    return text


def patch_v7_x6502(text: str) -> str:
    text = replace_once(
        text,
        "void FP_FASTAPASS(1) (*MapIRQHook)(int a);\n",
        "void FP_FASTAPASS(1) (*MapIRQHook)(int a);\n"
        "/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827:\n"
        " * this generated archive can only run FDS, whose hook is FDSFix. */\n"
        "void FP_FASTAPASS(1) FDSFix(int a);\n",
        "V7 x6502 direct FDS IRQ declaration",
    )

    old = "\t\tif (MapIRQHook) MapIRQHook(temp);\n"
    n = text.count(old)
    if n != 2:
        fail(f"V7 x6502 MapIRQHook calls: esperado 2, encontrado {n}")
    text = text.replace(
        old,
        "\t\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827: direct FDS timer/IRQ hook, same temp and instruction boundary. */\n"
        "\t\tFDSFix(temp);\n",
    )

    text = replace_once(
        text,
        "static uint8 CycTable[256] =\n",
        "static const uint8 CycTable[256] =\n",
        "V7 x6502 immutable cycle table",
    )
    return text



# AURORA_FCEUMM_FDS_V8_NTSC_LQ_PPU64_REVIVE_ACCURACY_20260827
# Conservative FDS-only post-pass.  Keep FCEUmm's existing LQ filter/output.

def v8_replace(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    if old not in text:
        fail("V8 " + label + ": trecho esperado não encontrado")
    return text.replace(old, new, 1)



def patch_v8_fceu(text: str) -> str:
    old = """void FCEU_ResetVidSys(void) {
\tint w;

\tif (GameInfo->vidsys == GIV_NTSC)
\t\tw = 0;
\telse if (GameInfo->vidsys == GIV_PAL)
\t\tw = 1;
\telse
\t\tw = FSettings.PAL;

\tPAL = w ? 1 : 0;

\tFCEUPPU_SetVideoSystem(w);
\tSetSoundVariables();
}
"""
    new = """void FCEU_ResetVidSys(void) {
\t/* AURORA_FCEUMM_FDS_V8_NTSC_LQ_PPU64_REVIVE_ACCURACY_20260827:
\t * Famicom Disk System hardware is NTSC-only. */
\tPAL = 0;
\tFSettings.PAL = 0;
\tFCEUPPU_SetVideoSystem(0);
\tSetSoundVariables();
}
"""
    text = v8_replace(text, old, new, "V8 FDS NTSC ResetVidSys")

    old = """void FCEUI_SetVidSystem(int a) {
\tFSettings.PAL = a ? 1 : 0;
\tif (GameInfo) {
\t\tFCEU_ResetVidSys();
\t\tFCEU_ResetPalette();
\t}
}
"""
    new = """void FCEUI_SetVidSystem(int a) {
\t(void)a;
\tFSettings.PAL = 0;
\tif (GameInfo) {
\t\tFCEU_ResetVidSys();
\t\tFCEU_ResetPalette();
\t}
}
"""
    text = v8_replace(text, old, new, "V8 FDS reject impossible PAL switch")

    old = """int32 FCEUI_GetDesiredFPS(void) {
\tif (PAL)
\t\treturn(838977920);\t// ~50.007
\telse
\t\treturn(1008307711);\t// ~60.1
}
"""
    new = """int32 FCEUI_GetDesiredFPS(void) {
\treturn(1008307711); /* V8: FDS NTSC ~60.1 */
}
"""
    text = v8_replace(text, old, new, "V8 FDS constant NTSC FPS")
    return text



def patch_v8_ppu(text: str) -> str:
    text = v8_replace(
        text,
        "#define GETLASTPIXEL    (PAL ? ((timestamp * 48 - linestartts) / 15) : ((timestamp * 48 - linestartts) >> 4))\n",
        "/* AURORA_FCEUMM_FDS_V8_NTSC_LQ_PPU64_REVIVE_ACCURACY_20260827: FDS NTSC-only. */\n"
        "#define GETLASTPIXEL    ((timestamp * 48 - linestartts) >> 4)\n",
        "V8 NTSC GETLASTPIXEL",
    )

    # V6 already specialized FetchSpriteData to the ordinary FDS path.
    text = v8_replace(
        text,
        "\t\tif (ns < maxsprites) {\n",
        "\t\tif (ns < 8) { /* V8: Aurora exposes no unlimited-sprites option for FDS. */\n",
        "V8 fixed FDS sprite limit",
    )
    text = v8_replace(
        text,
        "\tif (ns > 8) PPU_status |= 0x20;\n\tnumsprites = ns;\n",
        "\t/* V8: overflow is already latched in the ninth-sprite else branch. */\n\tnumsprites = ns;\n",
        "V8 remove impossible post-loop sprite overflow branch",
    )

    text = v8_replace(
        text,
        "static uint8 sprlinebuf[256 + 8];\n",
        "/* V8: aligned so the per-scanline scratch clear can use 64-bit stores. */\n"
        "static uint8 sprlinebuf[256 + 8] __attribute__((aligned(64)));\n",
        "V8 align sprite line buffer",
    )
    text = v8_replace(
        text,
        "\tFCEU_dwmemset(sprlinebuf, 0x80808080, 256);\n",
        "\t{\n"
        "\t\tuint64 *pClear = (uint64 *)sprlinebuf;\n"
        "\t\tconst uint64 clear64 = 0x8080808080808080ULL;\n"
        "\t\tint iClear;\n"
        "\t\tfor (iClear = 0; iClear < 32; ++iClear)\n"
        "\t\t\tpClear[iClear] = clear64;\n"
        "\t}\n",
        "V8 64-bit sprite scratch clear",
    )

    old = """\tif (ScreenON || SpriteON) {\t// Yes, very el-cheapo.
\t\tif (PPU[1] & 0x01) {
\t\t\tfor (x = 63; x >= 0; x--)
\t\t\t\t*(uint32*)&target[x << 2] = (*(uint32*)&target[x << 2]) & 0x30303030;
\t\t}
\t}
\tif ((PPU[1] >> 5) == 0x7) {
\t\tfor (x = 63; x >= 0; x--)
\t\t\t*(uint32*)&target[x << 2] = ((*(uint32*)&target[x << 2]) & 0x3f3f3f3f) | 0xc0c0c0c0;
\t} else if (PPU[1] & 0xE0)
\t\tfor (x = 63; x >= 0; x--)
\t\t\t*(uint32*)&target[x << 2] = (*(uint32*)&target[x << 2]) | 0x40404040;
\telse
\t\tfor (x = 63; x >= 0; x--)
\t\t\t*(uint32*)&target[x << 2] = ((*(uint32*)&target[x << 2]) & 0x3f3f3f3f) | 0x80808080;
"""
    new = """\t/* AURORA_FCEUMM_FDS_V8_NTSC_LQ_PPU64_REVIVE_ACCURACY_20260827
\t * V4 aligns XBuf to 64 bytes and every 256-byte scanline remains aligned.
\t * Fuse the old grayscale pass and emphasis pass into one byte-identical
\t * 32-store uint64 pass. */
\t{
\t\tuint64 andMask;
\t\tuint64 orMask;
\t\tuint8 byteAnd;
\t\tuint8 byteOr;
\t\tuint8 emphasis = (PPU[1] >> 5) & 7;
\t\tuint8 grayMask =
\t\t\t((ScreenON || SpriteON) && (PPU[1] & 0x01)) ? 0x30 : 0xFF;

\t\tif (emphasis == 7) {
\t\t\tbyteAnd = grayMask & 0x3F;
\t\t\tbyteOr = 0xC0;
\t\t} else if (emphasis) {
\t\t\tbyteAnd = grayMask;
\t\t\tbyteOr = 0x40;
\t\t} else {
\t\t\tbyteAnd = grayMask & 0x3F;
\t\t\tbyteOr = 0x80;
\t\t}

\t\tandMask = (uint64)byteAnd * 0x0101010101010101ULL;
\t\torMask  = (uint64)byteOr  * 0x0101010101010101ULL;
\t\tfor (x = 31; x >= 0; --x) {
\t\t\tuint64 *p = ((uint64 *)target) + x;
\t\t\t*p = (*p & andMask) | orMask;
\t\t}
\t}
"""
    text = v8_replace(text, old, new, "V8 fused 64-bit grayscale/emphasis")
    return text



def patch_v8_sound(text: str) -> str:
    # Keep the old function-pointer declarations because legacy HQ helpers still
    # compile, but hot call sites below use guarded direct LQ calls.
    anchor = """static void (*DoSQ2)(void) = Dummyfunc;

static uint32 ChannelBC[5];
"""
    replacement = """static void (*DoSQ2)(void) = Dummyfunc;

/* AURORA_FCEUMM_FDS_V8_NTSC_LQ_PPU64_REVIVE_ACCURACY_20260827
 * Embedded FDS always runs soundq=0.  Direct calls avoid indirect dispatch on
 * register writes while soundtsinc==0 preserves Dummyfunc semantics. */
static void RDoSQLQ(void);
static void RDoTriangleNoisePCMLQ(void);
static INLINE void AuroraFdsFlushSQ(void) {
\tif (soundtsinc) RDoSQLQ();
}
static INLINE void AuroraFdsFlushTND(void) {
\tif (soundtsinc) RDoTriangleNoisePCMLQ();
}

static uint32 ChannelBC[5];
"""
    text = v8_replace(text, anchor, replacement, "V8 LQ direct-call helpers")

    # Replace only call expressions, not the legacy pointer assignments.
    replacements = (
        ("DoSQ1();", "AuroraFdsFlushSQ();"),
        ("DoSQ2();", "AuroraFdsFlushSQ();"),
        ("DoTriangle();", "AuroraFdsFlushTND();"),
        ("DoNoise();", "AuroraFdsFlushTND();"),
        ("DoPCM();", "AuroraFdsFlushTND();"),
    )
    for old_call, new_call in replacements:
        text = text.replace(old_call, new_call)

    old_period = """static void LoadDMCPeriod(uint8 V) {
\tif (PAL)
\t\tDMCPeriod = PALDMCTable[V];
\telse
\t\tDMCPeriod = NTSCDMCTable[V];
}
"""
    new_period = """static void LoadDMCPeriod(uint8 V) {
\tDMCPeriod = NTSCDMCTable[V]; /* V8: FDS hardware is NTSC-only. */
}
"""
    text = v8_replace(text, old_period, new_period, "V8 NTSC DMC period")

    old_noise = """\t\t\t\tif (PAL)
\t\t\t\t\tnoiseacc += PALNoiseFreqTable[PSG[0xE] & 0xF] << (16 + 1);
\t\t\t\telse
\t\t\t\t\tnoiseacc += NTSCNoiseFreqTable[PSG[0xE] & 0xF] << (16 + 1);
"""
    text = text.replace(
        old_noise,
        "\t\t\t\tnoiseacc += NTSCNoiseFreqTable[PSG[0xE] & 0xF] << (16 + 1);\n",
    )

    # Replace the whole end-of-frame mixer with the exact old soundq=0 branch.
    # SexyFilter remains untouched: audio character/output is intentionally kept.
    start = "int FlushEmulateSound(void) {"
    end = "int GetSoundBuffer(int32 **W) {"
    i = text.find(start)
    j = text.find(end, i + len(start)) if i >= 0 else -1
    if i < 0 or j < 0:
        fail("V8 FlushEmulateSound: limites não encontrados")
    lq_flush = """int FlushEmulateSound(void) {
\tint x;
\tint32 end;

\tif (!timestamp) return 0;
\tif (!FSettings.SndRate || !soundtsinc) {
\t\tfor (x = 0; x < 5; ++x)
\t\t\tChannelBC[x] = 0;
\t\tsoundtsoffs = 0;
\t\tinbuf = 0;
\t\treturn 0;
\t}

\t/* AURORA_FCEUMM_FDS_V8_NTSC_LQ_PPU64_REVIVE_ACCURACY_20260827:
\t * exact legacy soundq=0 path, without HQ branches/function pointers. */
\tAuroraFdsFlushSQ();
\tAuroraFdsFlushTND();

\tend = (SOUNDTS << 16) / soundtsinc;
\tif (GameExpSound.Fill)
\t\tGameExpSound.Fill(end & 0xF);

\tSexyFilter(Wave, WaveFinal, end >> 4);

\tif (end & 0xF)
\t\tWave[0] = Wave[end >> 4];
\tWave[end >> 4] = 0;

\tfor (x = 0; x < 5; ++x)
\t\tChannelBC[x] = end & 0xF;
\tsoundtsoffs = (soundtsinc * (end & 0xF)) >> 16;
\tend >>= 4;
\tinbuf = end;
\treturn end;
}

"""
    text = text[:i] + lq_flush + text[j:]

    # Keep MakeFilters/SexyFilter, but make SetSoundVariables the exact NTSC LQ
    # configuration used by this archive.  Also explicitly zero soundtsinc when
    # audio is disabled so the guarded direct workers remain a no-op.
    start = "void SetSoundVariables(void) {"
    end = "void FCEUI_Sound(int Rate) {"
    i = text.find(start)
    j = text.find(end, i + len(start)) if i >= 0 else -1
    if i < 0 or j < 0:
        fail("V8 SetSoundVariables: limites não encontrados")
    setvars = """void SetSoundVariables(void) {
\tint x;

\tFSettings.soundq = 0;
\tfhinc = 14915 * 24; /* exact old NTSC LQ branch */

\tif (FSettings.SndRate) {
\t\twlookup1[0] = 0;
\t\tfor (x = 1; x < 32; ++x) {
\t\t\twlookup1[x] = (double)16 * 16 * 16 * 4 * 95.52 /
\t\t\t\t((double)8128 / (double)x + 100);
\t\t\twlookup1[x] >>= 4;
\t\t}
\t\twlookup2[0] = 0;
\t\tfor (x = 1; x < 203; ++x) {
\t\t\twlookup2[x] = (double)16 * 16 * 16 * 4 * 163.67 /
\t\t\t\t((double)24329 / (double)x + 100);
\t\t\twlookup2[x] >>= 4;
\t\t}
\t} else {
\t\tnesincsize = 0;
\t\tsoundtsinc = 0;
\t\treturn;
\t}

\t/* Keep the existing FCEUmm filter coefficients/output exactly. */
\tMakeFilters(FSettings.SndRate);

\tif (GameExpSound.RChange)
\t\tGameExpSound.RChange();

\tnesincsize = (int64)(((int64)1 << 17) * (double)NTSC_CPU /
\t\t(FSettings.SndRate * 16));
\tmemset(sqacc, 0, sizeof(sqacc));
\tmemset(ChannelBC, 0, sizeof(ChannelBC));

\tLoadDMCPeriod(DMCFormat & 0xF);
\tsoundtsinc = (uint32)((uint64)((long double)NTSC_CPU * 65536) /
\t\t(FSettings.SndRate * 16));
}

"""
    text = text[:i] + setvars + text[j:]

    text = v8_replace(
        text,
        """void FCEUI_SetSoundQuality(int quality) {
\tFSettings.soundq = quality;
\tSetSoundVariables();
}
""",
        """void FCEUI_SetSoundQuality(int quality) {
\t(void)quality;
\tFSettings.soundq = 0;
\tSetSoundVariables();
}
""",
        "V8 force embedded FDS soundq=0",
    )
    return text




# AURORA_FCEUMM_FDS_V8_1_GENERATOR_MAKE_FIX_20260827
# AURORA_FCEUMM_FDS_V8_1_FDSWRITE_FIX_V3_20260827
def patch_v8_fds(text: str) -> str:
    # AURORA_FCEUMM_FDS_V8_1_FDSWRITE_FIX_V3_20260827
    # V6 already specializes FDSSWrite() to the LQ RenderSound() path.
    # V8 must not inspect or rewrite FDSSWrite at all.

    name = "static void FDS_ESI(void)"
    start = text.find(name)
    if start < 0:
        fail("V8 FDS: FDS_ESI não encontrado")

    brace = text.find("{", start)
    if brace < 0:
        fail("V8 FDS: FDS_ESI sem corpo")

    depth = 0
    end = -1
    pos = brace
    while pos < len(text):
        ch = text[pos]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                end = pos + 1
                break
        pos += 1

    if end < 0:
        fail("V8 FDS: FDS_ESI incompleto")

    block = text[start:end]

    if ("((int64)1 << 40) * FDSClock" in block and
        "(int64)1 << 39" not in block and
        "FSettings.soundq" not in block):
        return text

    if ("((int64)1 << 40) * FDSClock" in block and
        "(int64)1 << 39" in block):
        new_block = (
            "static void FDS_ESI(void) {\n"
            "\tif (FSettings.SndRate) {\n"
            "\t\tfdso.cycles = ((int64)1 << 40) * FDSClock;\n"
            "\t\tfdso.cycles /= FSettings.SndRate * 16;\n"
            "\t}\n"
            "}"
        )
        return text[:start] + new_block + text[end:]

    fail("V8 FDS: FDS_ESI em forma não reconhecida")

def patch_v8_x6502(text: str) -> str:
    old = """\tif (PAL)
\t\tcycles *= 15;\t// 15*4=60
\telse
\t\tcycles *= 16;\t// 16*4=64
"""
    text = text.replace(
        old,
        "\t/* AURORA_FCEUMM_FDS_V8_NTSC_LQ_PPU64_REVIVE_ACCURACY_20260827: FDS NTSC-only. */\n"
        "\tcycles <<= 4;\n",
    )
    if "cycles *= 15" in text or "cycles *= 16" in text:
        fail("V8 x6502 ainda contém branch PAL no caminho de ciclos")
    return text



# AURORA_FCEUMM_FDS_V9_INPUT_PALETTE_CPU_FASTPATH_20260827

def patch_v9_ppu(text: str) -> str:
    if "AURORA_FCEUMM_FDS_V9_INPUT_PALETTE_CPU_FASTPATH_20260827" in text:
        return text

    old = "static uint8 deemp = 0;\n"
    if old not in text:
        fail("V9 PPU: estado de deemphasis não encontrado")
    text = text.replace(
        old,
        "static uint8 deemp = 0;\n"
        "/* AURORA_FCEUMM_FDS_V9_INPUT_PALETTE_CPU_FASTPATH_20260827:\n"
        " * expose the predominant emphasis used by FCEUmm's dynamic 0x40 bank. */\n"
        "static int aurora_fds_last_deemph = 0;\n"
        "int aurora_fds_get_deemph(void) { return aurora_fds_last_deemph; }\n",
        1
    )

    old = "\t\t\tSetNESDeemph(maxref, 0);\n"
    if old not in text:
        fail("V9 PPU: SetNESDeemph(maxref) não encontrado")
    text = text.replace(
        old,
        "\t\t\taurora_fds_last_deemph = maxref;\n"
        "\t\t\tSetNESDeemph(maxref, 0);\n",
        1
    )
    return text


def patch_v9_x6502(text: str) -> str:
    if "AURORA_FCEUMM_FDS_V9_INPUT_PALETTE_CPU_FASTPATH_20260827" in text:
        return text

    if '#include "cart.h"' not in text:
        anchor = '#include "fceu.h"\n'
        if anchor not in text:
            fail("V9 x6502: include fceu.h não encontrado")
        text = text.replace(anchor, anchor + '#include "cart.h"\n', 1)

    old_read = (
        "static INLINE uint8 RdMemNorm(uint32 A) {\n"
        "\treturn(_DB = ARead[A](A));\n"
        "}\n"
    )
    new_read = (
        "static INLINE uint8 RdMemNorm(uint32 A) {\n"
        "\t/* AURORA_FCEUMM_FDS_V9_INPUT_PALETTE_CPU_FASTPATH_20260827 */\n"
        "\tif (A < 0x2000)\n"
        "\t\treturn(_DB = RAM[A & 0x7FF]);\n"
        "\tif (A >= 0x6000)\n"
        "\t\treturn(_DB = Page[A >> 11][A]);\n"
        "\treturn(_DB = ARead[A](A));\n"
        "}\n"
    )
    if old_read not in text:
        fail("V9 x6502: RdMemNorm original não encontrado")
    text = text.replace(old_read, new_read, 1)

    old_write = (
        "static INLINE void WrMemNorm(uint32 A, uint8 V) {\n"
        "\tBWrite[A](A, V);\n"
        "}\n"
    )
    new_write = (
        "static INLINE void WrMemNorm(uint32 A, uint8 V) {\n"
        "\tif (A < 0x2000) {\n"
        "\t\tRAM[A & 0x7FF] = V;\n"
        "\t\treturn;\n"
        "\t}\n"
        "\tif (A >= 0x6000 && A < 0xE000) {\n"
        "\t\tPage[A >> 11][A] = V;\n"
        "\t\treturn;\n"
        "\t}\n"
        "\tBWrite[A](A, V);\n"
        "}\n"
    )
    if old_write not in text:
        fail("V9 x6502: WrMemNorm original não encontrado")
    text = text.replace(old_write, new_write, 1)
    return text



# AURORA_FCEUMM_FDS_V10_RESTORE_EXPANSION_AUDIO_20260827

def patch_v10_sound(text: str) -> str:
    if "AURORA_FCEUMM_FDS_V10_RESTORE_EXPANSION_AUDIO_20260827" in text:
        return text

    include_anchor = '#include "sound.h"\n'
    decl = (
        '/* AURORA_FCEUMM_FDS_V10_RESTORE_EXPANSION_AUDIO_20260827 */\n'
        'void FDSSound(int c);\n'
    )
    if "void FDSSound(int c);" not in text:
        if include_anchor not in text:
            fail("V10 sound: include sound.h não encontrado")
        text = text.replace(include_anchor, include_anchor + decl, 1)

    old = (
        "\tif (GameExpSound.Fill)\n"
        "\t\tGameExpSound.Fill(end & 0xF);\n"
    )
    new = (
        "\t/* V10: FDS-only archive; mix expansion audio directly at the\n"
        "\t * same pre-SexyFilter boundary used by stock FCEUmm. */\n"
        "\tFDSSound(end & 0xF);\n"
    )
    if old in text:
        text = text.replace(old, new, 1)
    elif "FDSSound(end & 0xF);" not in text:
        fail("V10 sound: ponto GameExpSound.Fill não encontrado")

    return text


def patch_v10_fds(text: str) -> str:
    if "AURORA_FCEUMM_FDS_V10_RESTORE_EXPANSION_AUDIO_20260827" in text:
        return text

    grouped = """static void RenderSound(void) {
\tint32 end, start;
\tint32 x;
\tint32 waveIndex;
\tint32 accum;

\tstart = FBC;
\tend = (SOUNDTS << 16) / soundtsinc;
\tif (end <= start)
\t\treturn;
\tFBC = end;

\tif (SPSG[0x9] & 0x80)
\t\treturn;

\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827
\t * x>>4 maps up to 16 consecutive FDS sub-samples to one Wave[] cell.
\t * FDSDoSound still executes once for every original x, in order. */
\twaveIndex = start >> 4;
\taccum = 0;
\tfor (x = start; x < end; x++) {
\t\tint32 t = FDSDoSound();
\t\tint32 idx = x >> 4;

\t\tt += t >> 1;
\t\tt >>= 4;
\t\tif (idx != waveIndex) {
\t\t\tWave[waveIndex] += accum;
\t\t\twaveIndex = idx;
\t\t\taccum = 0;
\t\t}
\t\taccum += t;
\t}
\tif (accum)
\t\tWave[waveIndex] += accum;
}
"""

    legacy = """static void RenderSound(void) {
\tint32 end, start;
\tint32 x;

\tstart = FBC;
\tend = (SOUNDTS << 16) / soundtsinc;
\tif (end <= start)
\t\treturn;
\tFBC = end;

\t/* AURORA_FCEUMM_FDS_V10_RESTORE_EXPANSION_AUDIO_20260827:
\t * stock FCEUmm LQ expansion-channel accumulation. */
\tif (!(SPSG[0x9] & 0x80))
\t\tfor (x = start; x < end; x++) {
\t\t\tuint32 t = FDSDoSound();
\t\t\tt += t >> 1;
\t\t\tt >>= 4;
\t\t\tWave[x >> 4] += t;
\t\t}
}
"""

    if grouped in text:
        text = text.replace(grouped, legacy, 1)
    elif "AURORA_FCEUMM_FDS_V10_RESTORE_EXPANSION_AUDIO_20260827:" not in text:
        start = text.find("static void RenderSound(void) {")
        if start < 0:
            fail("V10 FDS: RenderSound não encontrado")
        end_anchor = "\nstatic void RenderSoundHQ(void) {"
        end = text.find(end_anchor, start)
        if end < 0:
            fail("V10 FDS: fim de RenderSound não encontrado")
        cur = text[start:end]
        if "FBC" not in cur or "FDSDoSound()" not in cur or "Wave[" not in cur:
            fail("V10 FDS: RenderSound não parece ser o renderer FDS esperado")
        text = text[:start] + legacy.rstrip() + text[end:]

    reset_sig = "void FDSSoundReset(void) {"
    p = text.find(reset_sig)
    if p < 0:
        fail("V10 FDS: FDSSoundReset não encontrado")

    reset_end = text.find("\n}", p)
    if reset_end < 0:
        fail("V10 FDS: fim de FDSSoundReset não encontrado")
    block = text[p:reset_end + 2]

    if "\tFBC = 0;" not in block:
        anchor = "\tmemset(&fdso, 0, sizeof(fdso));\n"
        if anchor not in block:
            fail("V10 FDS: memset fdso não encontrado")
        block = block.replace(
            anchor,
            anchor +
            "\t/* V10: reset expansion-audio fractional cursor with the engine. */\n"
            "\tFBC = 0;\n",
            1
        )
        text = text[:p] + block + text[reset_end + 2:]

    p = text.find(reset_sig)
    reset_end = text.find("\n}", p)
    block = text[p:reset_end + 2]
    if "GameExpSound.Fill = FDSSound;" not in block:
        anchor = "\tFDS_ESI();\n"
        if anchor not in block:
            fail("V10 FDS: FDS_ESI em reset não encontrado")
        regs = (
            "\tGameExpSound.HiSync = HQSync;\n"
            "\tGameExpSound.HiFill = RenderSoundHQ;\n"
            "\tGameExpSound.Fill = FDSSound;\n"
            "\tGameExpSound.RChange = FDS_ESI;\n"
        )
        block = block.replace(anchor, anchor + regs, 1)
        text = text[:p] + block + text[reset_end + 2:]

    return text



# AURORA_FCEUMM_FDS_V11_SAVESTATE_FASTPATH_20260827

def aurora_v11_replace_c_function(text: str, signature: str,
                                  replacement: str, label: str) -> str:
    start = text.find(signature)
    if start < 0:
        fail("V11 " + label + ": função não encontrada")

    brace = text.find("{", start + len(signature))
    if brace < 0:
        fail("V11 " + label + ": corpo não encontrado")

    depth = 0
    i = brace
    state = "code"
    quote = ""
    end = -1

    while i < len(text):
        ch = text[i]
        nx = text[i + 1] if i + 1 < len(text) else ""

        if state == "line":
            if ch == "\n":
                state = "code"
        elif state == "block":
            if ch == "*" and nx == "/":
                state = "code"
                i += 1
        elif state == "string":
            if ch == "\\":
                i += 1
            elif ch == quote:
                state = "code"
        else:
            if ch == "/" and nx == "/":
                state = "line"
                i += 1
            elif ch == "/" and nx == "*":
                state = "block"
                i += 1
            elif ch == '"' or ch == "'":
                state = "string"
                quote = ch
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    end = i + 1
                    break
        i += 1

    if end < 0:
        fail("V11 " + label + ": fechamento não encontrado")

    return text[:start] + replacement.rstrip() + text[end:]


def patch_v11_state(text: str) -> str:
    marker = "AURORA_FCEUMM_FDS_V11_SAVESTATE_FASTPATH_20260827"
    if marker in text:
        return text

    # SubWrite(NULL, sf) already exists specifically to calculate a block's
    # serialized byte count without writing or touching emulated state.
    anchor = "static SFORMAT *CheckS(SFORMAT *sf, uint32 tsize, char *desc) {"
    p = text.find(anchor)
    if p < 0:
        fail("V11 state: CheckS anchor não encontrado")

    helper = """/* AURORA_FCEUMM_FDS_V11_SAVESTATE_FASTPATH_20260827
 * Exact serialized size without creating a temporary state.
 * Each state chunk is one type byte + uint32 payload size + SubWrite payload;
 * the FCS header is 16 bytes.  No PreSave/PostSave or disk XOR is required,
 * because those operations change bytes, never descriptor sizes. */
size_t FCEUSS_GetStateSize(void) {
\tsize_t total = 16;

\ttotal += 5 + (size_t)SubWrite(0, SFCPU);
\ttotal += 5 + (size_t)SubWrite(0, SFCPUC);
\ttotal += 5 + (size_t)SubWrite(0, FCEUPPU_STATEINFO);
\ttotal += 5 + (size_t)SubWrite(0, FCEUCTRL_STATEINFO);
\ttotal += 5 + (size_t)SubWrite(0, FCEUSND_STATEINFO);
\ttotal += 5 + (size_t)SubWrite(0, SFMDATA);

\treturn total;
}

"""
    return text[:p] + helper + text[p:]


def patch_v11_libretro(text: str) -> str:
    marker = "AURORA_FCEUMM_FDS_V11_SAVESTATE_FASTPATH_20260827"
    if marker in text:
        return text

    # Local declaration keeps this optimization isolated to the generated
    # FDS-only frontend; no public FCEUmm header/API change is necessary.
    decl_anchor = "static unsigned serialize_size = 0;\n"
    if decl_anchor not in text:
        fail("V11 libretro: serialize_size não encontrado")

    text = text.replace(
        decl_anchor,
        "/* AURORA_FCEUMM_FDS_V11_SAVESTATE_FASTPATH_20260827 */\n"
        "extern size_t FCEUSS_GetStateSize(void);\n"
        + decl_anchor,
        1
    )

    replacement = """size_t retro_serialize_size(void) {
\t/* V11: exact metadata-only size query.  The old implementation allocated
\t * 1 MB and performed a complete FCEUSS_Save() merely to discover this
\t * number, making the first Aurora save effectively serialize twice. */
\tif (serialize_size == 0)
\t\tserialize_size = FCEUSS_GetStateSize();
\treturn serialize_size;
}"""

    text = aurora_v11_replace_c_function(
        text,
        "size_t retro_serialize_size(void)",
        replacement,
        "libretro retro_serialize_size"
    )
    return text


def patch_v11_fds(text: str) -> str:
    marker = "AURORA_FCEUMM_FDS_V11_SAVESTATE_FASTPATH_20260827"
    if marker in text:
        return text

    helper_anchor = "static void PreSave(void) {"
    p = text.find(helper_anchor)
    if p < 0:
        fail("V11 FDS: PreSave não encontrado")

    helper = """/* AURORA_FCEUMM_FDS_V11_SAVESTATE_FASTPATH_20260827
 * diskdata/diskdatao come from malloc-compatible FCEU allocations and are
 * naturally aligned for uint64 on EE.  65500 = 8187*8 + 4, so the whole
 * XOR has an aligned 64-bit body plus one aligned uint32 tail.
 * This is byte-for-byte identical to the old 65,500-iteration byte loop. */
static INLINE void AuroraFDSXorStateSide(uint8 *dst, const uint8 *src) {
\tuint64 *d64 = (uint64 *)dst;
\tconst uint64 *s64 = (const uint64 *)src;
\tint i;

\tfor (i = 0; i < (65500 >> 3); ++i)
\t\td64[i] ^= s64[i];

\t*(uint32 *)(dst + (65500 & ~7)) ^=
\t\t*(const uint32 *)(src + (65500 & ~7));
}

"""
    text = text[:p] + helper + text[p:]

    presave = """static void PreSave(void) {
\tint x;
\tfor (x = 0; x < TotalSides; x++)
\t\tAuroraFDSXorStateSide(diskdata[x], diskdatao[x]);
}"""
    postsave = """static void PostSave(void) {
\tint x;
\tfor (x = 0; x < TotalSides; x++)
\t\tAuroraFDSXorStateSide(diskdata[x], diskdatao[x]);
}"""

    text = aurora_v11_replace_c_function(
        text, "static void PreSave(void)", presave, "FDS PreSave"
    )
    text = aurora_v11_replace_c_function(
        text, "static void PostSave(void)", postsave, "FDS PostSave"
    )
    return text



# AURORA_FCEUMM_FDS_V14_SAFE_20260827
#
# V14 deliberately does NOT touch the V10 RenderSound implementation,
# PPU/CPU timing, the 32050 Hz clock, or the number/order of FDSDoSound calls.
#
# The important audio correction is structural: V8 replaced FDS_ESI() with
# a rate-only body and accidentally discarded the stock FDS sound-register
# handlers. V10 restored GameExpSound.Fill but those CPU address handlers were
# still missing, so writes to the FDS wavetable/register block could not reach
# the expansion APU.
def patch_v14_fds(text: str) -> str:
    marker = "AURORA_FCEUMM_FDS_V14_SAFE_20260827"
    if marker in text:
        return text

    old = """static void FDS_ESI(void) {
\tif (FSettings.SndRate) {
\t\tfdso.cycles = ((int64)1 << 40) * FDSClock;
\t\tfdso.cycles /= FSettings.SndRate * 16;
\t}
}"""

    new = """static void FDS_ESI(void) {
\t/* AURORA_FCEUMM_FDS_V14_SAFE_20260827
\t * Keep V8's fixed NTSC/LQ clock calculation, but restore the four
\t * stock FCEUmm FDS audio address handlers that V8 accidentally removed. */
\tif (FSettings.SndRate) {
\t\tfdso.cycles = ((int64)1 << 40) * FDSClock;
\t\tfdso.cycles /= FSettings.SndRate * 16;
\t}

\tSetReadHandler(0x4040, 0x407f, FDSWaveRead);
\tSetWriteHandler(0x4040, 0x407f, FDSWaveWrite);
\tSetWriteHandler(0x4080, 0x408A, FDSSWrite);
\tSetReadHandler(0x4090, 0x4092, FDSSRead);
}"""

    if old not in text:
        fail("V14 FDS: FDS_ESI V8 rate-only não encontrado")
    return text.replace(old, new, 1)


def patch_v14_libretro(text: str) -> str:
    marker = "AURORA_FCEUMM_FDS_V14_SAFE_MASTER_20260827"
    if marker in text:
        return text

    old = """\temulator_set_custom_palette();

\tFCEUD_SoundToggle();
"""
    new = """\temulator_set_custom_palette();

\t/* AURORA_FCEUMM_FDS_V14_SAFE_MASTER_20260827
\t * The pinned frontend's late SoundToggle forces the core from its
\t * pre-load 256 down to an old hard-coded 100. Use a conservative
\t * master of 114 here; Aurora's shared NES 0..200 control remains
\t * the user-facing gain stage for QuickNES and FDS. */
\tFCEUI_SetSoundVolume(114);
"""

    if old not in text:
        fail("V14 libretro: FCEUD_SoundToggle pós-load não encontrado")
    return text.replace(old, new, 1)


# AURORA_V3_SAFE_FDS_GENERATOR_20260828
def patch_v3_safe_ppu(text: str) -> str:
    marker = "AURORA_V3_SAFE_FDS_PPU_LUT_DIRTY_20260828"
    if marker in text:
        return text

    old_cache = '''static uint16 fceu_bg_pair_lut[256];
static uint8 fceu_bg_pair_lut_pal[16];
static uint8 fceu_bg_pair_lut_valid = 0;

static void FCEU_BuildBgPairLUT(void) {
\tint b;

\t/* AURORA_FCEUMM_FDS_V7_REAL_FRAMESKIP_CPU_APU_20260827
\t * RefreshLine may run several times in one scanline after PPU writes.
\t * Rebuild the 256 pairs only when the 16 BG palette bytes changed. */
\tif (fceu_bg_pair_lut_valid &&
\t    memcmp(fceu_bg_pair_lut_pal, PALRAM, sizeof(fceu_bg_pair_lut_pal)) == 0)
\t\treturn;

\tmemcpy(fceu_bg_pair_lut_pal, PALRAM, sizeof(fceu_bg_pair_lut_pal));
\tfor (b = 0; b < 256; b++)
\t\tfceu_bg_pair_lut[b] = (uint16)PALRAM[b & 0x0F] |
\t\t\t((uint16)PALRAM[b >> 4] << 8);
\tfceu_bg_pair_lut_valid = 1;
}
'''
    new_cache = '''static uint16 fceu_bg_pair_lut[256];
static uint8 fceu_bg_pair_lut_valid = 0;
static uint8 fceu_bg_pair_lut_dirty = 1;

static void FCEU_BuildBgPairLUT(void) {
\tint b;

\t/* AURORA_V3_SAFE_FDS_PPU_LUT_DIRTY_20260828
\t * B2007/Power/LoadState invalidate this only when PALRAM[0..15] can
\t * actually change. RefreshLine therefore pays one byte test instead of
\t * a 16-byte memcmp. */
\tif (fceu_bg_pair_lut_valid && !fceu_bg_pair_lut_dirty)
\t\treturn;

\tfor (b = 0; b < 256; b++)
\t\tfceu_bg_pair_lut[b] = (uint16)PALRAM[b & 0x0F] |
\t\t\t((uint16)PALRAM[b >> 4] << 8);
\tfceu_bg_pair_lut_valid = 1;
\tfceu_bg_pair_lut_dirty = 0;
}
'''
    text = replace_once(text, old_cache, new_cache, "V3 PPU pair-LUT dirty cache")

    old_pal_write = '''\t} else {
\t\tif (!(tmp & 3)) {
\t\t\tif (!(tmp & 0xC))
\t\t\t\tPALRAM[0x00] = PALRAM[0x04] = PALRAM[0x08] = PALRAM[0x0C] = V & 0x3F;
\t\t\telse
\t\t\t\tUPALRAM[((tmp & 0xC) >> 2) - 1] = V & 0x3F;
\t\t} else
\t\t\tPALRAM[tmp & 0x1F] = V & 0x3F;
\t}
'''
    new_pal_write = '''\t} else {
\t\tconst uint8 aurora_pal_v = V & 0x3F;
\t\tif (!(tmp & 3)) {
\t\t\tif (!(tmp & 0xC)) {
\t\t\t\tif (PALRAM[0x00] != aurora_pal_v)
\t\t\t\t\tfceu_bg_pair_lut_dirty = 1;
\t\t\t\tPALRAM[0x00] = PALRAM[0x04] = PALRAM[0x08] = PALRAM[0x0C] = aurora_pal_v;
\t\t\t} else
\t\t\t\tUPALRAM[((tmp & 0xC) >> 2) - 1] = aurora_pal_v;
\t\t} else {
\t\t\tconst uint32 aurora_pal_i = tmp & 0x1F;
\t\t\tif (aurora_pal_i < 0x10 && PALRAM[aurora_pal_i] != aurora_pal_v)
\t\t\t\tfceu_bg_pair_lut_dirty = 1;
\t\t\tPALRAM[aurora_pal_i] = aurora_pal_v;
\t\t}
\t}
'''
    text = replace_once(text, old_pal_write, new_pal_write, "V3 B2007 LUT invalidation")

    text = replace_once(
        text,
        "\tmemset(PALRAM, 0x00, 0x20);\n",
        "\tmemset(PALRAM, 0x00, 0x20);\n"
        "\tfceu_bg_pair_lut_valid = 0;\n"
        "\tfceu_bg_pair_lut_dirty = 1;\n",
        "V3 PPU power LUT invalidation",
    )

    text = replace_once(
        text,
        '''void FCEUPPU_LoadState(int version) {
\tTempAddr = TempAddrT;
\tRefreshAddr = RefreshAddrT;
}''',
        '''void FCEUPPU_LoadState(int version) {
\tTempAddr = TempAddrT;
\tRefreshAddr = RefreshAddrT;
\t/* AURORA_V3_SAFE_FDS_PPU_LUT_DIRTY_20260828 */
\tfceu_bg_pair_lut_dirty = 1;
}''',
        "V3 PPU load-state LUT invalidation",
    )
    return text


def patch_v3_safe_sound(text: str) -> str:
    marker = "AURORA_V3_SAFE_FDS_SOUNDTSINC_CONST_20260828"
    if marker in text:
        return text

    text = replace_once(
        text,
        "uint32 soundtsinc = 0;\n",
        "uint32 soundtsinc = 0;\n"
        "/* AURORA_V3_SAFE_FDS_SOUNDTSINC_CONST_20260828\n"
        " * NTSC_CPU*65536/(32050*16) truncates exactly to 228733. */\n"
        "#define AURORA_FDS_SOUNDTSINC_32050 228733U\n",
        "V3 soundtsinc constant declaration",
    )

    old = "(SOUNDTS << 16) / soundtsinc"
    count = text.count(old)
    if count != 3:
        fail(f"V3 sound_fds divisions: esperado 3, encontrado {count}")
    text = text.replace(old, "(SOUNDTS << 16) / AURORA_FDS_SOUNDTSINC_32050")
    return text


def patch_v3_safe_fds(text: str) -> str:
    marker = "AURORA_V3_SAFE_FDS_RESTORE_XOR64_20260828"
    if marker in text:
        return text

    signature = "static void FDSStateRestore(int version) {"
    if text.count(signature) != 1:
        fail("V3 FDSStateRestore signature não é única")

    text = text.replace(
        signature,
        "static INLINE void AuroraFDSXorStateSide(uint8 *dst, const uint8 *src);\n"
        "/* AURORA_V3_SAFE_FDS_RESTORE_XOR64_20260828 */\n"
        + signature,
        1,
    )

    replacement = '''static void FDSStateRestore(int version) {
\tint x;

\tsetmirror(((FDSRegs[5] & 8) >> 3) ^ 1);

\tif (version >= 9810)
\t\tfor (x = 0; x < TotalSides; x++)
\t\t\tAuroraFDSXorStateSide(diskdata[x], diskdatao[x]);
}'''
    text = aurora_v11_replace_c_function(
        text,
        "static void FDSStateRestore(int version)",
        replacement,
        "V3 FDS restore XOR64",
    )

    old_div = "(SOUNDTS << 16) / soundtsinc"
    if text.count(old_div) != 1:
        fail(f"V3 fds_fds RenderSound division count: {text.count(old_div)}")
    text = text.replace(
        old_div,
        "(SOUNDTS << 16) / 228733U /* AURORA_V3_SAFE_FDS_FIXED_RENDER_DIV_20260828 */",
        1,
    )
    return text


# AURORA_FDS_V4_ZIP_GENERATOR_20260828

# AURORA_FDS_V4_ZIP_SAFE_20260828
def patch_v4_zip_file(text: str) -> str:
    marker = "AURORA_FDS_V4_ZIP_MEMVIEW_20260828"
    if marker in text:
        return text

    text = replace_once(
        text,
        '''typedef struct {
\tuint8 *data;
\tuint32 size;
\tuint32 location;
} MEMWRAP;
''',
        '''typedef struct {
\tuint8 *data;
\tuint32 size;
\tuint32 location;
} MEMWRAP;

/* AURORA_FDS_V4_ZIP_MEMVIEW_20260828
 * Borrowed, read-only view of Aurora's already-extracted .fds payload.
 * The wrapper owns only this tiny descriptor; it never owns/frees data. */
#define AURORA_FDS_MEMVIEW_TYPE 4U
typedef struct {
\tconst uint8 *data;
\tuint32 size;
\tuint32 location;
} AURORA_FDS_MEMVIEW;

FCEUFILE *FCEU_fopen_memory(const void *data, uint32 size) {
\tFCEUFILE *fp;
\tAURORA_FDS_MEMVIEW *view;

\tif (!data || !size)
\t\treturn 0;

\tfp = (FCEUFILE *)malloc(sizeof(FCEUFILE));
\tif (!fp)
\t\treturn 0;

\tview = (AURORA_FDS_MEMVIEW *)malloc(sizeof(AURORA_FDS_MEMVIEW));
\tif (!view) {
\t\tfree(fp);
\t\treturn 0;
\t}

\tview->data = (const uint8 *)data;
\tview->size = size;
\tview->location = 0;
\tfp->fp = view;
\tfp->type = AURORA_FDS_MEMVIEW_TYPE;
\treturn fp;
}
''',
        "V4 borrowed FDS memory view",
    )

    for signature, body, label in (
        (
            "int FCEU_fclose(FCEUFILE *fp) {\n",
            '''int FCEU_fclose(FCEUFILE *fp) {
\tif (fp && fp->type == AURORA_FDS_MEMVIEW_TYPE) {
\t\tfree(fp->fp);
\t\tfp->fp = 0;
\t\tfree(fp);
\t\treturn 1;
\t}
''',
            "V4 memory-view close",
        ),
        (
            "uint64 FCEU_fread(void *ptr, size_t size, size_t nmemb, FCEUFILE *fp) {\n",
            '''uint64 FCEU_fread(void *ptr, size_t size, size_t nmemb, FCEUFILE *fp) {
\tif (fp && fp->type == AURORA_FDS_MEMVIEW_TYPE) {
\t\tAURORA_FDS_MEMVIEW *view = (AURORA_FDS_MEMVIEW *)fp->fp;
\t\tuint64 requested;
\t\tuint32 available;
\t\tuint32 copy;

\t\tif (!view || !ptr || !size || !nmemb || view->location >= view->size)
\t\t\treturn 0;

\t\trequested = (uint64)size * (uint64)nmemb;
\t\tavailable = view->size - view->location;
\t\tcopy = requested < (uint64)available ? (uint32)requested : available;
\t\tmemcpy(ptr, view->data + view->location, copy);
\t\tview->location += copy;
\t\treturn copy / size;
\t}
''',
            "V4 memory-view fread",
        ),
        (
            "int FCEU_fseek(FCEUFILE *fp, long offset, int whence) {\n",
            '''int FCEU_fseek(FCEUFILE *fp, long offset, int whence) {
\tif (fp && fp->type == AURORA_FDS_MEMVIEW_TYPE) {
\t\tAURORA_FDS_MEMVIEW *view = (AURORA_FDS_MEMVIEW *)fp->fp;
\t\tlong target;

\t\tif (!view)
\t\t\treturn -1;

\t\tswitch (whence) {
\t\tcase SEEK_SET: target = offset; break;
\t\tcase SEEK_CUR: target = (long)view->location + offset; break;
\t\tcase SEEK_END: target = (long)view->size + offset; break;
\t\tdefault: return -1;
\t\t}
\t\tif (target < 0 || (uint32)target > view->size)
\t\t\treturn -1;
\t\tview->location = (uint32)target;
\t\treturn 0;
\t}
''',
            "V4 memory-view fseek",
        ),
        (
            "uint64 FCEU_ftell(FCEUFILE *fp) {\n",
            '''uint64 FCEU_ftell(FCEUFILE *fp) {
\tif (fp && fp->type == AURORA_FDS_MEMVIEW_TYPE) {
\t\tAURORA_FDS_MEMVIEW *view = (AURORA_FDS_MEMVIEW *)fp->fp;
\t\treturn view ? view->location : 0;
\t}
''',
            "V4 memory-view ftell",
        ),
        (
            "void FCEU_rewind(FCEUFILE *fp) {\n",
            '''void FCEU_rewind(FCEUFILE *fp) {
\tif (fp && fp->type == AURORA_FDS_MEMVIEW_TYPE) {
\t\tAURORA_FDS_MEMVIEW *view = (AURORA_FDS_MEMVIEW *)fp->fp;
\t\tif (view) view->location = 0;
\t\treturn;
\t}
''',
            "V4 memory-view rewind",
        ),
        (
            "int FCEU_fgetc(FCEUFILE *fp) {\n",
            '''int FCEU_fgetc(FCEUFILE *fp) {
\tif (fp && fp->type == AURORA_FDS_MEMVIEW_TYPE) {
\t\tAURORA_FDS_MEMVIEW *view = (AURORA_FDS_MEMVIEW *)fp->fp;
\t\tif (view && view->location < view->size)
\t\t\treturn view->data[view->location++];
\t\treturn EOF;
\t}
''',
            "V4 memory-view fgetc",
        ),
        (
            "uint64 FCEU_fgetsize(FCEUFILE *fp) {\n",
            '''uint64 FCEU_fgetsize(FCEUFILE *fp) {
\tif (fp && fp->type == AURORA_FDS_MEMVIEW_TYPE) {
\t\tAURORA_FDS_MEMVIEW *view = (AURORA_FDS_MEMVIEW *)fp->fp;
\t\treturn view ? view->size : 0;
\t}
''',
            "V4 memory-view fgetsize",
        ),
    ):
        text = replace_once(text, signature, body, label)

    return text


def patch_v4_zip_fceu(text: str) -> str:
    marker = "AURORA_FDS_V4_ZIP_MEMORY_LOADER_20260828"
    if marker in text:
        return text

    text = replace_once(
        text,
        "int FDSLoad(const char *name, FCEUFILE *fp);\n",
        "int FDSLoad(const char *name, FCEUFILE *fp);\n"
        "extern FCEUFILE *FCEU_fopen_memory(const void *data, uint32 size);\n",
        "V4 memory-loader declaration",
    )

    anchor = "/* AURORA_FCEUMM_FDS_V0_6_NO_COPYFAMI */\n"
    if text.count(anchor) != 1:
        fail("V4 FDS memory loader: NO_COPYFAMI anchor não é único")

    func = '''/* AURORA_FDS_V4_ZIP_MEMORY_LOADER_20260828
 * Load a validated .fds image from Aurora-owned memory. FDSLoad() copies every
 * disk side before return, so the caller may free the borrowed input buffer. */
FCEUGI *FCEUI_LoadGameMemory(const char *name, const void *data, uint32 size) {
\tFCEUFILE *fp;

\tif (!name || !name[0] || !data || size < 65500U || size > 524016U)
\t\treturn 0;

\tResetGameLoaded();

\tGameInfo = malloc(sizeof(FCEUGI));
\tif (!GameInfo)
\t\treturn 0;
\tmemset(GameInfo, 0, sizeof(FCEUGI));

\tGameInfo->soundchan = 0;
\tGameInfo->soundrate = 0;
\tGameInfo->name = 0;
\tGameInfo->type = GIT_CART;
\tGameInfo->vidsys = GIV_USER;
\tGameInfo->input[0] = GameInfo->input[1] = -1;
\tGameInfo->inputfc = -1;
\tGameInfo->cspecial = 0;

\tFCEU_printf("Loading %s from memory...\\n\\n", name);
\tGetFileBase(name);

\tfp = FCEU_fopen_memory(data, size);
\tif (!fp) {
\t\tfree(GameInfo);
\t\tGameInfo = 0;
\t\treturn 0;
\t}

\tif (!FDSLoad(name, fp)) {
\t\tFCEU_fclose(fp);
\t\tfree(GameInfo);
\t\tGameInfo = 0;
\t\treturn 0;
\t}

\tFCEU_fclose(fp);

\tFCEU_ResetVidSys();
\tPowerNES();
\tFCEUSS_CheckStates();
\tFCEU_LoadGamePalette();
\tFCEU_ResetPalette();
\tFCEU_ResetMessages();

\treturn GameInfo;
}

'''
    text = text.replace(anchor, func + anchor, 1)
    return text


def patch_v4_zip_libretro(text: str) -> str:
    marker = "AURORA_FDS_V4_ZIP_LIBRETRO_MEMORY_20260828"
    if marker in text:
        return text

    anchor = "static void fceu_init(const char * full_path) {"
    if text.count(anchor) != 1:
        fail("V4 libretro memory loader: fceu_init anchor não é único")

    text = text.replace(
        anchor,
        "/* AURORA_FDS_V4_ZIP_LIBRETRO_MEMORY_20260828 */\n"
        "extern FCEUGI *FCEUI_LoadGameMemory(const char *name, const void *data, uint32 size);\n\n"
        + anchor,
        1,
    )

    deinit_anchor = "void retro_deinit(void) {\n"
    if text.count(deinit_anchor) != 1:
        fail("V4 libretro memory loader: retro_deinit anchor não é único")

    mem_init = '''static void fceu_init_memory(const char *name, const void *data, uint32 size) {
\tFCEUI_Initialize();
\tFCEUI_SetSoundVolume(256);
\tFCEUI_Sound(32050);

\tGameInfo = FCEUI_LoadGameMemory(name, data, size);
\tif (GameInfo) {
\t\temulator_set_input();
\t\temulator_set_custom_palette();
\t\tFCEUI_SetSoundVolume(114);
\t}
}

'''
    text = text.replace(deinit_anchor, mem_init + deinit_anchor, 1)

    old_load = '''bool retro_load_game(const struct retro_game_info *game) {
\t/* AURORA_FCEUMM_FDS_V0_6_LOAD_RESULT */
\tif (!game || !game->path || !game->path[0])
\t\treturn false;
\t/* AURORA_FCEUMM_FDS_V0_6_STATE_SIZE_PER_GAME */
\tserialize_size = 0;
\tfceu_init(game->path);
\treturn GameInfo != NULL;
}
'''
    new_load = '''bool retro_load_game(const struct retro_game_info *game) {
\t/* AURORA_FCEUMM_FDS_V0_6_LOAD_RESULT */
\tif (!game || !game->path || !game->path[0])
\t\treturn false;
\t/* AURORA_FCEUMM_FDS_V0_6_STATE_SIZE_PER_GAME */
\tserialize_size = 0;

\t/* AURORA_FDS_V4_ZIP_LIBRETRO_MEMORY_20260828 */
\tif (game->data && game->size) {
\t\tif (game->size < 65500U || game->size > 524016U)
\t\t\treturn false;
\t\tfceu_init_memory(game->path, game->data, (uint32)game->size);
\t} else {
\t\tfceu_init(game->path);
\t}
\treturn GameInfo != NULL;
}
'''
    text = replace_once(text, old_load, new_load, "V4 libretro memory dispatch")
    return text
def audit_generated(files: dict[str, str]) -> None:
    # AURORA_FCEUMM_FDS_V0_6_15_PRUNED_LINK_STUBS_AUDIT
    fceu_fds = files["fceu_fds.c"]
    required_pruned_stubs = (
        "AURORA_FCEUMM_FDS_V0_6_15_PRUNED_LINK_STUBS",
        "uint8 mmc5ABMode = 0;",
        "void MMC5_hb(int scanline) { (void)scanline; }",
        "void DoNSFFrame(void) { }",
    )
    for token in required_pruned_stubs:
        if fceu_fds.count(token) != 1:
            fail(f"fceu_fds.c pruned linker stub invalid: {token}")
    # AURORA_FCEUMM_FDS_V0_6_13_GENERATED_INPUT_FIX
    # AURORA_FCEUMM_FDS_V0_6_14_STATE_MEMSTREAM_ASPRINTF_FIX_AUDIT
    state_fds = files["state_fds.c"]
    general_fds = files["general_fds.c"]
    if '#include "endian.h"' in state_fds:
        fail("state_fds.c retained FILE-based endian.h")
    if '#include "fceu-endian.h"' not in state_fds:
        fail("state_fds.c missing MEM_TYPE-aware fceu-endian.h")
    if "AURORA_FCEUMM_FDS_V0_6_14_NO_FILE_STATE_SCAN" not in state_fds:
        fail("state_fds.c retained FILE-based FCEUSS_CheckStates")
    if "AURORA_FCEUMM_FDS_V0_6_14_LOCAL_ASPRINTF" not in general_fds:
        fail("general_fds.c did not enable local asprintf fallback")
    if "AURORA_FCEUMM_FDS_LOADER_FIX_V2_20260826_TRACE" not in files["libretro_fds.c"]:
        fail("libretro_fds.c missing persistent FDS trace")
    if "AURORA_FCEUMM_FDS_LOADER_FIX_V2_20260826_NULL_IPS" not in files["file_fds.c"]:
        fail("file_fds.c missing NULL IPS guard")
    for generated_name in ("input_fds.c", "pads_fds.c"):
        if "\\t" in files[generated_name]:
            fail(f"{generated_name}: retained literal \\t escape in generated C")
    combined = "\n".join(files.values())
    forbidden = (
        "FCEU_ApplyPeriodicCheats(", "FCEU_LoadGameCheats(",
        "FCEU_FlushGameCheats(", "FCEU_CheatResetRAM(",
        "FCEU_CheatAddRAM(", "FCEU_PowerCheats(",
        "FCEUMOV_", "FCEUnetplay", "FCEUNET_",
        "FCEU_DrawMovies(", "FCEU_VSUniDraw(", "DrawNSF(",
    )
    for token in forbidden:
        if token in combined:
            fail(f"generated lean core retained forbidden runtime token: {token}")
    # ABI callbacks are deliberately still present and empty.
    if "void retro_cheat_reset(void)" not in files["libretro_fds.c"]:
        fail("libretro ABI cheat-reset stub was accidentally removed")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--core", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    args = ap.parse_args()

    core = args.core.resolve()
    out = args.out.resolve()
    paths = {
        "fceu": core / "src/fceu.c",
        "libretro": core / "src/drivers/libretro/libretro.c",
        "input": core / "src/input.c",
        "pads": core / "src/input/pads.c",
        "state": core / "src/state.c",
        "general": core / "src/general.c",
        "video": core / "src/video.c",
        "file": core / "src/file.c",
        "fds": core / "src/fds.c",
        "ppu": core / "src/ppu.c",
        "pputile": core / "src/pputile.h",
        "sound": core / "src/sound.c",
        "x6502": core / "src/x6502.c",
    }
    for path in paths.values():
        if not path.is_file():
            fail(f"missing pinned FCEUmm source: {path}")

    originals = {name: read(path) for name, path in paths.items()}
    audit_sources(
        originals["fceu"], originals["libretro"], originals["input"],
        originals["pads"], originals["state"], originals["general"],
        originals["video"]
    )

    generated = {
        "fceu_fds.c": patch_v4_zip_fceu(patch_v8_fceu(patch_v6_fceu(patch_fceu(originals["fceu"])))),
        "libretro_fds.c": patch_v4_zip_libretro(patch_v14_libretro(patch_v11_libretro(patch_v7_libretro(patch_v6_libretro(patch_v4_generated_libretro(patch_libretro(originals["libretro"]))))))),
        "input_fds.c": INPUT_FDS_C,
        "pads_fds.c": patch_v6_pads(PADS_FDS_C),
        "state_fds.c": patch_v11_state(patch_state(originals["state"])),
        "general_fds.c": patch_general(originals["general"]),
        "video_fds.c": patch_v4_generated_video(patch_video(originals["video"])),
        "file_fds.c": patch_v4_zip_file(patch_file(originals["file"])),
        "fds_fds.c": patch_v3_safe_fds(patch_v14_fds(patch_v11_fds(patch_v10_fds(patch_v8_fds(patch_v7_fds(patch_v6_fds(patch_fds(originals["fds"])))))))),
        "ppu_fds.c": patch_v3_safe_ppu(patch_v9_ppu(patch_v8_ppu(patch_v7_ppu(patch_v6_ppu(patch_v5_ppu(originals["ppu"])))))),
        "pputile_fds.h": patch_v5_pputile(originals["pputile"]),
        "sound_fds.c": patch_v3_safe_sound(patch_v10_sound(patch_v8_sound(patch_v6_sound(patch_v5_sound(originals["sound"]))))),
        "x6502_fds.c": patch_v9_x6502(patch_v8_x6502(patch_v7_x6502(originals["x6502"]))),
    }
    audit_generated(generated)
    for name, body in generated.items():
        write_atomic(out / name, body)

    print("[FCEUmm FDS prepare] generated lean FDS-only/two-pad overlays")
    print("[FCEUmm FDS prepare] removed runtime: cheat/movie/netplay/VS/peripherals")
    print("[FCEUmm FDS prepare] BIOS: <Aurora SYSTEM>/disksys.rom")
    for name in generated:
        print(f"  {out / name}")


if __name__ == "__main__":
    main()

