---
id: 2
title: HUD keyboard button badge shows the wrong key (baked artwork, not the live binding)
status: resolved
symptom: The keyboard key badge drawn on a HUD item/B button shows a key that is not the one actually bound — e.g. B shows 'C' while BTN_B is bound to F — and rebinding in the input editor does not change the badge.
tags: hud,input,keyboard,binding,badge,keycap,203
created: 2026-07-28
updated: 2026-07-28
---

ROOT CAUSE: the four keyboard badges were PNGs baked at build time from assets/zelda3d/key_{b,cleft,cdown,cright}.svg with the key LETTERS drawn into the SVG artwork. Nothing in the badge draw path ever consulted a binding, so the artwork silently desynchronised the moment the default scheme changed (BTN_B moved to F; the badge kept saying C for months).

FIX (2026-07-28, #203): the badge is composited at RUNTIME. zelda3d/input/zelda3d_keymap.cpp reads the live Ship::ControlDeck for the keyboard mapping on an N64 bitmask and folds LUS's key name into a keycap label; zelda3d/hud/zelda3d_hud_tex.cpp Zelda3D_KeyCapTex blits that label onto a blank cap from a generated monospaced glyph atlas (tools/zelda3d_gen_key_glyphs.sh). Multi-character labels widen the cap by horizontal 9-slice rather than shrinking the text.

GOTCHA worth remembering: ControllerButton::GetAllButtonMappings() returns an unordered_map, so 'the first keyboard mapping' is not stable — pick a deterministic one (lowest scancode) or the badge flickers between two bound keys.

RELATED CLASS OF BUG: tools/zelda3d_gen_kbd_glyphs.sh wrote its generated header to src/zelda3d/kbd_glyphs_png.h while the asset had moved to src/zelda3d/assets/ — a stale generator OUTPUT PATH, the same shape of defect as #201 c2 (gen_player_animmap.py). When a generated file looks stale, check where its generator writes before assuming the generator is right.

VERIFY: REPL 'keycap' prints the four live labels; 'keycap <label>' dumps the composited RGBA to scratch/raw/keycap.rgba.
