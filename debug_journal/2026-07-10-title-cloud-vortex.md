# 2026-07-10 — title Death Mountain cloud vortex: missing ACTOR-layer ring (fixed)

Full derivation + port record: **oot3d-decomp `docs/title_cloud_vortex.md`** (read that first).
Summary for the soh3d trail:

- The OoT3D title vortex = TWO concentric additive rings: spot99 room material 0
  (`doughnut_modelT` 64×64, MODULATE ×1) + the `zelda_efc_doughnut.zar` actor ring
  (`Model/doughnut_modelT.cmb` 128×128, MODULATE **×2**, rotating). SoH drew only the room
  ring → ~1/3 energy, no arms. Fix: `behaviors/title/title_cloud_vortex.cpp` emits the actor
  ring after the title room draw (anchor = room-ring centroid via new
  `Zelda3D_ModelGroupCentroid`; scale 0.1 and rotY −0x20/frame from N64
  `z_bg_spot16_doughnut.c`, byte-consistent with the 3DS assets).
- Verified az=1000/soh=1408: swirl pixels 137→711 (oracle 655), swirl mean
  (70,85,112)→(100,123,170) (oracle (118,119,164)), p95 (98,112,120)→(124,151,208) (oracle
  (142,149,203)); whole-frame MAD 23.86→23.23. Crop: `scratch/vortex/verify_ab.png`.
  lus_tests 438 pass / 0 fail.
- Side fix: `Zelda3D_UpdateLight` now counts BOTH EnvLightSettings lights unconditionally
  (`numEnabledLights=2`) — the earlier `l2len>0.5` degeneracy gate is falsified by the oracle
  (night slots author dirs (0,0,0) yet PRIMARY = ambient×2×vColor). No-op at the verified
  frames (blended l2dir non-degenerate there); matters at pure-night cursors / zero-dir scenes.
- Dead ends (don't re-chase): fog (`isFog=0` material byte) — global fog toggle changed
  nothing, fog factor ≈0 there; mip blur — additive groups already sample max_lod=0; the
  per-triangle draw-log vertex-color fields (`c0=...`) read zero for ALL CmbVShader draws —
  use the per-pixel `PIXEL` dump (`SOH3D_PIXEL_TEX`, see tools/soh3d_harness/AZAHAR_PATCH.md).
