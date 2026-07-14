# 2026-07-14 — PICA fog LUT-fill task: already fully decompiled + ported (2026-07-10); no new work needed

Task brief (this session): derive the 128-entry PICA fog LUT curve/encoding via Ghidra, port it
faithfully into the renderer, close-test at dawn instants, journal + commit in both repos. The
brief's premise ("currently only approximated in the SoH3D port") is **stale** — it was already
done, completely, in a prior session. This entry records the confirmation so the false "still
open" pointer (`2026-07-14-title-terrain-field-grass-mure2.md` item 3, now corrected in place)
doesn't cause a third session to redo this work.

## What was already done (2026-07-10, prior session)

- **Decomp**: `oot3d-decomp/docs/title_env_lighting.md` §13 fully documents the LUT-fill driver
  chain — `FUN_002cdbfc` (the fill, mode-0 linear: `eyeDist(t) = b/(a-t)` via the inverse
  projection matrix, then `factor(d) = clamp((far-d)/(far-near))`), `FUN_0047fd24`
  (FogResUpdater.cpp per-frame bridge, literal Grezzo source path in the alloc call), and the GX
  flusher's 1.1.11-fixed pack to `GPUREG_FOG_LUT_DATA`. Predict-gate passed float-exact
  (`max|err| <= 9.7e-8`) against live LUT dumps at 3 dayTimes. §13.4 names the load-bearing
  subtlety: the fog "look" comes from the 128-node LERP structure (entry 127 alone spans eye
  873..32000), not the smooth closed-form curve — a port that evaluates `factor(d)` directly
  would miss the entire visible haze.
- **Port**: commit `19081f9a` ("title: port the 3DS PICA distance fog — the dawn-hue root
  cause"). Current code (unchanged since that commit, checked this session):
  - `Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp:291-296` `fog3dNode(t)` — the exact
    closed-form node value, `uFog3d0 = (a, b, fogNear, fogFar)`.
  - Same file, lines 386-392 — the 128-entry index/LERP: `x = clamp(vFogDist,0,1)*128`,
    `i0 = min(floor(x),127)`, `factor = clamp(lerp(fog3dNode(i0/128), fog3dNode((i0+1)/128),
    x-i0), 0, 1)`, `rgb = mix(fogColor, rgb, factor)` — byte-for-byte the §13.4 structure, not
    an approximation.
  - `Shipwright/libultraship/src/fast/zelda3d_gl.cpp:349-389` `Zelda3D_Fog3dSet` — fills
    `gZelda3dFog3d = {a, b, fogNear, fogFar, fwd.xyz, dot(fwd,eye)}` from
    `(camNear=7, zFar=fogEnd, fogNear, fogFar)`, matching §13.2's live-verified inputs
    (near=blended palette fogNear in eye units directly, far=blended drawDist, zNear=7.0 exact,
    zFar=fogEnd=32000).
  - `Shipwright/soh/src/zelda3d/behaviors/title/title_presentation.cpp:405` still calls
    `Zelda3D_Fog3dSet(kTitleCamNear3ds, fogEnd, fogNear, fogFar, eye, fwd)` every frame; line 98
    calls `Zelda3D_Fog3dOff()` on exit. Wiring intact.
- **Close-test numbers already in `2026-07-10-title-3ds-fog-port.md`**: fog OFF→ON at
  az=1000/soh=1405 (dawn target) — rock |d| 5.1→1.4, skyNearMtn |d| 5.8→3.4, whole-frame mean|d|
  21.68→21.30. 10-point sweep: improves at 7/10 points, holds at 2, mean 5.54→5.23. `lus_tests`:
  438 PASSED.

## This session's verification (no rebuild — no code changed)

- `git log --oneline -- Shipwright/libultraship/src/fast/{zelda3d_gl,zelda3d_sdl3gpu}.cpp | grep
  fog` shows exactly one fog-touching commit (`19081f9a`), confirming nothing has modified this
  path since the 2026-07-10 port — the sweep numbers above are still the live, current state, not
  a stale snapshot. Since the source is byte-identical to what was already close-tested and no
  change is being made, a fresh rebuild+sweep would only reproduce those same numbers; skipped to
  respect the "one build at a time, real work only" discipline rather than burn a serial -j4
  rebuild for a no-op reconfirmation.
- Read `oot3d-decomp/docs/title_env_lighting.md` in full around §§9-13 to confirm the LUT-fill
  documentation is genuinely complete (driver chain, formula, live-verified inputs, predict-gate,
  the entry-127-LERP subtlety, port anchors) — it is; no gaps requiring further Ghidra work for
  this specific question.

## What remains genuinely open (not this task's target, not touched)

- `title_env_lighting.md` §10/§11: the oracle terrain ~1.9x-brighter-than-formula residual (the
  PICA vertex shader's per-enabled-light ambient SUMMATION, `CmbVShader.shbin` disassembled,
  mechanism named but the exact enabled-light count/duplication is runtime state, not yet
  dynamically confirmed). This is a distinct, larger residual axis from fog and was correctly
  flagged as still-open by both `title_env_lighting.md` and the 2026-07-14 terrain journal.
- Fireglow wordmark wash and rider framing at cs1093 (per the 2026-07-14 terrain journal) —
  unrelated to fog.

## Conclusion

No RE, no port, no commit needed for the fog-LUT task as briefed — it was correctly and
completely done four days prior. The only actionable output of this session is the correction to
the stale "blocked on LUT-fill decompile" line in
`2026-07-14-title-terrain-field-grass-mure2.md`, so a future session doesn't re-open this.
