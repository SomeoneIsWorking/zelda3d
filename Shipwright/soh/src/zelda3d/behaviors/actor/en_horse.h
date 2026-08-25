// Zelda3D behavior: En_Horse (Epona) — draw-adjacent OoT3D-parity surface. See en_horse.cpp.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_EN_HORSE_H
#define ZELDA3D_BEHAVIORS_ACTOR_EN_HORSE_H

#include "z64.h" // Actor, PlayState

#ifdef __cplusplus
extern "C" {
#endif

// Record the EnHorse actor's replaced-draw transform (called once per replaced draw from
// Zelda3D_EmitModelDraw). Zelda3D_HorseSaddleOffset reads this to anchor the rider seat to the DRAWN
// 3DS pose. A NULL actor clears the record. See en_horse.cpp.
void Zelda3D_EnHorse_RecordDraw(Actor* actor, int modelId, float worldScale, float groundOffset);
int Zelda3D_HoofDustWorldPos(PlayState* play, Actor* horseActor, float* ioPos);
int Zelda3D_HorseSaddleOffset(Actor* horseActor, float out[3]);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_ACTOR_EN_HORSE_H
