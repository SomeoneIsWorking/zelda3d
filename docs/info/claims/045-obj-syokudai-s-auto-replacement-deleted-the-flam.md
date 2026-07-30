---
id: C045
kind: claim
status: holds
created: 2026-07-30
tags: 
---

## Claim

Obj_Syokudai's AUTO replacement deleted the flame from lit torches -- INFERRED FROM SOURCE, NOT OBSERVED

## Evidence

Two directly-read facts: ObjSyokudai_Draw emits the stand DL and then gEffFire1DL whenever litTimer != 0 (same call, two lists); and z_actor.c does 'if (!Zelda3D_TryDrawActor(...)) actor->draw(...)', so a replaced actor's Draw never runs. NOT observed in game: every torch found is unlit, so auto 0 vs auto 1 renders the same flame-less torch and cannot discriminate. Fix (skip the actor in TryAuto) is correct regardless and is confirmed by autostate reporting zero syokudai slots.

## What would falsify it

A/B a LIT torch on 'auto': if the flame is present with auto 1 before the skip, this claim is false
