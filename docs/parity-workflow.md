# Oracle-driven parity workflow (SoH3D ↔ OoT3D)

The workflow that worked for title-screen parity. Reusable for ANY SoH3D↔OoT3D parity
work (a scene, an actor, a lighting pass). Distilled 2026-07-08 from a session where the
first three "bugs" turned out to be false alarms — the method below is what fixed that.

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
`oot3d-decomp/docs/`. **"It's an asset difference" is NOT a terminal answer** — SoH already
renders 3DS assets from the ROM, so an asset-rooted gap means "port that exact 3DS asset."

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

## The loop, in one line
tooling → audit@matched-frames → RE-to-ground-truth → fix-or-proven-negative → verify@matched-frames
→ land-on-main → next; decomp stream always running; one build at a time; build the module, not a patch.
