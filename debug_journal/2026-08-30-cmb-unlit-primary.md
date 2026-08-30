# CmbVShader unlit PRIMARY: preserve HasColor and MatDiffuse RGBA

## Root cause

The shared CMB loader replaced an absent color attribute with white vertices and discarded the
draw-level `HasColor` discriminator. Both native and unified shaders therefore treated every
unlit draw as `PRIMARY=aColor`. Exact `CmbVShader.shbin` words 112--120 instead do:

```
r10 = MatDiffuseColor
if (HasColor) r10 = VertexAttributeScale0.z * aColor
o1 = r10
```

The material parser also retained only MatDiffuse RGB even though c8 is RGBA and retail effect
materials author non-opaque diffuse alpha.

## Instrument correction and cached data

No oracle process was launched. `tools/cmb_primary_corpus_survey.py` scans the user ROM offline,
using the shared `tools/cmb_corpus.py` iterator also consumed by the TEV survey. Its first run
incorrectly tested material byte +0 (`IsFragmentLighting`) and reported 1,663 candidates. The
real-asset close-test falsified that result because `zorapeople.cmb` is vertex-lit. Correcting the
instrument to byte +1 (`IsVertexLighting`) gives the durable result in
`scratch/cmb_primary_corpus.txt`: 1,997 files, 154 candidates, zero parse failures.

`tools/test_cmb_primary_corpus_survey.py` now forces both answers: an unlit/no-color orange candle
is selected, while a vertex-lit material and a constant color attribute are rejected. This locks
the offset that produced the false first count.

## Port

- `CmbDrawGroup` keys batches by `(material, mesh_id, HasColor)`.
- `CmbMaterial`, `Zelda3DGlGroup`, and `SgGroup` preserve MatDiffuse RGBA and HasColor.
- `SgUbo` carries explicit `uMatDiffuse` and `uPrimaryCtl`; the unified UBO mirrors them.
- Native and unified vertex shaders choose authored MatDiffuse only for unlit/no-color draws.
- The synthetic sun/moon billboard declares its generated white color stream explicitly.

The shipping actor modulation remains an explicit host layer after authentic PRIMARY selection;
this change does not claim that wider caller-modulation policy is oracle-verified.

## Verification

- Fresh Clang build: `scratch/build-cmb-primary-clang`, `lus_tests` linked successfully.
- Real retail asset close-test: dungeon candle `efc_candle_modelT.cmb`, unlit/no-color,
  MatDiffuse `(255,140,0,255)`, survives parser -> draw group -> C renderer contract.
- Native/unified shader source close-tests and std140 UBO offset/cap tests pass.
- Full ROM-enabled suite: 470/470 tests pass.
- Python instrument falsifiers: 3/3 pass.
- Integrated `zelda3d_app` Clang build linked after 2,891 steps. A single host-only headless
  startup completed its cold 547-file N64 extraction, published the REPL, and logged
  `[Zelda3D_SG] resources ready (unified op model)` with no shader/SDL GPU failure. The manager's
  fixed 40-second readiness window expired during cold extraction, but the same owned process
  reached readiness after provisioning; it was then stopped by exact PID. No oracle ran.
- The combined suite initially skipped all ROM tests because the concurrently-added
  `RomSetupTest` cleared process environment without restoring it. The fixture now snapshots and
  restores all four ROM variables; 470/470 is the post-fix combined-tree result.
