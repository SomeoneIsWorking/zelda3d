#include "frame_step_control.h"

#include "zelda3d/input/zelda3d_input.h"

int gZelda3dFreeze = 0;

void Zelda3D_StepLogicFrame(PlayState* play) {
    Zelda3D_WalkInject(play);
    Play_Update(play);
}
