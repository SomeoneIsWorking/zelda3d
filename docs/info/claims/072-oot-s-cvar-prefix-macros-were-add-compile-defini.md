---
id: C072
kind: claim
status: holds
created: 2026-08-07
tags: n3,cvar,build,gui
depends: Shipwright/CMake/soh-cvars.cmake, Shipwright/soh/CMakeLists.txt, 2ship/2s2h/cvar_prefixes.h
---

## Claim

OoT's CVar prefix macros were add_compile_definition'd at the BUILD ROOT, so -DCVAR_PREFIX_* reached every target including MM, whose namespaces differ. MM's own '#define CVAR_PREFIX_COSMETIC "gCosmetic"' redefined OoT's "gCosmetics" (a real compiler warning), and MM's AudioCollection.cpp silently consumed OoT's -DCVAR_PREFIX_AUDIO without defining it. Scoping them to soh_settings is safe: no other target uses these macros.

## Evidence

Root CMakeLists.txt:51 includes soh-cvars.cmake before add_subdirectory(mm) at :317, so the directory-scoped add_compile_definitions propagated. Warning reproduced verbatim: 'CVAR_PREFIX_COSMETIC redefined ... <command-line>: note: this is the location of the previous definition' on every TU including BenGui/CosmeticEditor.h. Consumers audited before scoping: libultraship uses only CVAR_PREFIX_CONTROLLERS and CVAR_PREFIX_ADVANCED_RESOLUTION, which libultraship/cmake/cvars.cmake exports itself; cmb3d/ZAPD/zelda3d_app/zelda3d_shared use none. After scoping, the ONE hidden consumer failed loudly (AudioCollection.cpp:229) and was given MM's new 2s2h/cvar_prefixes.h. Post-change: both games build with 0 errors and 0 redefinition warnings, sequence gate mm,oot exit 0, and NO persisted key moved -- mm.elf still has 206 gCosmetic.* and 12 gAudioEditor.*, soh.elf still has 1889 gCosmetics.*.

## What would falsify it

if a target other than soh comes to need OoT's CVAR_PREFIX_* macros, the target-scoping breaks it at compile time (loudly, which is the point)
