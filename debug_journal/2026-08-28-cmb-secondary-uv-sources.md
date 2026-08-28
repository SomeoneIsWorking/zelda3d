# CMB secondary UV source recovery

## Cause

The CMB texture coordinator's `sourceCoordinate` is byte 0. Byte 1 is
`referenceCamera`. The prior corpus survey read byte 1, concluded that every
coordinator selected `texCoord0`, and the shipping vertex path consequently
retained only one UV stream.

`valbasiagnd.cmb` disproves that assumption directly. Materials 0, 1, and 5
select source 1 for TEX1 and combine it through an additive term equivalent to
`2 * TEX0 * TEX1`. Those materials cover four of seven groups, 598 unique
referenced source vertices, and 2,136 expanded triangle vertices; every
referenced `texCoord1` value differs from `texCoord0`. Reusing UV0 therefore
relocates the authored orange fire detail over most of the body.

The corrected corpus finds nonzero sources on 1 TEX0, 60 TEX1, and 16 TEX2
consumed coordinators, so this is a generic format defect rather than a
BossFd2 special case.

## Port

- `cmb.cpp` parses all three coordinator source bytes while all three CMB UV
  attributes are available, then resolves one authored UV pair per texture
  unit with a defined texCoord0 fallback for absent/invalid sources.
- `CmbVertex`, `Zelda3DGlVtx`, and `UnifiedVtx` carry the resolved secondary
  and tertiary coordinates.
- Native and unified SDL3GPU layouts/shaders consume those independent values;
  sphere-mapped coordinators still derive their coordinates from normals.
- The ROM-backed CMB test asserts the three BossFd2 material selectors and all
  2,136 expanded independent-UV1 vertices. `tools/tev_corpus_survey.py` now
  reads byte 0.

## Falsifiers and negative findings

The runtime falsifier is a paired capture: the four affected groups must move
toward the oracle while unaffected materials remain stable. No color gain is
part of the port.

Binary and asset audits ruled out a BossFd2-wide light bind, emission,
fragment-light/LUT state, blend/depth state, and rest/CSAB scale. The actor's
only pre-submit constant override is slot 4 on material 4's localized exposed-
face overlay.

The mane stress residual is separate comparator input debt. The oracle root
moved `(+0.061,-0.045)` over ten calls while the host root moved
`(-0.250,+0.184)`. Replaying those endpoints reproduces the observed maximum
(`28.280565` versus `28.2806`); identical roots reduce recovered-3DS versus
host trig/rounding error to `1.5259e-5`. A future solver comparison must freeze
or synchronize all three per-call posed-root trajectories before treating its
residual as a math divergence.

## Paired runtime result

`fd2_ground_paired_20260828_aa` used the same recovered parent handoff, matched
camera, and equal settle counts as the prior `_z` capture. The two oracle PPMs
are byte-identical (`sha256 53f9df4020f9ceb8d50e094e42ddbc9f6095085a3e7f680e8ad31dc1512de386`),
so the before/after host delta is not oracle pose drift. On the fixed
`130x300+330+170` actor crop, oracle/host RMSE moved from `0.185810` to
`0.183825` (about 1.07% closer), while the host before/after RMSE is `0.0141703`.
The added secondary detail is visible over the affected body region.

This verifies that the recovered UV source reaches the shipping renderer and
moves the image in the expected direction. It does not close BossFd2 parity:
the body remains substantially darker, and a future group-isolated capture is
still needed to prove unaffected materials remain byte-stable.
