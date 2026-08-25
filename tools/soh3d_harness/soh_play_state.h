#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_PLAY_STATE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_PLAY_STATE_H

extern "C" {
int SohState_HasPlayState(void);
int SohState_SceneNum(void);
int SohState_RoomNum(void);
int SohState_CsFrames(void);
int SohState_SetCsFrames(int frames);
}

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_PLAY_STATE_H
