# Oracle-driven parity workflow (SoH3D ↔ OoT3D)

The workflow that worked for title-screen parity. Reusable for ANY SoH3D↔OoT3D parity
work (a scene, an actor, a lighting pass). Distilled 2026-07-08 from a session where the
first three "bugs" turned out to be false alarms — the method below is what fixed that.

See also: **`docs/codemap.md`** (what subsystem you're closing a gap in),
**`docs/re-frontier.md`** (the ordered RE step this workflow is verifying — a step only becomes
`re-verified` there once it survives the matched-frame audit below), and **`docs/parity-map.md`**
(the CLOSED-CASES registry — when this workflow moves an item to parity, record a CLOSED-parity
row there so sweeps/loops don't re-examine it; check it FIRST so you don't re-audit a closed case).

## The one rule everything else serves
**Verify against the oracle at CONTENT-MATCHED frames before you trust a finding OR a fix.**
Static-only RE and eyeballed screenshots repeatedly produce confident-but-wrong claims
(three this session: a sky-color "divergence", a moon-halo "bug", and a whole wrong-asset
2D overlay — all retracted). If you can't compare SoH and the oracle at the *same content*,
your first task is to BUILD that comparison, not to guess a fix.

## Phase 0 — TOOLING FIRST (before any visual fix)
Build the deterministic content-matched A/B if it doesn't exist. For the title that's
`tools/title_ab.py` (harness embeds BOTH engines, steps each independently, matches by
image cross-correlation — NOT by frame number). Same-numbered frames are NOT same-content
(the two title clocks drift ~89 frames apart past step ~360). The tool must:
  1. establish + BAKE the verified frame correspondence (prove it, e.g. az360↔soh449),
  2. drive both engines to genuinely matched content,
  3. emit a SxS + a match-confidence score, and
  4. return an honest negative when content genuinely can't match (that itself is a finding).
Without this, skip to nothing — you'll just generate plausible-but-wrong work.

## Phase 1 — AUDIT at matched frames
Enumerate real divergences quantitatively at matched frames. Rank by severity. A divergence
only counts if it survives a genuine content-match. Persist the ranked list to
`debug_journal/`. Re-measure if the matching tool later improves (this session's first audit
used mismatched frames and had to be superseded).

## Phase 2 — RE each divergence to GROUND TRUTH (the oot3d-decomp, not memory pokes)
For each real gap, extend the OoT3D decomp until it covers the behavior; derive the correct
value/behavior from the 3DS binary (decomp-port / ghidra-re skills), NOT from guessed SoH
struct offsets (SoH is 64-bit; N64 offset comments are wrong past ~0x74). Record in
`oot3d-decomp/docs/`. **Ghidra derives; the running game only locates** (user directive
2026-07-09): constants/behavior come from static decompilation, never from measuring the
live oracle — dynamic observation (harness watchpoints, dump diffs) is permitted solely to
find the writer PC / struct address that static xrefs missed, after which you return to
Ghidra and derive the mechanism from code. **"It's an asset difference" is NOT a terminal answer** — SoH already
renders 3DS assets from the ROM, so an asset-rooted gap means "port that exact 3DS asset."

**A separate, complementary RE track — CONTROL/DEBUG tooling on the N64-side decomp** (the
`Shipwright/soh/src/`/`2ship/` code SoH vendors in-tree, NOT the 3DS ground-truth decomp
above): `docs/re_control_debug_backlog.md` tracks unnamed/poorly-understood N64-decomp functions
and fields whose further RE would unlock a better FORCE-state primitive or a cleaner debug readout
for the sweeps, instead of the current bypass-the-gate Force* hooks. Consult it before re-deriving
a sweep control/debug gap; add rows when a sweep session hits a fresh one.

## Phase 3 — FIX, and honor proven-negatives
Root-cause, never bandaid. If RE proves the "divergence" isn't a bug (this session: terrain
"3× dark" back-solved to a title-clock phase offset, byte-exact to ROM), REPORT THE
PROVEN-NEGATIVE and make no change. Refusing a magic-constant fit IS the correct outcome.

