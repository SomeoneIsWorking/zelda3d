// OoT3D replacements for the Kakariko well, windmill, and mountain gate structures.
#ifndef ZELDA3D_RENDER_KAKARIKO_STRUCTURE_REPLACEMENTS_H
#define ZELDA3D_RENDER_KAKARIKO_STRUCTURE_REPLACEMENTS_H

#include "global.h"

int Zelda3D_TryDrawKakarikoStructureReplacement(PlayState* play, Actor* actor);
int Zelda3D_KakarikoStructureRecordMeasure(int key, float height);
int Zelda3D_KakarikoStructureRetryNoMeasurement(void);

#endif // ZELDA3D_RENDER_KAKARIKO_STRUCTURE_REPLACEMENTS_H
