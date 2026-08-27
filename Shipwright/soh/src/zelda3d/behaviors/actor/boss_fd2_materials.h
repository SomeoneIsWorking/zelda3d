// OoT3D Boss_Fd2 CMB material-animation binding and sampling.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_MATERIALS_H
#define ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_MATERIALS_H

#include "boss_fd2_material_controller.h"

struct BossFd2;

namespace Zelda3D::BossFd2Materials {

void ApplyBody(int modelId, const BossFd2* boss, const Controller& controller);
void ApplyFireHair(int modelId, const Controller& controller);

} // namespace Zelda3D::BossFd2Materials

#endif // ZELDA3D_BEHAVIORS_ACTOR_BOSS_FD2_MATERIALS_H
