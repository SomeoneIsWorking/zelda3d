# SoH3D — agent instructions

SoH3D = Ship of Harkinian rendering OoT3D (3DS) models/world instead of N64. See `README`/notes for
the project itself; this file is the working contract for agents.

## Orient here FIRST — parity-map + codemap + RE frontier

Start every task by checking these THREE maps: skip closed cases (parity-map), find the code
(codemap), pick the next RE step (re-frontier).

- **`docs/parity-map.md`** — the CLOSED-CASES registry: what is CONFIRMED AT PARITY with the
  oracle. **A CLOSED item is NOT to be re-examined by sweeps/loops — it reopens ONLY on explicit
  user request or a confirmed regression.** This is the enforcement mechanism for "mark verified,
  don't revisit"; several lighting/terrain rows are OFF-LIMITS to tuning by user instruction.
- **`docs/codemap.md`** — subsystem-keyed map of what's where, what's done, what's missing. Find
  the subsystem before touching it; update the row in the same commit that changes it. Governed by
  `tools/codemap.py check`.
- **`docs/re-frontier.md`** — the ordered RE dependency chain: which behavior is real
  reverse-engineering (ground truth from the ROM) vs a `⛔ hack` standing in for it, and the next
  RE-ready step (`tools/re_frontier.py next`). This is where the "decomp is ground truth" rule
  (below) gets tracked concretely, per-arc.
