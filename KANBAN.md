# SoH3D Kanban

> **Generated mirror — do not hand-edit.** Source of truth = GitHub Issues on
> `SomeoneIsWorking/soh3d` (private). Regenerate with `tools/kanban.py render`.
> Move a card with `tools/kanban.py mv <#> <column>`; add with `tools/kanban.py add`.
> Columns: todo · in-progress · in-review · needs-confirmation · reopened · blocked · done(=closed).  Updated 2026-06-24 14:17.

**Counts:** todo:3 | in-progress:0 | in-review:0 | needs-confirmation:14 | reopened:0 | blocked:0 | done:77

## 📋 To Do  (3)
- [#17](../../issues/17) Hi-res world/scene textures (texpack) _(render)_
- [#33](../../issues/33) More foliage / vegetation density (lowest priority) _(scene)_
- [#106](../../issues/106) Gerudo Valley deep gorge shows skybox through backface-culled far wall (not water) _(render,scene)_

## 🔨 In Progress  (0)
_none_

## 🔍 In Review (agent verifying)  (0)
_none_

## 🙋 Needs User Confirmation  (14)
- [#12](../../issues/12) Inventory/pause background renders upside-down _(render,ui,inventory)_
- [#16](../../issues/16) First-person camera: early-load crash + position snap _(camera,crash)_
- [#18](../../issues/18) Crisp HUD/UI textures (hearts/digits/buttons/icons) _(ui,hud)_
- [#21](../../issues/21) Item/C-button UI icons render as garbled noise (texture corruption) _(render,crash,ui,hud)_
- [#72](../../issues/72) Graphics menu toggles (shadows/AO/scene lighting) do nothing _(render,ui)_
- [#91](../../issues/91) Window close does not exit cleanly (Vulkan/Wayland swapchain double-free + audio thread UAF) _(render,crash)_
- [#96](../../issues/96) Input (keyboard + controller): PC-native mapping (done) + universal Start/SPACE skip (needs work)
- [#97](../../issues/97) PC-native HUD: replace N64 Fast3D HUD with RmlUi overlay _(ui,hud)_
- [#98](../../issues/98) RmlUi menu Audio tab: Master/Music/SFX volume knobs (Phase 3 CVar binding) _(ui)_
- [#102](../../issues/102) Port real OoT3D/N64 F3DEX fog curve (Kokiri over-dense haze) _(render)_
- [#103](../../issues/103) OoT3D water surfaces render as flat opaque cyan (no texture/transparency/animation) _(render,scene)_
- [#107](../../issues/107) Stalchildren (En_Skb) zip away after spawning — broken behavior + visuals _(render,behavior)_
- [#108](../../issues/108) Extend #107 collision-sphere fix to the other SkelAnime draw choke points (DrawFlexOpa etc.) _(render,behavior)_
- [#109](../../issues/109) Stalchild (En_Skb) OoT3D model renders disassembled — bones scattered (visual half of #107) _(render,anim)_

## ♻️ Reopened (was done, found broken)  (0)
_none_

## ⛔ Blocked (needs live/user input)  (0)
_none_

## ✅ Done (recently closed)  (showing 30 of 77)
- [#105](../../issues/105) Port OoT3D water rendering to remaining scenes (Zora's/Kokiri/Gerudo river) — follow-up to #103 Lake Hylia
- [#104](../../issues/104) Lake Hylia: black void blob on grass terrain (baked shadow/AO decal renders solid dark)
- [#24](../../issues/24) Skip chest-open / get-item freeze + reliable dialog fast-forward
- [#15](../../issues/15) Universal SPACE skip for cutscenes/onepoint/item-get
- [#95](../../issues/95) Audio thread use-after-free on clean window-close teardown
- [#1](../../issues/1) Stairs: real stepped geometry, wall preserved (render broken)
- [#23](../../issues/23) Kokiri sword chest renders as the large chest, not small
- [#94](../../issues/94) Per-limb material/facial channel + Saria ocarina (keystone #3)
- [#92](../../issues/92) Replicate OoT3D title screen scene (visually indistinguishable)
- [#86](../../issues/86) 3D3 Link: upper torso briefly snaps ~90deg right after stopping a walk; run-off-edge jump looks wrong
- [#80](../../issues/80) Rolling boulder slides instead of rolling (no spin)
- [#78](../../issues/78) Kokiri Forest big chest renders TOO big (oversized scale)
- [#77](../../issues/77) Kakariko well: wooden frame structure floats at water level, should be above the well
- [#76](../../issues/76) Auto-replaced NPC animation playback wrong (Kokiri kids: one too fast, one frozen)
- [#75](../../issues/75) Gold Skulltula (En_Sw) renders malformed (legs splayed, distorted body)
- [#73](../../issues/73) Market/town NPCs (En_Hy adults) render in T-pose / arms splayed
- [#70](../../issues/70) Link roll animation broken (3DS Link)
- [#27](../../issues/27) Gohma model needs hand-curated multi-CMB assembly
- [#26](../../issues/26) Gohma arena void-out (matches N64 — confirm differs from vanilla?)
- [#14](../../issues/14) Title-screen flow unstable (crash / wrong gamestate)
- [#13](../../issues/13) Child Zelda renders ~half size
- [#11](../../issues/11) Kokiri clover/lilypad pond patch not walkable
- [#9](../../issues/9) 3DS Link can't pick up a cucco in 3DS-anim mode
- [#8](../../issues/8) 3DS Link (N64 anims): head pitched down, arms wrangled
- [#7](../../issues/7) 3DS Link (3DS anims): motionless slide + long right arm
- [#6](../../issues/6) Held cucco renders at pickup spot, not in Link's hands
- [#5](../../issues/5) Cucco wings don't flap (idle + held/agitated)
- [#4](../../issues/4) Title/cutscene camera goes under 3DS terrain
- [#2](../../issues/2) Kakariko well: water is a tiny teal diamond, not a surface
- [#89](../../issues/89) Tooling: export authoritative per-actor variant+animation from OoT3D (Azahar oracle) — base SoH3D on 3DS not N64
