# Aurora Runtime Trace

`Aurora Runtime Trace` is the project-wide runtime tracing infrastructure used
to diagnose hard freezes, timing stalls, polling loops and subsystem
handshakes on real PS2 hardware.

It belongs to **Aurora**, not to the SNES core. SNES is the first trace client
and currently supplies the instruction rings and most event hooks.

## Design rule

**A probe for one bug is not architecture.**

Game names, one-off frame numbers, temporary line selections and experimental
conditions belong in `aurora_runtime_trace_profile.h`. The trace engine must
not accumulate `if (DKC)`, `frame == 161`, or similar investigation-specific
policy.

## Layers

### Engine — `aurora_runtime_trace.c`

Stable responsibilities:

- binary record format and sequence numbers;
- file creation and append/reopen handling;
- crash-oriented `cf(pre-close)` / `ca(post-close)` durability evidence;
- generic records, snapshots and phase checkpoints;
- context extraction from currently available instruction rings;
- runtime enable/disable state.

Changing a probe normally should **not** require editing this layer.

### Public contract — `aurora_runtime_trace.h`

Contains stable event IDs, persistence flags, phase IDs and callable trace APIs.

Numeric record IDs are part of the binary contract. Do not renumber existing
IDs. Add new IDs and teach the decoder about them.

The phase API is generic:

```c
AuroraTracePhase(phase_id, value_a, value_b);
```

The current phase IDs describe SNES lifecycle boundaries because SNES is the
first client. A future core may add its own phase namespace without changing
the writer.

### Probe policy — `aurora_runtime_trace_profile.h`

This is the normal customization point.

The current profile preserves the DKC investigation:

```c
AURORA_TRACE_PROFILE_CORE        "SNES"
AURORA_TRACE_PROFILE_SCOPE       EXACT_FRAME
AURORA_TRACE_PROFILE_FRAME_FIRST 161
AURORA_TRACE_PROFILE_FRAME_LAST  161
```

For a boot/black-screen investigation, use a small frame range such as 0..16.
For continuous deep probing use `ALL_FRAMES`. For only the normal lightweight
trace use `DISABLED`.

SNES scanline selection is centralized in
`AuroraTraceProfileSnesLineSelected()`. Change raster sampling there rather
than inserting game-specific conditions into `SnesSystem::ExecuteLine()`.

### Client hooks

The current SNES client contributes:

- 65816 pre-dispatch instruction ring;
- SPC700 instruction ring;
- APUIO read/write events;
- SPC I/O and DSP events;
- KON/KOFF and BRR breadcrumbs;
- MDMA/HDMA breadcrumbs;
- frame/PPU/VBlank/SPC/mixer lifecycle phases.

Hooks describe **what happened**. The profile decides **when expensive capture
is worthwhile**.

### Decoder — `tools/aurora_trace_decode.py`

The standalone decoder is the canonical human-readable view of `.bin`
captures:

```sh
python3 tools/aurora_trace_decode.py a6.YYYYMMDD-HHMMSS.bin
```

Keep it backward-compatible whenever practical. A new runtime record or phase
ID should receive a decoder label.

## Durability model

A normal `fflush()` is not treated as a durable FAT metadata boundary.

Before a real close the writer emits `cf(pre-close)`. After `fflush()` plus
`fclose()` return successfully and the file is reopened it emits
`ca(post-close)`. A visible `ca` confirms the named earlier close returned
successfully. The newest `cf` may legitimately lack a later durable `ca` if
the PS2 freezes before another close persists that ACK.

## Performance rules

1. Do not write every instruction to disk. Keep hot history in RAM rings.
2. Do not add a durable close inside a hot polling loop.
3. Prefer a compact transition/counter over thousands of identical records.
4. Keep expensive probes profile-gated.
5. Preserve the normal emulator path when `AURORA_RUNTIME_TRACE=0`.
6. A diagnostic probe must not silently change emulation semantics.

