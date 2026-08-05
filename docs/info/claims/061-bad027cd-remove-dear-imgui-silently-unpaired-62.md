---
id: C061
kind: claim
status: holds
created: 2026-08-06
tags: imgui
depends: Shipwright/libultraship/src/ship/window/gui/Gui.cpp
---

## Claim

bad027cd (remove Dear ImGui) silently unpaired 62 InitElement() bodies from their callers: 2 became heap-corrupting unpaired free()s and 32 are unfixed FUNCTIONAL GAPS

## Evidence

Gui::AddGuiWindow no longer calls guiWindow->Init(), and GuiElement::Init() is the ONLY caller of InitElement(). Both MessageViewer classes calloc in InitElement and free in their destructor with no member initializer, so the destructor free()d foreign pointers -- MALLOC_PERTURB_=165 aborts with 'free(): invalid pointer' inside ~MessageViewerWindow, and zeroing just those two words under gdb (no rebuild) removes the downstream SIGSEGV. Survey of all 62 InitElement definitions: 2 hazards, 1 already safe, 27 no-ops, 32 functional gaps -- SaveManager stats/tracker sections never registered, cosmetics never hydrated, BenMenu (no other call site) never built, disabledMap empty before ~75 .at() calls.

## What would falsify it

a grep showing Gui::AddGuiWindow calls guiWindow->Init() again, or a survey finding the 32 functional-gap InitElement bodies have been given non-Init call sites
