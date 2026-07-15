// Z3DSohShim.c — inert definitions for the Zelda3D (OoT3D) on-screen hotbar/HUD symbols that the
// SHARED libultraship references (Controller.cpp, Gui.cpp, interpreter.cpp, zelda3d_gl/sdl3gpu.cpp).
// Those symbols are defined by the `soh` executable (soh/src/zelda3d/core/zelda3d.c) for its OoT3D hotbar
// feature; the MM (2S2H) executable links the same libultraship but has no such feature.
//
// STOPGAP: proper fix = decouple libultraship from app-defined HUD symbols (weak symbols or a
// callback-registration seam), because libultraship should not hard-reference executable-side
// state. Inert here because MM has no OoT3D hotbar — these values/callbacks are never exercised.

int gZelda3dInputDevice = 0;  // 0 = gamepad, 1 = keyboard (soh HUD glyph select)
int gZelda3dHotbarActive = 0; // selected slot 0-5
int gZelda3dHotbarFireB = 0;  // request B-fire this frame
int gZelda3dHlGroup = -1;     // room-group highlight index (-1 = disabled)

void Zelda3D_HudFrame(void) {
}

void Zelda3D_MeasureResult(int key, float height) {
    (void)key;
    (void)height;
}
