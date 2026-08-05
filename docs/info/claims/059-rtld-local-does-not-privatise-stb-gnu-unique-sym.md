---
id: C059
kind: claim
status: holds
created: 2026-08-06
tags: n3
depends: Shipwright/soh/CMakeLists.txt, 2ship/CMakeLists.txt
---

## Claim

RTLD_LOCAL does NOT privatise STB_GNU_UNIQUE symbols, and 303 core-owned ones were shared between the OoT and MM cores -- including GameInteractor's entire hook registry

## Evidence

nm -D on the built cores: soh_core 1575 'u' symbols, mm_core 925, 322 shared, 303 core-owned (tools/unique_symbol_collisions.py, exit 1). Live consequence: OoT's SohGui::SohMenu::AddMenuElements called BenGui::RegisterResolutionWidgets in libmm_core.so via the shared MenuInit::GetInitFuncs static vector, SIGSEGV. After building both cores -fno-gnu-unique: 0 unique symbols in either, 0 shared, exit 0, and the sequence run gets past the menu to RegisterImGuiItemIcons.

## What would falsify it

tools/unique_symbol_collisions.py reporting any core-owned shared unique symbol again (it exits 1), which would mean the -fno-gnu-unique flag was dropped from one or both cores
