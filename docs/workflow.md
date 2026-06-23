# SoH3D working workflow (the loop)

Durable record of the working loop so it survives a lost session / API-error crash.
Two parallel tracks. Track A is the primary parity loop; Track B is everything else.

## Track A — OoT3D ↔ SoH3D parity loop (primary)

A cycle. Each pass picks a behavior/asset where SoH3D's render or logic diverges from
OoT3D ground truth, and drives it to parity. Repeat.

1. **OoT3D decomp** — reverse-engineer the relevant OoT3D behavior from the 3DS binary
   (`SomeoneIsWorking/oot3d-decomp`, Ghidra headless pipeline / `decomp-port` skill).
   OoT3D is ground truth, NOT N64. Record RE findings in the decomp repo's docs.

2. **OoT3D ↔ SoH3D parity checks** — compare live SoH3D against the OoT3D oracle (Azahar,
   headless RPC RAM read/write + screenshot/input). Identify and quantify the divergence
   (pose/anim/material/geometry/timing). Measure, don't eyeball.

3. **OoT3D tooling** — when a check can't be reliably reproduced/observed, BUILD or extend
   the generic tooling first (Azahar oracle scripts, SoH3D REPL actor-control primitives,
   QA scanners). Tooling-first, before guessing at a fix.

4. **OoT3D ↔ SoH3D parity fixes** — port the OoT3D behavior into SoH3D (literal port over
   CSAB swap where directed), fix the divergence at root cause, post evidence to the kanban
   card, advance to needs-confirmation, let user confirm.

Then loop back to 1 for the next divergence.

## Track B — non-parity tasks

Everything that is NOT OoT3D ↔ SoH3D parity work: engine/renderer infra (Vulkan port,
shadows/AO, RmlUi menu), input/HUD (glyph hotswap, controller mapping), windowing/crash
fixes, build/repo hygiene, texture pack, etc. Driven off the same kanban board.

## Source of truth

- Backlog/board: GitHub Issues + `tools/kanban.py` (offline mirror `KANBAN.md`).
- Every fix posts evidence before leaving `in-progress` (see project `CLAUDE.md`).
- OoT3D is ground truth, not N64.
