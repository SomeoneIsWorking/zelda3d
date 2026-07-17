# Backlog → the local kanban in `KANBAN.md`

The backlog is a **local markdown board: [`KANBAN.md`](KANBAN.md)**. It IS the source of truth —
no GitHub Issues (dropped 2026-07-17; screenshot attachment there was awkward, and a local board is
easier to keep with the code). Edit `KANBAN.md` directly with normal file tools.

- **Columns:** `todo · in-progress · in-review · needs-confirmation · blocked · done`.
- **Add a card:** append `- [#N] <title> — <notes>` under `todo`.
- **Move a card:** move its line under the next column heading.
- **Screenshots:** attach in chat, or drop under `scratch/kanban/` (gitignored) and link it. Don't
  commit PNGs.
- **Rule:** the board is for USER-DRIVEN work only — agent sweeps fix-in-session + journal, they
  don't create cards. See `CLAUDE.md` "The backlog is a LOCAL kanban".
