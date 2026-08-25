# SoH3D — agent instructions

SoH3D = Ship of Harkinian rendering OoT3D (3DS) models/world instead of N64. See `README`/notes for
the project itself; this file is the working contract for agents.

**Project structure & naming: `docs/project-structure.md` is the canonical map** — the two-tier
taxonomy (**zelda** = N64 base engines: **soh** / **2ship**; **zelda3d** = the 3DS render layer built
on them: **soh3d** / **2ship3d**), where each part's code lives, and the human-facing-name ↔ embedded-
code-name aliases (`2ship`=vendored `2s2h`, `2ship3d`=`mm3d_*`; `zelda3d` is both the umbrella and the
literal `Zelda3D_*`/`src/zelda3d/` prefix shared by both branches). Use those terms consistently.

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

## The backlog is a LOCAL kanban in `KANBAN.md` — USE IT

The backlog is a **local markdown board, `KANBAN.md`** — it IS the source of truth (no GitHub
Issues; GitHub made screenshot attachment awkward, user directive 2026-07-17). `BACKLOG.md` is just a
pointer. Edit `KANBAN.md` directly with the normal file tools — there is no `kanban.py` / `gh` round-trip.

- **Find work:** read `KANBAN.md` (cards grouped under column headings).
- **Columns:** `todo · in-progress · in-review · needs-confirmation · blocked · done`.
- **Work a card:** move its line under the next column heading — `todo` → `in-progress` → `in-review`
  (while verifying) → `needs-confirmation` (fix shipped, awaiting user). The user confirms user-visible
  fixes (move to `done` / delete it); don't self-close them. Move back to an earlier column if it regressed.
- **Add a card:** append `- [#N] <title> — <notes>` under `todo` (N = next simple id). Capture user
  reports immediately.
- **HARD RULE — kanban is for USER-DRIVEN work ONLY.** It holds requests the user has personally made
  or parity issues the user has personally reported. **Agent-run parity sweeps do NOT produce cards.**
  When a sweep uncovers a divergence (wrong CMB, missing behavior, N64 fallback, whatever), fix it
  in-session — close-test + fix + commit — and record the finding in `debug_journal/`. A backlog of
  sweep-discovered gaps is a workflow smell. If a finding is genuinely beyond in-session scope, note it
  in the journal and continue the loop. Do not file. (User directive 2026-07-02.)
- **Screenshots:** attach them in chat, or drop the file under `scratch/kanban/` (gitignored) and link
  it from the card. **Never commit PNGs to the repo.**

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

USER 2026-08-24: "Ideally a file should be max 1200 lines"

USER 2026-08-24: "And a file should be focused around a responsibility, grab-bag files are banned"

The 1,200-line ceiling and responsibility boundary are independent requirements. A smaller grab-bag
still fails the architecture rule: every file owns one cohesive concept and a narrow interface;
entry points and registries compose those owners without absorbing their implementations. Existing
oversized legacy files are frozen by the normal verifier and their caps ratchet downward whenever a
responsibility is extracted. Never raise a cap to land a change.

Current application of the Dusklight composition pattern: the embedded harness `main.cpp`,
`harness_cli.py`, launcher, Clang verifier, and MM phase-tour composer delegate to
responsibility-named modules. The SoH and MM REPLs now route to focused command, transport, framing,
lifecycle, and domain owners. Do not move extracted libretro/process/transport/gameplay/cache/
command/report/build-policy implementations back into entry points. The concrete ownership map and
remaining violations are maintained in `docs/project-structure.md` and `docs/codemap.md`.
The SDL3GPU model renderer follows the same pattern: its C ABI adapter composes focused resource,
pipeline, pass/diagnostic, lifecycle, and shader owners; do not fold those implementations back into
`zelda3d_sdl3gpu.cpp`.

Treat the zelda3d layer as a brand-new PC game that needs proper structure. Do **NOT** keep cramming
logic into `core/zelda3d.c` (the multi-thousand-line dumping ground). When we RE/decomp an OoT3D
behavior and port it, it goes into a **dedicated, well-named module** under a game-like tree, e.g.:

```
soh/src/zelda3d/behaviors/actor/kokiri_kid.cpp   // En_Ko head/torso track + facial, ported from OoT3D
soh/src/zelda3d/behaviors/actor/<actor>.cpp      // one module per actor behavior
soh/src/zelda3d/behaviors/actor_behavior.h       // base interface + registry (dispatch by actor id)
```

(The 2ship3d layer mirrors this under `mm/2s2h/zelda3d/` — see `docs/project-structure.md`.)

- **Use OOP** where it fits: a base `ActorBehavior` (virtuals like `applyDrawOverrides`), concrete
  subclasses per actor, a registry that dispatches by `actor->id`. Prefer C++ classes over C-struct
  vtables when the headers cooperate.
- **The port carries the structure.** When a divergence needs RE, the deliverable is: (1) decomp it,
  (2) document the ground truth in `oot3d-decomp/docs/`, (3) port it into a properly-structured zelda3d
  module — not a patch bolted onto `core/zelda3d.c`.
