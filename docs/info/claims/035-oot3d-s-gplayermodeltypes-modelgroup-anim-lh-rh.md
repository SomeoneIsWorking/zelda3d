---
id: C035
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

OoT3D's gPlayerModelTypes (modelGroup -> {anim,LH,RH,sheath,waist}) is BYTE-IDENTICAL to N64's, so open hands during locomotion is CORRECT, not a bug

## Evidence

Table located at VA 0x0053a558, 16 rows x 5 bytes, by searching code.bin for distinctive N64 row signatures: CHILD_HYLIAN(01 02 09 13 14), SWORD_AND_SHIELD(01 02 0a 11 14), DEFAULT(00 00 08 12 14), BGS(03 04 09 13 14), BOW(04 01 0b 12 14) all found at consecutive 5-byte offsets in the right order. Every row matches the values in Shipwright/soh/src/code/z_player_lib.c gPlayerModelTypes. DEFAULT (index 3, the locomotion/idle group) is LH_OPEN + RH_OPEN on BOTH platforms.

## What would falsify it

OoT3D is observed rendering closed fists during ordinary running, which would mean the pose comes from somewhere other than gPlayerModelTypes + sPlayerDLists
