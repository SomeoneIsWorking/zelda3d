// Z3DSohShim.c — inert definitions for the SoH3D (OoT3D) on-screen hotbar/HUD symbols that the
// SHARED libultraship references (Controller.cpp, Gui.cpp, interpreter.cpp, soh3d_gl/sdl3gpu.cpp).
// Those symbols are defined by the `soh` executable (soh/src/soh3d/soh3d.c) for its OoT3D hotbar
// feature; the MM (2S2H) executable links the same libultraship but has no such feature.
//
// STOPGAP: proper fix = decouple libultraship from app-defined HUD symbols (weak symbols or a
// callback-registration seam), because libultraship should not hard-reference executable-side
// state. Inert here because MM has no OoT3D hotbar — these values/callbacks are never exercised.

int gSoH3dInputDevice = 0;  // 0 = gamepad, 1 = keyboard (soh HUD glyph select)
int gSoH3dHotbarActive = 0; // selected slot 0-5
int gSoH3dHotbarFireB = 0;  // request B-fire this frame
int gSoH3dHlGroup = -1;     // room-group highlight index (-1 = disabled)

void SoH3D_HudFrame(void) {
}

void SoH3D_MeasureResult(int key, float height) {
    (void)key;
    (void)height;
}
