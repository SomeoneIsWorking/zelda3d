---
id: I022
kind: instrument
status: trusted
created: 2026-08-04
---

## Instrument

tools/ahide_check.sh -- pixel-contribution check for a routed actor

## Validated by

Run against all three classes 2026-08-04 AFTER the fix: live routed prop (Obj_Kanban 0x141) -> 'DRAWS (1333 px, instance 0 of 1)'; actor absent from the scene (0x3FE) -> exit 3 'NO LIVE INSTANCE ... 0 instances examined, NOTHING TESTED'; REPL unreachable -> exit 2 'SEARCHED NOTHING'. It can now produce all three answers, which it could not before. WAS BROKEN 2026-07-30..2026-08-04: it cd'd to the repo's PARENT, so every zelda3d_repl.py call failed silently, no screenshot was captured, and it printed 'no contribution found -- INCONCLUSIVE' for EVERY input regardless of reality. Commit d3f50807's 'ahide_check.sh correctly returns INCONCLUSIVE' is VOID -- it was testing nothing; any ahide_check result recorded in that window must be re-run before it is believed.

## Known failure modes

(none recorded yet)
