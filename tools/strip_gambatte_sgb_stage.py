#!/usr/bin/env python3
from pathlib import Path
import argparse
import os
import re
import sys
import tempfile

MARK = "AURORA_GAMBATTE_GBC_ONLY_STAGE_V4_20260921"
STAMP = ".aurora-gambatte-gbc-only-v4"

class StripError(RuntimeError):
    pass

def atomic_write(path: Path, data: str):
    fd, tmp = tempfile.mkstemp(prefix=path.name + ".", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as f:
            f.write(data)
        os.replace(tmp, path)
    except Exception:
        try:
            os.unlink(tmp)
        except FileNotFoundError:
            pass
        raise

def remove_function(text: str, needle: str, label: str) -> str:
    start = text.find(needle)
    if start < 0:
        raise StripError(f"{label}: function anchor missing: {needle}")
    brace = text.find("{", start)
    if brace < 0:
        raise StripError(f"{label}: opening brace missing")
    depth = 0
    i = brace
    while i < len(text):
        ch = text[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                while end < len(text) and text[end] in " \t":
                    end += 1
                if end < len(text) and text[end] == ";":
                    end += 1
                if end < len(text) and text[end] == "\r":
                    end += 1
                if end < len(text) and text[end] == "\n":
                    end += 1
                if end < len(text) and text[end] == "\n":
                    end += 1
                return text[:start] + text[end:]
        i += 1
    raise StripError(f"{label}: unbalanced braces")

def remove_decl(text: str, pattern: str, label: str) -> str:
    # AURORA_GAMBATTE_STRIP_DECL_NEWLINE_V4_1I_20260921: preserve one line break so adjacent declarations do not concatenate.
    out, n = re.subn(pattern, "\n", text, count=1, flags=re.M | re.S)
    if n != 1:
        raise StripError(f"{label}: expected one declaration, found {n}")
    return out

def replace_once(text: str, old: str, new: str, label: str) -> str:
    n = text.count(old)
    if n != 1:
        raise StripError(f"{label}: expected one anchor, found {n}")
    return text.replace(old, new, 1)

def patch_gambatte_h(text: str) -> str:
    if MARK in text:
        return text
    # External-SGB clocking APIs.
    text = remove_decl(text, r"\n\s*/\* AURORA_SGB_JOYP_BRIDGE_V1_1_20260907.*?unsigned long runForClocks\(.*?\);\n", "gambatte.h runForClocks")
    text = remove_decl(text, r"\n\s*/\* AURORA_V6_RUNTIME_EFFECT_ALL5_20260908.*?unsigned long runForClocksSgb64\(.*?\);\n", "gambatte.h runForClocksSgb64")
    # Staged preparer adds runForAurora64 after runForClocksSgb64.
    text = remove_decl(text, r"\n\s*/\* AURORA_GB_AUDIO_NATIVE64_R9_20260909.*?long runForAurora64\(.*?\);\n", "gambatte.h runForAurora64")
    text = remove_decl(text, r"^\s*void clearSgbAudioDecimator\(\);\n", "gambatte.h clear SGB audio")
    text = remove_decl(text, r"\n\s*/\* AURORA_SGB_JOYP_BRIDGE_V1_1_20260907.*?typedef unsigned char \(\*SgbJoypCallback\).*?void setSgbJoypCallback\(SgbJoypCallback callback, void \*userdata\);\n", "gambatte.h JOYP API")
    text = remove_decl(text, r"\n\s*/\* AURORA_SGB_CLASSIC_PLUS_LINK_V2_20260907.*?void setScanlineCallback\(void \(\*callback\)\(unsigned\)\);\n", "gambatte.h scanline API")
    text = remove_decl(text, r"\n\s*/\* bsnes-classic HLEs.*?void setSgbPostBootState\(bool sgb2\);\n", "gambatte.h postboot API")
    text = remove_decl(text, r"\n\s*/\* AURORA_SGB_GAMBATTE_VIDEO_PERF_FIX_V1_1_3_20260907 \*/\n\s*void setSgbVideoBuffer\(gambatte::video_pixel_t \*videoBuf, int pitch\);\n", "gambatte.h SGB video API")
    text = text.replace("namespace gambatte {\n", f"namespace gambatte {{\n/* {MARK}: SGB host APIs stripped from the PS2 staged core. */\n", 1)
    return text

def patch_cpu_h(text: str) -> str:
    # Remove SGB-only execution accounting and forwarders. Normal GB::runFor
    # still owns all timing used by standalone GB/GBC.
    text = re.sub(r"^\s*unsigned long lastRunCycles\(\) const \{ return lastRunCycles_; \}\n", "", text, count=1, flags=re.M)
    text = re.sub(r"^\s*void setAccumulator\(unsigned char value\) \{ a_ = value; \}.*\n", "", text, count=1, flags=re.M)
    if "void setSgbPostBootState(bool sgb2)" in text:
        text = remove_function(text, "void setSgbPostBootState(bool sgb2)", "cpu.h SGB postboot")
    if "void setScanlineCallback(void (*callback)(unsigned))" in text:
        text = remove_function(text, "void setScanlineCallback(void (*callback)(unsigned))", "cpu.h scanline")
    if "void setSgbJoypCallback(" in text:
        text = remove_function(text, "void setSgbJoypCallback(", "cpu.h JOYP")
    text = re.sub(r"^\s*std::size_t fillSoundBufferSgb64\(\).*\n", "", text, count=1, flags=re.M)
    text = re.sub(r"^\s*void clearSgbAudioDecimator\(\).*\n", "", text, count=1, flags=re.M)
    text = re.sub(r"^\s*unsigned long lastRunCycles_;.*\n", "", text, count=1, flags=re.M)
    return text

def patch_cpu_cpp(text: str) -> str:
    text = re.sub(r"^, lastRunCycles_\(0\).*\n", "", text, count=1, flags=re.M)
    start = text.find("long CPU::runFor(unsigned long const cycles) {")
    if start < 0:
        raise StripError("cpu.cpp runFor missing")
    end_marker = "\nenum { hf2_hcf"
    end = text.find(end_marker, start)
    if end < 0:
        raise StripError("cpu.cpp runFor end missing")
    new = '''long CPU::runFor(unsigned long const cycles) {
\tprocess(cycles);

\tlong const csb = mem_.cyclesSinceBlit(cycleCounter_);

\tif (cycleCounter_ & 0x80000000)
\t\tcycleCounter_ = mem_.resetCounters(cycleCounter_);

\treturn csb;
}
'''
    return text[:start] + new + text[end:]

def patch_memory_h(text: str) -> str:
    if "void setSgbJoypCallback(" in text:
        text = remove_function(text, "void setSgbJoypCallback(", "memory.h JOYP")
    text = re.sub(r"^\s*std::size_t fillSoundBufferSgb64\(.*?^\s*}\n", "", text, count=1, flags=re.M | re.S)
    text = re.sub(r"^\s*void clearSgbAudioDecimator\(\).*\n", "", text, count=1, flags=re.M)
    if "void setScanlineCallback(void (*callback)(unsigned))" in text:
        text = remove_function(text, "void setScanlineCallback(void (*callback)(unsigned))", "memory.h scanline")
    text = re.sub(r"^\s*unsigned char \(\*sgbJoypCallback_\).*\n", "", text, count=1, flags=re.M)
    text = re.sub(r"^\s*void \*sgbJoypUser_;\n", "", text, count=1, flags=re.M)
    return text

def patch_memory_cpp(text: str) -> str:
    text = re.sub(r"^, sgbJoypCallback_\(0\).*\n", "", text, count=1, flags=re.M)
    text = re.sub(r"^, sgbJoypUser_\(0\)\n", "", text, count=1, flags=re.M)

    start = text.find("void Memory::updateInput() {")
    end = text.find("\nvoid Memory::updateOamDma", start)
    if start < 0 or end < 0:
        raise StripError("memory.cpp updateInput anchors missing")
    normal = '''void Memory::updateInput() {
\tunsigned state = 0xF;

\tif ((ioamhram_[0x100] & 0x30) != 0x30 && getInput_) {
\t\tunsigned input = (*getInput_)();
\t\tunsigned dpad_state = ~input >> 4;
\t\tunsigned button_state = ~input;
\t\tif (!(ioamhram_[0x100] & 0x10))
\t\t\tstate &= dpad_state;
\t\tif (!(ioamhram_[0x100] & 0x20))
\t\t\tstate &= button_state;
\t}

\tif (state != 0xF && (ioamhram_[0x100] & 0xF) == 0xF)
\t\tintreq_.flagIrq(0x10);

\tioamhram_[0x100] = (ioamhram_[0x100] & -0x10u) | state;
}
'''
    text = text[:start] + normal + text[end:]

    # AURORA_GAMBATTE_STRIP_JOYP_WRITE_SCOPE_V4_1K_20260921
    # FF00 returns to ordinary handheld JOYP semantics.  Scope the matcher to
    # nontrivial_ff_write() itself: an earlier version searched the whole file
    # and could start at nontrivial_ff_read()'s case 0x00, then run forward
    # until the writer's `return; case 0x01`, deleting the function boundary
    # and the OAM-DMA helper in between.
    writer_start = text.find("void Memory::nontrivial_ff_write(")
    writer_end = text.find("\nvoid Memory::nontrivial_write(", writer_start)
    if writer_start < 0 or writer_end < 0:
        raise StripError("memory.cpp nontrivial_ff_write anchors missing")
    writer = text[writer_start:writer_end]
    m = re.search(r"\tcase 0x00:\n.*?\n\t\treturn;\n\tcase 0x01:", writer, flags=re.S)
    if not m:
        raise StripError("memory.cpp FF00 SGB write block missing")
    ff00 = '''\tcase 0x00:
\t\tif ((data ^ ioamhram_[0x100]) & 0x30) {
\t\t\tioamhram_[0x100] = (ioamhram_[0x100] & ~0x30u) | (data & 0x30);
\t\t\tupdateInput();
\t\t}

\t\treturn;
\tcase 0x01:'''
    writer = writer[:m.start()] + ff00 + writer[m.end():]
    text = text[:writer_start] + writer + text[writer_end:]

    # Structural post-checks for the read/write boundary.  These are normal
    # GB/GBC paths and must never be removed by an SGB-only strip.
    read_start = text.find("unsigned Memory::nontrivial_ff_read(")
    read_end = text.find("\nstatic bool isInOamDmaConflictArea(", read_start)
    if read_start < 0 or read_end < 0:
        raise StripError("memory.cpp read/OAM helper boundary damaged")
    read_body = text[read_start:read_end]
    if "\tcase 0x00:\n\t\tupdateInput();\n\t\tbreak;" not in read_body:
        raise StripError("memory.cpp normal FF00 read path was altered")
    if "static bool isInOamDmaConflictArea(" not in text:
        raise StripError("memory.cpp OAM-DMA helper disappeared")
    if "unsigned Memory::nontrivial_read(" not in text:
        raise StripError("memory.cpp nontrivial_read disappeared")
    if "sgbJoypCallback_" in writer or "sgbJoypUser_" in writer:
        raise StripError("memory.cpp SGB JOYP writer survived")
    return text

def patch_gambatte_cpp(text: str) -> str:
    for needle, label in (
        ("unsigned long GB::runForClocks(", "GB runForClocks"),
        ("unsigned long GB::runForClocksSgb64(", "GB runForClocksSgb64"),
        ("long GB::runForAurora64(", "GB runForAurora64"),
        ("void GB::clearSgbAudioDecimator()", "GB clear SGB audio"),
        ("void GB::setSgbJoypCallback(", "GB JOYP"),
        ("void GB::setScanlineCallback(", "GB scanline"),
        ("void GB::setSgbPostBootState(", "GB postboot"),
        ("void GB::setSgbVideoBuffer(", "GB SGB video"),
    ):
        if needle in text:
            text = remove_function(text, needle, label)
    return text

def patch_video_h(text: str) -> str:
    if "void setScanlineCallback(void (*callback)(unsigned))" in text:
        text = remove_function(text, "void setScanlineCallback(void (*callback)(unsigned))", "video.h scanline setter")
    text = re.sub(r"^\s*void \(\*scanlineCallback_\)\(unsigned\);.*\n", "", text, count=1, flags=re.M)
    return text

def patch_video_cpp(text: str) -> str:
    # AURORA_GAMBATTE_STRIP_VIDEO_COMMENT_V4_1M_20260921
    # The top-of-file SGB banner names LCD::setScanlineCallback() even after
    # every executable scanline hook is stripped. Remove that obsolete banner
    # too, rather than weakening the whole-stage forbidden-symbol audit.
    top_banner = """/* AURORA_SGB_CLASSIC_PLUS_LINK_V2_20260907
 * SGB scanline notification is registered through LCD::setScanlineCallback(),
 * matching the bsnes-plus Gambatte contract and keeping this core standalone. */

"""
    text = replace_once(text, top_banner, "", "video.cpp obsolete SGB scanline banner")

    # AURORA_GAMBATTE_STRIP_VIDEO_EVENT_SCOPE_V4_1L_20260921
    # The same SGB comment marker also exists near the top of video.cpp.
    # Never run a DOTALL regex from that marker across the whole file: doing
    # so can consume LCD::reset(), helpers and most of LCD::event().  Restrict
    # the edit to the balanced LCD::event() body, then only strip the callback
    # immediately following the LY_COUNT bookkeeping.
    fn = "inline void LCD::event()"
    start = text.find(fn)
    if start < 0:
        raise StripError("video.cpp LCD::event anchor missing")
    brace = text.find("{", start)
    if brace < 0:
        raise StripError("video.cpp LCD::event opening brace missing")
    depth = 0
    i = brace
    end = -1
    while i < len(text):
        ch = text[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                end = i + 1
                break
        i += 1
    if end < 0:
        raise StripError("video.cpp LCD::event unbalanced braces")

    event = text[start:end]
    if event.count("case LY_COUNT:") != 1:
        raise StripError("video.cpp LCD::event LY_COUNT topology changed")
    if event.count("ppu_.doLyCountEvent();") != 1 or \
       event.count("eventTimes_.set<LY_COUNT>(ppu_.lyCounter().time());") != 1:
        raise StripError("video.cpp LCD::event LY_COUNT bookkeeping changed")

    ly_anchor = "eventTimes_.set<LY_COUNT>(ppu_.lyCounter().time());"
    ly_pos = event.find(ly_anchor)
    if ly_pos < 0:
        raise StripError("video.cpp LCD::event LY_COUNT anchor missing")
    tail_start = ly_pos + len(ly_anchor)
    tail = event[tail_start:]
    patterns = (
        r"\n[ \t]*/\* AURORA_SGB_CLASSIC_PLUS_LINK_V2_20260907.*?\*/\n"
        r"[ \t]*if \(scanlineCallback_\)\n"
        r"[ \t]*scanlineCallback_\(ppu_\.lyCounter\(\)\.ly\(\)\);",
        r"\n[ \t]*/\* AURORA_SGB_GAMBATTE_BSNESPLUS_VIDEO_V1_2_4_20260907.*?\*/\n"
        r"[ \t]*if \(scanlineCallback_\)\n"
        r"[ \t]*scanlineCallback_\(ppu_\.lyCounter\(\)\.ly\(\)\);",
    )
    total = 0
    for pat in patterns:
        tail2, n = re.subn(pat, "", tail, count=1, flags=re.M | re.S)
        if n:
            tail = tail2
            total += n
            break
    event = event[:tail_start] + tail
    if total != 1:
        raise StripError(f"video.cpp scanline callback in LCD::event: expected one block, found {total}")

    # Structural post-checks: the ordinary event switch and following update()
    # must survive intact; only the SGB callback block may disappear.
    for required in (
        "case LY_COUNT:",
        "ppu_.doLyCountEvent();",
        "eventTimes_.set<LY_COUNT>(ppu_.lyCounter().time());",
        "break;",
    ):
        if required not in event:
            raise StripError("video.cpp LCD::event structural post-check missing: " + required)
    if "scanlineCallback_" in event:
        raise StripError("video.cpp scanline callback survived scoped removal")

    out = text[:start] + event + text[end:]
    for required in (
        "void LCD::reset(",
        "void LCD::setStatePtrs(",
        "void LCD::update(const unsigned long cycleCounter)",
        "inline void LCD::event()",
    ):
        if required not in out:
            raise StripError("video.cpp surrounding structure lost: " + required)
    return out

def patch_video_libretro_cpp(text: str) -> str:
    # AURORA_GAMBATTE_STRIP_VIDEO_INIT_V4_1J_20260921: video_libretro.cpp owns LCD's constructor initializer.
    # video.h removes the SGB-only member itself, so this initializer must
    # disappear in the same staged transaction or the staged core will not compile.
    text, n = re.subn(
        r"^\s*scanlineCallback_\(0\),\s*/\* AURORA_SGB_CLASSIC_PLUS_LINK_V2_20260907 \*/\n",
        "", text, count=1, flags=re.M)
    if n != 1:
        raise StripError(f"video_libretro.cpp scanline initializer: expected one line, found {n}")
    return text

def patch_sound_h(text: str) -> str:
    text = re.sub(r"^\s*std::size_t fillBufferSgb64\(\);.*\n", "", text, count=1, flags=re.M)
    text = re.sub(r"^\s*void clearSgbDecimator\(\).*\n", "", text, count=1, flags=re.M)
    for pat in (
        r"^\s*unsigned sgbDecimCount_;.*\n",
        r"^\s*long sgbDecimLeft_;\n",
        r"^\s*long sgbDecimRight_;\n",
    ):
        text = re.sub(pat, "", text, count=1, flags=re.M)
    return text

def patch_sound_cpp(text: str) -> str:
    for pat in (
        r"^\s*,\s*sgbDecimCount_\(0\).*\n",
        r"^\s*,\s*sgbDecimLeft_\(0\)\n",
        r"^\s*,\s*sgbDecimRight_\(0\)\n",
    ):
        text = re.sub(pat, "", text, count=1, flags=re.M)
    # reset() and loadState() both used the SGB-only transient decimator.
    text = re.sub(r"^\s*clearSgbDecimator\(\);.*\n", "", text, flags=re.M)
    if "size_t PSG::fillBufferSgb64()" in text:
        text = remove_function(text, "size_t PSG::fillBufferSgb64()", "sound.cpp SGB decimator")
    return text

def verify(files):
    joined = "\n".join(files.values())
    forbidden = (
        "SgbJoypCallback", "sgbJoypCallback_", "sgbJoypUser_",
        "runForClocks(", "lastRunCycles_",
        "runForClocksSgb64", "runForAurora64", "fillBufferSgb64",
        "fillSoundBufferSgb64", "sgbDecimCount_", "sgbDecimLeft_",
        "sgbDecimRight_", "clearSgbDecimator", "clearSgbAudioDecimator",
        "setSgbPostBootState", "setSgbVideoBuffer",
        "setScanlineCallback", "scanlineCallback_",
    )
    bad = [x for x in forbidden if x in joined]
    if bad:
        raise StripError("SGB staged symbols survived: " + ", ".join(bad))
    # Things GB/GBC still needs must remain.
    required = (
        "long runFor(gambatte::video_pixel_t *videoBuf",
        "void setInputGetter(InputGetter *getInput)",
        "bool savedataDirty() const",
        "void clearSavedataDirty()",
        "void setBootloaderGetter",
        "void setSerialIO",
    )
    gh = files["libgambatte/include/gambatte.h"]
    missing = [x for x in required if x not in gh]
    if missing:
        raise StripError("required GB/GBC API removed: " + ", ".join(missing))
    mh = files["libgambatte/src/gambatte-memory.h"]
    if "bool saveDataDirty_;" not in mh:
        raise StripError("saveDataDirty_ was removed; refusing unsafe GBC stage")

def verify_entire_stage(stage):
    # V4_1 audit-only guard: no private SGB-only identifier may survive in
    # another compiled Gambatte source/header outside the edited file set.
    forbidden = (
        "sgbJoypCallback_", "sgbJoypUser_", "lastRunCycles_",
        "runForClocksSgb64", "runForAurora64", "fillBufferSgb64",
        "fillSoundBufferSgb64", "sgbDecimCount_", "sgbDecimLeft_",
        "sgbDecimRight_", "clearSgbDecimator", "clearSgbAudioDecimator",
        "setSgbPostBootState", "setSgbVideoBuffer", "scanlineCallback_",
    )
    root = stage / "libgambatte"
    for p in root.rglob("*"):
        if not p.is_file() or p.suffix.lower() not in (".h", ".hpp", ".c", ".cc", ".cpp"):
            continue
        try:
            text = p.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        bad = [x for x in forbidden if x in text]
        if bad:
            raise StripError("SGB staged identifier survived in %s: %s" %
                             (p.relative_to(stage), ", ".join(bad)))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", required=True)
    args = ap.parse_args()
    stage = Path(args.stage).expanduser().resolve()
    if not stage.is_dir():
        raise StripError(f"stage directory missing: {stage}")

    rels = [
        "libgambatte/include/gambatte.h",
        "libgambatte/src/cpu.h",
        "libgambatte/src/cpu.cpp",
        "libgambatte/src/gambatte-memory.h",
        "libgambatte/src/gambatte-memory.cpp",
        "libgambatte/src/gambatte.cpp",
        "libgambatte/src/video.h",
        "libgambatte/src/video.cpp",
        "libgambatte/src/video_libretro.cpp",
        "libgambatte/src/sound.h",
        "libgambatte/src/sound.cpp",
    ]
    before = {}
    for rel in rels:
        p = stage / rel
        if not p.is_file():
            raise StripError(f"staged source missing: {p}")
        before[rel] = p.read_text(encoding="utf-8")

    # Idempotent fast path: verify instead of silently trusting the marker.
    if MARK in before["libgambatte/include/gambatte.h"]:
        verify(before)
        verify_entire_stage(stage)
        atomic_write(stage / STAMP, MARK + "\n")
        print("[ Gambatte ] GBC-only stage already verified")
        return 0

    after = dict(before)
    after["libgambatte/include/gambatte.h"] = patch_gambatte_h(after["libgambatte/include/gambatte.h"])
    after["libgambatte/src/cpu.h"] = patch_cpu_h(after["libgambatte/src/cpu.h"])
    after["libgambatte/src/cpu.cpp"] = patch_cpu_cpp(after["libgambatte/src/cpu.cpp"])
    after["libgambatte/src/gambatte-memory.h"] = patch_memory_h(after["libgambatte/src/gambatte-memory.h"])
    after["libgambatte/src/gambatte-memory.cpp"] = patch_memory_cpp(after["libgambatte/src/gambatte-memory.cpp"])
    after["libgambatte/src/gambatte.cpp"] = patch_gambatte_cpp(after["libgambatte/src/gambatte.cpp"])
    after["libgambatte/src/video.h"] = patch_video_h(after["libgambatte/src/video.h"])
    after["libgambatte/src/video.cpp"] = patch_video_cpp(after["libgambatte/src/video.cpp"])
    after["libgambatte/src/video_libretro.cpp"] = patch_video_libretro_cpp(after["libgambatte/src/video_libretro.cpp"])
    after["libgambatte/src/sound.h"] = patch_sound_h(after["libgambatte/src/sound.h"])
    after["libgambatte/src/sound.cpp"] = patch_sound_cpp(after["libgambatte/src/sound.cpp"])

    verify(after)
    written = []
    try:
        for rel in rels:
            if after[rel] != before[rel]:
                atomic_write(stage / rel, after[rel])
                written.append(rel)
        # Re-audit the actual staged tree after atomic source replacement.
        verify_entire_stage(stage)

        # Force a clean staged-object/archive rebuild. This is deliberate:
        # a stale object compiled before the strip must never survive into ELF.
        for p in stage.rglob("*.o"):
            try:
                p.unlink()
            except FileNotFoundError:
                pass
        for p in stage.rglob("*.a"):
            try:
                p.unlink()
            except FileNotFoundError:
                pass
        atomic_write(stage / STAMP, MARK + "\n")
    except Exception:
        for rel in written:
            atomic_write(stage / rel, before[rel])
        raise

    print("[ Gambatte ] stripped SGB-only host APIs/state; GB/GBC retained")
    return 0

if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except StripError as exc:
        print("ERROR:", exc, file=sys.stderr)
        raise SystemExit(1)