## Phase 4 — VERIFY the fix at matched frames, then LAND it
Rebuild, re-run the A/B, show before/after numbers. Then FAST-FORWARD `main` in the real
checkout and rebuild it there — a fix stranded on a worktree branch does the user no good
(`git merge --ff-only <branch>` in the main checkout, then `cmake --build ... -j4`, then push).

## Build the 3DS thing as its OWN module — don't patch the N64 path
When a subsystem needs real work (the title), rebuild it as a cohesive, first-class module
(`behaviors/<area>/*.cpp`, OOP, one owner) driven from ported 3DS data — do NOT keep bolting
`gZelda3dInTitleDemo`-gated overrides onto the N64 path. Symptom-patching scattered across a
file is the failure mode; a single owner with one per-frame resolved state is the fix.

## Agent orchestration (what actually held up)
- **The main context only GUIDES — all work goes to sonnet subagents.** (user directive
  2026-07-09) The orchestrator reads handoffs/journals, decomposes, prompts agents with the
  needed project context (headless env vars, scratch/ rule, evidence rules), and synthesizes/
  commits results; it does not run builds, verification, RE, or fixes inline. Spawn as many
  sonnet agents as useful.
- **Fan out RE/spec/decomp agents freely** (Ghidra + docs, no soh build → no resource contention).
- **ONE soh build at a time.** This is a 16GB-RAM machine: `-j$(nproc)` or concurrent cold
  builds OOM, orphan their `cc1plus` children, and cascade-kill each other. Cap `-j4`; check
  `free -h` first; clear orphans with the safe-kill skill if starved. Do NOT give each fix its
  own isolated cold-build worktree — **consolidate fixes into one build**. `tools/zelda3d_game.sh`
  honors `ZELDA3D_SOH=<dir>/soh.elf` so one build serves all verification.
- **Keep a perpetual decomp stream running** (RE → port to `oot3d-decomp`) alongside the parity
  loop — it advances a primary goal and never touches the build queue.
- **Headless always**: `ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh` (NOT the stale `SOH3D_HEADLESS`,
  which silently opens a real window on `:0`); harness uses `SOH3D_HARNESS_HEADLESS=1`.
- **Keep notes honest**: retract falsified findings in place (this session has explicit
  RETRACTION/SUPERSEDED docs). A confidently-wrong note sends the next session down a dead end.

## Oracle data cache — warm once, reuse across sessions

The embedded-Azahar oracle's output at a given az (Azahar) title-cs frame is fully
deterministic given three inputs: the loaded savestate, the ROM bytes, and whatever the
`soh3d_harness` Azahar patches (`tools/soh3d_harness/AZAHAR_PATCH.md`) do to rendering.
Held fixed, re-running Az to frame N always reproduces the same pixels — so repeated A/B
and probe runs (`tools/title_ab.py`, future probes) shouldn't pay the Az boot+step cost
again for a frame already captured in a prior session.

- **Cache**: `scratch/oracle_cache/<key>/` (gitignored — contains ROM-derived frame data,
  never committed). `<key>` = `sha256(savestate)[:16]_sha256(rom)[:16]_<patch-marker>`
  (`harness_ctl.cache_key()`); the patch marker is derived from `AZAHAR_PATCH.md`'s
  heading list, so editing a patch mints a fresh key instead of silently serving stale
  frames. Frames stored as PNG; each context has an `index.json` recording the full key
  metadata (savestate/ROM paths+hashes, patch marker) for auditability.
- **API**: `harness_ctl.OracleCache` — `get_frame`/`put_frame` (by az frame number),
  `get_probe`/`put_probe` (by probe name + az frame + args, for deterministic structured
  probes like camera eye/at, `az_daytime`, `az_fog`, `vsuni_log`). `stats()`/`invalidate()`
  for housekeeping.
- **CLI**: `tools/oracle_cache.py stats|warm [frames...]|invalidate`. `warm` with no args
  pre-captures the standard title sweep points ({100,200,360,500,700,764,1000,1300,1522,
  1700,1900}) in one harness session.
