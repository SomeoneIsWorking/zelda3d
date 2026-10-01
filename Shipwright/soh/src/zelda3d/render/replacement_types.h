// POD state shared by replacement catalogue, calibration, and diagnostics.
#ifndef ZELDA3D_RENDER_REPLACEMENT_TYPES_H
#define ZELDA3D_RENDER_REPLACEMENT_TYPES_H

#include "actor_model_submission.h"

// Declared in padding-optimal order (pointers and floats first, then the two shorts last) so
// the table stays 6-byte-aligned instead of wasting 14. Initialized POSITIONALLY in
// replacement_catalog.cpp and model_table_control.cpp — keep the two in step.
typedef struct {
    const char* name;
    const char* anim;
    Zelda3D_AnimResolver resolveAnim;
    Zelda3D_JointResolver resolveJoints;
    float worldScale;
    int glModelId;
    float groundOffset;
    int n64anim;
    s16 actorId;
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
