#ifndef ZELDA3D_FACIAL_ASSETS_H
#define ZELDA3D_FACIAL_ASSETS_H

#include <string>

struct LoadedModel;

// Enrich a freshly loaded model with the eye/mouth texture palettes authored in sibling CMABs.
// The operation is a no-op for archives without a known facial-material contract.
void Zelda3D_AppendFacialFrames(LoadedModel* model, const std::string& archiveKey);

#endif // ZELDA3D_FACIAL_ASSETS_H
