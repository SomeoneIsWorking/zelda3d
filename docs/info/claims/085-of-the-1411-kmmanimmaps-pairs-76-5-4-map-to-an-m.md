---
id: C085
kind: claim
status: holds
created: 2026-08-12
tags: mm3d,animmap,csab
depends: tools/csab_anim_check.cpp
---

## Claim

Of the 1411 kMMAnimMaps pairs, 76 (5.4%) map to an MM3D clip that produces no bone motion -- and that residual is MM3D's own content, not a decoder gap.

## Evidence

Measured 2026-08-12 with the FIXED tools/csab_anim_check (I039; the previous version's wrong-model defect inflated this to 86 and is why I019 is distrusted). Across the 152 mapped MM3D actor GARs: 1945 clips, 97 FROZEN. Breakdown of the 97: 12 have nodes==0, i.e. the CSAB contains NO animation nodes at all and is an empty placeholder (ingo_anim_wait1 nodes=0 sits beside ingo_anim_wait2 nodes=22 in the same archive); 60 have tracks but duration<=2, i.e. a single-pose clip; 25 have tracks and duration>2 but move no bone, and those are dominated by non-skeletal assets carried in .csab form (sbn_yuka_model -- yuka = floor, pst_model, top_fc_mdl, demo_tre_lgt_*_fcurve_data, sand_shape01, weakpoint_eff01) and by explicit base/rest poses (jmp_base, last2_kihon, last3_kihon, in2_kihon, drs_kihon, bal_kihon, ha_default, gm_default, jsk_pose, pr_stop). 16 of the 23 multi-frame motionless MAPPED pairs carry a base/rest/unused name on one side or the other, several from N64 symbols named literally TPose, Empty, Unused or Stationary. Internal consistency check: 0 clips are reported as both ANIMATES and nodes==0.

## What would falsify it

The remaining candidates that a base-pose reading does NOT explain are wdb_pakupaku (paku-paku = chomping), moth_fly and wing_anm -- all have many nodes and plausible motion names. If any of those three is shown to move in the real game, the decoder still has a gap and this claim's 'not a decoder gap' conclusion is wrong for that class. The aggregate frozen RATE is not evidence on its own: it was previously argued from the rate matching OoT3D's ~5%, and that reasoning did not cover multi-frame motionless clips at all.
