---
id: C054
kind: claim
status: holds
created: 2026-08-05
tags: 
reconfirmed: 2026-08-05
verified_at: 2026-08-05
depends: Shipwright/libultraship/src/CMakeLists.txt, CMakeLists.txt
---

## Claim

RTLD_LOCAL per-game cores is NOT sufficient on its own: libultraship must become ONE SHARED object, or each core gets its own renderer and window

## Evidence

tools/shared_state_probe.py over the objects ninja links. libultraship defines 4937 globals (71 DATA, 4063 FUNC). Referenced by BOTH game cores: 444 FUNC and only 2 DATA (GImGui, Fast::g_exec_stack). 444 shared FUNCTIONS are harmless -- duplicate code behaves identically. Shared DATA is not: a duplicate is a second window/renderer/resource-manager that one game silently talks to. The 2 looks reassuring and IS NOT THE ANSWER: nm finds 59 function-local statics in libultraship.a (_ZZ*-mangled, e.g. ZeroPtr<ImGuiContext>::storage), and accessor-hidden singletons -- Context::GetInstance() over a file-static -- are how this codebase actually writes its state, so they are invisible to a direct-data probe by construction. Whatever their true count, the fix is the same and does not depend on it: if libultraship is ONE shared library that both core .so files link against, every copy question disappears at once, accessor-hidden or not. So C050's design stands but is INCOMPLETE as written -- RTLD_LOCAL privatises the game symbols, a shared libultraship.so unifies the engine state, and one without the other does not work.

## What would falsify it

An attempt to actually build libultraship as SHARED. Static-initialisation order, symbol visibility defaults (-fvisibility=hidden would hide what the cores need), and the ImGui/Fast3D globals above are each capable of breaking it, and none has been tried yet.

## Re-confirmed 2026-08-05

Falsifier exercised and did NOT fire. libultraship now builds as SHARED (libultraship.so, 62M) with CMAKE_POSITION_INDEPENDENT_CODE ON at the root. Two link failures appeared and both were build configuration, not architecture: StormLib et al. were not -fPIC (R_X86_64_32S against .rodata), and libzip was linked PRIVATE, which a static lib forgives and a shared one does not. Both games now link the SAME .so -- ldd on soh.elf and mm.elf resolves libultraship.so to one identical path -- and both RUN: OoT reaches gameplay with Link animating (16 [Zelda3D animPlay], waitF_itemB_20f, HUD resources ready) and MM reaches Clock Town loading MM3D models (zelda2_tokei_tobira/turret uploaded), with no undefined-symbol or loader errors on either side. soh.elf 96M->36M, mm.elf 100M->43M: the cores no longer each carry a private copy of the engine. NOT YET SHOWN: this proves a shared engine works for two SEPARATE processes. Loading two game cores as RTLD_LOCAL .so files into ONE process is the remaining step, and static-initialisation order across that boundary is untested.

## Re-confirmed 2026-08-05

See prior confirmation; recording depends so staleness is detectable.

## Re-confirmed 2026-08-05

Its remaining falsifier is now exercised: 'loading two game cores as RTLD_LOCAL .so files into ONE process', recorded here as untested, is done and measured -- see C056. Both cores load simultaneously and their colliding symbols stay private, and static-initialisation order across the dlopen boundary did not break. C054's design conclusion stands unchanged: the shared libultraship.so is what makes it work, because each core links it rather than carrying a copy.
