# 2026-08-30 — title unified-CMB state contract restored; residual remains open

## Scope and ground truth

This pass moved off BossFd2 and audited the open title-wordmark residual. It did not tune colors or
fit a gain. The behavior source was the existing OoT3D RE:

- `oot3d-decomp/docs/title_logo_actor.md` §6.3/§6.6: the wordmark binds one private CmbVShader
  light and produces `PRIMARY = vertexColor * clamp(0.18 + max(0, dot(N, -L)), 0, 1)`;
- the decoded CMB/material payload already carried the title camera's sphere-map basis, the
  classified dual-texture modes, and per-draw RGBA into the native SDL3GPU path;
- exact cs1093 draw identities plus the decompiled title draw later in this pass falsified the
  synthetic mat10/11 “mode 4” itself, not merely its clamp order. This pass does not declare title
  parity closed.

## Root cause

The render-unification boundary copied only part of the native CMB contract:

1. `VariantForGroup` ignored `dualTexMode`, so classified modes 1..3 went through the single-texture
   shader shape.
2. The unified vertex shader ignored the copied sphere-normal transform channel. At this stage the
   channel still carried the inferred title-camera basis; the exact audit below later corrected
   that payload to captured CmbVShader c4-c6 identity.
3. It ignored `uSheen.x`, even though force-unlit title draws still own the private RE'd wordmark
   light independently of world lighting.
4. The unified UBO initialized `uPrimColor` to white and never transported DrawModel's `r8/g8/b8/a8`.
   Consequently title fade alpha was lost after TEV/alpha-test; at cs464 the copyright appeared
   before the oracle and the wordmark rendered fully opaque.

These are one cause: a new renderer owner did not preserve the old owner's complete input contract.
The sampler change in `8ada8e84` made the loss conspicuous because the binary-authored no-mipmap
filter stopped blurring the title textures, but sampler enums were not the title fix.

## Implementation

- CMB dual-texture groups select the unified dual-texture variant.
- Unified CMB vertex generation consumes the copied sphere-normal transform and private-light
  payload. The final transform semantics are the exact CmbVShader c4-c6 rows derived below.
- The dual variant preserves the existing native mode 1..3 formulas instead of inventing a title
  special case.
- `PackCmbDrawModulation` is the one UBO adapter for caller RGBA and the native tint gate. RGB is
  applied only to the same non-vertex-lit PRIMARY classes as the native path; alpha is applied after
  TEV and alpha-test, preserving the native fade ordering.
- Stale “dormant” comments were corrected: both unified emitters are live behind their renderer
  bits.

## Oracle cache and deterministic host instrument

No oracle frame was rerun for this pass. Historical exact oracle frames were imported into the
formal cache under the correct vanilla context:

`9b68c40a7247d715_6510135ae6c38599_p37-345049fb_tpoff`

- title cs464 → cached az752, source `scratch/title_ab/sphfixy2_00_cs464.az.png`
- title cs1093 → cached az2010, source `scratch/title_ab/sphfixy2_01_cs1093.az.png`

`tools/title_host_capture.py` fails before spawning on a cache miss, pins both cache identity and
host rendering to texpack-off/native 400x240, naturally advances only SoH, verifies the live title
cursor, and captures via the new host-only `soh_snapshot` command. Two attempted instruments were
falsified before evidence was accepted:

- direct `soh_titlecs` writes do not synchronize all title state and the half-rate cursor can remain
  unchanged for one host tick;
- an initial cache-key/run mismatch paired a vanilla oracle “3D” logo with the host's 4K pack.

The two mis-keyed imported frames were invalidated from the texture-pack-on context after the
correct texture-pack-off entries were verified, so that failed instrument cannot be reused as
apparently valid evidence.

The harness now also exposes `soh_unified 0..3`, so a capture states which route it exercises instead
of silently measuring default-off legacy CMB.

## Quantitative evidence

