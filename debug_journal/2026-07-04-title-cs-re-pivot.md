# 2026-07-04 — title parity: pivot to Ghidra RE of 3DS title-cs, revert crutches

## User directives (2026-07-04, hard rules)

1. **NO opt-out gates.** SoH3D replicating 3DS one-to-one is the DEFAULT
   and ONLY behavior. No env vars, A/B toggles, or CVars for the
   N64-original path.
2. **Data-level compare, not visual.** No PNG side-by-side proofs; no
   proofs shown until 100% DATA-level match.
3. **RE + port via Ghidra.** For title parity, RE and port the 3DS title
   cutscene byte stream — do NOT chase visual compares, do NOT live-mirror
   Az state, do NOT transcribe timelines from observations.

## Reverted this commit

- Deleted `Shipwright/soh/src/zelda3d/zelda3d_title_lighting_timeline.inc`
  — the Az-transcribed slot table was a symptom fix, not a port. Az's
  cursor at 0x0054CC3C isn't fully deterministic under repeated pins
  (see prior journal `2026-07-04-title-lighting-timeline-port.md`) so
  the table drifted per-run anyway.
- Removed the timeline override from `Zelda3D_ApplyTitleCam`. Comment
  now points at the proper RE-driven port path.
- Removed the harness `MirrorAzEnvCtxIntoSoh` bridge from
  `HandleStep` — a dev crutch, not a port.

## Kept

- `SohState_SetEnvSlot(u8)` — low-level accessor, useful for the eventual
  port to poke SoH's envCtx during title cs setup.
- The RE'd envCtx layout (`play + 0x3135` base, `+0xA5` unk_BF, `+0xC8`
  unk_D8, stride 0x1C) — real ground truth, documented in
  `oot3d-decomp/docs/env_context_layout.md`.
- The `compare lighting` harness output printing both engines'
  envCtx.unk_BF side-by-side — the DATA-level compare that IS the yardstick.
- The `SDL_WINDOW_HIGH_PIXEL_DENSITY` drop in `gfx_sdl3.cpp` — SoH3D
  should render at the 3DS-native 400x240 unconditionally on all displays.
  The 400x240 window config in `HandleSohBoot` matches.

## RE angle: 0x0054CC3C is a VBLANK counter, NOT the title-cs cursor

Prior sessions treated `0x0054CC3C` (Az VA) as csCtx.frames — my
`force titletime` writes to it and there's SOME correlation with slot
changes. But `FindDataWriters` on that VA shows 4 refs total, and
decomp of the reader `FUN_003fd27c` (line 55) reveals it in a
`do { FUN_003101dc(param_2); } while (*(int*)(iVar6 + 8) < *(int*)(param_1 + 0x8c));`
polling loop — a VBLANK / frame-sync wait, not a cs cursor.

Writer `FUN_004175d4` is a one-shot init that resets it to 0. There is
no other STATIC ref to this VA — the per-frame increment must come
from a base+offset load Ghidra's DB doesn't recognize (likely the
guest OS's kernel-supplied frame counter, since this address is in
.data around the io/hw block).

**So the title-cs cursor lives elsewhere.** The prior mem-diff scan
that found 0x0054CC3C latched onto a monotonic-per-frame u32 in
memory, but that was the VBLANK counter, not the cs cursor. The
actual title-demo playback engine reads a DIFFERENT counter (probably
one it maintains internally, incrementing conditionally on gameplay
state).

## What the RE port arc actually needs

Per memory `[[soh3d-oot3d-title-not-play]]`: the 3DS title is a
lightweight scripted playback, not a Play state (Az's `gPlayState = 0`
throughout title). So the port target isn't SoH's `CutsceneContext`
byte stream — it's whatever data table drives the OoT3D title
playback engine.

### Next steps (concrete Ghidra attack plan)

1. **Find the title-demo playback engine's per-frame tick.** Watch
   distinct writer PCs to envCtx.unk_BF (env_base + 0xA5). Prior probe
   caught only Environment_Update at PC=0x0045e470. Extend the probe:
   pin cursor to sweeps across every ~50 frames, hitclear between, and
   collect the UNIQUE writer PCs across the full 3200-cursor demo.
   Any PC different from 0x0045e470 IS the title-cs SET_LIGHTING
   handler.

2. **Follow the caller chain.** From the SET_LIGHTING handler, walk
   upward (LR chain in watchhook's stack window) to find the
   playback-engine dispatcher. That's the fn that reads the true cursor
   counter and calls the per-command handlers.

3. **Locate the cs data table.** The playback engine reads a table of
   (frame, cmd, args) triples. `FindDataWriters` on the engine's
   base-ptr load reveals where the table lives. Its bytes ARE the
   title-cs data to port.

4. **Port the table into SoH.** Emit it as a `.inc` file consumed by a
   new `Zelda3D_TitlePlayback` module (Game-like structure per memory
   rule) that steps the same table on the SoH side, calling
   SohState_SetEnvSlot / envCtx pokes at the same cursor.

## No visual proofs until data-level 100%

Parity is measured at the DATA level: envCtx.unk_BF sequence, camera
basis frame-by-frame, actor positions frame-by-frame — all byte-
identical against Az. `compare firstdiv` / `compare lighting` / a
future `compare envctx_sequence` report the state. No PNG SxS until
data reports match.
