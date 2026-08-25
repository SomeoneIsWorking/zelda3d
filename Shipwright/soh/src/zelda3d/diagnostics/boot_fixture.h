// Deterministic headless boot configuration.
#ifndef ZELDA3D_DIAGNOSTICS_BOOT_FIXTURE_H
#define ZELDA3D_DIAGNOSTICS_BOOT_FIXTURE_H

#ifdef __cplusplus
extern "C" {
#endif

int Zelda3D_AutoWarpEnabled(void);
int Zelda3D_AutoWarpEntrance(void);
int Zelda3D_ColdBoot(void);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_DIAGNOSTICS_BOOT_FIXTURE_H
