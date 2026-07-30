# The claims and instruments ledgers

`claims/` — results that get cited as proof, each with the evidence under it and a stated falsifier.
`instruments/` — tools whose output gets believed, each with how it was validated.

Both are managed by the `project-info` skill (`info.py claim|instrument|brief|check`). This file records
what the ledgers have actually taught us, which is not the same as how they are meant to be used.

## Record MEASUREMENTS as claims. Hold MECHANISMS as hypotheses until directly tested.

On 2026-07-30, four claims were created and falsified within the same session (C028, C032, C038, C039).
Every one asserted a **mechanism inferred from symptoms** while a direct test existed and was cheap:

| claim | asserted | refuted by |
|---|---|---|
| C028 | OoT3D bakes the Deku Tree mouth into the room mesh | reading the actor's 4-line update fn: the mesh MOVES |
| C032 | REPL `warp` only works as the first action after a restart | printing `sceneNum`: it changed 85→81→85 |
| C038 | the `POLY_XLU` draw path does not render | forcing the model to `POLY_OPA`: still 0 px |
| C039 | the web is culled because its winding is opposite | winding vs normals: 100% CCW, same as every control |

The claims from the same session that HELD were all direct measurements — byte-exact table reads (C023,
C027), a footprint scale confirmed on two independent axes (C041). The distinction is not how confident
the reasoning felt; it is whether the thing asserted was the thing observed.

**So:** if a claim's subject is *why* something happens, run the direct test before filing it, or file it
as an open question with the test named. `--expires-on` is not a substitute for that — three of the four
above stated a perfectly good falsifier and were still wrong for a whole tick before it fired.

## Evidence must measure the CLAIM, not something adjacent to it

The `mm.skinned-csab` frontier step cited "standalone parse validation (dog 12/12, an1 37/37, dnt 19/19
clips, bone counts match CMBs)". That is real evidence — of PARSE SUCCESS. The claim it was supporting was
that MM3D animation worked, and in fact 100% of MM3D's 168803 tracks were being silently discarded while
every skinned actor stood in bind pose. Parse counts and bone counts cannot see motion.

Before filing evidence, ask what result would look identical if the claim were FALSE. If the answer is
"this same evidence", the evidence is adjacent, not supporting.

## An instrument that cannot show the other answer is not evidence

Five instruments produced misleading results in that one session: a shared-cap op dump (400 lines shared
across all draws, so "0 HUD draws" meant 0 *logged*), a misaligned 0x10 grid (reported 55 mismatches from
an off-by-8), `csab_anim_check` with no resolvable paths (`archives=0 … ANIMATES=0`, indistinguishable
from "nothing animates"), a `grep -c` against a log path that did not exist, and a single-camera pixel
check on a flat single-sided prop (0 px from behind a plane, which condemned two *correct* routings).

Each was fixed by making the negative case impossible to confuse with the positive: print the
denominator, print an independent observable next to the verdict, refuse instead of returning empty.
