# SoH3D Kanban

> **Generated mirror — do not hand-edit.** Source of truth = GitHub Issues on
> `SomeoneIsWorking/soh3d` (private). Regenerate with `tools/kanban.py render`.
> Move a card with `tools/kanban.py mv <#> <column>`; add with `tools/kanban.py add`.
> Columns: todo · in-progress · in-review · needs-confirmation · reopened · blocked · done(=closed).  Updated 2026-06-23 16:45.

**Counts:** todo:3 | in-progress:2 | in-review:0 | needs-confirmation:34 | reopened:1 | blocked:1 | done:43

## 📋 To Do  (3)
- [#17](../../issues/17) Hi-res world/scene textures (texpack) _(render)_
- [#33](../../issues/33) More foliage / vegetation density (lowest priority) _(scene)_
- [#82](../../issues/82) NPC drop-shadow circle renders on her head, not at her feet _(render,scene)_

## 🔨 In Progress  (2)
- [#29](../../issues/29) Giant boulder overlaps the Temple of Time (Market) _(render,scene)_
- [#89](../../issues/89) Tooling: export authoritative per-actor variant+animation from OoT3D (Azahar oracle) — base SoH3D on 3DS not N64

## 🔍 In Review (agent verifying)  (0)
_none_

## 🙋 Needs User Confirmation  (34)
- [#1](../../issues/1) Stairs: real stepped geometry, wall preserved (render broken) _(render,stairs,scene)_
- [#2](../../issues/2) Kakariko well: water is a tiny teal diamond, not a surface _(render,well)_
- [#4](../../issues/4) Title/cutscene camera goes under 3DS terrain _(render,camera)_
- [#5](../../issues/5) Cucco wings don't flap (idle + held/agitated) _(anim,cucco)_
- [#6](../../issues/6) Held cucco renders at pickup spot, not in Link's hands _(render,link,cucco)_
- [#7](../../issues/7) 3DS Link (3DS anims): motionless slide + long right arm _(anim,link)_
- [#9](../../issues/9) 3DS Link can't pick up a cucco in 3DS-anim mode _(anim,link)_
- [#11](../../issues/11) Kokiri clover/lilypad pond patch not walkable _(collision,scene)_
- [#12](../../issues/12) Inventory/pause background renders upside-down _(render,ui,inventory)_
- [#13](../../issues/13) Child Zelda renders ~half size _(render,scene)_
- [#14](../../issues/14) Title-screen flow unstable (crash / wrong gamestate) _(camera,scene)_
- [#15](../../issues/15) Universal SPACE skip for cutscenes/onepoint/item-get _(keyboard)_
- [#16](../../issues/16) First-person camera: early-load crash + position snap _(camera,crash)_
- [#18](../../issues/18) Crisp HUD/UI textures (hearts/digits/buttons/icons) _(ui,hud)_
- [#21](../../issues/21) Item/C-button UI icons render as garbled noise (texture corruption) _(render,crash,ui,hud)_
- [#24](../../issues/24) Skip chest-open / get-item freeze + reliable dialog fast-forward _(keyboard)_
- [#26](../../issues/26) Gohma arena void-out (matches N64 — confirm differs from vanilla?) _(collision,scene)_
- [#27](../../issues/27) Gohma model needs hand-curated multi-CMB assembly _(render,anim)_
- [#70](../../issues/70) Link roll animation broken (3DS Link) _(anim,link)_
- [#72](../../issues/72) Graphics menu toggles (shadows/AO/scene lighting) do nothing _(render,ui)_
- [#73](../../issues/73) Market/town NPCs (En_Hy adults) render in T-pose / arms splayed _(render,anim)_
- [#75](../../issues/75) Gold Skulltula (En_Sw) renders malformed (legs splayed, distorted body) _(render,anim)_
- [#76](../../issues/76) Auto-replaced NPC animation playback wrong (Kokiri kids: one too fast, one frozen) _(anim)_
- [#77](../../issues/77) Kakariko well: wooden frame structure floats at water level, should be above the well _(render,well,scene)_
- [#78](../../issues/78) Kokiri Forest big chest renders TOO big (oversized scale) _(render,scene)_
- [#80](../../issues/80) Rolling boulder slides instead of rolling (no spin) _(anim,scene)_
- [#86](../../issues/86) 3D3 Link: upper torso briefly snaps ~90deg right after stopping a walk; run-off-edge jump looks wrong _(anim,link)_
- [#91](../../issues/91) Vulkan/Wayland swapchain teardown double-free on window close _(render,crash)_
- [#92](../../issues/92) Replicate OoT3D title screen scene (visually indistinguishable) _(render,camera,scene)_
- [#94](../../issues/94) Per-limb material/facial channel + Saria ocarina (keystone #3) _(render,anim)_
- [#95](../../issues/95) Audio thread use-after-free on clean window-close teardown _(crash)_
- [#96](../../issues/96) PC-native default keyboard + controller mapping (input layer)
- [#97](../../issues/97) PC-native HUD: replace N64 Fast3D HUD with RmlUi overlay _(ui,hud)_
- [#98](../../issues/98) RmlUi menu Audio tab: Master/Music/SFX volume knobs (Phase 3 CVar binding) _(ui)_

## ♻️ Reopened (was done, found broken)  (1)
- [#8](../../issues/8) 3DS Link (N64 anims): head pitched down, arms wrangled _(anim,link)_

## ⛔ Blocked (needs live/user input)  (1)
- [#23](../../issues/23) Kokiri sword chest renders as the large chest, not small _(render,scene)_

## ✅ Done (recently closed)  (showing 30 of 43)
- [#99](../../issues/99) RmlUi VK: clip-mask path (overflow:hidden + border-radius) causes ghost/corrupt render
- [#87](../../issues/87) Kokiri kids (En_Ko): idle variants all identical + sitting girl plays idle sway (anim/variant selection)
- [#74](../../issues/74) Can't climb vine wall (climb does not initiate)
- [#10](../../issues/10) Child Link floats above plank platform by ladder
- [#25](../../issues/25) Link drops off every climbable surface halfway up (systemic)
- [#30](../../issues/30) NPC walks in mid-air above a Kakariko building
- [#69](../../issues/69) Child Link float fixed (posed-feet grounding)
- [#68](../../issues/68) run.sh preserves uncommitted submodule edits
- [#67](../../issues/67) run.sh cold-boots to the title screen by default
- [#66](../../issues/66) run.sh black screen on Vulkan (stale AdvancedResolution config)
- [#65](../../issues/65) SOH3D_ENTRANCE accepts hex (strtol base 0)
- [#64](../../issues/64) RmlUi 'Restart -> Title Screen' menu row
- [#63](../../issues/63) Warp day/night selection (Debug menu)
- [#62](../../issues/62) Dungeon entrances in the Debug warp menu
- [#61](../../issues/61) Link model/anim RmlUi menu toggle
- [#60](../../issues/60) Epona -> OoT3D model + corrected locomotion anims
- [#59](../../issues/59) Replaced actors (En_Ko) draw past N64 cull distance
- [#58](../../issues/58) Kakariko well shows 3DS water not windmill blades (forced-CMB)
- [#57](../../issues/57) Market NPCs zebra-striped/shattered (skin-pose interpolation)
- [#56](../../issues/56) Kokiri kids (En_Ko) stuck animation -> correct shared-bank anims
- [#55](../../issues/55) Cucco idle wing-flap (procedural OverrideLimbDraw replay)
- [#54](../../issues/54) DM gate collision — confirmed not a bug (no change)
- [#53](../../issues/53) Kakariko DM-trail gate renders the correct gate model
- [#52](../../issues/52) Wrong entrance spawn / graveyard bounce-out (exit-poly resourcing)
- [#51](../../issues/51) Stairs: SVG stone texture + configurable step size
- [#50](../../issues/50) Real stepped-polygon stairs from kaidan ramps (original render)
- [#49](../../issues/49) Stepped stair COLLISION: Link grounds on visible steps
- [#48](../../issues/48) 2D->3D item drops forced on by default
- [#47](../../issues/47) Press-to-skip onepoint cutscene cameras (Start/SPACE)
- [#46](../../issues/46) Xbox HUD glyphs reworked from overlay to corner badge
