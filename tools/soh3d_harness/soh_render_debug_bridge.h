#pragma once

// Shipping PICA-fog diagnostic latch and live uniform snapshot.
extern "C" {

extern int gZelda3dFog3dForceOff;
extern int gZelda3dFog3dOn;
extern float gZelda3dFog3d[8];
// The colour the PICA fog mixes toward. Reported by `soh_fog3d` because it is a shader INPUT like
// the window is: a fog with the right depth and the wrong colour is still wrong, and a diagnostic
// that prints the window alone makes that invisible.
extern float gZelda3dFogColor[3];

} // extern "C"
