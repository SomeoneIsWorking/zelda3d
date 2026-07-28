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

- [#201 e] Link: **sword on his back before he has picked it up** — hand-curated mesh_id midmask stands in for OoT3D's real per-state mesh visibility. Frontier row `player.mesh-id-selection`; blocked on locating the OoT3D Player_DrawImpl twin (the fn-ptr table @0x4bff48 is reached via base+offset, so Ghidra FindRangeRefs finds zero code refs — needs data-flow or a live watchpoint). MULTI-SESSION RE.
  NOTE: pose PLAYBACK measured at parity (idle/walk 1.2°, run 1.7° per bone) and the idle *picker* matches the oracle — so this is not a pose-selection bug.

## in-progress

- [#205] N64 HUD is drawn by the Fast3D INTERPRETER — move it to a NATIVE draw path — user request 2026-07-28 (screenshot: item-button discs rendering as black bars; explicit instruction NOT to fix the glitch but to change the architecture). **Pass 1 landed; the card stays here because the conversion is not complete.** Architecture in place: the N64 HUD keeps its LAYOUT and only its final texrect emission becomes a `Zelda3D_HudQuad*` record, drawn in `Gui::EndFrame` by the restored SDL3-GPU quad renderer (extended with a per-vertex ENV colour + combine mode for the heart `(PRIM-ENV)*TEXEL0+ENV` case). **Converted: the item-button cluster** (disc + item icon + ammo count + key badge) — the region in the photo; its black bars are gone, and the shared-resident-tile + `bgScale` coupling that caused them no longer exists on that path. Evidence `scratch/screenshots/hud_native_after{,_zoom}.png` vs `hud_before_native.png`. **Still on the interpreter (next passes):** health meter, magic bar, rupee/key counters, timers, do-action label, A button, C-Up/start (the residual bar stack left of the item buttons), minimap. Elements must convert as a GROUP — the native pass runs after the whole interpreter frame, so a half-converted element inverts its own layering. Writeup `debug_journal/2026-07-28-hud-off-the-interpreter.md`. NOTE on "you can also use 3DS HUD": taken as permission, not a mandate — OoT3D's HUD is a DUAL-SCREEN design, so transplanting its layout to one screen is a redesign; say the word if that is wanted.


## in-review

_(empty)_

## needs-confirmation

- [#203] PC-native keyboard UI/UX — item bar on 1/2/3 + HUD badges that read the LIVE binding — DONE 2026-07-28, awaiting user confirmation. Two real defects behind "none of it is wired": (1) the three C-button ITEM SLOTS were bound to the arrow keys (emulator-with-a-keyboard, not a PC game) — **input scheme v3** moves them to `1`/`2`/`3`, puts C-Up (first-person look / Navi, not an item slot) on `C`, and leaves the arrow keys unbound for the mouse-look pass; existing configs re-migrate via the scheme-version bump. (2) the keyboard HUD badges were four PNGs with the key letters drawn INTO the SVG artwork, so the B badge read **"C"** while `BTN_B` had been **F** for months and rebinding changed nothing on screen — the badge is now composited at runtime from the live ControlDeck (`input/zelda3d_keymap.cpp` + `Zelda3D_KeyCapTex`), widening the keycap by 9-slice for multi-character labels. Evidence: HUD shows `1`/`2`/`3` + `F` (`scratch/screenshots/kbdbadge_after_zoom.png`); REPL `keycap` reads back `B='F' C-Left='1' C-Down='2' C-Right='3' | C-Up='C'` from the live binding; injecting scancode 2 logs `appliedToPad=1` and takes Link to `upper=nml_carryB_wait`/`st1=0x800` (item actually raised, `scratch/screenshots/bomb_after_zoom.png`); widened caps `scratch/screenshots/keycap_sheet.png`; gamepad badges unaffected. Writeup `debug_journal/2026-07-28-pc-native-keyboard-item-bar.md`, finding `docs/issues/0002-*`. **Left open on purpose:** a keyboard-first bindings PAGE in the RmlUi menu — rebinding already works through the existing input editor and the HUD now follows it live, so that belongs with RmlUi Phase 2 input/nav rather than bolted on here.

- [#201 d] Link's face does not react during the yawn/stretch fidget — FIXED `54d81f7e`, awaiting user confirmation. Root cause: the facial channel was never ported for the PLAYER (every NPC had it). THE FACE IS PART OF THE ANIMATION: each of Link's 582 `boy/anim/<clip>.csab` has a sibling `<clip>.faceb` — an undocumented Grezzo step-keyframe track of (frame, eyeIdx, mouthIdx), RE'd from the retail ROM (`"fkb"` + u8 ver + u16 keyCount + keys; 0xFF = hold). It is Grezzo's re-encoding of the data N64 hides in the animation's fake limb 22. Indices select a TexturePalette CMAB frame on one eye + one mouth material (child mat 14/15, adult 16/17 — 8 eye / 4 mouth, exactly N64's `sEyeTextures`/`sMouthTextures`). The yawn is `wait_typeD_20f`: eye 7 (squeezed shut) f19–38, mouth 3 (wide open) f36–78. Verified: REPL `linkface` reproduces the ROM track exactly, and the FULL user path — stretch firing naturally at Link's House with no forcing, face animating through it. Also found: STRETCH only fires when `curRoom.behaviorType2 >= 4` (only 45 of 724 rooms qualify; Kokiri Forest is 0, which is why earlier capture attempts never saw it). Left open deliberately: the `shape.face` scripted-face fallback (damage/cutscene faces).

- [#204] Ladder climb: Link floats up on mount and warps down each anim cycle — FIXED `ff3d24df`, awaiting user confirmation. Root motion was applied twice (engine consumed the root delta into world.pos AND the CSAB draw sampled the same track). Now pinned per `movementFlags`, mirroring N64 `SkelAnime_UpdateTranslation`. Measured: clip-boundary jumps +21.7/+24.2/+22.8 world units -> ZERO, ascent monotonic. Clip: `scratch/screenshots/climb_after_fix.mp4`.

- [#201 a/b/c] Link walk vibration + door-exit slide + climb warp-up — FIXED 2026-07-23 (uncommitted working tree), awaiting user confirmation. Root causes: (a)+(c) the per-frame min-vertex feet-grounding draw anchor (replaced by the RE'd age root-translation scale 0.64 + the missing `shape.yOffset*scale.y` draw term); (b) scripted auto-walk (door exit / entrance walk-in) drives the legs via unk_868 while the named anim stays idle — selection now follows the leg driver. Evidence: `scratch/screenshots/{walk_jitter_before,walk_jitter_after,door_exit_after}.mp4`; numbers in `debug_journal/2026-07-23-link-movement-three-bugs.md` (walk vertical noise 0.9→0.000 units, climb-clip teleport 16.7→0 units, door walk-out now plays nml_walk_free). Known residual: real ladder-grab climb not yet reproducible headless (frontier `player.draw-anchor` gaps).

- [#201 c2] LADDER/wall climbing plays the IDLE pose — FIXED 2026-07-23 (uncommitted working tree), awaiting user confirmation. Root cause: the ENTIRE `gPlayerAnim_clink_*` (CHILD-only) anim namespace was invisible to `tools/gen_player_animmap.py`, which scanned only `gPlayerAnim_link_*` — so every child climb clip resolved to NULL and fell back to `ZELDA3D_LINK_IDLE_CSAB`. A vine/wall climb (`actionVar1 = (wallFlags & 8) ? 2 : 0` = 0) is 100% `clink_` clips; a real ladder (=2) animates its rungs from the shared `Fclimb_*` but takes its top/bottom DISMOUNTS from `clink_climb_endA*/endB*` — both idle before. Not an anchor bug (that was (c)) and not a playhead bug. Fix: generator now scans both namespaces (`cl_` prefix for the child twins) + its stale output path repointed at `tables/`; +36 rows, no existing row altered. Live AFTER: `cl_nml_climb_startA -> cl_nml_climb_upL -> cl_nml_climb_upR -> cl_nml_climb_endBR`, all `(unmapped)` before. Evidence: `scratch/screenshots/{climb_before,climb_after}.mp4` (+ `*_zoom.png` filmstrips, same camera/approach); writeup `debug_journal/2026-07-23-ladder-climb-child-anim-namespace.md`. New reusable repro: `tools/ladder_repro.py` (closes the recorded "climb never engaged headless" tooling gap).

- [#202] Remove the custom UI/HUD, restore the native HUD — DONE `c6daa4d4`, awaiting user confirmation. Custom PC/Vulkan HUD + the 6-slot hotbar deleted (the hotbar was suppressing the native C-button/D-pad cluster and clobbering `buttonItems[0]` every frame). Native HUD verified live: `scratch/screenshots/hud_verify2.png`. Keyboard/gamepad glyphs kept and still rendering.

_(empty)_

## blocked

_(empty)_

## done

_(empty)_
