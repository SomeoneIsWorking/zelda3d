# SoH3D backlog

Durable, in-repo task list (survives PC switches and fresh agent sessions). Keep this honest:
when an item lands, move it to **Done** with the commit; when you learn something, note it inline.
This file is the source of truth across sessions.

> **Fresh-session directive (from the user, 2026-06-18) — READ THIS FIRST:**
> **NEVER ask the user anything. NEVER. Do not even chat or narrate. ONLY WORK.** Pick items from
> this list and FIX them, committing each verified fix yourself (3-repo chain:
> libultraship→fork/soh3d, Shipwright→fork/develop, outer→origin/main). No questions, no status
> messages, no "should I" — just do the work and keep going until the list is done. The user is
> playtesting on Vulkan and will keep dropping new bug reports/screenshots; silently capture each
> new one into this file and keep fixing. Do NOT pause to confirm anything.
>
> Suggested early picks (but use your judgment, just work): #19 (dungeon warps — trivial, indices
> listed, unblocks testing), #13 (wrong entrance spawn points), #14/#16 (systemic climb/void
> collision — one root cause likely clears several), #1 (Vulkan FB upside-down — the big proper fix:
> negative-height viewport, Metal-style). MANY items share root causes (collision/terrain-warp Y
> for #4/#9/#10/#13/#14/#16/#25; En_Ko anim/variant for #7/#8; render/material for #11/#20/#24).
> Read the [[soh3d-vulkan-runsh-and-flip]] memory + `run.sh` first. Run the game via
> `tools/soh3d_game.sh` (skill: soh3d-game-control). Verify with screenshots; commit each fix.

## Open

1. **Cutscene / title / demo camera goes UNDER the 3DS terrain (N64 camera sequence vs 3DS terrain
   heights)** — FINAL diagnosis from the user (2026-06-19): the title was NOT upside-down; the
   **camera was below the rendered terrain looking up at its underside** (looks like a flip; sky
   below, ground above). Root cause, in the user's words: **"it is using 3DS terrain with the N64
   sequence."** The scene renders the OoT3D (3DS) terrain mesh, but the cutscene/demo CAMERA runs the
   original N64 sequence (camera eye positions authored for the N64 terrain heights). Because the 3DS
   terrain surface sits at a different height than the N64 floor those camera coords assume, the
   scripted camera ends up beneath the 3DS surface. It "self-corrects on a camera change" only
   because the camera then moves out from under the terrain. Confirmed in scene 0x51 with cam
   eye=(-4000,-1,5228) (eye Y=-1, under the surface). NOTE the EARLIER theories are RULED OUT: not an
   FB-sampling flip, and not a projection-matrix flip (instrumented P[1][1]=+1.71 stable from frame 0
   in a normal scene; reverted that SOH3D_PROJDBG probe). FIX DIRECTION: reconcile the scripted
   (N64) camera with the 3DS terrain height — likely the same terrain-warp axis as [[soh3d-terrain-warp]]
   (warp the OoT3D render mesh to the N64 floor so N64 camera coords match), but applied to/through
   the cutscene & title-demo camera path, not just gameplay.
   MECHANISM CONFIRMED (2026-06-19): terrain-warp (re-levels the 3DS render mesh to the N64 floor) is
   GATED OFF whenever collision is ON — `SoH3D_TerrainWarpEnabled() = gSoH3dTerrainWarp &&
   !SoH3D_CollisionEnabled()` (soh3d.c:269) — and collision is ON by default (`gSoH3dCollision=1`).
   So the default path injects OoT3D collision and renders the 3DS terrain at its NATIVE height (Link
   walks it fine, render==collision), but the N64 cutscene/demo camera (N64-authored eye coords) is
   never reconciled with the native 3DS terrain → camera under the surface. terrain-warp and
   collision are deliberately mutually exclusive (soh3d.c:266-268 "Collision wins"). FIX OPTIONS:
   (a) reconcile the scripted cutscene-camera eye.y to the 3DS surface (probe 3DS mesh height at the
   eye xz, lift when below) — risks breaking intentional low/indoor shots; (b) make 3DS terrain and
   N64 heights agree globally so N64 camera data "just works" (the real fix, biggest).

1b. **Title-screen flow is broken** (user, 2026-06-19) — (a) the title demo's scene load can CRASH:
   `Scene_CommandAlternateHeaderList` -> `OTRScene_ExecuteCommands` -> `Play_Init` while loading
   SCENE_ZORAS_RIVER for the title backdrop (seen via the "Restart -> Title" menu row). (b) Pressing
   Start/A on the title does NOT reliably go to File Select — sometimes lands in "a weird place". (c)
   If you DON'T press Start, the demo proceeds and dumps you into a playable/movable state (user
   ended up in the Sages cutscene area able to move). Title gamestate / demo-scene routing is
   unstable. Likely related to #1 (first-frame scene setup) and the title demo scene table.

