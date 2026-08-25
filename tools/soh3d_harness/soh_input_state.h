#ifndef ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_INPUT_STATE_H
#define ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_INPUT_STATE_H

extern "C" {
int SohState_SetInput(unsigned int button, int stickX, int stickY);
int SohState_ClearInputOverride(void);
int SohState_ApplyInputOverride(void* input0);
}

#endif // ZELDA3D_TOOLS_SOH3D_HARNESS_SOH_INPUT_STATE_H
