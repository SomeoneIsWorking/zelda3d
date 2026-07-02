# 2026-07-02 — Market Day parity sweep, first finding

## Sweep tool
`ZELDA3D_HEADLESS=1 python3 tools/parity_ab.py 0xB1 --time 0x6000 --name market`
Composite: `scratch/screenshots/ab_market_cmp.png`

## Findings (by signal strength)

### 1. BLACK-VOID sky in SoH3D (highest signal)
- SoH3D above the Market Day rooftops: pure black (RGB 0,0,0).
- OoT3D oracle at the same scene: full nightsky with stars + gradient.
- NOT the deleted "SKYBUG unresolved segment 8" (that was a stale-texel warning that
  paints garbage, not a black void). This is the sky draw itself missing / culled.
- Reproduce: launch Market Day (ent 0xB1) headless, look up from the fountain.
- Root cause: **FOUND** — `Zelda3D_TryDrawSky` early-outs when `skyboxId != SKYBOX_NORMAL_SKY`,
  and z_play.c's outer switch (line 1557) only calls it for `NORMAL_SKY | CUTSCENE_MAP`. Market
  Day uses `SKYBOX_MARKET_CHILD_DAY` (=9); Market Night uses `SKYBOX_MARKET_CHILD_NIGHT` (=0xA);
  MARKET_ADULT (=4) / OVERCAST_SUNSET (=3) similar. All fall through to the `unk_140`-gated N64
  bg-image path — which `Zelda3D_ShouldSuppressBgImageSkybox` also blocks (fullscreen backdrop
  would paint OVER the OoT3D geometry — verified 2026-07-02: unsuppression produces low-res
  N64 backdrop covering the whole frame). Net: black void.
- Fix: **LANDED** (2026-07-02) — Zelda3D_SkyBoxToTenkyuIndex maps the four outdoor non-NORMAL
  ids to BlueSky.zar dome variants (MARKET_CHILD_DAY→fine_tenkyu_1, MARKET_CHILD_NIGHT→
  fine_tenkyu_3, MARKET_ADULT→cloud_tenkyu_3, OVERCAST_SUNSET→fine_tenkyu_2). Extended
  Zelda3D_SkyActive/ActiveSkyIndex + threaded activeIdx through Zelda3D_TryDrawSky. Added an
  `else if (Zelda3D_TryDrawSky(play))` at z_play.c:1565 so the mapped skyboxes reach the dome
  path. Verified: Market Day renders with a clear blue OoT3D sky over buildings; Kokiri Forest
  (NORMAL_SKY / UNSET_1D) unchanged. See scratch/screenshots/market_sky_dome4.png +
  kokiri_sky_regress2.png.
- **Residual**: this uses stock BlueSky variants, not the OoT3D per-scene VR backdrop the 3DS
  remake actually ships (Market Adult should be a dedicated desolate-sky asset, not cloud-night).
  Full parity needs the OoT3D scene-specific vrbox asset — follow-up when the OoT3D romfs
  extraction lands (no docs/tool for it yet).

### 2. Scene-time divergence at 0x6000
- SoH3D at time=0x6000 loaded **SCENE_MARKET_DAY** (0x20) — sunlit.
- OoT3D oracle at the same entrance loaded **SCENE_MARKET_NIGHT** — moonlit, "Market"
  placard visible.
- Same entrance index (0xB1), same dayTime (0x6000), different scene selection.
- Root cause: **CLOSED** (2026-07-02) — this was pre-time-sync noise. The oracle
  had been using whatever dayTime its save carried; commit 93d564c9 made parity_ab
  forward `--time` to `link_ctl.py warp` and re-pin `gSaveContext.dayTime` post-warp.
- Close-test: `tools/market_scene_probe.py 0xB1 <times…>` reads back `play->sceneNum`
  from both engines and asserts equality. Green across the full bracket
  (0x0000/0x2000/0x4555/0x6000/0x8000/0xC000/0xE000) — both correctly cross
  MARKET_DAY (0x20) ↔ MARKET_NIGHT (0x21) at the same thresholds. Ran retroactively
  after 93d564c9 and is now the standing red-if-forked signal.

### 3. Crowd NPCs (En_Hy adults) — NO significant divergence at close range
- Initial suspicion from #118 ("mis-posed / low-detail") not reproduced on close
  inspection: `mark_enhy.png` (afreeze 1 + acam) shows En_Hy dancers, standing
  townsfolk, kids in yellow dress — all with correct materials/skinning.
- Cucco, chicken, tent awning also correct.
- **Ruled out** as a Market-Day divergence at this angle. #118's user report needs
  the specific angle/frame the user was looking at to be reproduced. Ask user for
  screenshot, or run parity_ab across all Market rooms.

## Next steps
- Sky bug (finding #1) is the highest signal — RE the sky-dispatch path against
  the working scenes.
- Market-time fork (finding #2) is systematic; use Azahar oracle RAM reads to
  compare `SelectMarketScene`/entrance-scene-select logic.
- Do NOT open kanban cards for these — they live here until either resolved or
  the user asks for a card.
