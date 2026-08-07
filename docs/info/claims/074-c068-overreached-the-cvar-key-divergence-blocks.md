---
id: C074
kind: claim
status: holds
created: 2026-08-07
tags: n3,gui,cvar
depends: 2ship/2s2h/BenGui/Menu.cpp, Shipwright/soh/soh/SohGui/Menu.cpp, 2ship/2s2h/BenGui/MenuTypes.h
---

## Claim

C068 overreached: the CVar-key divergence blocks exactly ONE file of the GUI framework (Notification.cpp), not the framework. Menu.cpp's keys are already byte-identical across the games, and UIWidgets hardcodes no keys at all. What actually blocks UIWidgets/Menu/MenuTypes is ordinary behavioural divergence, which is harder to fix than a migration, not easier.

## Evidence

Menu.cpp: OoT's 10 CVAR_SETTING(...) uses expand to gSettings.Menu.{ActiveHeader,BackgroundOpacity,Popout,PoppedHeight,PoppedPos.x,PoppedPos.y,PoppedWidth,SearchAutofocus,SidebarSearch,Theme} and MM's literals are that same set byte-for-byte (OoT has one extra, gSettings.DisableChanges). That is because gSettings./gOpenWindows. are a SHARED namespace by design -- lus-cvars.cmake builds the engine's key names from those prefixes for both games. UIWidgets.{hpp,cpp}: zero CVar calls with a literal first argument on either side; every key is caller-supplied. The real blockers, measured by diff: MenuTypes.h 146/337 lines differ (VoidFunc is std::function in OoT and a raw function pointer in MM; WidgetType and DisableOption enums have different members; windowBackendsMap lists SDL3-GPU only in OoT vs DX11/OpenGL/Metal in MM); UIWidgets.hpp 570 differing lines with divergent widget APIs; UIWidgets.cpp 471 differing lines though 45 of 64 function bodies are identical; Menu.cpp 302 differing lines (race mode, search navigation, backend handling).

## What would falsify it

if a diff of Menu.cpp's CVar keys ever shows a divergent key, or UIWidgets gains a hardcoded key, the CVar migration becomes a blocker for more than Notification.cpp
