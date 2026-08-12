---
id: I039
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

tools/csab_anim_check (model-per-clip fix + per-clip mode)

## Validated by

Re-validated 2026-08-12 after fixing the wrong-model defect that got I019 distrusted. The harness now picks the model sharing the clip's top-level archive directory (door/anim/* -> door/model/*.cmb), falling back to the first cmb. VALIDATED AGAINST BOTH CLASSES AND AGAINST ITS OWN PREDECESSOR: (a) POSITIVE -- all 8 gameplay_keep door clips now ANIMATE against the 4-bone door skeleton, where they previously read FROZEN; (b) SCALE -- over the 152 mapped MM3D actor GARs, FROZEN 113 -> 97 of 1945 clips, 16 recovered; (c) CONTROL -- the OoT3D path re-run against the OLD binary on 58 archives / 179 clips gives 173 ANIMATES / 6 FROZEN in BOTH, so the change is confined to multi-model archives; (d) INTERNAL CONSISTENCY -- 0 clips are reported as both ANIMATES and nodes==0, which is impossible by construction and would indicate the motion test was reading stale buffers. CSAB_ANIM_CHECK_PER_CLIP=1 prints per-clip verdict plus dur/bones/nodes; without it the summary cannot say WHICH clips froze, which is the question you have the moment the count is non-zero. STILL TRUE: it reports archives=0 clips=0 ANIMATES=0 FROZEN=0 cleanly when no path resolves -- always check archives= against the paths passed.

## Known failure modes

(none recorded yet)
