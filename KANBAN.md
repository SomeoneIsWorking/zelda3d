# SoH3D Kanban

> **Generated mirror — do not hand-edit.** Source of truth = GitHub Issues on
> `SomeoneIsWorking/soh3d` (private). Regenerate with `tools/kanban.py render`.
> Move a card with `tools/kanban.py mv <#> <column>`; add with `tools/kanban.py add`.
> Columns: todo · in-progress · in-review · needs-confirmation · reopened · blocked · done(=closed).  Updated 2026-06-20 15:45.

**Counts:** todo:9 | in-progress:1 | in-review:0 | needs-confirmation:18 | reopened:1 | blocked:2 | done:38

## 📋 To Do  (9)
- [#10](../../issues/10) Child Link floats above plank platform by ladder _(collision)_
- [#11](../../issues/11) Kokiri clover/lilypad pond patch not walkable _(collision,scene)_
- [#14](../../issues/14) Title-screen flow unstable (crash / wrong gamestate) _(camera,scene)_
- [#17](../../issues/17) Hi-res world/scene textures (texpack) _(render)_
- [#20](../../issues/20) Keyboard UI / control scheme + default mapping _(ui,keyboard)_
- [#21](../../issues/21) Item/C-button UI icons render as garbled noise (texture corruption) _(render,crash,ui,hud)_
- [#24](../../issues/24) Skip chest-open / get-item freeze + reliable dialog fast-forward _(keyboard)_
- [#28](../../issues/28) Deku Baba no combat interaction (uncertain / maybe state corruption) _(scene)_
- [#33](../../issues/33) More foliage / vegetation density (lowest priority) _(scene)_

## 🔨 In Progress  (1)
- [#15](../../issues/15) Universal SPACE skip for cutscenes/onepoint/item-get _(keyboard)_

## 🔍 In Review (agent verifying)  (0)
_none_

## 🙋 Needs User Confirmation  (18)
- [#1](../../issues/1) Stairs: real stepped geometry, wall preserved (render broken) _(render,stairs,scene)_
- [#2](../../issues/2) Kakariko well: water is a tiny teal diamond, not a surface _(render,well)_
- [#3](../../issues/3) Kokiri kids pop out at distance (actor unload, want infinite) _(render,scene)_
- [#4](../../issues/4) Title/cutscene camera goes under 3DS terrain _(render,camera)_
- [#5](../../issues/5) Cucco wings don't flap (idle + held/agitated) _(anim,cucco)_
- [#6](../../issues/6) Held cucco renders at pickup spot, not in Link's hands _(render,link,cucco)_
- [#7](../../issues/7) 3DS Link (3DS anims): motionless slide + long right arm _(anim,link)_
- [#8](../../issues/8) 3DS Link (N64 anims): head pitched down, arms wrangled _(anim,link)_
- [#9](../../issues/9) 3DS Link can't pick up a cucco in 3DS-anim mode _(anim,link)_
- [#12](../../issues/12) Inventory/pause background renders upside-down _(render,ui,inventory)_
- [#13](../../issues/13) Child Zelda renders ~half size _(render,scene)_
- [#16](../../issues/16) First-person camera: early-load crash + position snap _(camera,crash)_
- [#18](../../issues/18) Crisp HUD/UI textures (hearts/digits/buttons/icons) _(ui,hud)_
- [#22](../../issues/22) Large boulder renders half-buried underground _(render,scene)_
- [#25](../../issues/25) Link drops off every climbable surface halfway up (systemic) _(collision)_
- [#26](../../issues/26) Gohma arena void-out (matches N64 — confirm differs from vanilla?) _(collision,scene)_
- [#27](../../issues/27) Gohma model needs hand-curated multi-CMB assembly _(render,anim)_
- [#29](../../issues/29) Giant boulder overlaps the Temple of Time (Market) _(render,scene)_

## ♻️ Reopened (was done, found broken)  (1)
- [#31](../../issues/31) Hand-weave the 3DS Link model (multi-CMB assembly + equipment) _(render,anim,link)_

## ⛔ Blocked (needs live/user input)  (2)
- [#23](../../issues/23) Kokiri sword chest renders as the large chest, not small _(render,scene)_
- [#32](../../issues/32) Xbox control scheme: modern dual-stick mapping + chords (no C-pad) _(ui,keyboard)_

## ✅ Done (recently closed)  (showing 30 of 38)
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
- [#45](../../issues/45) A action button → Xbox 'A' glyph on the 3D quad
- [#44](../../issues/44) Xbox face-button HUD glyphs (B + 3 C buttons cluster)
- [#43](../../issues/43) Crisp HUD counter icons (rupee gem / small key / clock)
- [#42](../../issues/42) Crisp HUD button-background disc (B/C/item/A buttons)
- [#41](../../issues/41) Crisp HUD counter font (rupee/key/ammo/timer digits)
