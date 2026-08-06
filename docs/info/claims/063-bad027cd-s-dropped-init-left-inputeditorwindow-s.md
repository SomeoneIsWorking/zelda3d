---
id: C063
kind: claim
status: holds
created: 2026-08-06
tags: imgui
depends: Shipwright/libultraship/include/libultraship/window/gui/InputEditorWindow.h
---

## Claim

bad027cd's dropped Init() left InputEditorWindow's timers uninitialised, and TestingRumble() gates OTRGlobals' rumble path -- rumble was very likely suppressed outright

## Evidence

InputEditorWindow.h declared mRumbleTimer / mGameInputBlockTimer / mMappingInputBlockTimer / mInputEditorPopupOpen with NO in-class initialiser; the only code that set them was InitElement, whose sole caller (GuiElement::Init via Gui::AddGuiWindow) was removed by bad027cd. TestingRumble() returns mRumbleTimer != INT32_MAX, and OTRGlobals.cpp:2443 uses it as an early-return gate that skips both StartRumble and StopRumble. Garbage differs from INT32_MAX with overwhelming probability. Separately confirmed: nothing in the repo calls Update()/UpdateElement() on a GuiWindow, so the timer could never decrement back either. NOT reproduced on the old binary -- the code path is unambiguous, the actual garbage value was not measured. Fixed twice over: Init() restored (93e342d4) plus in-class initialisers (ade82b6d).

## What would falsify it

these members declared without in-class initialisers again, or GuiElement::Init losing its caller a second time
