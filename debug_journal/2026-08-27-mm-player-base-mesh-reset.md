# MM Player base mesh reset

## Symptom and root cause

The MM Player replacement loaded the correct form CMB and exact form-directory
CSAB, but emitted every CMB mesh group because no MM mesh visibility policy was
installed. This is not a harmless default: Human has 93 meshes across 34 IDs,
including mutually exclusive hands, bows, hookshot, shields, swords, and
sheaths.

## Binary ground truth

- callback installer `0x001f47ec` pins `Player_Draw = 0x001f9038`;
- Player draw calls the base visibility reset `0x0020cfa4`;
- the reset enumerates all groups and uses the five-by-five table at
  `0x00626b5c`;
- exact base masks are FD `{9,10,12}`, Goron `{6,7,10}`, Zora `{6,7}`, Deku
  `{5,9,10,11,13}`, Human `{28,29,30,32,33}`.

The derivation, raw table, helper call chain, and asset counts are recorded in
`mm3d-decomp/docs/player_draw.md`.

## Port and falsifier

`mm3d_player_mesh_policy.{cpp,h}` owns the base mask and `mm3d_player.c` submits
it before pose emission. The first live trace falsified the integration: all
five submissions still carried `0xffffffffffffffff`. MM's centralized
`Zelda3D_EmitModelDraw` had never called `Zelda3D_GL_EmitPose`, which is the
renderer seam that snapshots both the current skin pose and material overrides
for a deferred display-list draw. Adding that missing emit-order snapshot fixed
the root cause for all MM models rather than adding a Player-only log or mask
copy.

The corrected headless five-form run in Termina Field reported:

| Form | Model | Submitted mask | Phase evidence |
| --- | ---: | ---: | --- |
| Human | 0 | `0x0000000370000000` | `boy/anim/link_normal_wait_free.csab` moved, 160 samples |
| Deku | 15 | `0x0000000000002e20` | `nuts/anim/pn_maskoffstart.csab` moved, 58 samples |
| Goron | 16 | `0x00000000000004c0` | `goron/anim/pg_wait.csab` moved, 303 samples |
| Zora | 17 | `0x00000000000000c0` | `zora/anim/pz_wait.csab` moved, 329 samples |
| Fierce Deity | 18 | `0x0000000000001600` | `boy/anim/link_fighter_wait_long.csab` moved, 178 samples |

Each submitted value exactly equals the decompiled base set. The run also
exposed an honest animation limitation: Deku's normal wait/walk resources were
unmapped, so its only sampled exact clip was the transformation start. One Human
walk-end-right pair remained at frame zero for 49 samples; this short form tour
was not the canonical no-static phase gate.

A focused policy test asserts all five masks, and a production-seam regression
requires `Zelda3D_GL_EmitPose` before `gSPZelda3DDraw`. The corrected MM v10
parser plus `tools/mm_player_cmb_dump.py` reports the real form inventories
without extracting asset bytes. Screenshots are under
`scratch/screenshots/mm_link_emit_*.png`; only the Deku framing is credible as
visual evidence, because the generic side framing placed the larger forms partly
below the image.

The implementation is falsified if a neutral live form shows a mesh outside its
documented base set, if a documented base mesh is absent, or if the emitted
submission mask differs from the policy result. Equipment states are not a
falsifier for this stage: retail applies separate selectors afterward, and those
remain open at `0x0020cfa4`/`0x00211aa4`.
