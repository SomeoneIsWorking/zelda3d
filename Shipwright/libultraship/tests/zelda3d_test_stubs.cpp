// Test-only stubs for the Zelda3D app-side hooks that libultraship references from Gui/Controller.
// The lus_tests executable links libultraship.a WITHOUT the soh application objects (zelda3d.c et al.)
// that normally define these, so the standalone test link was failing with undefined references.
// Provide inert definitions here to satisfy the linker; production links the real ones from soh.
extern "C" {
void Zelda3D_HudFrame(void) {}
void Zelda3D_HudFlushPoint(void) {}
int gZelda3dInputDevice = 0;
}
