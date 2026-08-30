---
id: 22
title: Clang structure gate blocked by overgrown SoH enhancement monoliths
status: open
symptom: tools/verify_clang.py fails before format and tidy because eleven untouched SoH enhancement files exceed their frozen legacy line ceilings by 1 to 9 lines
tags: tooling,structure,blocker
created: 2026-08-30
updated: 2026-08-30
---

The 2026-08-30 BossFd2 sampler pass exposed a repository-baseline failure in the normal Clang verifier. The touched SDL backend shrank and its ceiling was ratcheted 2837 -> 2835, but eleven unrelated committed SoH enhancement files already exceed their frozen ceilings: timesaver_hook_handlers.cpp, debugconsole.cpp, actorViewer.cpp, debugSaveEditor.cpp, randomizer/{draw,location_access,logic,randomizer_check_tracker,settings}.cpp, tts.cpp, and SaveManager.cpp. Raising limits or deleting blank lines merely to pass is forbidden. Each file needs a real responsibility extraction (or the prior growth reverted at its owning change), followed by a lower ceiling. Touched graphics TUs were independently proven with git clang-format and clang-tidy, but the repository-wide structure gate remains red until this issue is resolved.
