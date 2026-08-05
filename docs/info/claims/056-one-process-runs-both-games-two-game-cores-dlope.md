---
id: C056
kind: claim
status: holds
created: 2026-08-05
tags: n3,launcher,dlopen
depends: Shipwright/zelda3d_app/zelda3d_main.cpp, Shipwright/libultraship/include/ship/zelda3d_core.h, Shipwright/libultraship/src/ship/Context.cpp
---

## Claim

One process runs BOTH games: two game cores dlopen'd RTLD_LOCAL coexist, their colliding decomp symbols stay private, and each game reaches gameplay through the launcher

## Evidence

Shipwright/zelda3d_app is a launcher exe holding no game code; each game builds as a shared object exporting one symbol (Zelda3D_CoreEntry, ship/zelda3d_core.h). 'zelda3d --probe-cores' dlopens BOTH libsoh_core.so and libmm_core.so with RTLD_NOW|RTLD_LOCAL into a single process and reports 'loaded 2/2 cores simultaneously'; the static-initialisation order across the dlopen boundary that C054 flagged as untested did not bite. Loading is necessary but not sufficient, so the probe also dlsyms known-colliding decomp symbols per handle: Play_Init oot=0x7fb2236499e0 mm=0x7fb21fd6de30, Actor_Draw oot=0x7fb2235cac20 mm=0x7fb21fcf02c0, Actor_Kill oot=0x7fb2235c89e0 mm=0x7fb21fcee0b0 -- 3 pairs compared, 0 shared. Full user path verified, not just the load: 'zelda3d oot' reaches Kokiri Forest with OoT3D models and the complete HUD (hearts, magic, item buttons, minimap, rupees) in scratch/screenshots/launcher_oot.png, and 'zelda3d mm' reaches Clock Town loading 7 MM3D models in scratch/screenshots/launcher_mm.png. One defect surfaced and was fixed rather than worked around: Context::GetAppBundlePath was derived from /proc/self/exe, which is the launcher, so OoT aborted with assetsExist=false at installPath=build-cmake/zelda3d; Context::SetAppBundlePath now takes the core's own directory from dladdr() on the loaded entry point. Archives were never affected -- they resolve from the CWD, which the launcher sets to the core's dir.

## What would falsify it

The probe measures LINK-TIME/dlsym visibility and a short run to gameplay. It would NOT catch state the two cores wrongly share through libultraship at runtime (accessor-hidden singletons, function-local statics), which only shows up with both games ACTIVE in turn rather than merely loaded -- and nothing yet runs a second core after a first has run. Also: switching games in-process is untested by construction, since the launcher picks a core before any engine exists.
