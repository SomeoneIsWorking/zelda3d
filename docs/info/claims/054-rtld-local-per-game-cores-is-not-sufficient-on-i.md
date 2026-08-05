---
id: C054
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

RTLD_LOCAL per-game cores is NOT sufficient on its own: libultraship must become ONE SHARED object, or each core gets its own renderer and window

## Evidence

tools/shared_state_probe.py over the objects ninja links. libultraship defines 4937 globals (71 DATA, 4063 FUNC). Referenced by BOTH game cores: 444 FUNC and only 2 DATA (GImGui, Fast::g_exec_stack). 444 shared FUNCTIONS are harmless -- duplicate code behaves identically. Shared DATA is not: a duplicate is a second window/renderer/resource-manager that one game silently talks to. The 2 looks reassuring and IS NOT THE ANSWER: nm finds 59 function-local statics in libultraship.a (_ZZ*-mangled, e.g. ZeroPtr<ImGuiContext>::storage), and accessor-hidden singletons -- Context::GetInstance() over a file-static -- are how this codebase actually writes its state, so they are invisible to a direct-data probe by construction. Whatever their true count, the fix is the same and does not depend on it: if libultraship is ONE shared library that both core .so files link against, every copy question disappears at once, accessor-hidden or not. So C050's design stands but is INCOMPLETE as written -- RTLD_LOCAL privatises the game symbols, a shared libultraship.so unifies the engine state, and one without the other does not work.

## What would falsify it

An attempt to actually build libultraship as SHARED. Static-initialisation order, symbol visibility defaults (-fvisibility=hidden would hide what the cores need), and the ImGui/Fast3D globals above are each capable of breaking it, and none has been tried yet.
