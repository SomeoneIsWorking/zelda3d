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

## Sweep signals are structured — visual compare is the LAST step (2026-07-02 tighter rule)

Full-game parity sweeps produce **structured signals only**. Never pixel-region
observations as findings.

- Structured signals allowed as sweep output:
  1. per-actor draw records (actor id, matrix hash, texture id, tev/combiner config,
     vtx format, primitive, vert count) — ordered by draw sequence
  2. render-state hashes per frame
  3. decomp-derived expectations (what OoT3D says should be drawn at that scene/frame)
- Visual/pixel compare is only allowed as the **final confirmation** step, and only
  AFTER upstream is already a full match: game state, camera coordinates, rendered
  lights, geometry, textures. If any of those still diverges, the pixel diff is
  caused by the upstream mismatch and teaches you nothing new.
- When investigating a divergence: FIRST tool is the per-actor draw record diff
  (structured), NOT a screenshot. Screenshots are reserved for the specific single
  case where the defect literally cannot be expressed as a render-state difference
  (extremely rare), and only after upstream is a full match at that state.

**Tooling TODO (open):** `parity_ab.py` / GX-capture must expose a structured-record
sweep mode as the default; the current PNG composite is for the acceptance-of-a-named-
defect case, not the sweep signal.
