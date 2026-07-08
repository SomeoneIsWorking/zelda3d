# 2026-07-08 — Title terrain darkness (#1 audit finding): model+input cleared, plumbing gap isolated

Follow-up to `2026-07-08-title-parity-audit-ranked.md` item #1 (spot99/spot00 terrain ~3x too
dark & desaturated vs Azahar, both content-matched pairs). Full ground-truth derivation now
lives in the decomp repo: `oot3d-decomp/docs/spot00_field_lighting_ground_truth.md` — read
that first, this is the SoH3D-side half (what's ruled in/out on our side + the fix spec).

## What's PROVEN correct (do not re-litigate these)

1. **The terrain CMB drawn at the title is spot00's** (real Hyrule Field room CMB, from
   `/scene/spot00_0_info.zsi`), not spot99 — spot99 only supplies the title cs/camera/light
   schedule (`Zelda3D_SceneName` maps N64 `sceneNum` -> OoT3D folder name; title runs with
   `play->sceneNum == SCENE_HYRULE_FIELD` -> `"spot00"`, per `zelda3d_scene_names.inc:88`).
2. **ROM ground truth for spot00's ground/hill/grass materials** (34 of 38 mats): vertex-lit,
   matAmbient=WHITE, **matDiffuse=BLACK**, combiner stage0 = MODULATE scale **2.0**. So the
   real OoT3D formula is `saturate(2 * texel * vColor * sceneAmbient/255)` — no NdotL/diffuse
   term needed (matDiffuse black makes it a provable no-op), exactly like the earlier-solved
   Kokiri case.
3. **SoH3D's shipped shader already implements exactly that formula** for scene/room draws
   (`zelda3d_sdl3gpu.cpp` `kFrag`, `sceneLitPath` branch: `rgb=tex*vColor; rgb*=uAmbient.xyz;
   rgb=clamp(rgb,0,1)*uExtra.w(combScale)`), gated by `grp.vertexLighting && gZelda3dWorldLit`
   (`gZelda3dWorldLit` defaults **on**, only ever changed by REPL — confirmed no code path
   zeroes it for title).
4. **The CPU-side ambient feeding the shader is provably correct at the audited frame.**
   `gZelda3dAmbient` (which becomes `uAmbient.xyz`) is fed every frame straight from
   `play->envCtx.lightSettings.ambientColor/255` (`Zelda3D_UpdateLight`), and THAT value is
   the ported, ±1-verified title ambient (`Zelda3D_TitleLightSettingsOverride`, see
   `2026-07-07-title-lighting-solved.md`) — `envCtx->adjAmbientColor` (an N64-only additive
   adjustment, written into the SEPARATE `lightCtx.ambientColor`, not `lightSettings`) cannot
   be the culprit because `Zelda3D_UpdateLight` reads `lightSettings` directly, upstream of
   that adjustment.
5. **Room draws pass `lit=0` unconditionally** (`Zelda3D_DrawRoomGL` never sets the sign bit
   on `modelId`), so they always take the scene-geometry shader branch, never the
   character/half-Lambert one — ruled out a lit-flag misroute.
6. **The CPU flat-tint / `worldshade` (ka/kd/ke) path is provably NOT what's rendering the
   terrain** — it's default-off, and even when the vertex-lit `sceneLitPath` IS active the
   shader explicitly skips `uTintSkin`/`shade` (2026-07-04 fix, comment in `kFrag`). The
   audit's own line "`ka*ambient` alone" framing was analyzing the WRONG code path — the CPU
   worldshade tint isn't in the live path for this material class at all.

Net: **the shading MODEL and the ambient INPUT are both individually correct.** A model bug
or a stale/wrong ambient value are both ruled out by the above. This is a real, narrowed
result — it means whoever picks this up next should NOT re-derive the lighting formula or
re-verify the ambient port; both are done.

## What's NOT yet proven (needs one live REPL call, not more static tracing)

The residual ~0.3x (R/G/B all ~0.29-0.35x of Azahar, consistent across 2 sample points, 2
content-matched frames — audit's own numbers) must be one of:

1. `uAmbient.xyz` not actually reaching the GPU as `sceneAmbient/255` for this specific draw
   (a UBO-fill/upload bug, e.g. wrong group / stale buffer / off-by-one binding) — the
   ambient CPU value is right, but "right on the CPU" and "right in the shader" are two
   different claims until measured.
2. `texel * vColor` (the baked asset ingredients) decoding measurably dark for spot00's field
   ground specifically, vs. the ROM's real PICA200-decoded values — a vertex-color
   datatype/scale corner case in `Cmb::readAttr` not exercised by the previously-verified
   Kokiri asset.
3. The sampled screen pixels (200,150)/(200,220) actually resolve to material 17 (fully
   diffuse-lit, matDiffuse=WHITE) or 33 (unlit) rather than the "typical" ground-material
   class — those two would need separate handling and the audit didn't identify which
   material index the sampled pixels hit.

## Fix spec / next action (concrete, no offline tuning)

Do **not** touch any Ka/Kd/Ke/tint constant — none of them are in the live code path for this
material class, so "tuning" them would be a no-op bandaid on the wrong lever.

1. Build the game (headless: `ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh`, verify via
   `/proc/<pid>/environ` per the corrected env-var rule), warp to the title, hold the
   content-matched frame (reuse `scratch/title_settled.state` / raw `step 360` from the
   audit), and find the spot00 field's `modelId` (REPL `roominfo`/existing model-id dump).
2. `sgdump <modelId>` (existing facility, `zelda3d_sdl3gpu.cpp` ~L1480) at that exact frame.
   Compare against `oot3d-decomp/docs/spot00_field_lighting_ground_truth.md`:
   - `ambColor` should print `(0.20,0.26,0.43)`-ish (matches `soh_env` ambient/255). If it
     doesn't, the bug is the UBO/upload path (candidate 1) — fix where `base.uAmbient`/
     `ubo.uAmbient` get filled/bound for this draw.
   - Per-group `matAmb`/`matDif`/`combScale`/`vtxLit` for the group covering the sampled
     pixels should read `(1,1,1)`/`(0,0,0)`/`2.0`/`1` (candidate 3 ruled in/out directly).
   - `vColor0` (dumped first baked vertex color) sanity-checked against a fresh ROM byte read
     at the same vertex index (candidate 2) — extend `mat_dump.py`/write a sibling script to
     pull the actual per-vertex `color` VATR bytes for the hit sepd if `vColor0` looks off.
3. Whichever candidate lights up, the fix is in that exact spot (UBO fill code, `readAttr`
   scale handling, or nothing — if all three check out, the sampled pixels are hitting a
   DIFFERENT model/group than assumed and the audit's screen-space sample needs re-mapping to
   the actual draw, which is itself the finding).

Tools added this session: `scratch/title_lighting/mat_dump.py` (ROM-direct CMB material
lighting-field dump, mirrors `cmb.cpp`'s `parseMats()` byte-for-byte — reusable for any scene,
not just spot00).
