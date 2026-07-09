# Title demo now renders `spot99`, not `spot00` (2026-07-09)

Ground truth: `<oot3d-decomp>/docs/title_scene_spot99.md` (RE'd 2026-07-08). Fixes the largest
open title-screen parity gap flagged in that doc's §7 and in
`debug_journal/HANDOFF-2026-07-09-title-logo-phase.md` item (a).

## What was wrong

SoH3D's title module gated on `play->sceneNum == SCENE_HYRULE_FIELD` and then let the existing
sceneNum -> OoT3D-folder-name table (`kZelda3dSceneNames[0x51] == "spot00"`) resolve which OoT3D
scene asset to render. That's correct for normal gameplay (SCENE_HYRULE_FIELD *is* spot00), but
the oracle's title demo does NOT load spot00 at all — it loads `spot99`, Grezzo's own dedicated
title-demo scene: same actor spawn table and env-light palette as spot00 (byte-identical, RE'd),
but a drastically decimated, camera-path-only room mesh/collision (~13% the collision poly count,
~79% the room-mesh size, smaller XZ footprint). SoH was rendering the wrong (larger, full-field)
asset — a substitution bug, not a stand-in.

## Why it's a one-seam fix, not an asset-import job

`Zelda3D_RoomModelId`/`Zelda3D_LoadSceneCollisionRaw` (`zelda3d_model.cpp`) are a **live romfs
loader**: given a folder-name string, they open `/scene/<name>_<R>_info.zsi` straight out of
`ZELDA3D_OOT3D_ROM` at runtime (via the existing 3DS-romfs reader) and build the room CMB / raw
collision blob lazily on first use. There is no separate build-time "scene import" step to
re-run — the entire "port" is telling the existing pipeline to ask for `"spot99"` instead of
`"spot00"` while title is active. Every consumer of the scene name (room draw, terrain-warp
collision synthesis in `z_scene.c`'s `Scene_CommandCollisionHeader`, cam-lift ground query, the
`meshfloor` REPL command) already goes through one function, `Zelda3D_SceneName(PlayState*)` in
`zelda3d.c` — so the fix is a single hook there.

## Change

- `Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.h`/`.cpp`: new C bridge
  `Zelda3D_Title_SceneName()` — returns the string literal `"spot99"` while
  `TitlePresentation::isActive()`, else `NULL`. The policy constant (the scene name) lives in the
  title module, not in `zelda3d.c`, per the project's per-behavior-module rule.
- `Shipwright/soh/src/zelda3d/zelda3d.c`'s `Zelda3D_SceneName()`: checks
  `Zelda3D_Title_SceneName()` first; falls through to the existing `kZelda3dSceneNames[sceneNum]`
  table when title isn't active (unchanged behavior for every other scene).

## Why gameplay collision correctly stays on spot00 (not a bug, verified deliberate)

`Scene_CommandCollisionHeader` (`z_scene.c`) calls `Zelda3D_BuildSceneCollision` once, during
scene-command processing at `Play_Init` time — **before** the title module's `update()` has ever
run for the new scene, so `Zelda3D_Title_IsActive()` is still false at that call site and
`Zelda3D_SceneName` resolves the ordinary N64-scene table (spot00) for `BgCheck` collision. Only
the per-frame room-mesh *draw* calls (which run after `update()` has flipped `mActive` true) see
`"spot99"`. This means the title demo's `BgCheck` gameplay collision stays on spot00's full-field
geometry (matching the byte-identical actor spawn table, which spans the whole field, not just
spot99's small sub-region) while the *rendered* mesh is spot99's — exactly the split the decomp
doc implies is correct (spot99's collision only needs to cover "just enough ground for the title
cs's camera flyover + rider path"; SoH's rider is driven directly from cs cues, not from `BgCheck`
floor queries, so it never needs spot99's own restricted collision). No further work needed here.

## Verification

- Build: clean (`ninja soh.elf` / `cmake --build . --target soh.elf -j4`, only
  `title_presentation.cpp` + `zelda3d.c` recompiled).
- Boot (`ZELDA3D_HEADLESS=1 ZELDA3D_WARP= tools/zelda3d_game.sh start`): run.log shows
  `[Zelda3D] loaded scene-room model 1000 (/scene/spot99_0_info.zsi): 29 groups, 30 textures`
  (previously this line read `spot00_0_info.zsi`). The one-time gameplay-collision log line
  still reads `/scene/spot00_info.zsi: 2243 verts, 3753 polys` — confirms the deliberate split
  above.
- Visual: live screenshots at multiple title-cs frames (`scratch/screenshots/spot99_a.png`,
  `spot99_b.png`, `spot99_seq_1.png`, `spot99_seq_3.png`, `spot99_rider_natural2.png` — all
  gitignored, not committed) show the distinctive spot99 rocky-hill/castle-rampart silhouette
  that also appears in the oracle's own frames (see `scratch/title_ab/spot99_c2_sxs.png`'s Az
  panel — same hill shape, same castle towers + moon in a later frame) — previously SoH's title
  showed flat, generic full-Hyrule-Field terrain with no such landmark, because it was rendering
  spot00 at the wrong (full-field) scale/region. Logo fade/gating (landed previous session,
  64ba86f0) keeps working unmodified on top of the new scene.
- Oracle A/B (`tools/title_ab.py calibrate`): content-match scores in the 0.23-0.37 range at the
  az=600/900 windows tried — LOW, but this is the pre-existing, already-documented title-cs
  **cursor-phase-sync** gap (`debug_journal/2026-07-08-title-daytime-schedule-re.md` /
  handoff item (f): SoH's cs cursor runs at a different wall-clock phase than Az's, so the
  content-correlation search lands on frames with a real lighting/pose offset even when the
  underlying scene geometry is correct — the search itself is contaminated by that unrelated,
  known gap, not by this change. The `spot99_c2_sxs.png` side-by-side (matched by the tool's
  best-scoring pair, az=900/soh=631) is a mismatched close-up-vs-far-shot pair for that reason,
  but the Az panel's hill silhouette visibly matches what SoH itself renders elsewhere in its own
  clock (`spot99_seq_1.png`) — i.e. the SAME landmark geometry exists on both sides, just not at
  the same clock instant in this particular auto-matched pair. Fixing the cursor-phase-sync issue
  is out of scope here (tracked separately, handoff item (f)) and would make a future re-run of
  this A/B score cleanly; re-run `title_ab.py calibrate` after that lands to get a real
  apples-to-apples number.

## Follow-up (not done here, out of scope for this port)

- Cursor-phase-sync (handoff item f) — would let a future oracle A/B actually reflect this fix's
  visual improvement in the correlation score, not just in eyeballed screenshots.
- The 3 swapped OoT3D room-object-bank slots spot99 vs spot00 (decomp doc §6, unresolved ids
  180/32324/11) — unidentified assets those slots pull in; not visibly missing in the screenshots
  taken here, but not independently confirmed either.
- decomp doc item 4 (§7): which of spot99's 7 alt-headers OoT3D boots into isn't RE'd; this port
  uses room 0's default header (index 0) since `Zelda3D_RoomModelId`/`Zelda3D_LoadSceneCollisionRaw`
  don't do alt-header selection at all (same as the existing spot00 path) — if a future divergence
  traces to a non-default alt-header (e.g. a specific lighting/prop variant), that's the next
  place to look.
