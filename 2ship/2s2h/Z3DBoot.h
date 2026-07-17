// Z3DBoot.h — Zelda3D headless debug-warp boot for native MM.
//
// Mirrors zelda3d's Zelda3D_AutoWarp* seam for OoT (soh/src/zelda3d/core/zelda3d.c): when the env gate is set,
// MM boots straight into the MapSelect debug overlay and warps into a gameplay scene, skipping the
// title screen + attract-demo cutscene loop (SPOT00 -> Z2_CLOCKTOWER -> ... -> Z2_TOWN) that a
// no-input headless boot otherwise cycles forever. This is the deterministic path to real gameplay.
//
// Gates (read once, cached):
//   ZELDA3D_MM_WARP      non-empty -> enable the debug-warp boot.
//   ZELDA3D_MM_ENTRANCE  raw entrance value (strtol base 0) to warp to; unset/negative -> the
//                        caller's default (South Clock Town). Hex or decimal both accepted.
#ifndef Z3D_BOOT_H
#define Z3D_BOOT_H

#ifdef __cplusplus
extern "C" {
#endif

// 1 when ZELDA3D_MM_WARP is set (non-empty), else 0.
int Z3D_AutoWarpEnabled(void);

// Raw entrance value from ZELDA3D_MM_ENTRANCE, or -1 when unset (caller picks a default).
int Z3D_AutoWarpEntrance(void);

#ifdef __cplusplus
}
#endif

#endif // Z3D_BOOT_H
