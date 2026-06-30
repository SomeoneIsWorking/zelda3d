// Test-only stubs for the SoH3D app-side hooks that libultraship references from Gui/Controller.
// The lus_tests executable links libultraship.a WITHOUT the soh application objects (soh3d.c et al.)
// that normally define these, so the standalone test link was failing with undefined references.
// Provide inert definitions here to satisfy the linker; production links the real ones from soh.
extern "C" {
void SoH3D_HudFrame(void) {}
int gSoH3dInputDevice = 0;
}
