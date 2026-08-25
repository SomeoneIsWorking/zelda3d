#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_ACTOR_STATE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_ACTOR_STATE_H

extern "C" {
typedef void (*SohState_ActorSink)(void* user, int cat, int id, unsigned long addr, float px, float py, float pz,
                                   short rx, short ry, short rz);
int SohState_WalkActors(SohState_ActorSink sink, void* user);
int SohState_ActorParamsAt(int cat, int index);
int SohState_ActorListLen(int cat);
int SohState_ActorInfoAt(int cat, int index, int* outId, int* outParams, unsigned int* outFlags, float* outPx,
                         float* outPy, float* outPz, short* outRx, short* outRy, short* outRz);
}

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_ACTOR_STATE_H
