---
id: 14
title: MM's collision viewer colour settings do nothing: the picker writes <key>.Value, the renderer reads <key>
status: resolved
symptom: Picking a colour in MM's Collision Viewer has no effect on the drawn collision, and Reset Colors does nothing either; the colour always renders as the built-in default
tags: mm,collision-viewer,cvar,devtools,n3
created: 2026-08-07
updated: 2026-08-07
---

## Cause

An exact-key mismatch across the three sites in `2ship/2s2h/DeveloperTools/CollisionViewer.cpp`,
symmetric across all 11 colours:

| site | key written/read |
|---|---|
| `UIWidgets::CVarColorPicker` (:82-:99) — the only WRITER | `gCollisionViewer.<X>Color.Value` |
| `CVarGetColor` in `DrawDynapoly`/:586/:637 — the READER | `gCollisionViewer.<X>Color` |
| `CVarClear` in "Reset Colors" (:53-:63) | `gCollisionViewer.<X>Color` |

`CVarColorPicker(label, valueCvar, …)` uses `valueCvar` verbatim for both its `CVarGetColor` and
its `CVarSetColor` (`BenGui/UIWidgets.cpp:996,1016`), and `ConsoleVariable::GetColor` is an exact
map lookup with no suffix logic (`libultraship/src/ship/config/ConsoleVariable.cpp:58`). So the
picked colour was stored under a key nothing ever read, and Reset cleared a key nothing ever wrote.
Measured: 11 reads and 11 clears on the bare key, 11 writes on `.Value`, and **zero** reads using
`.Value`.

Registering the same name both as a scalar and as the parent of `.Value` is also the exact shape
`Config::Save` refuses to serialise — it logs "dropping mis-registered scalar CVar" and drops one
(`libultraship/src/ship/config/Config.cpp:219-238`).

## Fix

Point the reader and the reset at `.Value`, matching the writer — that is the side holding any
colour a user has already picked, so the other direction would have orphaned real data. Non-colour
keys (`Enabled`, `ApplyShading`, `DecalMode`) are untouched.

## Evidence

After the change `strings mm.elf` shows all 11 colour keys as `gCollisionViewer.<X>Color.Value`
and **no bare parent key at all**, so the scalar-vs-parent shape is gone as well. Runtime: injected
`SceneCollisionColor.Value = (17,34,51,255)` into MM's config, booted MM headless to gameplay, and
the value survived the save/reload round trip byte-for-byte with **0** "dropping mis-registered
scalar" errors.

NOT verified: that the collision viewer visibly draws in the chosen colour — that needs the overlay
enabled and a pixel measurement. What is verified is that reader and writer now name the identical
key and that the key persists cleanly.
