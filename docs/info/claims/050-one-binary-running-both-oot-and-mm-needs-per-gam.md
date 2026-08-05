---
id: C050
kind: claim
status: holds
created: 2026-08-05
tags: 
---

## Claim

One binary running BOTH OoT and MM needs per-game SHARED OBJECTS (RTLD_LOCAL), not symbol renaming -- the collision is 6,616 globals

## Evidence

Measured with nm over the built object trees rather than taken from docs/MM_NATIVE.md's assertion: soh defines 27,275 global symbols outside libultraship/shared deps, mm defines 26,372, and 6,616 collide -- 3,809 of them unmangled C, i.e. the decomp game code itself (Actor_Draw, Actor_Delete, Play_Init, aADPCMdecImpl, ...). Renaming that many symbols in either decomp is not viable and would destroy its correspondence to the upstream decomp. The standard fix is what MM_NATIVE.md N3 already gestures at: build each game core as a SHARED OBJECT and dlopen it with RTLD_LOCAL, under which two .so files may each define Play_Init with no conflict because neither is exported into the global namespace. The launcher process then holds no game symbols itself and activates one core at a time. NOTE the earlier framing in this session -- that this is 'blocked' -- was repeating the doc; it is not blocked, it is scoped: the blocker is a linking model, and RTLD_LOCAL removes it.

## What would falsify it

a measurement showing the two cores also collide on DATA that must be shared across the dlopen boundary (a single libultraship-owned global written by both), which RTLD_LOCAL would not resolve
