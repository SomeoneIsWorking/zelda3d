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

- [#201] Link bug list — USER-OBSERVED 2026-07-23, replaces "too many bugs to list". HIGH PRIORITY (disorienting): (a) **jittery/vibrating while walking**, (b) **door-exit slide**, (c) **climb warp-up**. All three are long-standing (NOT regressions from the 2026-07-23 phase-lock — user confirmed). Likely a shared position/movement-integration cause. Lower priority: (d) yawns when idle long but **face does not react** — Link facial texture-anim (mouth/eyes) appears unported; (e) **sword on his back before he has picked it up** = the hand-curated mesh_id midmask (`player.mesh-id-selection` frontier row).
  NOTE: pose PLAYBACK measured at parity (idle/walk 1.2°, run 1.7° per bone) and the idle *picker* matches the oracle — so none of these are pose-selection bugs.
- [#203] PC-native keyboard UI/UX — the thing #202's HUD was SUPPOSED to be — user request 2026-07-23: "I wanted like a keyboard UI/UX when playing on keyboard with more PC game like item mapping etc but none of it is wired". Needs its own focused effort: PC-style item mapping/binding surfaced in the UI, keyboard-first affordances. Build it on the NATIVE HUD (modify to taste), not a parallel HUD stack — that is what #202 removes. The existing keyboard/gamepad HUD glyph work (memory soh3d-hud-glyphs) is the right shape to extend.

## in-progress

_(empty)_

## in-review

_(empty)_

## needs-confirmation

- [#202] Remove the custom UI/HUD, restore the native HUD — DONE `c6daa4d4`, awaiting user confirmation. Custom PC/Vulkan HUD + the 6-slot hotbar deleted (the hotbar was suppressing the native C-button/D-pad cluster and clobbering `buttonItems[0]` every frame). Native HUD verified live: `scratch/screenshots/hud_verify2.png`. Keyboard/gamepad glyphs kept and still rendering.

_(empty)_

## blocked

_(empty)_

## done

_(empty)_
