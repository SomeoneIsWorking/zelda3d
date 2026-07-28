// Zelda3D key-binding lookup — "which keyboard key is bound to this N64 button, and what should the
// HUD print on its keycap?". One place, so the HUD can never disagree with the input layer.
//
// Before this module the keyboard HUD badges were four PNGs baked at build time from
// assets/zelda3d/key_{b,cleft,cdown,cright}.svg, with the key letters drawn INTO the SVGs. That is
// what kanban #203 means by "none of it is wired": the badge for the B button read "C" while the
// default binding had been F for months, and rebinding anything in the input editor changed nothing
// on screen. The label is now READ from the live ControlDeck every frame instead.
#ifndef ZELDA3D_INPUT_ZELDA3D_KEYMAP_H
#define ZELDA3D_INPUT_ZELDA3D_KEYMAP_H

#ifdef __cplusplus
extern "C" {
#endif

// Short uppercase keycap label for the keyboard key currently bound to N64 button `bitmask`
// (BTN_A / BTN_B / BTN_CLEFT / ... from libultra/controller.h). Returns "" when the button has no
// keyboard binding, so a caller can skip the badge entirely rather than draw an empty cap.
//
// The returned pointer is owned by this module and stays valid until the next call for the same
// button; copy it if you need to hold it. Labels are folded to the HUD glyph alphabet
// (kKeyGlyphChars) and shortened to fit a keycap: SDL's "Left Shift" becomes "LSHIFT", "Space"
// becomes "SPACE", "Escape" becomes "ESC". Anything outside the alphabet becomes '?'.
//
// When several keyboard keys are bound to one button, the lowest scancode wins — the map LUS hands
// back is unordered, so the badge needs a deterministic tie-break or it would flicker between keys.
const char* Zelda3D_KeyLabelForButton(unsigned short bitmask);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_INPUT_ZELDA3D_KEYMAP_H
