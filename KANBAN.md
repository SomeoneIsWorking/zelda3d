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

- [#201] Link has too many bugs to list — user report 2026-07-23, after the 3DS Link draw was made default-on (`5902dcb1`, was gated behind `ZELDA3D_LINK`). Needs a systematic pass, not a bug-by-bug chase: enumerate what is actually broken in normal play, decide whether the port is fit to be the default, and either fix or restore the N64 draw as default until it is.
- [#203] PC-native keyboard UI/UX — the thing #202's HUD was SUPPOSED to be — user request 2026-07-23: "I wanted like a keyboard UI/UX when playing on keyboard with more PC game like item mapping etc but none of it is wired". Needs its own focused effort: PC-style item mapping/binding surfaced in the UI, keyboard-first affordances. Build it on the NATIVE HUD (modify to taste), not a parallel HUD stack — that is what #202 removes. The existing keyboard/gamepad HUD glyph work (memory soh3d-hud-glyphs) is the right shape to extend.

## in-progress

- [#202] Remove the custom UI/HUD, restore the native HUD — user request 2026-07-23: "I'd rather have the old HUD back and modify it to taste rather than this". Deleting the custom PC/Vulkan HUD (`zelda3d_hud*.cpp`, the VK HUD layer, and the z_parameter/z_lifemeter suppression hooks that skip the native HUD). Keeping the keyboard/gamepad glyph work, which targets the native HUD.
_(empty)_

## in-review

_(empty)_

## needs-confirmation

_(empty)_

## blocked

_(empty)_

## done

_(empty)_
