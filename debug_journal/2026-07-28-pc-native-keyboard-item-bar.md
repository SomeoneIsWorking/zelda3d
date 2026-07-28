# 2026-07-28 — PC-native keyboard item bar + HUD badges derived from the live binding (#203)

User report (2026-07-23, kanban #203): *"I wanted like a keyboard UI/UX when playing on keyboard with
more PC game like item mapping etc but none of it is wired."*

## What "none of it is wired" actually was

Two separate defects, both real, both found by reading the code rather than guessing at what a
"keyboard UX" should be:

1. **The item slots were on the arrow keys.** OoT's three C buttons ARE the item slots. Binding them
   to ←/↓/→ is emulator-with-a-keyboard thinking; every PC game puts the item bar on the number row.
   The old comment in `ControllerDefaultMappings.cpp` even called them "Camera up/down/left/right —
   arrow keys = camera fallback", which is not what those buttons do in OoT.

2. **The HUD key badges were baked pictures and had been LYING for months.** They were four PNGs
   generated from `assets/zelda3d/key_{b,cleft,cdown,cright}.svg` with the key letters drawn INTO the
   SVG artwork. The B-button badge read **"C"** while `BTN_B`'s default binding had been **F** since
   the PC-native scheme landed. Rebinding anything in the input editor changed nothing on screen,
   because nothing in the badge path ever consulted a binding.

Defect 2 is the more important one: it is the reason the feature could never be "wired" by adjusting
defaults alone. Any change to the bindings desynchronises artwork that cannot follow it.

## Fix

**The badge is now composited at runtime from the live `ControlDeck`.** Three pieces:

- `zelda3d/input/zelda3d_keymap.{h,cpp}` — asks the port-0 controller which keyboard keys are bound
  to an N64 bitmask, picks one, and folds LUS's key name (`Window::GetKeyName` →
  `SDL_GetScancodeName`) into a keycap label. `GetAllButtonMappings()` returns an **unordered_map**,
  so "the first keyboard mapping" is not a stable notion — the tie-break is the lowest scancode, or
  the badge would flicker between two bound keys. Long SDL names get conventional keycap
  abbreviations (`Left Shift` → `LSHFT`, `Return` → `ENTER`, `Page Down` → `PGDN`).
- `Zelda3D_KeyCapTex(label)` in `zelda3d/hud/zelda3d_hud_tex.cpp` — blank keycap + glyphs blitted
  from a monospaced alphabet atlas, cached per label string. A multi-character label **widens** the
  cap (horizontal 9-slice, repeating the uniform middle column) instead of shrinking the text into
  illegibility; only past 3x width does it scale down. `z_parameter.c`'s badge draw therefore takes
  the texture's aspect rather than assuming a square.
- `tools/zelda3d_gen_key_glyphs.sh` → `assets/key_glyphs_png.h` — the blank cap plus a 48-cell glyph
  atlas. The atlas font is the **repo-vendored** `Inconsolata-Regular.ttf`, not a system font, so the
  output is reproducible on any machine (this machine has no DejaVu, which the old SVGs asked for and
  silently fell back from). Monospace means one cell width covers every character, so the runtime
  needs no per-glyph advance table.

**Input scheme v3** (`kZelda3dInputSchemeVersion` 2 → 3, so existing configs re-migrate):
C-Left/C-Down/C-Right = **1 / 2 / 3**, C-Up (first-person look / Navi — *not* an item slot) = **C**.
The arrow keys are deliberately left **unbound**, reserved for the camera in the mouse-look pass;
leaving them on the item buttons as a second binding would have made the badge tie-break arbitrary.

`KeyboardKeyToAnyMapping::GetKeyboardScancode()` was added to LUS — there was no public accessor, and
ordering bindings needs one.

## Verification (live game, full user path)

- `keycap` REPL (new): `B='F' C-Left='1' C-Down='2' C-Right='3' | C-Up='C'` — read back from the live
  ControlDeck, not from a constant.
- HUD screenshot: item slots show **1 / 2 / 3**, B shows **F** (was the stale "C").
  `scratch/screenshots/kbdbadge_after_zoom.png`.
- Binding reaches the pad: with `log input 1`, injecting scancode 2 logs
  `poll scancode=2 keyPressed=1 KeyboardGameInputBlocked=0 appliedToPad=1`.
- **Item actually used**: pressing "2" takes Link from `upper=nml_wait_free` / `st1=0x8` to
  `upper=nml_carryB_wait` / `st1=0x800` — he is holding the C-Down item overhead.
  `scratch/screenshots/bomb_after_zoom.png`.
- Widened cap: `keycap LSHFT|ENTER|SPACE` → 118x64, `F|1|K1` → 64x64; contact sheet
  `scratch/screenshots/keycap_sheet.png` shows corners and gradient intact at both widths.
- Gamepad badge path unaffected (`inputdev 0` still draws A/B/X/Y).
- Build clean, zero warnings.

## Not done (deliberately, not faked)

A dedicated keyboard-bindings **page** in the RmlUi menu. Rebinding already works through the
existing input editor and the HUD now follows it live, so the binding surface is honest; a
keyboard-first settings page belongs with the RmlUi Phase-2 input/nav work, not bolted on here.

## Dead ends / notes for next time

- `imstb_truetype.h` exists under `libultraship/imgui_shim/`, and `common.cmake` already downloads
  `stb_image.h` from upstream at configure time — so a runtime TTF rasterizer *was* available. It was
  not used: a pre-rasterized monospace atlas needs no font file at runtime, no new dependency, and
  reuses the ImageMagick asset-gen idiom every other HUD texture already uses.
- `tools/zelda3d_gen_kbd_glyphs.sh` wrote its header to `src/zelda3d/kbd_glyphs_png.h` while the
  asset had long since moved to `src/zelda3d/assets/` — the same stale-generator-output-path bug that
  hid the `clink_` animation namespace in #201 c2. Regenerating would have silently produced a file
  nothing includes. The replacement generator writes to `assets/` directly.
