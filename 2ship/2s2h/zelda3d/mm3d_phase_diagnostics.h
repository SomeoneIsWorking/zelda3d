#pragma once

namespace Zelda3D::MM3D {

void RecordAnimationPhase(int modelId, const char* animation, const void* actorKey, float frame, float duration,
                          bool phaseLocked, float morphWeight);
void DumpAndClearAnimationPhases();

} // namespace Zelda3D::MM3D