2. **Universal Start-skip for cutscenes** — everything that takes control away from Link must be
   Start-skippable: scripted CS cutscenes, onepoint cameras, actor-driven sequences, item-get
   freezes. No auto-skip, no forced-watch. Scripted CS terminators already skip on Start
   (`z_demo.c` `csSkipButton`, frames>20). REMAINING: Player cutscene-mode (`csMode`), onepoint
   cams, actor-driven. User accepts the iterate-on-softlock approach (they report what softlocks).
   (DONE: holding Start skips dialogs — `z_message_PAL.c` all 4 text-advance sites.)

9. **Grass/lilypad area not walkable (collision regression)** — Link can't enter a forest pond-edge
   lilypad patch that "used to work." Check collision / terrainwarp path; needs in-game repro.

10. **Boulder half-clipped underground** — a large rock's render sits too low (bottom buried). Per-
    actor yoff/anchor. Identify rock actor/model (En_Ishi large / Obj rock), raise to ground.

11. **Kokiri sword chest too big** — renders as the large treasure chest; should be the small chest.
    INVESTIGATED 2026-06-19: NOT an OoT3D-replacement scale bug. En_Box's OoT3D model `tr_box.cmb`
    (zelda_box.zar) is SKINNED (3 bones — animated lid), so the auto path SKIPS it and chests render
    as **N64**. The N64 En_Box already draws small (type SMALL/6/ROOM_CLEAR_SMALL/SWITCH_FALL_SMALL →
    `Actor_SetScale 0.005`) vs big (0.01) correctly, and CSMC (ChestSizeAndTextureMatchContents) is
    OFF by default (only read in z_en_box). So if the Kokiri-sword chest looks big, check: (a) is that
    chest's `type` actually a SMALL type in this build/save? (b) is some CVar (CSMC / size-by-contents
    / rando) enabled? Repro the SPECIFIC chest (Kokiri Forest training maze) and read its
    `params>>12 & 0xF`. The proper 3DS path would need a SKINNED chest replacement (animated lid),
    like the calibrated sModelTable n64anim entries — bigger than a scale tweak.

