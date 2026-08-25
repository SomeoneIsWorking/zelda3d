// Internal capture seam used by animation execution.
#ifndef ZELDA3D_ANIM_POSE_INSPECTION_INTERNAL_H
#define ZELDA3D_ANIM_POSE_INSPECTION_INTERNAL_H

#include "pose_inspection.h"

#include <array>
#include <vector>

bool Zelda3D_SkinDumpActiveForModel(int modelId);
void Zelda3D_CaptureSkinDump(int modelId, const char* animName, float frame,
                             const std::vector<std::array<float, 16>>& animatedWorld);

#endif // ZELDA3D_ANIM_POSE_INSPECTION_INTERNAL_H
