# 2026-08-30 — title unified-CMB state contract restored; residual remains open

## Scope and ground truth

This pass moved off BossFd2 and audited the open title-wordmark residual. It did not tune colors or
fit a gain. The behavior source was the existing OoT3D RE:

- `oot3d-decomp/docs/title_logo_actor.md` §6.3/§6.6: the wordmark binds one private CmbVShader
  light and produces `PRIMARY = vertexColor * clamp(0.18 + max(0, dot(N, -L)), 0, 1)`;
- the decoded CMB/material payload already carried the title camera's sphere-map basis, the
  classified dual-texture modes, and per-draw RGBA into the native SDL3GPU path;
- `debug_journal/2026-07-15-mode4-combiner-order-FALSIFIED.md` remains authoritative that the
  remaining mat10/11 residual is upstream UV/PRIMARY evidence debt. This pass does not declare the
  historical final 4% closed.

## Root cause

The render-unification boundary copied only part of the native CMB contract:

1. `VariantForGroup` ignored `dualTexMode`, so classified modes 1..4 went through the single-texture
   shader shape.
2. The unified vertex shader ignored the copied `uSphRot0..2` title-camera basis.
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
- Unified CMB vertex generation consumes the copied live sphere basis and private-light payload.
- The dual variant preserves the existing native mode 1..4 formulas instead of inventing a title
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
`re-partial` and the parity-map final-4% row remains OPEN. The next grounded step is a selected-draw
PRIMARY/UV capture for mat10/11 on the unified route, using the cached oracle artifact and the
host-only instrument. Do not tune the remaining shape/coverage.
