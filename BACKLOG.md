# SoH3D backlog → moved to a GitHub-Issues kanban

The backlog is now a **kanban board on GitHub Issues** (`SomeoneIsWorking/zelda3d`, private), so
any agent — local sessions, cloud subagents, another PC — can read and drive it via the GitHub
API, not just a file in one checkout.

## Where things are
- **Board (live):** GitHub Issues. Columns are labels `status:<column>`; **done = closed issue**.
  Columns: `todo · in-progress · in-review · needs-confirmation · reopened · blocked · done`.
- **Board (offline mirror):** [`KANBAN.md`](KANBAN.md) — a compact, generated snapshot grouped by
  column. Regenerate with `tools/kanban.py render`. Read this first if you're offline / fresh.
- **Card details:** each issue body holds the full notes (root cause, repro, next step, screenshot
  paths). Legacy backlog ids (`#5`, `#24`, `#29`…) are recorded in each body as `Legacy backlog id`.
- **History before the kanban:** [`kanban/ARCHIVE_BACKLOG_pre_kanban.md`](kanban/ARCHIVE_BACKLOG_pre_kanban.md)
  — the full pre-migration backlog incl. the entire "Done (recent)" log. Nothing was lost.

## Driving the board — `tools/kanban.py` (thin wrapper over `gh`)
```
tools/kanban.py ls [--status reopened] [--label type:stairs] [--all]
tools/kanban.py show <issue#>
tools/kanban.py add --title "..." --status todo --labels type:render,type:scene [--body-file f] [--legacy 5]
tools/kanban.py mv <issue#> <column>      # column = a status, or `done` to close
tools/kanban.py reopen <issue#> [<column>]
tools/kanban.py render                     # rebuild KANBAN.md
tools/kanban.py stats
```
A "kanban movement" is one command: `tools/kanban.py mv 1 in-progress`. After changing the board,
run `tools/kanban.py render` and commit `KANBAN.md` so the offline mirror stays current.

## Screenshots — evidence & verification proof
```
tools/kanban.py evidence <issue#> <shot.png> [more.png ...] [--caption "..."] [--to-body]
```
Uploads each screenshot as a GitHub **release asset** (`evidence-assets` release — off the source
tree, on GitHub's CDN; `gh` can't push browser drag-drop attachments but `gh release upload` is
supported) and embeds it by URL. `--to-body` appends to the issue body (use for original BUG
evidence); default posts a comment (use for fix VERIFICATION proof). Do NOT commit PNGs to the repo.

**Verification loop (how a fix gets confirmed):** take a `mv <#> in-progress` card → fix it →
take an after screenshot → `tools/kanban.py evidence <#> after.png --caption "fixed: ..."` (comment)
→ `tools/kanban.py mv <#> needs-confirmation`. The USER reviews the posted picture and closes it
(`mv <#> done`) or reopens (`mv <#> reopened`). Use `in-review` while you're still verifying.

> **Fresh-session directive (user, 2026-06-18):** pick a card, FIX it, commit each verified fix
> yourself (3-repo chain: libultraship→fork/soh3d, Shipwright→fork/develop, outer→origin/main).
> Move the card to `in-review` while verifying, then `done` (close) or `needs-confirmation`. Capture
> new playtest bug reports as new issues (`tools/kanban.py add`). Verify the FULL user-facing path,
> not a narrow mechanism, before closing.