## Adding another Aurora core later

A new core should:

1. call `AuroraTraceBeginGame(name, "CORE_ID")`;
2. emit generic records/phases at useful lifecycle boundaries;
3. optionally provide instruction-context rings/adapters;
4. add profile selectors only when expensive probing is needed;
5. add decoder labels for new record/phase IDs.

Do not copy the storage backend into each emulator core.

## Build and runtime

```sh
make AURORA_RUNTIME_TRACE=1
```

The current runtime toggle is `L3+R3`.

Turning tracing off closes the active capture. Switching games while tracing
remains enabled creates a separate capture for the next game.

## Binary compatibility

This refactor preserves the existing v6 layout and current `VR10` marker. It
is an organizational refactor, not a wire-format revision. Existing R8/R9/R10
captures remain decodable.

## DKC hard-freeze hunt profile (AURORA_TRACE_PROFILE_DKC_HUNT_V1_20260918)

The DKC diagnostic profile demonstrates the intended separation between engine
and investigation policy:

- engine/binary persistence remains unchanged;
- the profile activates only for SNES emu frames 150 through 170;
- only selected coarse before/after phases become durable;
- SNES raster checkpoints use a 64-line grid plus hardware-sensitive
  visible/vblank boundaries;
- each selected phase carries the last 8 S-CPU and 8 SPC instructions.

The range is intentionally temporary. Once the failing subsystem is localized,
replace this profile with a narrower investigation rather than teaching the
engine about DKC.

## EE crash diagnostics (AURORA_EE_CRASH_DIAG_DKC_V1_20260918)

When `AURORA_RUNTIME_TRACE=1`, the build also enables
`AURORA_EE_CRASH_DIAG=1` by default.

This layer is host-side and belongs to Aurora, not to SNES. It uses PS2SDK
`eedebug` Level-1 exception vectors and a RAM-only 64-entry breadcrumb ring.
Each breadcrumb snapshots the EE `$sp`, `$ra` and `$gp` registers.

The current SNES client publishes host progress around:

- the frontend `ExecuteFrame()` virtual call;
- `SnesSystem::SyncSPC()`;
- the `SNSPCExecute()` call;
- APUIO queue synchronization.

Breadcrumb publication performs no file I/O.

If the EE raises a catchable Level-1 exception while trace mode is armed, the
normal emulator renderer is abandoned and libdebug displays a crash card with
`EPC`, `BadVAddr`, `Cause`, `Status`, `SP`, `RA`, `GP` and the six newest host
breadcrumbs. The console then intentionally remains halted so the screen can
be photographed.

For the DKC hunt, durable `spc-sync-begin` and `mix-begin` checkpoints are
intentionally disabled. Their END checkpoints remain durable. This removes
`fclose()/reopen` from immediately before those subsystems and helps distinguish
a real host/core failure from trace-storage perturbation.

Map a reported EPC with:

```sh
mips64r5900el-ps2-elf-addr2line -f -C -e build/SNESticle.elf 0xEPC
```

Build-time A/B override:

```sh
make AURORA_RUNTIME_TRACE=1 AURORA_EE_CRASH_DIAG=0
```

## Preemptive EE hang watchdog (AURORA_EE_HANG_WATCHDOG_DKC_V2_20260918)

A second EE thread is armed around the complete traced SNES frame, including
TraceFrameBegin, core execution and TraceFrameEnd. It wakes every 250 ms and,
after approximately four seconds with no RAM breadcrumb progress, attempts to
suspend the emulation thread and replaces the display with an
`AURORA EE HANG WATCHDOG` card.

The card shows the armed frame, main-thread state/wait type/priority, and the
eight newest RAM breadcrumbs with captured EE SP/RA/GP.

During this DKC hunt, phase-triggered durable close/reopen operations are
disabled. Phase hooks still publish RAM breadcrumbs before the profile rejects
their file-backed record. This separates observation from USB/FAT persistence.

