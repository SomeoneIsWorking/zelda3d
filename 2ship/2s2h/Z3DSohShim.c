// Z3DSohShim.c — inert definitions for the Zelda3D (OoT3D) symbols that the SHARED libultraship
// references (Controller.cpp, Gui.cpp, interpreter.cpp, zelda3d_sdl3gpu.cpp). Those symbols are defined by
// the `soh` executable (soh/src/zelda3d/...); the MM (2S2H) executable links the same libultraship
// but has no OoT3D layer.
//
// STOPGAP: proper fix = decouple libultraship from app-defined symbols (weak symbols or a
// callback-registration seam), because libultraship should not hard-reference executable-side
// state. Inert here because MM has no OoT3D layer — these values/callbacks are never exercised.

int gZelda3dInputDevice = 0;  // 0 = gamepad, 1 = keyboard (soh HUD glyph select)
int gZelda3dHlGroup = -1;     // room-group highlight index (-1 = disabled)

// The OoT-side native HUD (soh/src/zelda3d/hud/zelda3d_hud.cpp), called unconditionally from the
// shared Gui::EndFrame. MM has no OoT3D HUD, so this is inert here.
void Zelda3D_HudFrame(void) {
}

void Zelda3D_MeasureResult(int key, float height) {
    (void)key;
    (void)height;
}
