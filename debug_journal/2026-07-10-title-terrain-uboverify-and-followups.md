# 2026-07-10 — Terrain-lighting UBO verification + dormant palette-offset fix + sky-schedule
# root cause + star-brightness remeasure

Follow-up session to `2026-07-10-title-arc-closing-measurement.md` (residuals 2–8) and
`<oot3d-decomp>/docs/title_env_lighting.md` (commit 8e23c4b — static derivation that PROVED the
terrain lighting math and its CPU-side inputs correct and named the runtime UBO-fill test and a
measurement confound as the two remaining candidates). Four tasks, in the order that doc
prescribed.

## 1. Terrain darkness — the prescribed runtime test, run live

Booted headless (`ZELDA3D_WARP= ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh start`), free-ran the
title cs, and ran the exact `sgdump <modelId>` call `spot00_field_lighting_ground_truth.md`
specified, against the title's own terrain model (`geomscan all` → `model=1000
/scene/spot99_0_info.zsi`, 29 groups):

```
[SG_DUMP] model=1000 groups=29 ... worldLit=1 worldAmb=0.020 ambColor=(0.00,0.00,1.00) ...
[SG_DUMP]  g21 ... vtxLit=1 combScale=2.000 ... vColor0=(0.647,0.647,0.647,1.000) matAmb=(1.00,1.00,1.00) matDif=(0.00,0.00,0.00) ...
```

(`ambColor` in that header line is `gZelda3dWorldAmbColor`, the DORMANT worldshade tint path —
see Task 2 — not the live scene-lit ambient; the SG_DUMP facility doesn't print
`gZelda3dAmbient` itself, so it was cross-checked via the `lightparams` REPL command, which
prints exactly that CPU value and is read directly at UBO-fill time
(`zelda3d_sdl3gpu.cpp:1672-1675`: `ubo.uAmbient.xyz = gZelda3dAmbient * grp.matAmbient`, no
other transform in between):

```
lightparams: ambient=(0.192,0.263,0.435) light1col=(0.278,0.278,0.243) ...
```

This matches `spot00_field_lighting_ground_truth.md`'s independently-ROM-derived expectation
(`ambColor≈(0.20,0.26,0.43)`) to within frame-to-frame schedule noise, and every group's
`vtxLit/matAmb/matDif/combScale` matches `title_env_lighting.md` §3/§5's formula exactly
(`matDif=(0,0,0)` — the N·L term is provably a no-op for these materials, `combScale=2.0` is
applied). **Candidate 1 (UBO-fill defect) is RULED OUT** — the value that should reach the
shader, per the decomp's formula, does reach it, byte-for-byte close enough that the residual
noise is schedule interpolation, not a fill bug.

### Measurement confound — re-checked on a pixel-aligned pair

The doc flagged that one sweep pair (az=700/cs438) has a camera-framing mismatch despite being
cs-frame-exact, and asked for the darkness ratio to be re-measured only on a pair independently
confirmed pixel-aligned. Re-ran `tools/title_ab.py ab 500 --soh 908` (the closing-measurement
doc's own residual 5 already confirms az=500/550 do NOT show the az=700 framing divergence —
mountain/tree/castle-wall silhouettes align) against the CURRENT build:

```
(0, 0, 100, 80): az=(36,60,23) soh=(18,28,12) d=(+18,+32,+11)      R2.0 G2.1 B1.9
(100, 0, 200, 80): az=(38,63,24) soh=(18,28,11) d=(+20,+35,+13)    R2.1 G2.3 B2.2
(200, 0, 300, 80): az=(42,66,30) soh=(20,31,16) d=(+21,+35,+14)    R2.1 G2.1 B1.9
... (all 12 regions in scratch/title_ab/reverify500 — 4x3 grid, ratio 1.9-2.3x uniformly)
```

Byte-identical to the prior session's `final_sweep.txt` az=500 row — reproducible, not frame
noise. **The ratio SURVIVES pixel-aligned measurement.**

### Verdict — outcome (b): honest negative

Every derivable-by-inspection link in the terrain-lighting chain (ROM material bytes, ROM
vertex-color bytes, the shader's arithmetic, the ambient CPU value's byte layout AND schedule
blend, the UBO-fill call, and now the LIVE runtime UBO value itself) checks out against ground
truth. The ~1.9–2.3x residual is **not explained by anything in the currently-identified
pipeline**. Per the stop-micro-tuning-lighting directive, this is NOT being chased with a fitted
constant. Marked as an honest open residual in the closing-measurement journal (see there for
the updated residual-2 entry) — the next investigation step, if picked up, would need to look
OUTSIDE the shading-math pipeline entirely (e.g. Az's own capture/display gamma path, which is
outside SoH3D's renderer and outside this task's scope to touch).

## 2. Dormant palette-offset bug — fixed

`Zelda3D_TitleLightSlotsConvert` (`Shipwright/soh/src/zelda3d/zelda3d.c:210-230`) read the
on-disk cmd-0x0F palette entries with offsets that put color BEFORE direction (swapped,
off-by-one from the correct layout). `title_env_lighting.md` §6 independently re-derived the
correct on-disk/runtime layout from a fresh full decompile of `Environment_Update`
(`FUN_0045dd30`) and from raw ROM bytes: direction BEFORE color, matching N64's
`EnvLightSettings` field order exactly (`ambient@0x00, l0dir@0x03, l0col@0x06, l1dir@0x09,
l1col@0x0c` for the on-disk 3-byte-group layout this function consumes). Fixed to match:

```c
for (int j = 0; j < 3; j++) {
    o->amb[j]   = e[0x00 + j];
    o->l0dir[j] = (signed char)e[0x03 + j];
    o->l0col[j] = e[0x06 + j];
    o->l1dir[j] = (signed char)e[0x09 + j];
    o->l1col[j] = e[0x0c + j];
}
```

Still dormant (`gZelda3dWorldShade` defaults off, only reachable via the `worldshade` REPL
command — `zelda3d.c:40`) — zero visible effect today, same as when the bug was found. Landed
per the doc's "fix it now, before the next session wires worldshade into a live path" framing,
not because it moves any current residual.

## 3. Sky R/G — root cause confirmed AND fixed (contained to title code)

`title_env_lighting.md` §8 attributed the frozen R/G to `skybox1Index==skybox2Index` collapsing
the cross-fade guard in `Zelda3D_TryDrawSky`'s `doBlend` condition
(`idx2 != play->envCtx.skybox1Index`), reconfirming `2026-07-08-title-divergence-remeasure.md`
Verdict 3's live measurement (`idx1==idx2==3` at every sampled title frame while Az's kept
warming). Traced the actual N64-side mechanism this session: `Environment_UpdateSkybox`
(`z_kankyo.c:613+`) selects `skybox1Index`/`skybox2Index` from a schedule table
(`D_8011FC1C[envCtx->unk_17][i]`) keyed by `gSaveContext.skyboxTime` — a **separate** field
from `gSaveContext.dayTime`. `skyboxTime` is normally kept in sync with `dayTime` by a guard in
`Environment_Update` (`z_kankyo.c:939-944`):

```c
if (((sceneSetupIndex >= 5 || gTimeIncrement != 0) && dayTime > skyboxTime) ||
    (dayTime < 0xAAB || gTimeIncrement < 0)) {
    skyboxTime = dayTime;
}
```

`TitlePresentation::step` writes `gSaveContext.dayTime = csTime` directly every frame (the
title cs owns time, bypassing the normal clock), but `gTimeIncrement` is 0 and
`sceneSetupIndex` isn't ≥5 during the title — so this guard's first branch never fires, and the
second branch (`dayTime < 0xAAB`) only covers a narrow near-midnight window. Result:
`skyboxTime` sticks at its scene-load value for most of the title's runtime, so
`Environment_UpdateSkybox` keeps re-selecting the SAME schedule row every frame —
`skybox1Index==skybox2Index`, exactly what was measured.

**Fix** (`Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.cpp`): write
`gSaveContext.skyboxTime = csTime` in lockstep with `dayTime`, the identical pattern the engine
already uses at every other dayTime-jump site (`z_scene.c:370/390` on scene load, `z_demo.c:496`
for scripted cutscenes) — not a new mechanism, just applying the existing one at the title's own
discontinuity point. Contained to `TitlePresentation`; the general gameplay skybox path's own
`skyboxTime` writes are untouched.

### Verification (live, headless, rebuilt binary)

```
titlecs frame=154  sky info: idx1=3 idx2=0 blend=90   active=1
titlecs frame=186  sky info: idx1=3 idx2=0 blend=108
titlecs frame=218  sky info: idx1=3 idx2=0 blend=126
titlecs frame=250  sky info: idx1=3 idx2=0 blend=144
titlecs frame=282  sky info: idx1=3 idx2=0 blend=162
titlecs frame=314  sky info: idx1=3 idx2=0 blend=11    # row transition, blend restarts
titlecs frame=346  sky info: idx1=3 idx2=0 blend=29
titlecs frame=378  sky info: idx1=3 idx2=0 blend=47
```

`idx1=3, idx2=0` — genuinely different dome variants (previously `idx1==idx2` always) — and
`blend` climbs and cycles through real schedule-row transitions instead of sitting frozen. This
is the mechanism that drives the R/G cross-fade; residual 3 (dawn warmth lag) in the
closing-measurement journal is marked FIXED pending a fresh A/B remeasure (not re-run this
session — the harness's embedded SoH predates this fix, same staleness trap the prior session
hit; a harness rebuild is the natural follow-up before claiming a numeric before/after on this
one).

## 4. Star brightness — remeasured on a fresh build

The 2026-07-08 L8-decode fix (`pica_texture.cpp`: L8/L4 decode as `{L,L,L,0xFF}`) is confirmed
present in source and in this session's freshly-built binary (the closing-measurement sweep's
"unchanged" note was the stale prebuilt-harness artifact that same session already flagged
elsewhere). Re-measured peak star luminance (moon bounding-box masked out of the sky region,
`y∈[0,150) x∈[0,300)`) at two independently-matched pairs using the CURRENT `soh.elf`:

| pair | Az peak | SoH peak | ratio |
|---|---|---|---|
| az=200/soh=608 | 153.7 | 112.0 | 0.73 |
| az=360/soh=768 | 156.7 | 125.3 | 0.80 |

Up from the pre-fix ~70/140 (ratio ~0.5) this arc's earlier docs measured. **Improved, not
fully closed** — SoH's brightest stars still run 20–27% below the oracle's. No further tuning
applied (constants here would be exactly the kind of fitted magic-offset the no-bandaids rule
bans); marked open with numbers.

## Build

`Shipwright/build-cmake` rebuilt (`cmake --build . -j4 --target soh`) after the zelda3d.c and
title_presentation.cpp edits — clean build, both diagnostics above are against that binary.

## Files touched

- `Shipwright/soh/src/zelda3d/zelda3d.c` — `Zelda3D_TitleLightSlotsConvert` offset fix (Task 2).
- `Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.cpp` — `skyboxTime` sync fix
  (Task 3).
- `debug_journal/2026-07-10-title-arc-closing-measurement.md` — residuals 2/3/8 updated with
  this session's verdicts.

## Cross-references

- `<oot3d-decomp>/docs/title_env_lighting.md` (commit 8e23c4b) — the static derivation this
  session's runtime test closes the loop on.
- `debug_journal/2026-07-10-title-arc-closing-measurement.md` — residual numbering/definitions.
- `debug_journal/2026-07-08-title-divergence-remeasure.md` — Verdict 2 (star deficit numbers),
  Verdict 3 (idx1==idx2 smoking gun).
- `debug_journal/2026-07-08-title-star-brightness-L8-decode.md` — the L8 decode fix this
  session reconfirmed live and remeasured.
