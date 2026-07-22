# Making the 3DS oracle warp like SoH (2026-07-22)

## Symptom

`tools/oracle_shot.py` / `link_sweep.OracleSession.boot()` reported success, `warp 0xEE`
replied `ok`, and every captured "oracle screenshot" was OoT3D's **title screen** (a plain
sky gradient). This read as "the embedded harness cannot render" or "the harness has no
warp". Both readings are wrong.

## Root cause — two compounding defects, neither of them the warp

**1. `playstate` is not a gameplay test, but every driver used it as one.**

`CurrentPlayState()` (tools/soh3d_harness/main.cpp) reads `gPlayState @ 0x0050AF34` and,
when that is 0, deliberately falls back to `TITLE_PLAYSTATE_PTR_VA = 0x00539F98` — the
title demo's live PlayState. That fallback is *correct and wanted*: it makes
scene/actor/camera introspection work at the title. It also makes `playstate` answer `ok`
on the title screen, so `harness_ctl.poll_playstate()` and `OracleSession.boot()`'s tap
schedule both concluded "we're in game" while sitting on the title.

**2. OoT3D's warp cannot work from the title, and said `ok` anyway.**

The warp mechanism is already RE'd (`oot3d-decomp/docs/ram_map.md`) and the harness
already implements exactly it — the same pair SoH writes:

    nextEntranceIndex  s16 @ play+0x5C32
    transitionTrigger  s8  @ play+0x5C2D = 20 (TRANS_TRIGGER_START)

Nothing was missing. But `HandleWarp` resolved its target through `CurrentPlayState()`, so
at the title it wrote those fields into the **title** PlayState and printed `ok`. No save
file is loaded there, so the transition driver has nothing to spawn into and the write is
inert. A silent no-op that reports success is what turned this into a multi-session
mystery.

A third, smaller trap: `OracleSession.boot()` tapped with `hold=30/release=60`, which does
not advance OoT3D's title/file-select at all. Short rapid taps (`hold=4, release=8`) do —
verified, `az_linkanim` then returns a live Player at `0x098f4010`.

## Fix

*Harness* (`tools/soh3d_harness/main.cpp`):

- `GameplayPlayState()` — gPlayState populated **and** `!TitleActive()`. The honest
  discriminator.
- `playstate` now prints `ok 0x<ptr> mode=play|title`.
- new `gameplay` command → `ok yes|no`. This is what drivers gate on.
- `warp` and `force warp` resolve through `GameplayPlayState()` and **fail loudly** off the
  title instead of reporting a no-op success.

*Drivers*:

- `harness_ctl.in_gameplay(h)` replaces `poll_playstate` as a readiness check
  (`poll_playstate` stays, documented as pointer-only).
- `harness_ctl.boot_to_gameplay(h, entrance=…)` — the single SoH-equivalent entry point.
  Loads `scratch/gameplay_settled.state` if present (**no input driving at all**), else
  drives the title once with the short-tap sequence and saves that state so the cold path
  runs at most once per machine. Then warps and verifies it stayed in gameplay.
- `link_sweep.OracleSession.boot()` and `oracle_shot.py` both delegate to it.

## The generalisable lesson

"Warping like SoH" was never a missing mechanism — it was a **missing precondition** (a
loaded save) plus a **readiness check that could not fail**. When a poke-based command
reports success and nothing happens, suspect the target resolution before the poke.

## Build note

`Azahar/build-libretro` is dead: its CMake cache has the pre-rename absolute source path
(`/home/bhamil/repo/soh3d`) baked in and can no longer regenerate. The harness build dir is
now `Azahar/build-harness`, configured with:

    cmake -S Azahar -B Azahar/build-harness -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PROJECT_citra_INCLUDE=<repo>/tools/soh3d_harness/wire_in.cmake \
      -DENABLE_LIBRETRO=ON -DENABLE_QT=OFF -DENABLE_SDL2=OFF -DENABLE_CUBEB=OFF \
      -DENABLE_OPENAL=OFF -DENABLE_TESTS=OFF -DENABLE_VULKAN=ON -DENABLE_OPENGL=ON \
      -DENABLE_SOFTWARE_RENDERER=ON -DENABLE_LTO=OFF -DUSE_SYSTEM_GLSLANG=ON \
      -DENABLE_BUILTIN_KEYBLOB=ON

`harness_ctl.HARNESS_BIN` searches `build-harness` first, `build-libretro` second.
