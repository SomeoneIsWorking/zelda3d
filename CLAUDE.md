# SoH3D — agent instructions

SoH3D = Ship of Harkinian rendering OoT3D (3DS) models/world instead of N64. See `README`/notes for
the project itself; this file is the working contract for agents.

## The backlog is a kanban on GitHub Issues — USE IT

The backlog is **GitHub Issues** on this repo (`SomeoneIsWorking/soh3d`, private), driven by
**`tools/kanban.py`**. It is the source of truth across all agents and machines. `BACKLOG.md` is
just a pointer; `KANBAN.md` is a generated offline mirror.

- **Find work:** `tools/kanban.py ls` (open cards by column) / `ls --status reopened` / `show <#>`.
  Offline or fresh: read `KANBAN.md`.
- **Columns** are `status:<col>` labels; **done = closed issue**:
  `todo · in-progress · in-review · needs-confirmation · reopened · blocked`.
- **Work a card:** `mv <#> in-progress` → fix → `mv <#> in-review` (while verifying) →
  `mv <#> needs-confirmation` (fix shipped, awaiting user) → user closes it. `reopen <#>` if it
  regressed. Don't self-close a user-visible visual fix — post proof and let the user confirm.
- **File a bug:** `tools/kanban.py add --title "..." --labels type:render,... [--body-file f]`.
  Capture new playtest reports as cards immediately.
- **Screenshots:** `tools/kanban.py evidence <#> shot.png --caption "..."` — uploads to the
  `evidence-assets` GitHub release and embeds by URL. `--to-body` = original bug evidence;
  default (comment) = fix-verification proof. **Never commit PNGs to the repo.**
- After board changes, `tools/kanban.py render` and commit `KANBAN.md`.

Full workflow: the **soh3d-kanban** skill. Detail on any item: its issue body +
`kanban/ARCHIVE_BACKLOG_pre_kanban.md` (pre-migration history).

## RULE: every fix MUST post evidence before it leaves `in-progress`

A card does **not** advance to `needs-confirmation` or `done` until you have posted proof to the
issue. **No evidence = not fixed.** This is mandatory, not optional.

- **User-visible fix** → an AFTER screenshot from the live game:
  `tools/kanban.py evidence <#> after.png --caption "fixed: <what now works>"`. For a regression
  the user reported with a picture, frame the SAME view so it's a like-for-like before/after.
- **Non-visual / tooling fix** → post the proof that applies (REPL/log output, a quantitative
  measurement, a test result) as an issue comment. Still required.
- Then `mv <#> needs-confirmation` and let the USER confirm user-visible fixes (don't self-close
  them). Close outright only for non-user-visible work.

## Verify the FULL user-facing path, not a narrow mechanism

A card is only fixed when the real user-facing behavior works in a realistic run — not when a
frozen-cam / forced-state / single-frame harness passes. Prior "VERIFIED headless" marks were
repeatedly falsified by playtest. Run the live game (skill **soh3d-game-control**), exercise the
actual path, and capture the evidence above.

## Commit chain (commit each verified fix yourself, reference the issue `#<n>`)

- `libultraship/` → `fork/soh3d`  (edit it freely when the fix belongs there: renderer, input
  mapping, windowing — don't push a fix into the wrong layer to keep LUS "UNTOUCHED")
- `Shipwright/` → `fork/develop`
- outer repo → `origin/main`

## Hard rules

- **Headless always:** this is a Wayland machine — `SOH3D_HEADLESS=1`, never a headed window.
- Run the game via `tools/soh3d_game.sh`; REPL via `tools/soh3d_repl.py` (skill soh3d-game-control).
- Scratch/build artifacts go in the gitignored `scratch/`, never `/tmp`, never committed.
- Verify quantitatively/visually; send screenshots for any UI/UX call.
