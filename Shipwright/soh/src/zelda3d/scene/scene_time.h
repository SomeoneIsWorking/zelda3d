// Apply the configured deterministic time before scene initialization.
#ifndef ZELDA3D_SCENE_TIME_H
#define ZELDA3D_SCENE_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_ApplyForceTime(void);
extern int gZelda3dForceTime;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_SCENE_TIME_H
