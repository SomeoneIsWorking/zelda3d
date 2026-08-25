// Runtime control surface for procedural stepped scene geometry.
#ifndef ZELDA3D_SCENE_STAIR_CONTROL_H
#define ZELDA3D_SCENE_STAIR_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_SetStairs(int on);
int Zelda3D_GetStairs(void);
void Zelda3D_SetStairRiserY(float value);
float Zelda3D_GetStairRiserY(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_SCENE_STAIR_CONTROL_H
