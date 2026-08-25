// POD state shared by replacement catalogue, calibration, and diagnostics.
#ifndef ZELDA3D_RENDER_REPLACEMENT_TYPES_H
#define ZELDA3D_RENDER_REPLACEMENT_TYPES_H

#include "actor_model_submission.h"

typedef struct {
    s16 actorId;
    const char* name;
    float worldScale;
    int glModelId;
    const char* anim;
    float groundOffset;
    Zelda3D_AnimResolver resolveAnim;
    Zelda3D_JointResolver resolveJoints;
    int n64anim;
} Zelda3D_ModelEntry;

typedef struct {
    float measuredH;
    float measFootX;
    float measFootZ;
    short measYaw;
    float scale;
    float groundOff;
    int modelId;
    signed char state;
    signed char tries;
    signed char skinned;
} Zelda3D_AutoEntry;

#endif // ZELDA3D_RENDER_REPLACEMENT_TYPES_H
