---
id: C062
kind: claim
status: holds
created: 2026-08-06
tags: imgui
depends: Shipwright/libultraship/src/ship/window/gui/Gui.cpp
---

## Claim

The InitElement gap splits WINDOW vs MENU TREE: restoring Gui::AddGuiWindow's Init() is correct and safe, restoring SetMenu/SetMenuBar's is not

## Evidence

AddGuiWindow Init() restored: sohStats save section registers again -- a save carrying both sohStats and a deliberately-unregistered zelda3dBogusControlSection, loaded via REPL savecycle, warns 'unloadable section' for the control ONLY. Solo oot exit 0, 121 live-engine lines, no new errors; sequence mm,oot still exit 0 with 4/4 fresh + 0 inherited. SetMenu/SetMenuBar Init() attempted and REVERTED: gdb catch throw gives BenGui::SetupMenu -> GuiElement::Init -> BenMenu::InitElement -> Ship::Menu::UpdateWindowBackendObjects -> std::out_of_range unordered_map::at, MM boot dying before gameplay (live lines 20->5). Reverting restored MM to its baseline exactly (20 lines, exit 134 in StyleSheetFactory, 0 out_of_range).

## What would falsify it

a solo mm run throwing std::out_of_range again (menu-tree Init re-enabled), or a savecycle test where sohStats appears as an unloadable section while the bogus control does not
