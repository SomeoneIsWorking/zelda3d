#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Convert typed 2S2H Player/equipment values to the recovered MM3D sheath
// policy. Returns only additive equipment mesh bits, not the base body mask.
unsigned long long Zelda3D_MM_PlayerSheathMeshMask(int playerForm, int sheathType, int currentShield, int currentMask,
                                                   int swordEquipValue);

#ifdef __cplusplus
}
#endif