All numbers below come from the same tracked tool and explicit logo ROI/predicate. `union_rgb_mae`
is lower-is-better. The legacy baseline and unified captures used the same cached oracle images and
natural host cursor targets.

| anchor | route/state | content score | oracle/host gold mean R | union RGB MAE |
|---|---:|---:|---:|---:|
| cs464 / az752 | legacy CMB after authored no-mip sampler | 0.2767 | 76.2 / 239.7 | 64.39 |
| cs464 / az752 | unified, light/sphere/dual restored but RGBA still missing | 0.2781 | 76.2 / 142.1 | 31.58 |
| cs464 / az752 | combined fix, including post-TEV draw alpha | **0.7800** | 76.2 / 125.0 | **26.22** |
| cs1093 / az2010 | legacy CMB after authored no-mip sampler | 0.7310 | 201.0 / 243.9 | 74.32 |
| cs1093 / az2010 | combined unified fix | 0.6925 | 201.0 / **205.1** | **57.50** |

The cs464 SxS (`scratch/title_host_capture/unified_state_fix_rgba_cs464_sxs.png`) is the strongest
contract falsifier: the premature copyright is gone and framing/content returns to the cached
oracle segment. The cs1093 SxS proves the private-light brightness transport, but its lower content
score and visible rider/scene differences show that broader unified-CMB parity remains incomplete.

## Honest state

This closes the unified ownership regression, not title parity. `title.wordmark-decoration` remains
`re-partial`. The next sections record the exact identity audit and independent coordinator-0 fix;
do not tune the remaining shape/coverage.

## Exact mat10/11 identity audit and cache-first instruments

`tools/title_oracle_context.py` now owns the RE'd title clock and vanilla cache context shared by
host and oracle tools. `tools/title_oracle_probe.py` stores immutable exact-cursor uniform or
selected-fragment artifacts in `OracleCache`; its identity includes capture version, title cursor,
oracle frame, software renderer, and selected draw. A hit returns before `spawn`, covered by a unit
test. `title_host_capture.py --draw-list` similarly publishes host identities on the second
half-rate tick without changing the requested cursor.

The software-renderer uniform captures were run once and then proven to hit immediately:

- cs1093 / az2010:
  `artifacts/title-vsuni_5b3dcc734c84.log`
- cs464 / az752:
  `artifacts/title-vsuni_fa8d85c588c4.log`

Both live under cache key
`9b68c40a7247d715_6510135ae6c38599_p37-345049fb_tpoff`. The ten host mat10/11 groups match ten
oracle draws one-for-one by vertex count (`123,123,201,201,135,141,60,72,126,120`). Every oracle
identity reports `texEn=1/0/0` and the authored
`MODULATE(PRIMARY,TEX0) x1 → REPLACE(PREVIOUS)` chain. The CMB gives `tex1_idx=-1` and
`coord0_mapping=3`. `oot3d-decomp/docs/title_logo_actor.md` §6 and `build/decomp/001da4f4.c`
confirm that the title draw writes alpha, private-light, and transform state before generic
submission, with no TEV rewrite or second texture binding.

Therefore `kDualTexSelfSphereAdd` was fabricated: an old draw-log observation belonged to another
draw and was incorrectly attributed to mat10/11. The parser-side alias, fourth shader mode, and
`3*PRIMARY*TEX0` combine were removed. Coordinator 0's mapping method and scale/translation now
travel independently to TEX0; coordinator 1 remains independently carried for genuine TEX1 users.

## Cache-only evidence after the correction

No oracle image was rerun. The host advanced naturally against the existing cached az752/az2010
frames:

