---
id: 26
title: tools/re_frontier.py set silently deletes 149 lines of docs/re-frontier.md on a no-op edit
status: open
symptom: Running `python3 tools/re_frontier.py set <id> notes=...` on docs/re-frontier.md rewrote the file from 801 lines to 653 and reported success. `re_frontier.py check` still reported "re-frontier OK" afterwards. Whole measurement tables and annotated sub-fields were gone, including the unified-vs-native fog A/B table, the `render.per-draw-uniform-carriage` audit notes, and the MM3D oracle/fog A/B notes.
state_items: none
tags: tooling,re-frontier,data-loss,docs
created: 2026-09-29
updated: 2026-09-29
---

## Reproduction

Deterministic, and it needs no game input:

```
cp docs/re-frontier.md /tmp/refr.bak
python3 tools/re_frontier.py set render.cmb-fragment-lighting "notes=PROBE"
wc -l /tmp/refr.bak docs/re-frontier.md     # 801 -> 653
diff /tmp/refr.bak docs/re-frontier.md | grep -c '^<'
```

That last count is **149**. `re_frontier.py check` exits 0 and prints
`re-frontier OK: no unknown deps, no cycles, every re-verified step cites evidence.`

The change is made by a command whose only argument was `notes=PROBE`, so this is not "a set that
happened to rewrite other fields" — it is a **no-op edit destroying unrelated records**.

## Root cause

`docs/re-frontier.md` is a **hand-enriched** record, not a machine-generated one. Its entries carry
content the tracker's schema has no field for:

* sub-fields with a qualifier in the name — `- where (audit): ...`, `- notes (the audit's own three
  defects...): ...`, `- gap (MM3D's side, restated 2026-09-28): ...`
* multi-line content inside a single field — the `lighting.pica-fog` gap holds a markdown table
  (`| route | fog | union_rgb_mae | content |`) across seven lines, and `- notes: no pixel win is
  claimed...` is a whole paragraph.

The parser evidently recognises only the canonical `- <field>: <value>` shape, and the writer
re-emits only what it parsed. Anything else is simply not in its model, so it is not written back.
`set` then **overwrites the whole file** from that lossy model.

The deeper problem is the failure mode, not the parse. Three things had to be true for 149 lines of
recorded measurements to vanish without a word of warning, and each is independently fixable:

1. **No loss detection.** The writer does not compare its re-emission against the file it read, so
   it cannot notice it is about to drop content it never modelled.
2. **No backup, no dry run, and a success exit code.** `set` is destructive and reports success.
3. **`check` validates the wrong thing.** It verifies dependency integrity and evidence citation
   over the *parsed model*, so it cannot see that content the parser dropped is no longer in the
   file. It passing is exactly what made this look safe.

This is the project's own standing lesson applied to its own tooling: an instrument that reports
success while measuring nothing. The same class appeared twice in this session inside the new
lighting-register work — a mutation harness that measured a stale `.pyc`, and a mutation that
`continue`d without restoring — both of which were caught only because a control was added.

## Why it matters beyond this file

`docs/re-frontier.md` is one of the four orientation authorities (`docs/project-goals.md`,
`docs/project-state.md`, `docs/issues/`, `docs/codemap.md`, plus this and `docs/parity-map.md`), and
its entries are the provenance for closed parity rows. A `set` that silently deletes the evidence
behind a closed row leaves the row looking intact and its justification gone — the exact state the
parity map's "do not revisit" rule cannot detect, because a deleted `notes:` line reads as "no notes
recorded", not as "the justification was deleted".

Any prior `set` invocation in this repo's history is therefore suspect, and the deletions should be
recovered from git rather than re-derived.

## Fix

1. **Refuse to lose content.** Before writing, re-emit the file and diff it against the input; if the
   re-emission drops any non-blank line that the input had, abort and report the dropped lines.
   This is the same non-vacuous-parse guard the generators use before overwriting a table.
2. **Extend the schema to the file's real shape**, or make `set` refuse to write a file it cannot
   round-trip losslessly. Silently narrowing the model is the defect.
3. **Make `check` loss-aware**: it should fail when the document contains field shapes the parser
   does not model, so a future annotation cannot be dropped by the next edit.
4. **Add `--dry-run`** and write through a temp file + rename, so a failed write cannot truncate the
   only copy.

## Verification

* The no-op reproduction above must, after the fix, print the lines it would drop and change nothing.
* A fixture entry carrying a sub-field (`- notes (x): ...`) and a multi-line table must survive a
  `set` of a *different* field byte-for-byte.
* `re_frontier.py check` must fail on a document containing an unmodelled field shape.
