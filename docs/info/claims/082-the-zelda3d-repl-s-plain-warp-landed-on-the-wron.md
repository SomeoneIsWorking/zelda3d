---
id: C082
kind: claim
status: holds
created: 2026-08-12
tags: tooling
depends: Shipwright/soh/src/zelda3d/repl/zelda3d_repl.cpp
---

## Claim

The zelda3d REPL's plain `warp` landed on the WRONG entrance in every headless run that booted through the title, because it inherited gameMode=GAMEMODE_TITLE_SCREEN and cutsceneIndex=0xFFF3 from the opening gamestate; z_play.c:515 then selected a cutscene setup layer and Play_SpawnScene read gEntranceTable[entranceIndex + sceneSetupIndex].

## Evidence

Measured 2026-08-12. Same command, same destination, before vs after clearing both fields: `warp 0xEE` put Link at (4167,-171,-539) before and (-68,-79,941) after -- the latter is Kokiri Forest's actual spawn. It also crashed two ordinary destinations: `warp 0x109` SIGSEGV in Scene_CommandAlternateHeaderList (header 6 of 6), `warp 0x209` SIGSEGV in func_8002C0C0 on an empty PLAYER actor list (entrance 2 of 2, start position 7 of 1). After the fix all three land, each in a distinct in-scene position, with zero scene errors or warnings in the log. SoH's own Warping.cpp and debugconsole.cpp already set GAMEMODE_NORMAL; the REPL warp was the one entry point that did not.

## What would falsify it

a headless run whose posinfo after `warp <n>` disagrees with the same entrance reached by normal play -- and note the failure mode is SILENT, so re-check by position, never by 'it did not crash'