12. **Skip chest-opening + reliable dialog fast-forward** (part of #2) — opening a chest takes
    control (get-item freeze) and must be Start-skippable. Also dialogs aren't fast-forwarding for
    the user — verify the hold-Start dialog skip (just added) works, and consider enabling the
    SkipText enhancement by default so B/Start fast-advance text instantly.

13. **Wrong entrance spawn points (CLUSTER, some SEVERE)** — entrances land at the wrong spawn:
    (a) Kokiri shop door → Link's house; (b) Kakariko graveyard → immediately transitions back out
    (spawns Link ON the exit/leave trigger); (c) a Kakariko door (screenshot: posted-notices door)
    → enter → fall into the void → respawn inside → fall again FOREVER (infinite void loop, can only
    escape by debug-teleport; can never die/land). Likely one root cause: wrong spawn index /
    spawn-point lookup / door→entrance routing, dropping Link outside the room floor. HIGH priority
    (the void loop is a hard softlock). Check entrance table + spawn handling.

14. **Link drops off EVERY climbable surface halfway** (collision, SYSTEMIC) — not just one ladder;
    all climbables (ladders/vines/walls) drop Link partway up. Likely a general climb-collision
    regression (terrain-warp / OoT3D-vs-N64 collision height for climb surfaces). High-value: one
    root cause probably fixes the whole climb cluster. Cluster with #4 (ladder render too high),
    #9 (grass not walkable).

29. **Hand-weave the 3DS Link model (multi-CMB assembly)** — with the new menu toggle (#15), mode 1
    (3DS model + N64-retarget anim) renders, but the user reports it "looks weird, doesn't look like
    N64 anim — must be hand-weaved." I.e. the 3DS Link replacement needs a hand-curated multi-CMB
    assembly (like `kAssemblies` in [[soh3d-auto-replace]], same as Gohma #17) rather than the
    generic path; the N64-anim retarget alone isn't producing a correct Link. See
    [[soh3d-link-player-path]]. (The #15 menu toggle itself works — this is the underlying model
    quality.)

16. **Gohma arena void-out** — walking in the Gohma (Deku Tree boss) arena drops Link through the
    floor → void. Collision hole. Repro at entrance 1039.

17. **Gohma model hand-weave** — Gohma needs hand-curated multi-CMB assembly (like kAssemblies in
    [[soh3d-auto-replace]]); generic auto-merge is unsound.

18. **Deku Baba no combat interaction (UNCERTAIN)** — a Deku Baba inside the Deku Tree couldn't be
    hurt / couldn't hurt Link. Only seen AFTER teleporting to Gohma, void-dying several times → game
    over → respawn in Deku Tree room 1, so it may be state corruption from the void deaths, not a
    general Deku Baba bug. Low confidence; revisit once #14/#16 (collision void-outs) are fixed.

21. **Giant boulder overlapping Temple of Time** (Market) — a large rock mesh clips into/overlaps
    the Temple of Time building (user screenshot). Misplaced/wrong-scale scene geometry or actor.
    Repro: Market (177), look toward ToT.

23. **Cucco wing-flap animation not implemented** — the cucco (chicken, En_Niw 0x19) OoT3D
    replacement doesn't play its wing-flap. ROOT-CAUSED 2026-06-19 (asset + code, ground truth):
    - The N64 cucco wing-flap is **PROCEDURAL, not in any animation**: `EnNiw_OverrideLimbDraw`
      (z_en_niw.c:1112) ADDS per-limb rotations from `unk_2C4..unk_2E0` onto N64 limbs **7 & 11
      (the two wing-tips), 13 & 15 (head/comb)** during the skeleton draw traversal. Those fields
      are driven by `func_80AB5BF8(this,play,arg2)`: idle/walk (arg2=1) flaps the wings ~7000 binang
      (~38°), agitated/thrown up to 25000 (~137°), about the wing's local **Z** axis (unk_2C4 →
      limb7.z, unk_2D0 → limb11.z). gCuccoAnim/jointTable does NOT contain the flap.
    - The OoT3D cucco (`/actor/zelda_nw.zar`: chicken.cmb 9 bones + nw_hane_model.cmb wing + ONLY
      `nw_wait.csab`) plays via the **AUTO path** (gCuccoAnim→nw_wait in soh3d_animmap.inc:676).
      nw_wait (11 frames) animates bones 2-8 but only a **subtle idle ruffle** (wings = bones 3 & 5,
      tips 4 & 6; rX swing only ±9°). There is **no big flap** in the asset, so OoT3D needs it
      injected procedurally too — exactly like N64.
    - WHY it can't be a simple anim-map: the AUTO path (SoH3D_DoRetarget, gSoH3dPendingAuto) plays a
      CSAB phase-locked to the N64 playhead and **ignores jointTable per-limb** entirely. The
      retarget hook (SoH3D_SkelAnimeDraw) also drops the `overrideLimbDraw` callback, so the
      procedural rotation is invisible to it.
    - PROPER FIX (multi-layer, the real work — NOT a bandaid): inject a per-bone local rotation
      DELTA onto the CSAB pose for the cucco's wing bones. (1) thread `overrideLimbDraw`+`arg` from
      `SkelAnime_DrawSkeletonOpa` into `SoH3D_SkelAnimeDraw`; (2) for limbs the override touches,
      delta = (override-applied rot) − jointTable rot; (3) map N64 wing limbs 7,11 → OoT3D wing
      bones 3,5 (derive exact corr via SOH3D_SKELDUMP=1 — N64 dump captured, OoT3D bone dump TODO);
      (4) pass `{boneIdx,dRotXYZ}` deltas through SoH3D_UpdateAnimAuto→SoH3D_UpdateAnim→Csab::
      skinMatrices and apply the extra local rotation at those bones in csab.cpp's
      animated_bone_world before world-composing. VERIFY via REPL `isolate` of the wing region with
      the flap forced 0 vs high. Deferred (low priority, substantial plumbing). See
      [[soh3d-n64anim-csab-map]]. **Generalizes**: any actor whose limb motion is procedural via an
      OverrideLimbDraw (not in its anim) needs this same delta-injection.

25. **NPC walking in mid-air** (Kakariko) — a townsperson (running man?) is animated walking high
    above a building/roof instead of on the ground (user screenshot). Actor Y-placement vs floor;
    same collision/terrain-warp Y cluster as #4/#9/#10/#13/#14/#16.

26b. **DM gate collision (UNVERIFIED leftover from #26)** — the #26 MODEL is fixed (gate renders
    correctly now). The user also reported "no collision". The N64 Bg_Gate_Shutter keeps its own
    collision (we only swap the render), so this may have been a misperception caused by the old
    wrong model rendering away from the real gate. Re-check in-game whether Link is actually blocked;
    if genuinely passable, investigate the gate's dynapoly separately. (Lower priority.)

4. **Kakariko ladder render too high** — the OoT3D ladder model renders a bit high, so Link appears
   to float when climbing. Lower it slightly (per-actor yoff / placement). Tune live via REPL
   `yoff`, then bake.

5. **Real polygon stairs for fake flat stairs** — original primary goal. Stairs rendered as flat
   textured planes should become real 3D stepped polygon geometry. NOTE: OoT3D ALSO uses flat
   fake-textured stairs, so this CANNOT be solved by routing to OoT3D geometry — the stepped
   meshes must be PROGRAMMED/GENERATED ourselves (synthesize real steps where the game has fake
   flat stairs).

6. **Epona → OoT3D model** (lower priority) — Epona still renders as the N64 model; actor-
   replacement gap.

27. **More foliage** (LOWEST priority — do last) — add more foliage/vegetation density to the world
    for a lusher look.

28. **Sky looks bad (low-res N64 skybox)** (NORMAL priority — not low) — the sky is the N64 skybox
    (N64 Fast3D path, not OoT3D assets), blurry/low-res. Improve: hi-res sky texture or use the
    OoT3D sky. Note: the texpack (model textures) doesn't cover the N64 sky/HUD path —
    see [[soh3d-texpack]]. (Foliage #27 is the only "extra/lowest" item — foliage is already 3DS.)

## Done (recent)

- **SOH3D_ENTRANCE accepts hex** — `SoH3D_AutoWarpEntrance` used `atoi()` (decimal-only), so
  `tools/soh3d_game.sh start 0xDB` silently parsed `0xDB`→`0` and loaded the Deku Tree (scene 0x0)
  instead of Kakariko (0x52). entrance_table.h indices + the BACKLOG/memory notes quote entrances
  in hex as often as decimal, so this was a real footgun. Switched to `strtol(v,NULL,0)` (base 0:
  hex OR decimal), matching SOH3D_TIME. VERIFIED: `start 0xDB` now lands in scene=0x52 (== decimal
  219). soh3d.c only.

- **#8 Kokiri kids (En_Ko) stuck animation** — every auto-replaced kid looped ONE frozen pose
  (and a kid who should SIT stood). ROOT CAUSE: the km1/kw1 Kokiri skeletons animate from the
  SHARED `object_os_anime` bank (`gKokiri*Anim`), but the anim-match pipeline keys N64 anims to a
  character's OWN object (`zar_to_object`), and `object_km1`/`object_kw1` have NO AnimationHeaders
  — so km1/kw1 never entered `animmap.json` and EVERY live anim (e.g. `gKokiriCuttingGrassAnim`)
  fell through `SoH3D_ResolveAutoCsab` to the model default idle (`fad_kusu_to_wait`, a Fado pose).
  FIX (durable, in the pipeline — no hand-edit of the generated `.inc`): (1) added `SHARED_ANIM_BANKS`
  to `tools/soh3d_anim_export.py` so a zar can source N64 anims from extra shared-bank objects
  (`/actor/zelda_km1.zar -> object_os_anime`); the runtime OTR key stays `objects/object_os_anime/
  gKokiri*Anim`, and since km1 & kw1 carry the SAME `km1_*` CSAB set one km1 key resolves for both
  bodies (no duplicate entries). (2) The gKokiri anims are mostly 2-frame static-pose holds, so
  frame-delta auto-matching is meaningless → added 26 SEMANTIC overrides to
  `tools/skeldata/charcompare_overrides.tsv` (suwari=sit, agura=cross-legged, kusakari=cut-grass,
  shinpai=worried, usirote=hands-behind, nokezori=lean-back, etc.). Regenerated animmap.json +
  soh3d_animmap.inc (978 entries, 30 overrides) + charcompare_index.inc; diff is CONTAINED (only
  new `object_os_anime` rows, zero change to the other 116 characters). VERIFIED headless in Kokiri
  Forest (238): `animdbg` shows `gKokiriCuttingGrassAnim->km1_kusakari`, `gKokiriIdleAnim->
  km1_ukiuki_wait` (no more `[default-idle]`); the km1 boy renders a cutting-grass crouch and the
  kw1 girl a natural standing idle (both correct 3DS poses). See [[soh3d-n64anim-csab-map]].

- **#20 Market NPCs render "zebra-striped" / contorted (skin-pose interpolation bug)** — the
  townsfolk by the Market fountain (En_Hy/En_Mu, e.g. zelda_mu `marketpeople.cmb`, an animated
  crowd) shattered into white striped spikes. NOT a texture/material/variant bug — the En_Hy body
  texture and the bind pose render correctly (verified: python CPU skinning is clean across the
  whole `mu_matsu` anim, in-game bind pose is clean). ROOT CAUSE: the per-subframe FPS interpolation
  in `soh3d_gl.cpp` blended the two logic-frames' skin matrices COMPONENT-WISE. That is only valid
  for tiny rotations; for the crowd's large per-frame limb swings the blend of two rotation matrices
  is no longer a rotation and collapses, exploding the mesh into spikes. FIX (3 parts): (1) a skin
  matrix = animWorld·invBind, whose translation column bakes in invBind, so even rotation-aware
  interpolation of the skin matrix drifts — instead RECOVER the animated bone-world (skin·bind,
  a clean rigid R|T), interpolate THAT (quaternion nlerp + scale/translation lerp via interpRigid),
  then re-apply invBind. (2) Upload the model's constant bind matrices to the GL layer once
  (`SoH3D_GL_SetBoneBind`, called from all three skin-matrix producers in soh3d_model.cpp). (3)
  Detect an animation DISCONTINUITY (non-seamless loop wrap / anim switch: any bone rotating >90deg
  in one 20fps step, impossible for continuous motion) and snap the whole pose to the current frame
  rather than morph through a wrong intermediate. Applied at all 4 interp sites (shadow/AO/main/Vk).
  VERIFIED headless across the full anim cycle (8+ frames): crowd animates smoothly, no shatter;
  dogs/soldiers/Malon/Gerudo unaffected (small-rotation actors interpolate as before). Files:
  libultraship soh3d_gl.cpp/.h, soh soh3d_model.cpp.

- **#26 Kakariko DM-trail gate renders the correct gate model** — Bg_Gate_Shutter uses
  OBJECT_SPOT01_MATOYAB (zelda_spot01_matoyab.zar), shared with the windmill mechanism; the auto
  "largest CMB" pick gave the gate the mechanism CMB (c_matoate_before), so it rendered as a
  beam/brick structure. Forced it to its own CMB (c_s01tomegate = 留め門). The two CMBs are authored
  at different unit scales (matoate ~1402 vs gate ~111), so the gate uses its OWN scale, calibrated
  live to 1.4 (gate fills the archway). VERIFIED headless: barred gate blocks the passage. Collision
  is the N64 actor's own (left as #26b to re-check).
- **#24 Kakariko well shows 3DS water, not windmill blades** — root cause: `Bg_Spot01_Fusya`
  (windmill), `_Idohashira` (well pillar) and `_Idomizu` (well water) all share OBJECT_SPOT01_OBJECTS,
  so the auto "largest CMB" pick gave every one the windmill blades (`c_s01fusya`, 56KB). Added a
  forced-CMB auto key (`"<zar>|<cmbSubstr>"` in loadAutoModel) and routed each actor to its own CMB
  (fusya / idohashira / idomizu) in SoH3D_TryDrawActor at the shared object scale (0.01268, REPL
  `gscale 7|8|9`). VERIFIED: well now renders the teal 3DS water + pillar (no blades); windmill
  unchanged. Tooling: `tools/` ctr_romfs+zar can list a ZAR's CMBs (used to find the names).
- **#7 En_Ko (replaced actors) draw past the N64 cull distance** — added a pure predicate
  `SoH3D_ActorHasReplacement(play, actor)` (mirrors the table/auto lookups in SoH3D_TryDrawActor /
  SoH3D_TryAuto without drawing) and called it in `Ship_CalcShouldDrawAndUpdate` (z_actor.c): a
  replaced actor forces `shouldDraw=shouldUpdate=true`, so it keeps rendering+animating instead of
  popping out at the vanilla cull distance. Generalizes to ALL replaced actors, not just kids. Also
  added `drawn=<0|1>` (Actor.isDrawn) to the `actorscan` REPL output. VERIFIED quantitatively: kid at
  dist 1790 from Link → drawn=1 replaced / drawn=0 not-replaced (`auto` toggle A/B).
- **#19 Dungeon entrances in Debug menu** — added a "Dungeons" section (12 first-room warp rows) to
  the Debug pane of `soh3d_test.rml`. NOTE: the BACKLOG's listed indices were all off-by-one; the
  correct literal `entrance_table.h` hex→dec values are Deku Tree 0, Dodongo's 4, Jabu 40, Forest
  361, Fire 357, Water 16, Shadow 55, Spirit 130, Well 152, Ice 136, Gerudo Training 8, Ganon's
  Castle 1127 (verified vs the working Kokiri 238=0xEE / Gohma 1039=0x40F rows). VERIFIED: Deku Tree
  warp loaded scene 0x0 with Link on the floor.
- **#22 Warp day/night selection** — "Warp Time-of-Day" cycle row (Default/Day/Night) in the Debug
  pane; soh3d.c sets `gSoH3dForceTime` (Day=0x6000, Night=0x0000, Default=-1) when it consumes
  `gSoH3dMenuWarp`, so the new scene's `Play_Init` (`SoH3D_ApplyForceTime`) picks the right day/night
  actor set. Generic `cycle="<id>"` row infra added to SohRmlUi.cpp (CycleSpec, mirrors ToggleSpec).
- **#15 Link model/anim menu toggle** — "Link Model / Anim" cycle row (N64 / 3DS·N64 anim /
  3DS·3DS anim) in the Graphics pane → `gSoH3dMenuLinkMode` (defined in SohRmlUi.cpp); soh3d.c seeds
  it from the live mode once, then applies changes to `gSoH3dLinkOn`/`gSoH3dLinkAnimSrc`. VERIFIED:
  the row cycles and applies live (Link switched to the 3DS model in-game). Underlying 3DS-Link
  render quality is now tracked as #29 (hand-weave needed).
- **RmlUi "Restart → Title Screen"** — Debug-tab row (`restart="1"` → `gSoH3dMenuRestart` →
  soh3d.c `SoH3D_ReplPoll` → `SET_NEXT_GAMESTATE(Title_Init)` + `NA_BGM_STOP`).
- **run.sh black screen on Vulkan** — root cause: a stale config (`shipofharkinian.json`, cwd-
  relative; run.sh's `cd "$SOH"` loaded a different one than the game-manager) had
  `AdvancedResolution.Enabled=1`, which makes the game render to an offscreen `mGameFb` whose
  composite-to-window used to be SoH's ImGui pass — REMOVED in SoH3D (RmlUi replaced ImGui) — so
  fb 0 stayed black while RmlUi still drew. AdvancedResolution is effectively unsupported now.
- **run.sh cold boot to title screen** — plain `./run.sh` now powers on to the title screen with a
  normal save + normal day/night clock; the dev-warp shortcuts (`SOH3D_WARP`/`ENTRANCE`/`TIME`/
  `COLDBOOT`) are opt-in env overrides, off by default.
- **run.sh no longer erases uncommitted engine edits** — `sync_submodule_to_pin` now preserves a
  dirty submodule working tree instead of hard-resetting it to the pin.
