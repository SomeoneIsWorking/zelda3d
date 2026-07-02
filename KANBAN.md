# Zelda3D Kanban

> **Generated mirror — do not hand-edit.** Source of truth = GitHub Issues on
> `SomeoneIsWorking/zelda3d` (private). Regenerate with `tools/kanban.py render`.
> Move a card with `tools/kanban.py mv <#> <column>`; add with `tools/kanban.py add`.
> Columns: todo · in-progress · in-review · needs-confirmation · reopened · blocked · done(=closed).  Updated 2026-07-02 18:00.

**Counts:** todo:11 | in-progress:2 | in-review:1 | needs-confirmation:26 | reopened:0 | blocked:0 | done:80

## 📋 To Do  (11)
- [#17](../../issues/17) Hi-res world/scene textures (texpack) _(render)_
- [#33](../../issues/33) More foliage / vegetation density (lowest priority) _(scene)_
- [#118](../../issues/118) Market (SCENE_MARKET_DAY) parity: crowd NPCs (En_Hy/En_Mu) mis-rendered, door still N64 _(render,scene,behavior)_
- [#130](../../issues/130) Make repo public — audit + clean git history first (no outside-repo paths, no copyrighted assets) _(infra)_
- [#133](../../issues/133) Add HTTP/remote layer to the REPL control channel (beyond FIFO)
- [#135](../../issues/135) Market: EN_TG (dancing couple) OoT3D port _(render,scene,behavior)_
- [#136](../../issues/136) Market: EN_MU (haggling townspeople) OoT3D port _(render,scene,behavior)_
- [#137](../../issues/137) Market: EN_HEISHI4 (patrolling guards) OoT3D port _(render,scene,behavior)_
- [#138](../../issues/138) Market: EN_MA1 (young Malon) OoT3D port _(render,scene,behavior)_
- [#139](../../issues/139) Market: EN_DOG OoT3D port _(render,scene,behavior)_
- [#140](../../issues/140) Navi (EN_ELF) renders as N64 sprite — needs OoT3D native replacement _(render,behavior)_

## 🔨 In Progress  (2)
- [#111](../../issues/111) World night R/G too bright — flat tint under-darkens at night (needs vertex-lighting port) _(render)_
- [#115](../../issues/115) Character/object render-parity gaps: doors, shop props/NPCs, switches still render as N64 (whole-game audit) _(render,scene)_

## 🔍 In Review (agent verifying)  (1)
- [#112](../../issues/112) START can't skip the final intro segment (Navi flies in / wakes Link) _(render,keyboard)_

## 🙋 Needs User Confirmation  (26)
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
- [#106](../../issues/106) Gerudo Valley deep gorge shows skybox through backface-culled far wall (not water) _(render,scene)_
- [#107](../../issues/107) Stalchildren (En_Skb) zip away after spawning — broken behavior + visuals _(render,behavior)_
- [#110](../../issues/110) World night/dusk lighting hue wrong — stays warm-green, not OoT3D cool-blue (multiplicative shade can't add ambient) _(render)_
- [#113](../../issues/113) Flat pale-tan triangle on Kokiri ground (distant plane slammed to full fog by steep ramp) _(render)_
- [#114](../../issues/114) Deku Tree crashes on load (SIGSEGV) — En_Box chest collider re-walk uses wrong postLimbDraw ABI _(render,crash)_
- [#116](../../issues/116) Kokiri kids (En_Ko) heads twisted into weird orientations (head-track applies non-physical headRot) _(render,anim)_
- [#117](../../issues/117) Link (OoT3D) animation parity: walk, walk-stop, pickup (rock/cucco), carry-walk diverge from OoT3D _(anim,behavior)_
- [#119](../../issues/119) Spirit Temple child Nabooru (En_Nb) lower body collapses into a white blob (skinned retarget mis-poses crossed legs) _(render,anim)_
- [#120](../../issues/120) Faithfully port OoT3D Boss_Goma (Queen Gohma) from the 3DS decomp _(render,behavior)_
- [#123](../../issues/123) Queen Gohma floats off the pillars (gap) while climbing _(render,behavior)_
- [#125](../../issues/125) Headless lavapipe SIGSEGV ~1s after first OoT3D frame (SKYBUG async vertex crash) _(render)_
- [#126](../../issues/126) Keyboard/Start input dead after ImGui removal — stub GetTopMostPopupModal returns non-null _(keyboard)_
- [#127](../../issues/127) RmlUi UI scales out of view on retina/HiDPI Mac (points-vs-pixels framebuffer mismatch) _(ui)_
- [#132](../../issues/132) Corrupt save (null JSON field) crashes at title/file-select via std::terminate _(crash)_
- [#134](../../issues/134) Link's House interior wall looks N64-rough vs overview 3DS-smooth (unresolved)

## ♻️ Reopened (was done, found broken)  (0)
_none_

## ⛔ Blocked (needs live/user input)  (0)
_none_

## ✅ Done (recently closed)  (showing 30 of 80)
- [#129](../../issues/129) Unify SoH3D into the ONE SDL3 GPU backend — single op-list + single sampler-bind path
- [#124](../../issues/124) Spirit Temple doors (jya_door/boss_door) render ~13x oversized (auto-scale measures N64 door height wrong)
- [#121](../../issues/121) Queen Gohma OoT3D model: smooth-skinning geometry defect (stretched limb)
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
