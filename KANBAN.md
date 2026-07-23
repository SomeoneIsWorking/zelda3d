# Kanban — local board

Local task board for **zelda3d** (OoT3D / soh3d + MM3D / 2ship3d). **This markdown file IS the source
of truth** — no GitHub Issues. Add a card only when the user reports/requests something (same
USER-DRIVEN-ONLY rule as before: agent sweeps fix-in-session + journal, they do NOT create cards).
Move a card between columns as work progresses; delete it (or move to `done`) when the user confirms.

Screenshots for a card: attach in chat, or drop the file under `scratch/kanban/` (gitignored) and link
it here — we don't commit PNGs to the repo.

Columns: **todo · in-progress · in-review · needs-confirmation · blocked · done**

Card format: `- [#N] <title> — <notes / evidence link>`  (N = simple incrementing id you assign)

## todo

- [#201] Link bug list — USER-OBSERVED 2026-07-23. **(a) walk jitter FIXED, (b) door-exit slide FIXED, (c) LEDGE-climb warp FIXED — all user-confirmed** (`bc8072e1`). Remaining: (d) yawn/stretch fidget plays but **Link's face does not react at all** — user photo confirms neutral face (eyes open, mouth closed) mid-stretch; facial texture-anim appears unported (we already do this for En_Ko via eyeTextureIndex/applyFacialFrame); (e) **sword on his back before he has picked it up** = hand-curated mesh_id midmask (`player.mesh-id-selection` frontier row, blocked on locating OoT3D Player_DrawImpl).
  NOTE: pose PLAYBACK measured at parity (idle/walk 1.2°, run 1.7° per bone) and the idle *picker* matches the oracle — so none of these are pose-selection bugs.
- [#203] PC-native keyboard UI/UX — the thing #202's HUD was SUPPOSED to be — user request 2026-07-23: "I wanted like a keyboard UI/UX when playing on keyboard with more PC game like item mapping etc but none of it is wired". Needs its own focused effort: PC-style item mapping/binding surfaced in the UI, keyboard-first affordances. Build it on the NATIVE HUD (modify to taste), not a parallel HUD stack — that is what #202 removes. The existing keyboard/gamepad HUD glyph work (memory soh3d-hud-glyphs) is the right shape to extend.

## in-progress

- [#204] Ladder climb: Link floats up on mount and warps down each anim cycle — user-reported 2026-07-23 WITH screenshot. REGRESSION EXPOSED BY `f6d73b98`: before it, climbs played an idle clip with no root motion; now the real climb clips play and their hip/root translation is double-counted (engine moves the actor up the rung AND our CSAB draw applies the clip's own translation), so he lifts off and snaps back on loop. This is the `player.draw-anchor` open residual ("anim-movement hip consumption not mirrored in the CSAB draw") — now unblocked because `tools/ladder_repro.py` gives the live repro it was waiting for.

_(empty)_

## in-review

_(empty)_

## needs-confirmation

- [#201 a/b/c] Link walk vibration + door-exit slide + climb warp-up — FIXED 2026-07-23 (uncommitted working tree), awaiting user confirmation. Root causes: (a)+(c) the per-frame min-vertex feet-grounding draw anchor (replaced by the RE'd age root-translation scale 0.64 + the missing `shape.yOffset*scale.y` draw term); (b) scripted auto-walk (door exit / entrance walk-in) drives the legs via unk_868 while the named anim stays idle — selection now follows the leg driver. Evidence: `scratch/screenshots/{walk_jitter_before,walk_jitter_after,door_exit_after}.mp4`; numbers in `debug_journal/2026-07-23-link-movement-three-bugs.md` (walk vertical noise 0.9→0.000 units, climb-clip teleport 16.7→0 units, door walk-out now plays nml_walk_free). Known residual: real ladder-grab climb not yet reproducible headless (frontier `player.draw-anchor` gaps).

- [#201 c2] LADDER/wall climbing plays the IDLE pose — FIXED 2026-07-23 (uncommitted working tree), awaiting user confirmation. Root cause: the ENTIRE `gPlayerAnim_clink_*` (CHILD-only) anim namespace was invisible to `tools/gen_player_animmap.py`, which scanned only `gPlayerAnim_link_*` — so every child climb clip resolved to NULL and fell back to `ZELDA3D_LINK_IDLE_CSAB`. A vine/wall climb (`actionVar1 = (wallFlags & 8) ? 2 : 0` = 0) is 100% `clink_` clips; a real ladder (=2) animates its rungs from the shared `Fclimb_*` but takes its top/bottom DISMOUNTS from `clink_climb_endA*/endB*` — both idle before. Not an anchor bug (that was (c)) and not a playhead bug. Fix: generator now scans both namespaces (`cl_` prefix for the child twins) + its stale output path repointed at `tables/`; +36 rows, no existing row altered. Live AFTER: `cl_nml_climb_startA -> cl_nml_climb_upL -> cl_nml_climb_upR -> cl_nml_climb_endBR`, all `(unmapped)` before. Evidence: `scratch/screenshots/{climb_before,climb_after}.mp4` (+ `*_zoom.png` filmstrips, same camera/approach); writeup `debug_journal/2026-07-23-ladder-climb-child-anim-namespace.md`. New reusable repro: `tools/ladder_repro.py` (closes the recorded "climb never engaged headless" tooling gap).

- [#202] Remove the custom UI/HUD, restore the native HUD — DONE `c6daa4d4`, awaiting user confirmation. Custom PC/Vulkan HUD + the 6-slot hotbar deleted (the hotbar was suppressing the native C-button/D-pad cluster and clobbering `buttonItems[0]` every frame). Native HUD verified live: `scratch/screenshots/hud_verify2.png`. Keyboard/gamepad glyphs kept and still rendering.

_(empty)_

## blocked

_(empty)_

## done

_(empty)_
