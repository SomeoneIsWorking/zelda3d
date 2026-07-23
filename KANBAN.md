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

- [#201] Link bug list — USER-OBSERVED 2026-07-23. Remaining here: (d) yawns when idle long but **face does not react** — Link facial texture-anim (mouth/eyes) appears unported; (e) **sword on his back before he has picked it up** = the hand-curated mesh_id midmask (`player.mesh-id-selection` frontier row). Parts (a)(b)(c) fixed 2026-07-23 → see the needs-confirmation entry.
  NOTE: pose PLAYBACK measured at parity (idle/walk 1.2°, run 1.7° per bone) and the idle *picker* matches the oracle — so none of these are pose-selection bugs.
- [#203] PC-native keyboard UI/UX — the thing #202's HUD was SUPPOSED to be — user request 2026-07-23: "I wanted like a keyboard UI/UX when playing on keyboard with more PC game like item mapping etc but none of it is wired". Needs its own focused effort: PC-style item mapping/binding surfaced in the UI, keyboard-first affordances. Build it on the NATIVE HUD (modify to taste), not a parallel HUD stack — that is what #202 removes. The existing keyboard/gamepad HUD glyph work (memory soh3d-hud-glyphs) is the right shape to extend.

## in-progress

_(empty)_

## in-review

_(empty)_

## needs-confirmation

- [#201 a/b/c] Link walk vibration + door-exit slide + climb warp-up — FIXED 2026-07-23 (uncommitted working tree), awaiting user confirmation. Root causes: (a)+(c) the per-frame min-vertex feet-grounding draw anchor (replaced by the RE'd age root-translation scale 0.64 + the missing `shape.yOffset*scale.y` draw term); (b) scripted auto-walk (door exit / entrance walk-in) drives the legs via unk_868 while the named anim stays idle — selection now follows the leg driver. Evidence: `scratch/screenshots/{walk_jitter_before,walk_jitter_after,door_exit_after}.mp4`; numbers in `debug_journal/2026-07-23-link-movement-three-bugs.md` (walk vertical noise 0.9→0.000 units, climb-clip teleport 16.7→0 units, door walk-out now plays nml_walk_free). Known residual: real ladder-grab climb not yet reproducible headless (frontier `player.draw-anchor` gaps).

- [#202] Remove the custom UI/HUD, restore the native HUD — DONE `c6daa4d4`, awaiting user confirmation. Custom PC/Vulkan HUD + the 6-slot hotbar deleted (the hotbar was suppressing the native C-button/D-pad cluster and clobbering `buttonItems[0]` every frame). Native HUD verified live: `scratch/screenshots/hud_verify2.png`. Keyboard/gamepad glyphs kept and still rendering.

_(empty)_

## blocked

_(empty)_

## done

_(empty)_
