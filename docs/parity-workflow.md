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
  honors `ZELDA3D_SOH=<dir>/zelda3d` (the single launcher binary, run as `zelda3d oot`) so one build
  serves all verification.
- **Keep a perpetual decomp stream running** (RE → port to `oot3d-decomp`) alongside the parity
  loop — it advances a primary goal and never touches the build queue.
- **Headless always**: `ZELDA3D_HEADLESS=1 tools/zelda3d_game.sh` (NOT the stale `SOH3D_HEADLESS`,
  which silently opens a real window on `:0`); harness uses `SOH3D_HARNESS_HEADLESS=1`.
- **Keep notes honest**: retract falsified findings in place (this session has explicit
  RETRACTION/SUPERSEDED docs). A confidently-wrong note sends the next session down a dead end.

## Oracle data cache — warm once, reuse across sessions

The embedded-Azahar oracle's output at a given az (Azahar) title-cs frame is fully
deterministic given four inputs: the loaded savestate, the ROM bytes, the resolved hi-res texture
pack, and the declared render-affecting Azahar contract
(`tools/soh3d_harness/AZAHAR_RENDER_CONTRACT`).
Held fixed, re-running Az to frame N always reproduces the same pixels — so repeated A/B
and probe runs (`tools/title_ab.py`, future probes) shouldn't pay the Az boot+step cost
again for a frame already captured in a prior session.

- **Cache**: `scratch/oracle_cache/<key>/` (gitignored — contains ROM-derived frame data,
  never committed). `<key>` = `sha256(savestate)[:16]_sha256(rom)[:16]_<patch-marker>`
  plus a texture-pack manifest marker (`harness_cache.cache_key()`). The cache resolves `.env`
  before hashing the ROM, matching the child launcher instead of silently producing a `norom`
  context. The patch marker is the explicit render contract; descriptive patch-document changes
  do not invalidate visual evidence. The pack marker records its resolved on/off state and hashes
  every relative filename, size, and modification timestamp.
  Editing either input mints a fresh key instead of silently serving stale frames. Frames are
  stored as PNG; each context has an `index.json` recording the full key metadata for auditability.
- **API**: `harness_cache.OracleCache` — `get_frame`/`put_frame` (by az frame number),
  `get_probe`/`put_probe` (by probe name + az frame + args, for deterministic structured
  probes like camera eye/at, `az_daytime`, `az_fog`, `vsuni_log`). `stats()`/`invalidate()`
  for housekeeping. `get_artifact`/`put_artifact` cache raw logs, PPMs, and other binary
  capture outputs by a named setup plus sorted arguments; use these for scene probes whose
  result is larger than structured JSON.
- **CLI**: `tools/oracle_cache.py stats|warm [frames...]|import-artifact|invalidate`. `warm` with no args
  pre-captures the standard title sweep points ({100,200,360,500,700,764,1000,1300,1522,
  1700,1900}) in one harness session. If an Azahar patch change is provably observer-only,
  `adopt-frame <old-key> <frames...> --observer-only` reuses existing PNGs after checking the
  savestate, ROM, and texture-pack identities and records the source key/path; never rerun the
  oracle merely because a logger gained fields. `import-artifact <exact-key> <name> <source>
  --args-json '<variant object>'` attaches an existing raw state/log/image to the exact historical
  context that produced it; it refuses a missing context and conflicting bytes, so it cannot
  silently relabel old evidence as current. Do not use adoption for renderer changes.
- **title_ab.py `ab`** is cache-aware: a cache hit on the target az frame skips the `run
  <az>` stepping loop entirely and reuses the stored PNG for the oracle side; the SoH side
  is NEVER cached (it changes every build) and always runs live via `soh_step`. Reports
  "oracle: cache hit" or "oracle: live run (cached now)" so a caller can see which path ran.
- **`title_host_capture.py` is cache-only**: it resolves the RE'd title-cursor→oracle-frame
  mapping, refuses a missing frame before spawning the harness, advances only SoH naturally, and
  verifies the final live cursor before `soh_snapshot`. It pins vanilla 400×240 before cache-key
  construction, so a cached vanilla “3D” logo cannot be compared to a host 4K replacement. Use its
  `--unified-renderer` selector when auditing the optional unified CMB route; never infer the route
  from a screenshot.
- **`title_oracle_probe.py` caches exact-cursor draw evidence**: `uniforms <cs>` stores the complete
  software-renderer draw/uniform log and `fragments <cs> <draw>` stores the selected fragment stream
  plus its summary. Artifact identity includes capture version, cursor, oracle frame, renderer, and
  draw. Both commands return before spawning on a cache hit; analyze the stored artifact thereafter.
  Long advances are split into checked 25-frame commands and immutable 400-frame savestate
  checkpoints, so an interrupted probe resumes near its target instead of replaying the title.
- **Invalidate** only to reclaim space. Savestate, ROM, patch-contract, texture-pack mode, and
  texture-pack manifest changes rotate the key automatically, so stale contexts sit unused
  rather than serving wrong data.
- **soh3d_harness is single-instance** (PID-locked) — the frame cache does not change
  that; `warm`/`ab` cache-miss paths still need exclusive access to the harness process.
  The tracked `tools/oracle_draw_isolate.py` also caches a completed per-draw sweep (including
  its raw logs, base image, masks, and report) by entrance/time/probe settings before starting
  a new oracle instance.

## Hi-res texture pack — ONE switch, both sides (`ZELDA3D_HARNESS_TEXPACK`)

An A/B is only honest if both engines sample the same texels. The harness therefore governs
the hi-res pack for BOTH sides from a single control, and there is deliberately **no way to
get hi-res on one side and vanilla on the other**:

