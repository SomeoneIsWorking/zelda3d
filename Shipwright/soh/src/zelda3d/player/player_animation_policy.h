// Resolve native Link animation identity to its OoT3D-authored playback policy.
#ifndef ZELDA3D_PLAYER_ANIMATION_POLICY_H
#define ZELDA3D_PLAYER_ANIMATION_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

const char* Zelda3D_ResolvePlayerCsab(const char* otr);
const char* Zelda3D_LinkWalkRunGate(const char* csab, float speedXZ);
extern int gZelda3dLinkAnimSrc;
int Zelda3D_LinkAnimSrc(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_PLAYER_ANIMATION_POLICY_H
