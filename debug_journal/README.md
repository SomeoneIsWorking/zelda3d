# debug_journal

Parity-sweep findings, RE root-cause notes, and dead ends. Per project directive
(user 2026-07-02): parity findings are tracked HERE, not on the kanban. The kanban
is user-requested work only.

Layout: one file per finding, named `<date>-<slug>.md`. Include:
- Symptom (with quantitative measurement or oracle A/B ref)
- Reproducing tooling (REPL cmds / sweep tool)
- Root cause (if known, with disasm/decomp evidence). Mark "OPEN" if unresolved.
- Dead ends (things tried and ruled out)
- Fix (if landed) or status

## Discipline (user directive 2026-07-02, hard)

**Investigations start with RE and render-state divergence — NOT with screenshot
inspection.** Screenshot-first "defects" reverse (see `2026-07-02-dmc-missing-lava.md`:
the "missing lava" spawn A/B was a camera-framing artifact, not a render bug —
2 back-to-back commits `df8582a3` → `2a985ccb` proved the failure mode).

Order:
1. **RE first.** Open the OoT3D decomp (or `z_*.c` augmented by decomp) and READ the
   draw function: what actors are drawn, what matrices/textures, what gates the draw.
   That is the expected behavior. Not "brown pixels look different".
2. **Render-state compare over pixel compare.** Per-actor draw records (actor id,
   matrix hash, texture id, tev config, vtxfmt, primitive, vert count), ordered,
   hash-diffed against oracle_keeper. Fix ONE named render-state divergence per commit.
3. **Tooling to answer "why is this actor/draw missing" mechanically** — never eyeball.
4. **Screenshots ONLY for the final acceptance of a specifically named defect** derived
   from render-state divergence. Never for defect discovery.

Fingerprint of good work: a commit message names a specific draw call / actor /
texture / matrix that diverges between SoH and the OoT3D-decomp expectation
(`e945f4d0`, `196286ae`). Fingerprint of drift: a commit message names a pixel region
("lava missing", "sky looks off") as the defect. If your candidate defect can only be
stated in pixels, either (a) go find the decomp function that draws that thing and
state the divergence in draw-call terms, or (b) drop it — it's a mirage.
