# OoT3D RAM reverse-engineering journal (3DS oracle, #89)

Documents the OoT3D (3DS) emulated-RAM layout we reverse-engineer to drive the Azahar oracle:
warp-to-any-scene injection + per-actor variant/animation dumps for matching SoH3D to the 3DS game.
OoT3D has **no symbols**, so addresses here are derived empirically (RAM scanning + OoT-N64 layout
similarity). Tool: the embedded harness (`tools/harness_cli.py` — `mem`/`r`/`w`/`snapshot`/`input`/`warp`);
see `oot3d-decomp/docs/oracle.md`. **Update this as facts are found; mark guesses.**

## Address space (3DS virtual, per the running OoT3D process)
- `0x00100000` — `.text` (ARM code; BL-opcode stream confirmed).
- `0x08000000` — heap.
- (3DS regions per Azahar memory.h: PROCESS_IMAGE, HEAP, LINEAR_HEAP, N3DS_EXTRA_RAM.)

## Methodology
- **Find a struct by known values:** read SoH3D's value for the same thing (e.g. an actor world pos
  via `asel`/`ainfo`), then scan OoT3D RAM for those float triples to locate the actor; the actor
  list head is reachable from there. (Floats are exact across the port for shared geometry/spawns.)
- **OoT-N64 parallels:** OoT3D ports the OoT actor system — Actor struct (id s16, params, world pos
  Vec3f), SkelAnime (animation ptr, curFrame f32, jointTable ptr), global ctx with per-category actor
  lists, gSaveContext with nextEntranceIndex. Layouts differ (32-bit ARM, 3DS-specific fields) but the
  shapes guide the search.

## Findings (CONFIRMED / GUESS)
_(none yet — RE in progress)_

### Entrance / scene load (for warp injection)
- TODO: locate the next-entrance index + the load trigger (OoT N64: gSaveContext.nextEntranceIndex +
  a transition flag). Plan: warp by writing it via RPC WriteMemory, then verify with a screenshot.

### Actor list + Actor struct
- TODO: find the global actor context / per-category actor list heads; Actor field offsets (id,
  params, world.pos, next-ptr); SkelAnime offset within animated actors (animation OTR/CSAB ref,
  curFrame).

### En_Ko (Kokiri kids) variant + animation (the #87 driver)
- TODO: per-instance type/variant + the CSAB/model it plays, read live in Kokiri Forest. This is the
  ground truth that replaces the N64-derived guess that regressed #87.
