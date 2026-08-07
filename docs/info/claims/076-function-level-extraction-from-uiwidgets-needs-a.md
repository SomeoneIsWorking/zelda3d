---
id: C076
kind: claim
status: holds
created: 2026-08-07
tags: n3,gui,shared,method
depends: Shipwright/zelda3d_shared/gui/ui_primitives.cpp
---

## Claim

Function-level extraction from UIWidgets needs a THREE-part gate, not identical bodies: (1) body byte-identical, (2) header declarations identical including defaults AND the whole overload set, (3) no parameter type from the divergent part of UIWidgets.hpp. Screening on bodies alone produces wrong answers.

## Evidence

Applied to the 18 identical-body functions remaining after ui_theming: only 7 passed. Rule 2 caught Tooltip -- its const char* body is byte-identical in both games, but OoT declares only a std::string overload and MM only a const char* one, so extracting on body equality would have changed OoT's public API. Rule 3 caught RadioButton and StateButton, which take RadioButtonsOptions/ButtonOptions, structs that differ per game and live in the header that would have to include the shared one (circular). Also excluded: WrappedText (currentLineLength is unsigned int in OoT, int in MM -- picking one is a change, not an extraction), ClampFloat (declared in neither header, file-local), and the five CVar* widgets (identical bodies but they call ShipInit::Init, supplied from a per-game header). Verified after extracting the 7: both games build with 0 errors, nm shows all 7 defined once per game from ui_primitives.cpp.o with none left in either UIWidgets.cpp, sequence gate mm,oot exit 0.

## What would falsify it

if a function passes all three rules and still cannot be extracted, the gate is missing a rule and should gain one rather than be worked around