- **title_ab.py `ab`** is cache-aware: a cache hit on the target az frame skips the `run
  <az>` stepping loop entirely and reuses the stored PNG for the oracle side; the SoH side
  is NEVER cached (it changes every build) and always runs live via `soh_step`. Reports
  "oracle: cache hit" or "oracle: live run (cached now)" so a caller can see which path ran.
- **Invalidate** whenever the savestate, ROM, or an `AZAHAR_PATCH.md` entry changes — the
  key naturally rotates, so a stale cache just sits unused rather than serving wrong data;
  run `invalidate` to reclaim the disk space it would otherwise occupy.
- **soh3d_harness is single-instance** (PID-locked) — the frame cache does not change
  that; `warm`/`ab` cache-miss paths still need exclusive access to the harness process.

## Link (on-foot) state-matrix sweep — `tools/link_sweep.py`

Reusable for the ZELDA3D_LINK on-foot Link body specifically (locomotion + discrete actions).
Don't re-derive a Link state matrix or a fresh oracle transport — start here.

- **Tool**: `tools/link_sweep.py sweep [--skip-oracle] [--only a,b,c]` drives Link through a
  full state matrix in BOTH engines and writes `docs/link_parity_checklist.md`
  (auto-generated — never hand-edit it; edit `STATE_MATRIX` in the tool instead). Raw
  per-run JSON: `scratch/link_sweep/<ts>.json` + `latest.json` (gitignored, diffable).
  `show <state>` / `list [--status]` / `resolve <state> --commit <hash>` round out the CLI.
- **Composes, does not reimplement**: `parity_state_sweep.py` (discrete forced-state CSAB
  selection vs oot3d-decomp ground truth) and `parity_speed_sweep.py` (locomotion continuum
  by speedXZ, `classify()`/`windows_overlap()` reused verbatim) supply the per-dimension
  drive/verdict logic; `link_sweep.py` only orchestrates + adds states those tools don't
  cover + writes the checklist.
- **Oracle transport is the EMBEDDED harness, not `azahar_rpc.py`**: `parity_state_sweep.py`
  /`parity_speed_sweep.py`/`oracle_link_pose.py`/`oracle_link_animid.py` all default to the
  external `azahar_rpc.py` oracle, which needs a standalone Qt-frontend Azahar binary
  (`Azahar/build/bin/Release/azahar`) — this fork only builds that frontend with
  `ENABLE_QT=ON`, and Qt6 is **not installed** on this machine (confirmed 2026-07-15; no
  `qmake6`, no `Qt6` pkg-config modules). `link_sweep.py`'s `OracleSession` instead drives
  the ALREADY-BUILT embedded harness (`Azahar/build-harness`, target `soh3d_harness`, via
  `harness_ctl.py`) — the CLAUDE.md-blessed direction anyway. It reads Link's selected
  animation through a new REPL command, **`az_linkanim`** (added to
  `tools/soh3d_harness/main.cpp` this session), at the same `PLAYER+0x254+0x30` offset
  `oracle_link_animid.py` documents, so both oracle transports name selections identically
  against `oot3d-decomp/tools/skeldata/player_animid_names.json`. If a future session gets
  Qt6 installed and rebuilds the RPC oracle, the external tools become usable again — no
  further plumbing needed on that side.
- **State matrix is honestly bounded by drivable recipes.** States with no existing
  REPL/oracle input recipe (backwalk, sidestep, turn-in-place, combo, item/bottle use,
  throw, climb traversal, dive, Z-target, get-item pose, death) are recorded
  `UNREACHABLE` with a concrete reason — never guessed into a verdict. Extend
  `STATE_MATRIX` (and the underlying REPL primitive) before trying to force a verdict for
  one of these.

## The loop, in one line
tooling → audit@matched-frames → RE-to-ground-truth → fix-or-proven-negative → verify@matched-frames
→ land-on-main → next; decomp stream always running; one build at a time; build the module, not a patch.