- Together with `docs/parity-workflow.md` (the method for moving an item OPEN→CLOSED) and the
  kanban (user-driven work items), these form one system: parity-map = what's closed,
  codemap = what exists, re-frontier = RE progress, parity-workflow = method, kanban = work items.

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
- **HARD RULE — kanban is for USER-DRIVEN work ONLY.** Kanban holds requests the user
  has personally made or parity issues the user has personally reported. **Agent-run
  parity sweeps do NOT produce kanban cards.** When a sweep uncovers a divergence
  (wrong CMB, missing behavior, N64 fallback, whatever), fix it in-session — close-test
  + fix + commit — and record the finding in `debug_journal/` for the durable trail.
  A backlog of sweep-discovered gaps is a workflow smell: it lets parity work accumulate
  as a to-do list instead of being closed in the session that found it. If the finding
  is genuinely beyond in-session scope, note it in the journal and continue the loop
  in the same session (or start a fresh context with a handoff brief). Do not file.
  (User directive 2026-07-02 — cards #135-#143 were misfiled sweep output and cleaned up.)
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

## RULE: structure SoH3D like a real PC game — per-behavior modules, OOP, NOT one giant soh3d.c

Treat SoH3D as a brand-new PC game that needs proper structure. Do **NOT** keep cramming logic into
`soh3d.c` (it is already a multi-thousand-line dumping ground). When we RE/decomp an OoT3D behavior and
port it, it goes into a **dedicated, well-named module** under a game-like tree, e.g.:

```
soh/src/soh3d/behaviors/actor/kokiri_kid.cpp   // En_Ko head/torso track + facial, ported from OoT3D
soh/src/soh3d/behaviors/actor/<actor>.cpp      // one module per actor behavior
soh/src/soh3d/behaviors/actor_behavior.h       // base interface + registry (dispatch by actor id)
```

- **Use OOP** where it fits: a base `ActorBehavior` (virtuals like `applyDrawOverrides`), concrete
  subclasses per actor, a registry that dispatches by `actor->id`. Prefer C++ classes over C-struct
  vtables when the headers cooperate.
- **The port carries the structure.** When a divergence needs RE, the deliverable is: (1) decomp it,
  (2) document the ground truth in `oot3d-decomp/docs/`, (3) port it into a properly-structured SoH3D
  module — not a patch bolted onto `soh3d.c`.
- **Restructuring existing code into this shape is welcome**, incrementally: each time you touch a
  behavior, migrate it out of `soh3d.c` / monolithic files into its module. Don't regress working
  behavior; fall through to legacy for not-yet-migrated actors. (user directive, 2026-06-25, hard rule)

## RULE: ground truth for any behavioral divergence is the OoT3D DECOMP — extend it, don't memory-poke

OoT3D decomp (the private `oot3d-decomp` repo, fed by the **decomp-port** skill / Ghidra pipeline) is
vendored **in-repo as a git submodule at `oot3d-decomp/`** (remote `SomeoneIsWorking/oot3d-decomp`,
added 2026-07-15). The same pattern applies to **MM (Majora's Mask 3D) decomp**, vendored at
`mm3d-decomp/` (remote `SomeoneIsWorking/mm3d-decomp`, added 2026-07-15) — both are the SAME
deliberate exception to the "no submodules" flatten (see "Commit chain" below): the engine itself
stays flattened to kill multi-repo build friction, but `oot3d-decomp`/`mm3d-decomp` are read-mostly
external reference repos, not part of the soh3d build, so a submodule is the right shape for both.
Update either like any submodule: `cd oot3d-decomp && git pull origin main && cd .. && git add
oot3d-decomp && git commit` (same for `mm3d-decomp`). All soh3d tooling (`tools/parity_ab.py`,
`oracle_cache.py`, `link_sweep.py`, etc.) resolves the decomp path repo-relatively
(`REPO/oot3d-decomp`), never via a hardcoded home path — do not reintroduce
`os.path.expanduser("<oot3d-decomp>")` or a sibling-repo (`../oot3d-decomp`) assumption; the
same rule applies to any future `mm3d-decomp` reference. As of 2026-07-15 there are zero code
references to `mm3d-decomp` in the repo (MM work is early/native) — if/when tooling needs it, resolve
it the same repo-relative way. OoT3D decomp is a
**primary project goal and a prerequisite for full parity** — not a side quest. So when you find a
behavioral difference (an actor moving/posing/animating wrong vs OoT3D), the correct response is to
**extend the OoT3D decomp until it covers that behavior**, derive the ground truth from the 3DS binary,
and port THAT faithfully. (user directive, 2026-06-25, hard instruction)

- **Do NOT reverse-engineer the behavior by poking SoH's N64-struct memory by raw byte offset.** SoH is
  a 64-bit build; the N64 struct-offset *comments* (`z_*.h`) do NOT match the real runtime layout (8-byte
  pointers shift everything past the first pointer), so `apeek <n64off>`-style raw reads return GARBAGE
  past ~0x74 and any "fix" built on them is luck, not engineering. Read fields through the C struct, and
  establish what the behavior SHOULD be from the OoT3D decomp — never from guessed SoH offsets.
- The decomp is the source of truth; SoH observation only confirms the *port matches it*. If the decomp
  doesn't yet cover the function you need, decompiling it IS the task (it advances the primary goal too).
- Record each newly-decompiled behavior in `oot3d-decomp/docs/` (addresses + derived C), then port.

## RULE: every bugfix STARTS by proving the tooling can investigate it

Before touching a fix, confirm you can **reliably drive the game to the failing situation and observe
it** — reproduce the state on demand, hold it still, frame it, and read back the relevant engine
values. If you can't, your first task is to BUILD/extend that tooling, not to guess at a fix. A fix
attempted on top of flaky control produces "evidence" that's really just luck (e.g. #5: identical
before/after shots because the cucco was never reliably posed/observed). "If you can't control the
game reliably, you shouldn't be working on the bug fix." (user directive, 2026-06-20)

- Prefer **GENERIC, reusable** control primitives over one-off per-bug hacks. The generic actor
  surface in `soh3d.c` REPL is the model: `asel <id|any> [n]` (select nth-nearest live actor),
  `afreeze <0|1>` (pin its transform — no wander/hop/AI drift), `apos/arot/aparams` (set
  transform/params), `acam [dist] [axis]` (auto-frame it as a side profile — no coordinate
  guessing), `ainfo` (dump pos/rot/params/velocity). These work on ANY actor. Build bug-specific
  controls only for state with no generic form (e.g. the cucco wing state machine: `cuccostate`,
  `flapinfo`). Driven per-frame from `SoH3D_ActorPostUpdate` in `Actor_UpdateAll`.

## Direction: build a direct harness that EMBEDS Azahar as a library, not runs it

Current Azahar tooling (`tools/azahar_rpc.py`, `tools/azahar_repl.py`, `tools/azahar_scan.py`) treats
Azahar as an external GUI process to be launched and poked. The durable direction is different: build
a harness that **embeds Azahar's core** — link its emulator library into a headless C++ program that
loads the ROM, drives to a target scene deterministically, and reads actor tables / object lists /
memory ranges directly. No window, no manual warp, no IPC race.

Capabilities the harness should ship:

- Load OoT3D ROM into embedded Azahar core, boot to a specific scene/room deterministically
  (scripted warp, no input driving).
- Enumerate live objects/actors in the current room: print each entry's actorId, params, pos, rot,
  plus a hex dump of `actor+0x00..0x1F0` for the ones we care about.
- Compare SIDE-BY-SIDE with SoH3D's state at the same scene: for every actor SoH3D spawns, the
  harness shows the corresponding OoT3D entry (or "not present" / "mismatched"). This is the direct
  divergence surface — no more RPC round-trips, no more "is Azahar running."
- Same shape for scene-object tables, collision, room metadata — whatever the current parity
  investigation needs.

**Rule:** when the current investigation's next observation would come from Azahar, the answer is
"drive the direct harness" — not "start Azahar externally." Extend the harness (new dump routine,
new comparison field, new scene warp) if it doesn't cover the observable yet. This is the same
workflow-first principle as the existing "build the tool if you can't investigate" rule (§91), now
applied to the OoT3D reference side.

Approach the first cut incrementally: (a) audit whether Azahar exposes a library / core API or is
GUI-only (skim `Azahar/src/core/` for entry points that don't touch the window layer); (b) if
library-usable, land a minimal C++ program that loads a ROM and prints "boot succeeded" — commit
that as the harness scaffold; (c) add scene warp; (d) add actor table dump; (e) wire the side-by-side
against SoH3D. Each step commits + gets close-tested.

## Verify the FULL user-facing path, not a narrow mechanism

A card is only fixed when the real user-facing behavior works in a realistic run — not when a
frozen-cam / forced-state / single-frame harness passes. Prior "VERIFIED headless" marks were
repeatedly falsified by playtest. Run the live game (skill **soh3d-game-control**), exercise the
actual path, and capture the evidence above.

## Architecture docs (read before re-investigating)

- **`docs/lus_input_architecture.md`** — libultraship's `Ship::` (generic framework, `src/ship/`) vs
  `LUS::` (concrete N64 impl, `src/libultraship/`) split (NOT duplicate trees — base/derived by design),
  the per-frame physical→N64-pad input path, where button-mapping classes live, and the chord/modifier
  design for #32. Read this instead of re-deriving the controller code.

## Commit chain — ONE repo now (commit each verified fix yourself, reference the issue `#<n>`)

The former 3-level submodule chain (soh3d → Shipwright → {libultraship, ZAPDTR, OTRExporter}) was
**flattened into this single repo** (2026-06-22): the engine is vendored as plain directories, there
are **no submodules**, and everything commits + pushes to **`origin/main`** in one shot. Edit
`Shipwright/` and `Shipwright/libultraship/` freely in-tree (renderer, input mapping, windowing — put
the fix in the layer it belongs to). No more `fork/develop` / `fork/soh3d` / per-submodule pushes; the
old history still lives on those fork remotes if ever needed. `Azahar/` (the oracle) is NOT part of
this repo — it's gitignored. ROMs (`*.z64`), archives (`*.o2r`/`*.otr`) and `build-cmake/` stay
gitignored — never commit them.

## Hard rules

- **Headless always:** this is a Wayland machine — launch the game with **`ZELDA3D_HEADLESS=1`**,
  never a headed window. NOTE: `SOH3D_HEADLESS=1` is STALE (renamed in the SoH3D→Zelda3D refactor)
  and is SILENTLY IGNORED → real window on `DISPLAY=:0` (bit multiple agents 2026-07-08). The
  embedded-Azahar oracle harness is windowless and uses `SOH3D_HARNESS_HEADLESS=1` (separate var).
- Run the game via `tools/zelda3d_game.sh` (formerly `soh3d_game.sh`); REPL via
  `tools/zelda3d_repl.py` (skill soh3d-game-control). Assume every stale `soh3d_*`/`SOH3D_*`
  reference in older notes maps to `zelda3d_*`/`ZELDA3D_*`.
- Scratch/build artifacts go in the gitignored `scratch/`, never `/tmp`, never committed.
- Verify quantitatively/visually; send screenshots for any UI/UX call.
