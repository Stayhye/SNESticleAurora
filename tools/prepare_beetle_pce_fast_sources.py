#!/usr/bin/env python3
# AURORA_PCE_MIPS_STAGE_V1_20260928
from __future__ import annotations

import argparse
from pathlib import Path
import io
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

EXPECTED_HEAD = "02ee59546765b50d88d50210bb8ff5188ee4af2f"
EXPECTED_BLOBS = {
    "Makefile.common": "f10aecd5b405dfcb664ac2fe8da2e08f0ea86252",
    "mednafen/pce_fast/huc6280.c": "df7e37c775d494efc12f66e7e56661c0f50579c7",
    "mednafen/pce_fast/huc6280.h": "58878a560274e09f1124bd3fce532d715ab030f4",
    "mednafen/pce_fast/vdc.c": "ab26711b3b61c2ab714a2d397d4d9427d1b57034",
    "mednafen/pce_fast/psg.c": "7ff88864bc5fcd96c914e08fc058e32339513c44",
    "mednafen/sound/Blip_Buffer.c": "556575f66ed74b6667711c4208f7f01dced4bfe8",
    "mednafen/include/blip/Blip_Buffer.h": "326c3ba39b280d7bfc9b99acb55658b43f901400",
}
MARKER = "AURORA_PCE_MIPS_STAGE_V1_20260928"