| `ZELDA3D_HARNESS_TEXPACK` | oracle (Azahar) | Zelda3D |
| --- | --- | --- |
| `on` (default) | `citra_custom_textures=enabled`, `LoadDir/textures` symlinked at the pack | `ZELDA3D_TEXPACK=<abs pack root>` |
| `off` | `citra_custom_textures=disabled` | `ZELDA3D_TEXPACK=off` |

- Pack root resolution is the same rule on both sides (`ZELDA3D_TEXPACK=<path>`, else
  `textures/` next to the repo root, else `<romdir>/textures`), resolved to an **absolute**
  path because `soh_boot` chdir()s. `ZELDA3D_TEXPACK=off|0|none` still means "off, both sides".
  One pack on disk, no copies: Azahar reaches it through a
  `scratch/harness/save/Azahar/load/textures` symlink.
- This is a **test-harness control**, not an N64-vs-3DS behaviour gate — the no-opt-out-gates
  rule is about game behaviour; keeping an A/B like-for-like is exactly what a harness switch
  is for. Use `off` when a comparison depends on stock texture content (e.g. the title
  wordmark/copyright that `tools/title_sbs_verify.py`'s `content_score` reads) — but set it
  for the whole run, so both sides move together.
- **Prove it, don't assume it**: the `texpack` REPL command prints both sides in one line —
  `ok texpack mode=on root=<abs> az=<files>/<materials> az_hits=<h>/<m> soh=<indexed>
  soh_hits=<h>/<m>`. Because Azahar loads replacements asynchronously (≤8 uploads per rendered
  frame; its synchronous path crashes, see `AZAHAR_PATCH.md` Patch 8), **step until the hit
  counters stop growing before capturing a frame**, or an early capture can show the oracle
  still vanilla while our side is already hi-res.
- **The harness switch does NOT cover the standalone game.** `tools/zelda3d_game.sh` sets no
  texpack env, so a game launched from the repo root (where `textures/` lives) is **hi-res** —
  and comparing that against oracle artifacts captured vanilla is the trap below.

### Worked example: an entire "renderer deficit" that was only this asymmetry

`render.zora-ground-deficit` sat on the RE frontier as an unexplained scene-wide 0.79/0.86
darkening of Zora's ground and walls, with three shading hypotheses queued behind it. It was
none of them: the oracle masks had been captured by a harness predating the "both sides" switch
(Azahar vanilla) while the comparison screenshots came from the standalone game (hi-res), and
the pack's Zora rock/ground art is ~20% darker than the ROM texels. Vanilla on both sides the
same draws measure 0.977 and 1.002. Two rules came out of it, both now enforced in code:

- `tools/oracle_draw_isolate.py` writes `texpack.txt` beside the masks, and
  `tools/tev_mask_ratio.py` **hard-fails on an asymmetry** (or on an unknown state) rather than
  printing a ratio. It also excludes our HUD by default — OoT3D's HUD is on the 3DS **bottom**
  screen, so the oracle's top-screen capture has none while ours is fully overlaid.
- A draw's isolation mask is *"pixels this draw changes"*, so a mask lying under a translucent
  layer **inherits that layer's error**. Zora's rock wall read 0.88 over its whole mask and
  1.002 over the pixels no other draw touches. Attribute residuals with
  `tev_mask_ratio.py … --exclusive` before believing a per-surface number.

## Link (on-foot) state-matrix sweep — `tools/link_sweep.py`

Reusable for the ZELDA3D_LINK on-foot Link body specifically (locomotion + discrete actions).
Don't re-derive a Link state matrix or a fresh oracle transport — start here.

- **Tool**: `tools/link_sweep.py sweep [--skip-oracle] [--only a,b,c]` drives Link through a
  full state matrix in BOTH engines and writes `docs/link_parity_checklist.md`
  (auto-generated — never hand-edit it; edit `STATE_MATRIX` in the tool instead). Raw
  per-run JSON: `scratch/link_sweep/<ts>.json` + `latest.json` (gitignored, diffable).
  `show <state>` / `list [--status]` / `resolve <state> --commit <hash>` round out the CLI.
- **Composes, does not reimplement**: `parity_state_sweep.py` (discrete forced-state CSAB
  selection vs oot3d-decomp ground truth) and `parity_speed_sweep.py` (SoH-side locomotion
  continuum by speedXZ, `classify()`/`windows_overlap()` reused verbatim) supply the per-dimension
  drive/verdict logic; `link_sweep.py` only orchestrates + adds states those tools don't
  cover + writes the checklist.
- **Oracle transport is the embedded harness** — the only one. `link_sweep.py`'s `OracleSession`
  drives `soh3d_harness` through `harness_cli.py`, reaching gameplay via
  `boot_to_gameplay()` (loadstate a gameplay save, then warp — no input driving) and reading Link's
  selected animation with the `az_linkanim` REPL command at `PLAYER+0x254+0x30`, named against
  `oot3d-decomp/tools/skeldata/player_animid_names.json`. The standalone Qt-frontend Azahar and its
  UDP-RPC tools are gone.
- **State matrix is honestly bounded by drivable recipes.** States with no existing
  REPL/oracle input recipe (backwalk, sidestep, turn-in-place, combo, item/bottle use,
  throw, climb traversal, dive, Z-target, get-item pose, death) are recorded
  `UNREACHABLE` with a concrete reason — never guessed into a verdict. Extend
  `STATE_MATRIX` (and the underlying REPL primitive) before trying to force a verdict for
  one of these.

## The loop, in one line
tooling → audit@matched-frames → RE-to-ground-truth → fix-or-proven-negative → verify@matched-frames
→ land-on-main → next; decomp stream always running; one build at a time; build the module, not a patch.