- **Restructuring existing code into this shape is welcome**, incrementally: each time you touch a
  behavior, migrate it out of `core/zelda3d.c` / monolithic files into its module. Don't regress working
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
oot3d-decomp && git commit` (same for `mm3d-decomp`). All soh3d tooling (`tools/oracle_cache.py`,
`link_sweep.py`, etc.) resolves the decomp path repo-relatively
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
  `flapinfo`). Driven per-frame from `Zelda3D_ActorPostUpdate` in `Actor_UpdateAll`.

## The OoT3D oracle IS the embedded harness — there is no external Azahar

`tools/soh3d_harness` links Azahar's core as a **library** into a headless C++ program that loads the
ROM, warps deterministically, reads actor tables / memory / framebuffers, and runs SoH3D side-by-side
in the same process. Build/run it with executable `tools/soh3d_harness.py`; drive it with
`tools/harness_cli.py` (`boot_to_gameplay()` in `harness_gameplay.py` is the entry point — it
loadstates a gameplay save and warps, with no input driving). `oot3d-decomp/docs/oracle.md`
documents the REPL surface.

**Rule:** when the next observation would come from OoT3D, drive the harness — and if it doesn't
cover the observable yet, EXTEND it (new dump routine, new comparison field). Same workflow-first
principle as "build the tool if you can't investigate", applied to the reference side.

Gate readiness on `gameplay` (ok yes|no), never on `playstate`: `playstate` deliberately falls back
to the title demo's PlayState so introspection works there, so it answers ok on the title screen.
`warp` needs a loaded save — it is inert at the title, where nothing can spawn.

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

**Exception — genuinely third-party libs are NOT vendored flat; they are fork/upstream submodules.**
Our own engine code (soh/2ship/libultraship) stays flattened as above, but third-party dependencies do
NOT live in-tree. Two are GitHub forks (patched, so a fork carries the patch; each on a `zelda3d`
branch byte-identical to the old vendored tree so the build is unchanged):
- `SomeoneIsWorking/StormLib` (fork of ladislav-zezula/StormLib v9.25 + free-space optimization) →
  submodule `Shipwright/libultraship/extern/StormLib`.
- `SomeoneIsWorking/ZAPDTR` (fork of HarbourMasters/ZAPDTR @ #36 base + the ZTextMM MM message-table
  extraction + a ZRom narrowing fix) → submodule `Shipwright/ZAPDTR`. **libgfxd + tinyxml2 live INSIDE
  ZAPDTR** (`ZAPDTR/lib/…`, as upstream vendors them) — so they're out of the zelda3d repo via this one
  submodule, no separate entries.

Update a fork like any submodule (edit on its `zelda3d` branch, push, bump the pointer here). The
CMake-`FetchContent` deps (rmlui/prism/dr_libs/monocypher/libgfxd-in-libultraship/…) already point at
their real upstreams. (User directive 2026-07-17: third-party code is a fork/upstream submodule, not
flattened. The old flat copies were also purged from git history to shrink the repo.)

## Context is a budget — spend it on answers, not on paging

A long arc dies of context exhaustion long before it runs out of work, and the failure mode is not
"I stopped": it is reaching for cheap probes that cannot distinguish the answers, and being confidently
wrong. Three such errors landed in one session (2026-07-28) — two draw-order claims and a "the harness
isn't built" that was a `find -maxdepth 3` failing to reach a depth-4 path. Rigor is the fix for those;
these rules are how you keep enough budget to afford rigor.

- **DELEGATE surveys to subagents.** "Find every call site of X and what colour it sets", "which of
  these 12 files touches Y" — a subagent reads the 7,000-line file and hands back twenty lines. This
  is the single biggest lever; use it before the others. (Standing authorization: [[soh3d-subagents-authorized]].)
- **MEASURE instead of looking.** A screenshot costs about a hundred lines of source, and a pixel
  measurement is usually the better evidence anyway: "960 green px at identical extents" and "349 vs
  350 bright px in the same bbox" settled two HUD elements more convincingly than any render could,
  and a colour mask over two separately-captured frames once produced a bogus mismatch that was
  *grass moving between captures*. Read an image only when the question is genuinely visual and no
  measurement can settle it — a layout judgement, a "does this look right" call, or evidence for the
  user. This is the existing verify-quantitatively rule extended to its context cost.
- **Ask `tools/codequery.py` for a SYMBOL, never `sed -n` for a line range.** `outline <file>` /
  `slice <file> <fn>` / `def` / `callers` / `find` return what you asked for instead of a guessed
  window that overshoots and gets re-issued. Guessing ranges in `z_parameter.c` (7,200 lines) was the
  second-largest context sink of the HUD arc.
- **Keep build/run output on a leash** — `| tail -3` plus a `grep -E "error:"`, never a raw dump.
- **Start a big arc in a fresh session.** #205 grew from "port the HUD" into a renderer change, a new
  Gfx opcode and a texture decoder; by pass 3 it wanted a clean session and would have been done
  faster in one. If an arc has already spawned two sub-arcs, that is the signal.
- **Durable state is what makes running out survivable, and it works** — commits, `docs/issues/`,
  the frontier and the codemap carried every finding of that session across the thin patch. Write the
  finding down when you get it, not when you have room.

## Hard rules

- **Headless always:** this is a Wayland machine — never open a headed window.
  `tools/zelda3d_game.sh` now defaults to headless (`ZELDA3D_HEADLESS=0` is the explicit headed
  opt-out; the user's own headed path is `./run.sh`). NOTE: `SOH3D_HEADLESS=1` is STALE (renamed
  in the SoH3D→Zelda3D refactor) and is SILENTLY IGNORED (bit multiple agents 2026-07-08). The
  embedded-Azahar oracle harness is windowless and uses `SOH3D_HARNESS_HEADLESS=1` (separate var).
- Run the game via `tools/zelda3d_game.sh` (formerly `soh3d_game.sh`); REPL via
  `tools/zelda3d_repl.py` (skill soh3d-game-control). Assume every stale `soh3d_*`/`SOH3D_*`
  reference in older notes maps to `zelda3d_*`/`ZELDA3D_*`.
- Scratch/build artifacts go in the gitignored `scratch/`, never `/tmp`, never committed.
- Verify quantitatively/visually; send screenshots for any UI/UX call.
