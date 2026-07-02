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
- Root cause: **FOUND** — `Zelda3D_TryDrawSky` (Shipwright/soh/src/zelda3d/zelda3d.c:3144)
  early-outs when `play->skyboxId != SKYBOX_NORMAL_SKY`. Market Day uses
  `SKYBOX_MARKET_CHILD_DAY` (=9, Shipwright/soh/include/z64.h:405); Market Night uses
  `SKYBOX_MARKET_CHILD_NIGHT` (=0xA). Neither is `SKYBOX_NORMAL_SKY` (=1) so the
  BlueSky.zar dome path is skipped. There is no fallback for the ~20 non-NORMAL
  skybox variants (BAZAAR, MARKET_ADULT, HOUSE_LINK, HAPPY_MASK_SHOP, all houses,
  shops, HOUSE_ROYAL_FAMILY, MARKET_RUINS, GANONS_TOWER_COLLAPSE, etc.), so every
  scene using one of them shows a black void.
- Fix direction: port the OoT3D **matte-painting** sky class. The N64 game handles
  these via a prerendered fullscreen VR image (a full frame painted onto the
  skybox). OoT3D uses a similar prerendered VR image per variant (`vrbox/*` in
  kankyo). Extend `Zelda3D_TryDrawSky` to also handle `skyboxId >= 2` by looking
  up the matching OoT3D vrbox texture and drawing it as a screen-space quad
  (no dome geometry). Different code path from the BlueSky dome — DON'T reuse
  Zelda3D_SkyModelId.
- Scope note: this is a NEW rendering class, not tuning. Do NOT tune the existing
  dome path to fake it. Per user directive [[soh3d-port-dont-pixelcompare]].

### 2. Scene-time divergence at 0x6000
- SoH3D at time=0x6000 loaded **SCENE_MARKET_DAY** (0x20) — sunlit.
- OoT3D oracle at the same entrance loaded **SCENE_MARKET_NIGHT** — moonlit, "Market"
  placard visible.
- Same entrance index (0xB1), same dayTime (0x6000), different scene selection.
- This IS an OoT3D behavior fork (Market has 3 time-variant scenes: Day/Night/Ruins);
  the SoH3D-vs-oracle disagreement is on the day/night threshold at 0x6000, or on
  Market's scene-select fork logic. Sample both games' `SelectMarketScene` (name TBC)
  at various times to bracket the threshold.
- Root cause: **OPEN**. This falsifies "same entrance+time → same scene" for Market.

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