| anchor | state | content score | oracle/host gold px | oracle/host gold mean R | union RGB MAE |
|---|---|---:|---:|---:|---:|
| cs464 | authored TEV, before coord0 transport | 0.7786 | 309 / 1178 | 76.2 / 66.6 | 26.70 |
| cs464 | independent coord0 sphere map | **0.8008** | 309 / 4162 | 76.2 / 54.1 | 26.76 |
| cs1093 | authored TEV, before coord0 transport | 0.7424 | 5159 / 987 | 201.0 / 98.9 | 61.74 |
| cs1093 | independent coord0 sphere map | **0.7465** | 5159 / 3669 | 201.0 / 123.0 | **50.04** |

Both anchors improve in whole-frame content and late color error drops substantially, which is the
anti-overfit evidence for transporting the authored mapping. The early gold predicate is visibly
confounded by white shield/sword highlights, so its count is diagnostic rather than a parity gate.
The remaining title difference is still visible; the selected-fragment command is the next
ground-truth observable and will itself be cached after one run.

## Selected-fragment result: the camera-basis sphere transform was false

The first selected-fragment attempt exposed a tooling defect before producing evidence: a
`run 100` command exceeded the harness's 60-second response timeout after roughly eight minutes,
and no artifact was saved. `title_oracle_probe.py` now:

- advances in checked 25-frame chunks;
- stores immutable `title-checkpoint` savestates every 400 oracle frames;
- resumes from the latest checkpoint while leaving at least three post-load warmup frames; and
- keys checkpoints by schema version, exact oracle frame, and software-renderer mode.

The protected retry completed once and cached cs1093/draw86 under the observer-schema key
`9b68c40a7247d715_6510135ae6c38599_p37-6f57056b_tpoff` as
`artifacts/title-fragments_1564d9e6eac1.log`. An immediate repeat reported `oracle: cache hit` in
2.41 seconds and did not spawn Azahar.

The selected draw generated 2,452 unique fragments with no occlusion discards. Its exact values
were:

- PRIMARY = `(193,193,193,255)` everywhere;
- TEX1 = zero everywhere;
- TEX0 = `(206,40,49..57,255)` in the software rasterizer;
- combined = `(155,30,37..43,255)`, exactly PRIMARY × TEX0.

The unified host fragment taps independently reproduced PRIMARY exactly and the TEV product
exactly. Before the final normal-transform correction, 412 clean selected host pixels sampled
TEX0 `(148,28,16)`; lighting and TEV were therefore exonerated and texture-coordinate state was
the only remaining cause.

The oracle logger was then extended generically to serialize CmbVShader c4-c7, c10-c16, and c92.
The same cached run records all wordmark draws 75-87 with c4-c6 exact identity, coordinator-0
texture matrix exact identity, and mapping method 3. `/CmbVShader.shbin` independently names c4-c7
`uModelView`; words 59-61 transform the normal by c4-c6 and words 295-296 compute
`0.5*n.xy+0.5`. This falsifies the historical claim that the title's live camera basis belongs in
the sphere-map input.

The host reconciliation channel was renamed from a camera-specific `SphereMapViewRot` to the
actual responsibility, `SphereMapNormalMatrix`, and now transports the oracle c4-c6 identity for
the title independently of the host orthographic placement matrix. The selected TEX0 tap moved
from `(148,28,16)` to `(209,42,58)` on all 412 clean pixels.

That small residual is an oracle-instrument limitation, not a port offset to tune. Azahar's
software rasterizer explicitly leaves min/mag filtering as a TODO and truncates `(0.5,0.5)` to
RGB565 texel `(64,63)=(206,40,49)`. The decoded center 2×2 average is `(208,42,57.25)`, which the
shipping SDL linear sampler returns as `(209,42,58)`. Instrument I043 is distrusted for filtered
sample values. No half-texel correction was added.

The cache-only normal host frame is
`scratch/title_host_capture/title_sphere_normal_identity_cs1093_sxs.png`. The whole-frame
software-oracle content score changes 0.7465→0.7377 because the metric now penalizes the correct
linear-filter result against the nearest-only software oracle; it is not a valid reason to restore
the contradicted camera transform. Wider rider/scene/title differences remain open.
