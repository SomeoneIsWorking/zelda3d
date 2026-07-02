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
