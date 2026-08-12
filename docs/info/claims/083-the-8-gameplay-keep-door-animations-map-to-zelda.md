---
id: C083
kind: claim
status: holds
created: 2026-08-12
tags: mm3d,animmap
depends: tools/gen_mm_animmap.py
---

## Claim

The 8 gameplay_keep door animations map to zelda2_keep's 8 door CSABs as Human=clink, FierceDeityZora=link, Goron=pg, Deku=pn, with A=Left and B=Right.

## Evidence

Two independent chains, neither of which is frame counts alone. (1) FORM: zelda2_link_new.gar.lzs files the same door animations under form-NAMED directories -- child/anim/clink_demo_door*, goron/anim/pg_door*, nuts/anim/pn_door*, zora/anim/pz_door*. zelda2_keep has NO pz and instead has 'link'; the one N64 symbol covering two adult-height forms is FierceDeityZora, so the door needs 4 height classes where the player rig needs 5. (2) SIDE: exact duration equality within each already-pinned form, measured N64 frameCount u16@0x44 of the mm.o2r resource vs MM3D csab duration u32@0x34 -- Human 88/85, FD+Zora 66/74, Goron 66/85, Deku 81/85, all matching clink A/B, link A/B, pg A/B, pn A/B respectively. The two clips DIFFER within every form, so A=Left is confirmed four independent times. Re-derived on demand by 'tools/gen_mm_animmap.py --verify-overrides' (8 checked, 0 failed).

## What would falsify it

Duration alone does NOT determine this mapping: 66 and 85 each recur across forms, so bipartite matching on duration admits many assignments. The claim fails if the form lexicon anchor is wrong -- i.e. if zelda2_link_new's directory names do not denote player forms, or if a pz_door clip is found in zelda2_keep after all. --verify-overrides failing is the mechanical falsifier.