def run(cmd, cwd=None, check=True):
    return subprocess.run(cmd, cwd=cwd, check=check, text=True,
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def commit_blob(src: Path, rel: str) -> str:
    out = run(["git", "ls-tree", "HEAD", "--", rel], cwd=src).stdout.strip()
    parts = out.split()
    if len(parts) < 3 or parts[1] != "blob":
        raise SystemExit(f"missing PCE committed source: {rel}")
    return parts[2]


def validate_source(src: Path) -> None:
    if not (src / ".git").exists():
        raise SystemExit(f"PCE source is not a populated submodule: {src}")
    head = run(["git", "rev-parse", "HEAD"], cwd=src).stdout.strip()
    if head != EXPECTED_HEAD:
        raise SystemExit(f"PCE submodule HEAD mismatch: {head} != {EXPECTED_HEAD}")
    # Validate the commit, not the working tree. Old Aurora builds may have
    # left generated/modified build products inside the submodule; staging
    # must be reproducible from the pinned commit and must ignore all of them.
    for rel, expected in EXPECTED_BLOBS.items():
        got = commit_blob(src, rel)
        if got != expected:
            raise SystemExit(f"PCE committed fingerprint mismatch for {rel}: {got} != {expected}")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n != 1:
        raise SystemExit(f"{label}: expected one anchor, found {n}")
    return text.replace(old, new, 1)


def replace_define(text: str, name: str, new_line: str) -> str:
    pattern = re.compile(r"^#define\s+" + re.escape(name) + r"\b[^\n]*$", re.M)
    text2, n = pattern.subn(new_line, text, count=1)
    if n != 1:
        raise SystemExit(f"huc6280.c: expected one {name} definition, found {n}")
    return text2


def patch_huc(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    anchor = "struct HuC6280 HuCPU;\n"
    insert = r'''

#ifdef AURORA_PS2_PCE_FAST
/* AURORA_PCE_MIPS_CPU_BMT_V4_20260928
 * Keep HuC6280 timing, event checks, callbacks and state in C.  Only bulk
 * byte movement between pages already certified as plain RAM is delegated
 * to R5900 assembly.  Chunks never cross an 8 KiB MPR page and are limited
 * so every copied byte remains strictly before next_user_event. */
extern void AuroraPceBmtCopyIncPS2(uint8 *dst, const uint8 *src, uint32 count);
extern void AuroraPceBmtCopyDecPS2(uint8 *dst, const uint8 *src, uint32 count);

static INLINE uint32 AuroraPceMinU32(uint32 a, uint32 b)
{
   return a < b ? a : b;
}

static INLINE uint32 AuroraPceBmtEventBudget(const int32 next_user_event)
{
   const int32 delta = next_user_event - HuCPU.timestamp;
   if(delta <= 6)
      return 0;
   return (uint32)(delta - 1) / 6U;
}

static INLINE uint32 AuroraPceBmtLogicalLength(void)
{
   return HuCPU.bmt_length ? (uint32)HuCPU.bmt_length : 0x10000U;
}

static INLINE uint32 AuroraPceBmtFastTII(const int32 next_user_event)
{
   const uint32 src_addr = HuCPU.bmt_src;
   const uint32 dst_addr = HuCPU.bmt_dest;
   const uint32 src_off = src_addr & 0x1FFFU;
   const uint32 dst_off = dst_addr & 0x1FFFU;
   const uint8 *src = HuCPU.FastReadPage[src_addr >> 13];
   uint8 *dst = HuCPU.FastWritePage[dst_addr >> 13];
   uint32 n;

   if(!src || !dst)
      return 0;

   n = AuroraPceBmtLogicalLength();
   n = AuroraPceMinU32(n, 0x2000U - src_off);
   n = AuroraPceMinU32(n, 0x2000U - dst_off);
   n = AuroraPceMinU32(n, AuroraPceBmtEventBudget(next_user_event));
   if(n < 4U)
      return 0;

   AuroraPceBmtCopyIncPS2(dst + dst_off, src + src_off, n);
   HuCPU.bmt_src = (uint16)(HuCPU.bmt_src + n);
   HuCPU.bmt_dest = (uint16)(HuCPU.bmt_dest + n);
   HuCPU.bmt_length = (uint16)(HuCPU.bmt_length - n);
   HuCPU.timestamp += (int32)(n * 6U);
   return n;
}

static INLINE uint32 AuroraPceBmtFastTDD(const int32 next_user_event)
{
   const uint32 src_addr = HuCPU.bmt_src;
   const uint32 dst_addr = HuCPU.bmt_dest;
   const uint32 src_off = src_addr & 0x1FFFU;
   const uint32 dst_off = dst_addr & 0x1FFFU;
   const uint8 *src = HuCPU.FastReadPage[src_addr >> 13];
   uint8 *dst = HuCPU.FastWritePage[dst_addr >> 13];
   uint32 n;

   if(!src || !dst)
      return 0;

   n = AuroraPceBmtLogicalLength();
   n = AuroraPceMinU32(n, src_off + 1U);
   n = AuroraPceMinU32(n, dst_off + 1U);
   n = AuroraPceMinU32(n, AuroraPceBmtEventBudget(next_user_event));
   if(n < 4U)
      return 0;

   AuroraPceBmtCopyDecPS2(dst + dst_off, src + src_off, n);
   HuCPU.bmt_src = (uint16)(HuCPU.bmt_src - n);
   HuCPU.bmt_dest = (uint16)(HuCPU.bmt_dest - n);
   HuCPU.bmt_length = (uint16)(HuCPU.bmt_length - n);
   HuCPU.timestamp += (int32)(n * 6U);
   return n;
}
#else
#define AuroraPceBmtFastTII(next_user_event) 0U
#define AuroraPceBmtFastTDD(next_user_event) 0U
#endif
'''
    text = replace_once(text, anchor, anchor + insert, "huc6280.c HuCPU anchor")

    text = replace_define(
        text, "BMT_TDD",
        "#define BMT_TDD BMT_PREHONK(TDD); do { if(AuroraPceBmtFastTDD(next_user_event) && !HuCPU.bmt_length) break; ADDCYC(6); WrMem(HuCPU.bmt_dest, RdMem(HuCPU.bmt_src)); HuCPU.bmt_src--; HuCPU.bmt_dest--; BMT_HONKHONK(TDD); HuCPU.bmt_length--; } while(HuCPU.bmt_length);"
    )
    text = replace_define(
        text, "BMT_TII",
        "#define BMT_TII BMT_PREHONK(TII); do { if(AuroraPceBmtFastTII(next_user_event) && !HuCPU.bmt_length) break; ADDCYC(6); WrMem(HuCPU.bmt_dest, RdMem(HuCPU.bmt_src)); HuCPU.bmt_src++; HuCPU.bmt_dest++; BMT_HONKHONK(TII); HuCPU.bmt_length--; } while(HuCPU.bmt_length);"
    )
    path.write_text(text, encoding="utf-8")


def patch_blip(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    include_anchor = '#include <math.h>\n'
    decl = r'''

#ifdef AURORA_PS2_PCE_FAST
/* AURORA_PCE_MIPS_AUDIO_BLIP_V2_20260928
 * Pure sample-reader kernel: raw delta buffer, interleaved output stride,
 * high-pass accumulator and exact 16-bit clipping. Buffer removal/memmove
 * remains in the original C code. */
extern int32_t AuroraPceBlipReadStereoPS2(
      const int32_t *buffer, int16_t *out, int32_t count,
      int32_t bass_shift, int32_t reader_accum);
typedef char AuroraPceBlipLongMustBe32[(sizeof(blip_long) == 4) ? 1 : -1];
typedef char AuroraPceBlipSampleMustBe16[(sizeof(blip_sample_t) == 2) ? 1 : -1];
#endif
'''
    text = replace_once(text, include_anchor, include_anchor + decl,
                        "Blip_Buffer.c include anchor")

    pattern = re.compile(
        r"long Blip_Buffer_read_samples\(Blip_Buffer\* bbuf, blip_sample_t\* out,\n"
        r"\s+long max_samples\)\n\{.*?\n\}\n\nvoid Blip_Buffer_mix_samples",
        re.S,
    )
    replacement = r'''long Blip_Buffer_read_samples(Blip_Buffer* bbuf, blip_sample_t* out,
                              long max_samples)
{
   long count = Blip_Buffer_samples_avail(bbuf);
   if (count > max_samples)
      count = max_samples;

   if (count)
   {
#ifdef AURORA_PS2_PCE_FAST
      bbuf->reader_accum = AuroraPceBlipReadStereoPS2(
            (const int32_t *)bbuf->buffer,
            (int16_t *)out,
            (int32_t)count,
            (int32_t)BLIP_READER_BASS(*bbuf),
            (int32_t)bbuf->reader_accum);
#else
      int const bass = BLIP_READER_BASS(*bbuf);
      BLIP_READER_BEGIN(reader, *bbuf);

      blip_long n;

      for (n = count; n; --n)
      {
         blip_long s = BLIP_READER_READ(reader);
         if ((blip_sample_t) s != s)
            s = 0x7FFF - (s >> 24);
         *out = (blip_sample_t) s;
         out += 2;
         BLIP_READER_NEXT(reader, bass);
      }

      BLIP_READER_END(reader, *bbuf);
#endif
      Blip_Buffer_remove_samples(bbuf, count);
   }
   return count;
}

void Blip_Buffer_mix_samples'''
    text2, n = pattern.subn(replacement, text, count=1)
    if n != 1:
        raise SystemExit(f"Blip_Buffer.c: read_samples replacement count={n}")
    path.write_text(text2, encoding="utf-8")



def patch_vdc_sprite_clear(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    include_anchor = '#include "../state_helpers.h"\n'
    decl = (
        "\n#ifdef AURORA_PS2_PCE_FAST\n"
        "/* AURORA_PCE_SPR_CLEAR128_STAGE_V16_20260929\n"
        " * Only the sprite-line zero clear uses the new R5900 kernel.\n"
        " * Revision-v2 DrawBG remains C. */\n"
        "extern void AuroraPceClearSpriteLinePS2(uint32 *dst, uint32 count);\n"
        "#endif\n"
    )
    text = replace_once(
        text, include_anchor, include_anchor + decl,
        "vdc.c sprite-clear declaration anchor"
    )

    old = (
        "   MDFN_FastU32MemsetM8(\n"
        "      (uint32 *)spr_linebuf, 0, ((end + 3) >> 1) & ~1);\n"
    )
    new = (
        "#ifdef AURORA_PS2_PCE_FAST\n"
        "   AuroraPceClearSpriteLinePS2(\n"
        "      (uint32 *)spr_linebuf, "
        "(uint32)(((end + 3) >> 1) & ~1));\n"
        "#else\n"
        "   MDFN_FastU32MemsetM8(\n"
        "      (uint32 *)spr_linebuf, 0, ((end + 3) >> 1) & ~1);\n"
        "#endif\n"
    )
    text = replace_once(
        text, old, new, "vdc.c sprite-line zero clear"
    )
    path.write_text(text, encoding="utf-8")


def patch_vdc(path: Path) -> None:
    text = path.read_text(encoding="utf-8")
    include_anchor = '#include "../state_helpers.h"\n'
    decl = r'''

#ifdef AURORA_PS2_PCE_FAST
/* AURORA_PCE_MIPS_GFX_BG_V3_20260928
 * One assembly iteration performs the same BAT lookup and emits the same
 * packed eight indexed pixels as the existing Aurora C fast path. */
extern void AuroraPceDrawBgTilesPS2(
      const uint16 *bat_base, const uint64 *cg_base, uint64 *target,
      uint32 tile_count, uint32 bat_boom, uint32 bat_width_mask,
      const uint64 *cg_mask, const uint64 *color_lut);
#endif
'''
    text = replace_once(text, include_anchor, include_anchor + decl,
                        "vdc.c include anchor")

    old = r'''#ifdef AURORA_PS2_PCE_FAST
      {
         /* AURORA_PCE_EXPERIMENTAL_V7
          * DrawBG emits one uint64 == eight indexed pixels per iteration. */
         const unsigned aurora_tile_count = (count + 7U) >> 3;
         x = (int)aurora_tile_count;

         while(x--)
         {
            const uint16 bat = BAT_Base[bat_boom];
            const uint64 color_or = cblock_exlut[bat >> 12];

            *target64 = (CG_Base[(bat & 0xFFF) * 8] & cg_mask) | color_or;

            bat_boom = (bat_boom + 1) & bat_width_mask;
            target64++;
         }
      }
#else'''
    new = r'''#ifdef AURORA_PS2_PCE_FAST
      {
         const unsigned aurora_tile_count = (count + 7U) >> 3;
         AuroraPceDrawBgTilesPS2(
               BAT_Base, CG_Base, target64,
               aurora_tile_count, (uint32)bat_boom, (uint32)bat_width_mask,
               &cg_mask, cblock_exlut);
      }
#else'''
    text = replace_once(text, old, new, "vdc.c DrawBG fast path")
    path.write_text(text, encoding="utf-8")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", type=Path, required=True)
    ap.add_argument("--stage", type=Path)
    ap.add_argument("--asm", type=Path)
    ap.add_argument("--check-source", action="store_true")
    args = ap.parse_args()

    src = args.source.resolve()
    validate_source(src)
    if args.check_source:
        print(f"[PCE stage] source OK: {EXPECTED_HEAD}")
        return 0

    if args.stage is None or args.asm is None:
        ap.error("--stage and --asm are required unless --check-source is used")
    asm = args.asm.resolve()
    if not asm.is_file() or "AURORA_PCE_MIPS_HOTPATHS_V4_20260928" not in asm.read_text(encoding="utf-8"):
        raise SystemExit(f"missing or unrecognized PCE MIPS source: {asm}")

    stage = args.stage.resolve()
    stage.parent.mkdir(parents=True, exist_ok=True)
    tmp = Path(tempfile.mkdtemp(prefix=stage.name + ".tmp-", dir=stage.parent))
    try:
        # Export exactly HEAD. Never copy the submodule working tree, so local
        # objects/.d files or unrelated dirty files cannot leak into Continuous.
        # run() is text-oriented, so use check_output for the binary tar stream.
        tar_bytes = subprocess.check_output(
            ["git", "archive", "--format=tar", "HEAD"], cwd=src)
        with tarfile.open(fileobj=io.BytesIO(tar_bytes), mode="r:") as tf:
            tf.extractall(tmp)
        # AURORA_ASM_AUDIT_V11_PCE_SELECTIVE_C_20260929
        # Final conservative PCE policy after comparing the current MIPS
        # kernels with the last pre-MIPS build.
        #
        # Keep BMT assembly: C still owns timing/event/page certification;
        # assembly only copies already-certified plain RAM bytes and removes
        # the old per-byte RdMem/WrMem hot-path overhead.
        #
        # Keep Blip and VDC in the pinned C source instead of replacing their
        # already -O3/-funroll-loops loops with scalar assembly.  We do not
        # recreate stale C here: the pristine pinned Beetle commit is exported
        # first, so all existing Aurora C fast paths remain exactly intact.
        patch_huc(tmp / "mednafen/pce_fast/huc6280.c")
        patch_vdc_sprite_clear(tmp / "mednafen/pce_fast/vdc.c")
        asm_dst = tmp / "mednafen/pce_fast/aurora_pce_hotpaths_ps2.S"
        shutil.copy2(asm, asm_dst)
        (tmp / ".aurora-pce-mips-stage-v1").write_text(
            f"{MARKER}\nsource={EXPECTED_HEAD}\n", encoding="utf-8")
        if stage.exists():
            shutil.rmtree(stage)
        os.replace(tmp, stage)
    except BaseException:
        if tmp.exists():
            shutil.rmtree(tmp, ignore_errors=True)
        raise

    print(f"[PCE stage] prepared pristine build-tree copy: {stage}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
