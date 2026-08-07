---
id: C075
kind: claim
status: holds
created: 2026-08-07
tags: n3,gui,shared
depends: Shipwright/zelda3d_shared/gui/ui_theming.cpp, Shipwright/soh/soh/SohGui/UIWidgets.cpp, 2ship/2s2h/BenGui/UIWidgets.cpp
---

## Claim

UIWidgets is shareable at FUNCTION level though not at file level: 45 of its 64 function bodies are byte-identical between the games. The 26 PushStyle*/PopStyle* theming functions are now one source; ~19 identical functions remain extractable by the same method.

## Evidence

Brace-matched function extraction from both UIWidgets.cpp and compared body-for-body: of 26 theming functions, 25 had byte-identical bodies and all 26 DECLARATIONS matched including default arguments. The 26th, PushStyleInput(const ImVec4&), differed by exactly one number -- ImGuiStyleVar_FramePadding y of 6.0f (OoT) vs 8.0f (MM) -- preserved per game via ZELDA3D_UI_INPUT_FRAME_PADDING_Y rather than unified. Verified after extraction: build.ninja shows soh compiling ui_theming.cpp with =6.0f and mm with =8.0f; nm reports exactly 26 global T definitions per game, all from the single ui_theming.cpp.o and none left in either game's UIWidgets.cpp; sequence gate mm,oot exit 0. The file as a whole is NOT mergeable -- 471 differing lines, and GetRandomValue's signature differs between the games so they have different determinism contracts.

## What would falsify it

if a future edit makes one game's copy of a shared theming function diverge, the extraction has to be revisited rather than the difference papered over with another build define
