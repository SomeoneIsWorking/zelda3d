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

1. **[VISIBLE ARTIFACT FIXED 2026-06-19 via backface culling; see Done] Cutscene / title / demo
   camera goes UNDER the 3DS terrain.** ROOT CAUSE of the visible "flip" was NOT terrain height:
   the OoT3D meshes were drawn DOUBLE-SIDED (no backface cull), so when the demo camera dipped under
   the Hyrule Field surface we rendered the terrain UNDERSIDE ("ground above, sky below"). N64
   backface-culls those faces (you'd see through to sky). Fixed by honoring the CMB cull byte in the
   GL+Vk draw path (faceCull). **The old "3DS terrain sits higher than N64 → cam under 3DS surface"
   hypothesis below is FALSIFIED by measurement:** N64 floor vs OoT3D mesh floor in Hyrule Field
   AGREE to ~1u (at (-4000,5228): N64 11.18 vs OoT3D 10.30), so terrain-delta camera reconciliation
   would shift the cam <1u and could not lift it above the ~11u terrain. The camera may still dip
   under terrain in some demo shots, but the RENDER now matches N64 (culled, see-through) so it no
   longer reads as a flip. REMAINING (lower priority, only if a demo shot still looks wrong): whether
   the scripted demo cam eye.y is itself mis-derived. Original (now-falsified) writeup kept below.
   ~~FINAL diagnosis from the user (2026-06-19): the title was NOT upside-down; the~~
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

13. **[FULLY VERIFIED + CLOSED 2026-06-19; see Done] Wrong entrance spawn points (CLUSTER, some SEVERE)**
    — (b) Kakariko graveyard "immediately transitions back out" ROOT-CAUSED + FIXED: SoH spawns
    Link at N64-authored coords, but the OoT3D scene-collision's scene-EXIT triangles cover
    different XZ than N64's, so the N64 spawn poly landed on an OoT3D exit poly → instant bounce.
    Fix: re-source each OoT3D floor poly's exit+cam (surfaceType low 13 bits) from the N64 floor at
    that location (soh3d.c SoH3D_N64FloorData0 + per-poly surfaceTypes). VERIFIED: graveyard (0xE4)
    now stays loaded; real exit-1 preserved at N64's (-1600,z); spawn exit=0 == N64.
    **(a)/(c) NOW RE-TESTED QUANTITATIVELY (2026-06-19, session 10) → RESOLVED, no residual.** Built a
    durable `exitgrid` REPL diagnostic (soh3d.c, like floorgrid but dumps per-floor type/exit/cam in
    ONE FIFO round-trip) and dense-scanned Kokiri Forest (0xEE, scene 0x55) and Kakariko (0xDB, scene
    0x52) under OoT3D collision (`collision 1`) vs N64 (`collision 0`):
    - Kokiri Forest step-15 grid: 27178 cells / 24202 floor hits → **0 holes either direction, 0
      exit-index mismatches**; exit histograms byte-identical (indices 3,6,9,10,11) incl. every narrow
      building doorway. So the shop doorway resolves to the N64 shop, NOT Link's house — (a) cannot
      occur via the collision/exit mechanism.
    - Kakariko step-100 grid: 2695 cells / 1110 floor hits → **0 holes, 0 floor-Y diffs >10, 0
      exit/cam mismatches**. No missing-OoT3D-floor hole at the spawn → (c) "void-loop" not
      reproducible at the collision level.
    The #13 fix re-sources exit/cam from the N64 floor wherever an N64 floor exists, and an N64 floor
    exists at EVERY OoT3D floor cell in both scenes, so OoT3D collision/exits are correct-by-
    construction == N64 (now empirically confirmed across 51k+ raycasts). If a wrong-destination is
    ever seen again it's a door/transition ACTOR (uses unmodified N64 params, vanilla-correct), not
    this collision path. Verified headless on Vulkan.

14. **Link drops off EVERY climbable surface halfway** (collision, SYSTEMIC) — not just one ladder;
    all climbables (ladders/vines/walls) drop Link partway up. Likely a general climb-collision
    regression (terrain-warp / OoT3D-vs-N64 collision height for climb surfaces). High-value: one
    root cause probably fixes the whole climb cluster. Cluster with #4 (ladder render too high),
    #9 (grass not walkable).
    **LEADING HYPOTHESIS RULED OUT 2026-06-19 (session 14): the OoT3D collision's climbable WALLS
    match N64 — this is NOT a climb-flag/height/normal regression.** Built the long-missing wall
    probe: REPL `wallscan <path>` (soh3d.c) dumps EVERY wall poly of the installed static collision
    (`play->colCtx.colHeader`) to CSV with its vertical extent (ymin/ymax), wall-property index
    (`func_80041D94`, data[0] bits 21..25), and wall flags (`func_80041DB8`). The climb mechanic
    (verified by reading z_player.c): a wall is climbable to START via `func_8083EC18` and CONTINUES
    climbing in **`func_8083FBC0` (z_player.c:7895)** only while the touched wall flag has bit1(&2),
    bit2(`func_80041E4C`), or bit3(&8=ladder) — flag bit0 alone (ledge-grab) does NOT keep you
    climbing. So a "halfway drop" would require the OoT3D climbable wall to be SHORTER than N64's, or
    to lose those flags partway up. Ran `wallscan` under `collision 1` (OoT3D) vs `collision 0` (N64)
    in TWO scenes and diffed the climb-continue walls (flags & 14):
    - Kakariko 0x52: OoT3D 13 climb-continue walls / N64 14 — the well, the two +x vine walls, and
      the ladder all present with matching flags, **identical yspans** (e.g. the 570u-tall well wall
      [200,770] in both), and perfectly vertical normals (ny=0) in both.
    - Kokiri 0x55: OoT3D 5 climb-continue walls / N64 6 — every tall N64 climb wall has a matching
      OoT3D climb wall within 0–4u XZ and **ymax within 5u** (no truncation); the one N64 "orphan"
      ladder is the same ladder shifted ~71u (still full-height). (The big flags&9 counts — 148 vs 95
      — are prop=1 flag-bit0-only ledge walls that don't allow climb-continue anyway.)
    So across both scenes the OoT3D climbable walls are faithful to N64 in flags/height/normal — same
    class of result as #13/#16 (OoT3D collision == N64 by construction). **COULD NOT repro the actual
    climb headless** (the `tp` REPL only sticks within Link's current room; Kakariko's climbables sit
    in a +x room not loaded at the entrance spawn, and Kokiri's climb input wasn't driven), so the
    item is NOT formally closed. NEXT: (a) get the USER to confirm #14 differs from vanilla SoH and
    name a SPECIFIC scene+climbable that drops; (b) if real & SoH3D-specific but not the walls, the
    remaining systemic suspects are a spurious OoT3D FLOOR poly partway up a climbable (the ledge
    check at z_player.c:11397 `BgCheck_EntityRaycastFloor1` would see it as a ledge and dismount
    mid-climb — `floorat`/`floorgrid` UP the face of a known climbable to look for an unexpected
    floor), or the climb on the 3DS-Link path (#29). Don't fabricate a wall fix — the walls are fine.

29. **Hand-weave the 3DS Link model (multi-CMB assembly)** — with the new menu toggle (#15), mode 1
    (3DS model + N64-retarget anim) renders, but the user reports it "looks weird, doesn't look like
    N64 anim — must be hand-weaved." I.e. the 3DS Link replacement needs a hand-curated multi-CMB
    assembly (like `kAssemblies` in [[soh3d-auto-replace]], same as Gohma #17) rather than the
    generic path; the N64-anim retarget alone isn't producing a correct Link. See
    [[soh3d-link-player-path]]. (The #15 menu toggle itself works — this is the underlying model
    quality.)

16. **Gohma arena void-out** — walking in the Gohma (Deku Tree boss) arena drops Link through the
    floor → void. Collision hole. Repro at entrance 1039. INVESTIGATED 2026-06-19: NOT an OoT3D-
    collision regression. Grid-probed `floorat` across the arena under OoT3D collision (default) vs
    N64 collision (`collision 0`) — the two are IDENTICAL: both have floor only in a small patch
    (x∈[150,300], z∈[400,850] @ y=-640) and "NO FLOOR" everywhere else (e.g. (0,700),(450,700)).
    ydan_boss has just 95 verts/151 polys in BOTH. So the void-out is the same under N64 — either
    original behavior (small arena, Gohma knocks you off) or a deeper non-3DS issue; fixing it would
    mean ADDING collision the original lacks (bandaid). Deprioritize / needs the user to confirm it
    differs from vanilla.

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

5. **[DONE 2026-06-19; see Done — render AND collision] Real polygon stairs for fake flat stairs**
   — original primary goal. OoT3D (like N64) renders staircases as one FLAT textured ramp whose
   step lines are PAINTED into the texture. Detection is the game's own asset label: the stair
   texture name contains "kaidan" (階段). At scene-room build time every kaidan ramp is replaced
   by GENERATED 3D step geometry (horizontal treads + vertical risers) over the same footprint, on
   the same kaidan material (UV/texture/lighting/cull preserved). Generic — applies to every kaidan
   patch in every scene. **Collision is now stepped too (2026-06-19):** the same kaidan→treads
   transform feeds the OoT3D scene-collision build, so Link grounds on the visible steps (not the
   smooth ramp). REMAINING (lower priority): step undersides are open (fine against terrain). Gate:
   env SOH3D_STAIRS (default 1) / REPL `stairs <0|1>` (render: GL caches per id — env for
   same-scene A/B; collision: built at scene load — env for A/B).

6. **Epona → OoT3D model** (lower priority) — PARTLY STALE (2026-06-19): the ranch horses
   (En_Horse_Normal 0x3C, object_horse_normal → zelda_horse_normal.zar) ALREADY render+animate as
   the OoT3D model and look correct (verified in Lon Lon Ranch — 3DS horse, grazing + mid-stride
   poses). The shared auto-skinned path (SOH3D_N64ANIM=1, default in soh3d_game.sh) drives them.
   FIXED 2026-06-19: Epona herself (En_Horse 0x14, object_horse → zelda_horse.zar/epona.cmb) had
   WRONG locomotion anim mappings — gEpona{Walking,Trotting,Galloping}Anim all auto-collided onto
   `hl_anim_slowrun_to_fastrun` (a transition clip), and JumpingHigh→wait022. Corrected to the
   exact-name CSABs (walk→hl_anim_walk2_30, trot→slowrun2_30, gallop→fastrun2_30,
   jumpHigh→jump2002), mirroring the already-correct object_horse_normal + object_hni mappings.
   REMAINING: En_Horse 0x14 (the named, rideable Epona) is CONDITIONALLY spawned (Epona's Song /
   riding), so it couldn't be rendered in a static headless scan to verify the fix in-motion — the
   mapping is correct-by-construction (parallel to the verified sibling horses) but a live ride
   should be eyeballed when reachable. Also TODO if Epona-mount still looks off: she's a SKIN-type
   N64 skeleton (gEponaSkel, ~47 limbs) vs OoT3D epona.cmb's 25 bones, so the N64-anim RETARGET
   path won't apply — she relies on the CSAB-matching path (now fixed).

28e. **[DONE 2026-06-19; see Done] OoT3D sun/moon discs** (finished #28c). REMAINING OPTIONAL polish
    (lower priority): the fine_lensflare.ctxb lens-flare (still N64 Environment_DrawSunLensFlare, a
    separate call site) and the fine_sun.cmb vertex-coloured sun-GLOW dome-cap (would reuse the SKY:
    infra but must be oriented toward the sun azimuth). The discs themselves — the core of
    Environment_DrawSunAndMoon — are replaced and verified.

27. **More foliage** (LOWEST priority — do last) — add more foliage/vegetation density to the world
    for a lusher look.

28. **[DONE 2026-06-19; see Done] Sky looks bad (low-res N64 skybox)** — replaced the N64
    normal-sky skybox with the OoT3D /kankyo/BlueSky.zar dome (tenkyu gradient + kumo clouds),
    selected by the game's own time-of-day skybox1Index. REMAINING ENHANCEMENTS (optional, lower
    priority): (a) **[DONE 2026-06-19; see Done]** blend the TWO domes (skybox1Index/skybox2Index
    by skyboxBlend) at dawn/dusk instead of snapping to the dominant one; (b) **[DONE 2026-06-19;
    see Done]** animate the cloud drift via the kumo .cmab; (c) OoT3D sun/moon/stars — **STARS
    [DONE 2026-06-19; see Done]**; sun/moon DISCS still N64 (asset reality below); (d)
    broaden past SKYBOX_NORMAL_SKY (shop/indoor skyboxes still N64). Indoor/enclosed scenes (Kokiri)
    have skyboxDisabled so the dome correctly does not draw there.
    **ASSET REALITY for #28c (CORRECTED 2026-06-19 — the old "billboard the sun CMB" note was wrong):**
    dumped /kankyo/BlueSky.zar with tools/cmb.py — the sun/moon DISCS have NO CMB. `fine_sun.cmb`
    (tex0_idx=-1, UVs all 0) is an UNtextured VERTEX-COLOURED glow DOME-cap (x/z ±148, y[-82,2]); it
    is NOT the sun disc. `fine_star.cmb` is an L8 ADDITIVE (src=SRC_ALPHA dst=ONE) textured star
    DOME-cap (the night star field) — DONE. The actual sun/moon disc textures `tex/fine_sun.ctxb`,
    `tex/fine_moon0..2.ctxb`, `tex/fine_lensflare.ctxb` are STANDALONE ctxb sprites with no CMB —
    OoT3D's engine billboards them itself (like the N64 gSun1Tex quad). So finishing #28c (discs)
    needs NEW infra: a ctxb reader (ctxb = a "tex " chunk identical to CMB's at file offset 0x18 →
    reuse pica decode) + a synthetic textured billboard quad fed to the renderer as a model, drawn at
    eye±sunPos (sunPos from Environment_DrawSunAndMoon) with play->billboardMtxF for camera-facing.
    The sun-glow dome (fine_sun.cmb, vertex-coloured) can use the SKY: infra but needs orienting
    toward the sun azimuth. Left as #28e below.

## Done (recent)

- **#5 STEPPED COLLISION — Link now grounds on the visible kaidan steps (completes #5)** — the
  render already drew real treads+risers (below), but gameplay collision was still the smooth OoT3D
  ramp, so Link's feet clipped a riser / floated a tread by ≤½ step. Now the SAME kaidan→steps
  transform produces the COLLISION floor. Refactored the render-side patch analysis into shared
  pure-geometry helpers (`stairTriNormals`/`stairPatches`/`stairFrameOf` in soh3d_model.cpp) so the
  render geometry and the collision geometry can never diverge. New
  `SoH3D_CollectSceneStairTreads(sceneName,…)` (soh3d_model.cpp) walks every room of the scene,
  finds kaidan groups, runs those helpers, and emits each step's horizontal TREAD as a world-space
  quad (2 tris) — risers omitted ON PURPOSE: the original OoT3D ramp collision stays in place
  underneath, so the treads (which sit on/above it) just become the higher walking surface BgCheck
  returns. `SoH3D_BuildSceneCollision` (soh3d.c) appends those treads as +Y floor polys
  (normal=(0,1,0), dist=-y), re-sourcing each tread's cam+exit from the N64 floor at its centroid
  EXACTLY like the main floor loop (copying the underlying poly's data0 wholesale was unsafe — the
  entrance staircase abuts the Hyrule-Field transition, so the nearest base poly under a tread can
  be an EXIT triangle that would warp Link). 13-bit vertex-index budget guarded (skip stairs
  collision if base+treads ≥ 8000 verts / 60000 polys). Coordinates are world-space (rooms draw at
  identity, gSoH3dSceneScale=1/off=0), matching the collision frame. VERIFIED headless on Vulkan
  (Kakariko 0x52): the collision build logs "spliced 608 stepped tread polys (1216 verts)";
  `floorgrid` down the entrance staircase (patch 0) AND a village staircase (patch 6) shows a clean
  STAIRCASE Y-profile (flat treads ny=1.000 + ~8u riser jumps) where the smooth-ramp baseline was a
  continuous ny=0.894 slope; Link walks the steps smoothly (descent identical to the smooth ramp,
  Kakariko↔Hyrule gate transition normal). Important dead-end recorded: a tp-drop from 100+ units
  onto the fine steps SKITTERS Link unpredictably — but this is IDENTICAL on the smooth ramp (it's a
  teleport-drop artifact, not the steps), so not a regression. Gate env SOH3D_STAIRS (shared with
  the render). soh only (soh3d_model.cpp, soh3d.c, soh3d_collision.h); libultraship UNTOUCHED.
  Memory [[soh3d-stairs]].

- **#5 Real stepped-polygon stairs from the fake-flat "kaidan" ramps (ORIGINAL PRIMARY GOAL)** —
  OoT3D renders staircases as ONE flat textured ramp; the steps are painted into the texture, whose
  name contains "kaidan" (階段, "staircase" — e.g. spot01's `s01_kaidan_01`). That asset name is the
  grounded detection signal (not a per-scene magic region). At scene-room build (soh3d_model.cpp
  `generateRoomStairs`/`generateStairsGroup`, called from `loadSceneRoom`→`buildFromCmb(stairs=true)`)
  every kaidan draw group's flat triangles are REPLACED by generated 3D step geometry: union-find the
  group into connected+coplanar ramp patches; per patch build a horizontal ascend/across frame from
  the plane normal, footprint = (a,c) bbox, fit the ramp's affine UV(a,c); emit N = round(rampRiseY /
  kStairRiserY) treads (top face) + risers (front face, downhill normal), keeping the same material
  (texture/UV/cull/baked-color). `kStairRiserY = 7.8` is asset-derived: the kaidan texture paints ~11
  steps per 128px V-tile (FFT) and the ramp UV maps ~86 world-Y per V-tile → ~7.8u per painted step.
  Winding matches the CMB CCW-outward convention so the cull=1 kaidan material renders correctly.
  VERIFIED headless Vulkan (Kakariko 0x52 entrance staircase, frozen cam looking down the flight):
  SOH3D_STAIRS=0 = flat ramp with straight diagonal silhouette + painted stripes; default ON = sawtooth
  step silhouette + tread/riser relief; isolate = 103k px changed on the staircase, rest of scene
  unchanged + no corruption. Generic: applies to every kaidan patch in every scene. KNOWN LIMITS:
  collision is still the smooth N64 ramp (Link can clip a riser/float a tread by ≤½ step — visual-only
  steps); step undersides open. Gate env SOH3D_STAIRS / REPL `stairs <0|1>` (GL caches geometry per
  model id, so the live toggle applies to rooms loaded AFTER it — use the env for same-scene A/B).
  Shipwright (fork/develop) + outer (main); libultraship untouched. Memory [[soh3d-stairs]].

- **#1 Backface-cull OoT3D meshes (the "camera under terrain / flip look" artifact)** — the OoT3D
  mesh draw path (GL + Vulkan) rendered everything DOUBLE-SIDED (GL_CULL_FACE off; Vk
  rs.cullMode=NONE). So whenever the camera saw a backface — the title/demo camera dipping under the
  Hyrule Field surface, or any view inside geometry — we drew it, where N64 backface-culls (you'd see
  through to sky). That is the real cause of "cutscene/title camera under terrain → sky-below /
  ground-above flip". Honor the CMB material cull byte (game-wide ONLY two values: 1 = single-sided /
  cull-back, 3 = double-sided / no-cull — confirmed across 37 room CMBs (266×1,50×3) + 60 actor zars
  (250×1,49×3)). makeCgroup sets `faceCull = (mat->cull==1)`; the color pass enables
  GL_BACK / VK_CULL_MODE_BACK for those groups. Front-face winding flips with invertY (the vertex
  shader negates clip.y), so `frontCW = invertY ^ gSoH3dFaceCullFlip`; flip=1 is the verified-correct
  convention (one global works for both backends because the invertY term already accounts for the
  GL-vs-Vk screen orientation difference). State-leak guarded: beginPass/endPass save+restore
  GL_CULL_FACE_MODE + GL_FRONT_FACE; shadow/AO depth passes keep culling off (cullPass=false). Sky
  dome (inward-facing, cull byte 1) stays visible from inside — same normal-side-is-front convention
  as the terrain. Gate: env SOH3D_FACECULL (default ON) / REPL `facecull <0|1> [flip]`. VERIFIED
  headless on Vulkan: camera frozen UNDER the Hyrule Field terrain — upper region went from
  terrain-underside RGB (89,66,60) to sky-blue (82,132,205) (terrain culled, see-through to the dome,
  == N64); normal above-ground view, Market (buildings + townsfolk + fountain) and the Deku Tree
  interior (inward walls visible) all unchanged. The old #1 "3DS terrain higher than N64" hypothesis
  was FALSIFIED by measurement (the two floors agree to ~1u in Hyrule Field). libultraship d272d6a9
  (fork/soh3d), Shipwright 8dc225e37 (fork/develop).

- **#28e OoT3D sun/moon DISCS (replace N64 Environment_DrawSunAndMoon billboards)** — the sun/moon
  were the last N64 sky sprites. OoT3D ships them as standalone CTXB sprites in /kankyo/BlueSky.zar
  (no CMB): `tex/fine_sun.ctxb` (128x128 ETC1 glow-on-black) + `tex/fine_moon0.ctxb` (128x128
  RGBA4444 alpha-masked full moon). NEW infra: (1) a **CTXB reader** `asset/ctxb.{h,cpp}` — a CTXB
  is a 0x18-byte header (texChunkOff@0x10, texDataOff@0x14) wrapping a tex chunk BYTE-IDENTICAL to a
  CMB's, so each entry decodes via the existing `pica_texture.cpp PicaDecode` (glFormat =
  data_type<<16|fmt); (2) a synthetic **billboard quad** built as a LoadedModel in
  `soh3d_model.cpp loadBillboard` (no CMB) via the `BILLBOARD:`/`BILLBOARDADD:<zar>|<ctxb>` auto-key
  prefix — one quad with the N64 sun-sprite verts (-31..32, weights[0]=1 so identity-skin = no-op),
  one decoded CTXB tex, blend = additive (sun, glow on black) or alpha (moon); (3) `SoH3D_TryDrawSunMoon`
  (soh3d.c) hooked at z_play.c:1523 (return 1 to skip N64), drawing the sun at eye+sunPos / moon at
  eye-sunPos, camera-facing via `play->billboardMtxF`, with N64's EXACT sunPos/scale/alpha formulae
  (sunPos from gSaveContext.dayTime; sun scale (color*2)+10; moon scale -15*color+25, alpha
  min(-y/80,1)*255 so it fades in only at night). Far-plane pinned (handle bit 30) + depth-write off
  like the dome, so terrain occludes them below the horizon. Tint left WHITE (CTXBs carry their own
  colour, unlike N64's I-format sprites). Gated like the dome (SKYBOX_NORMAL_SKY + OoT3D scene +
  gSoH3dSky). VERIFIED headless on Vulkan (Hyrule Field 0xCD, frozen cam): morning (0x6000) sun glow
  disc + halo ring renders at the N64 sun's exact screen position (A/B vs `sky 0`); night (0xE000)
  full moon with surface detail renders at eye-sunPos over the #28c starfield, disc diameter ~490px
  matching N64's, center within ~6px; `sky 0` correctly reverts to the N64 sun/moon. soh only
  (Shipwright/soh: ctxb.{h,cpp}, soh3d_model.cpp, soh3d.{c,h}, z_play.c); libultraship untouched.

- **#28c OoT3D night-sky STARS (fine_star.cmb additive star dome) — the night sky was starless** —
  our #28 dome replacement draws only the gradient (tenkyu); OoT3D layers a separate star dome
  (`model/fine_star.cmb` in /kankyo/BlueSky.zar) over the dark night gradient, so the OoT3D night sky
  had NO stars (N64 baked them into its skybox texture). fine_star.cmb is an L8 (luminance) textured
  dome-cap with ADDITIVE blend (src=GL_SRC_ALPHA dst=GL_ONE) + per-vertex baked brightness; it adds
  star points over the dome. Drawn via the EXISTING SKY infra (forced-CMB `SKY:/kankyo/BlueSky.zar|
  fine_star` key → depth-write off, far-plane pin via handle bit 30) — no new mechanism needed.
  `SoH3D_TryDrawSky` now layers it between the night gradient dome and the cloud band (clouds are
  nearer), and ALSO on the idx2 cross-fade layer at alpha=skyboxBlend, so stars fade in/out WITH the
  night dome (no fabricated star-alpha curve). Gated to night variants only (`SoH3D_SkyIsNight`:
  skybox1Index 3 fine-night / 7 cloud-night). VERIFIED headless on Vulkan (Hyrule Field, frozen cam
  looking up): NIGHT same overhead region lum mean 26 / max 189 / std 25.6 (bright additive star
  spikes); DAY mean 138 / max 169 / std 12.4 (smooth gradient, NO spikes → stars correctly off);
  DUSK cross-fade (idx1=2 sunset, idx2=3 night, blend 47→238) max 215 / std 27.6 — stars fade in via
  the star2/blend path, no garbage/crash. soh only (Shipwright/soh/src/soh3d/soh3d.c). The sun/moon
  DISCS + sun-glow dome remain (#28e — need ctxb-sprite billboard infra; see #28 asset note).

- **#28b OoT3D sky cloud band drifts (kumo .cmab texcoord scroll, no longer static)** — the
  BlueSky.zar `kumo` cloud band rendered STATIC. OoT3D scrolls its texcoords via tiny `.cmab`s in
  `/kankyo/BlueSky.zar` (`misc/<group>_kumo_a.cmab`): each a single linear texcoord-U translation
  looping over a `duration` — i.e. a constant U scroll. PARSED the real rate with a NEW reusable
  tool `tools/cmab.py` (full CMAB parser: header / mads / mmad / linear+hermite+integer tracks,
  layout from noclip's cmab.ts; channel-0 base-layer rate, DO NOT fabricate): fine (idx0-3) &
  cloud (idx4-7, ch0) = dU −1/900 per frame; holy (idx8) = dU −1/600; all U-only (dV 0). Plumbed a
  per-draw texcoord SCROLL offset through the SoH3D draw path, mirroring the #28a alpha exactly:
  new `gSPSoH3DDrawUV(pkt,handle,alpha,uvU,uvV,r,g,b)` packs uvU/uvV as 16-bit fixed (offset*65536)
  into the UPPER 32 bits of the 64-bit w0 (handle/alpha/tint untouched); `gSPSoH3DDrawA` is now a
  uv=0 wrapper, every existing call site unchanged. interpreter decodes w0[32:48]/[48:64] → float
  /65536 → `SoH3D_GL_Submit(...,uvOffU,uvOffV)` → `DrawItem` → GL `uUVOffset` vec2 uniform (added to
  `vUv`) AND Vk `uExtra.yz` (added to `vUv`). `SoH3D_TryDrawSky` drives it: U = wrap(
  `play->gameplayFrames` * rate) so the band advances at OoT3D's own logic-frame clock; kumo tex is
  GL_REPEAT so it tiles seamlessly. Dome/world draws pass uv=0 (no-op). VERIFIED headless on
  **Vulkan** (Hyrule Field, camera FROZEN, ~20s gap): sky-top region changed 27.7% of pixels,
  near-horizon 0.24%, GROUND control 0.00% (proves it's the clouds drifting, not camera/world
  motion); zoom crops show the cloud wisps shifted horizontally while the HUD is fixed.
  libultraship (fork/soh3d), Shipwright/soh (fork/develop), tools/cmab.py + BACKLOG (outer/main).

- **#28a OoT3D sky cross-fades the two domes at dawn/dusk (no snap)** — the game blends two sky
  variants at dawn/dusk (`skybox2Index` over `skybox1Index` at alpha = `envCtx.skyboxBlend`).
  `SoH3D_TryDrawSky` previously drew only `skybox1Index`, so the OoT3D dome snapped to whichever
  variant was dominant at the blend midpoint (a sudden colour pop). Now it draws the lower variant
  (dome+clouds) opaque, then the upper variant over it at alpha=skyboxBlend. This needed a per-draw
  ALPHA on the SoH3D draw path (reusable beyond sky): new `gSPSoH3DDrawA(pkt,handle,alpha,r,g,b)`
  packs the alpha into the upper 32 bits of the 64-bit w1 (handle untouched); `gSPSoH3DDraw` is now
  an alpha=255 wrapper. Threaded through interpreter -> SoH3D_GL_Submit -> DrawItem -> drawOne
  (GL `uAlpha` uniform + force standard SRC_ALPHA blend when an opaque material is drawn
  translucent) AND the Vulkan path (UBO `uExtra.x` + synthesized alpha-over pipeline). Gated to a
  real cross-fade (blend>0, idx2 in 0..8 and != idx1) so the single-dome steady state is unchanged.
  REPL `sky info` now prints skyboxId/idx1/idx2/blend. VERIFIED headless on **Vulkan** (Hyrule
  Field, forward time sweep): sky region RGB blend 0->127->223->255 reads day (70,154,175) ->
  (134,131,147, intermediate: dist 74 from day, 108 from sunset) -> (161,80,87) -> sunset
  (167,64,69) — a smooth cross-fade, not a snap. libultraship d4615973 (fork/soh3d), Shipwright
  a55786793 (fork/develop).

- **#28 OoT3D sky (BlueSky.zar dome + clouds) replaces the low-res N64 skybox** — the N64
  normal-sky skybox is a blurry 128x64 CI8 image. Now: `SoH3D_TryDrawSky` (soh3d.c, hooked in
  z_play.c ahead of `SkyboxDraw_Draw`) draws the OoT3D `/kankyo/BlueSky.zar` celestial dome
  (`tenkyu`, a vertex-coloured gradient) + its cloud band (`kumo`, textured alpha) centred on the
  camera eye, picked by the game's own `envCtx.skybox1Index` (0..8 = fine/cloud/holy × time-of-day).
  The OoT3D `fine_tenkyu_0..3` baked vertex colours line up 1:1 with the N64 order (0=sunrise
  yellow-green, 1=day blue, 2=sunset red, 3=night dark-blue — verified by dumping the dome vertex
  colours). MECHANISM: a per-draw "sky" flag (SoH3D draw handle bit 30) makes the vertex shader pin
  the dome's clip z to the far plane (z=w) in BOTH the GL and Vulkan backends, so it fills only
  untouched (far) pixels under LEQUAL — never occludes world geometry, never clips against the far
  plane, independent of its geometric scale. The "SKY:" model-key prefix (loadAutoModel) loads the
  CMB with baked vertex colour + depth-write off; sky draws are excluded from shadow casting and AO;
  untextured groups now bind a 1x1 white texture (GL) so the dome shows pure vertex colour. REPL
  `sky <0|1>` / `sky scale <f>`. VERIFIED headless on Vulkan (Hyrule Field): day = saturated blue
  gradient + 3DS clouds (sky-band RGB ~(60,183,255) vs N64 ~(109,133,255)); night dome dark blue
  (~(25,21,79)); full overhead coverage; world unoccluded. 3-repo: libultraship 89f1c136
  (fork/soh3d), Shipwright b13a530b3 (fork/develop). Remaining enhancements tracked in #28 above.

- **#13(b) Kakariko graveyard bounces straight back out (OoT3D collision exit-poly mismatch)** —
  warping to the graveyard (entrance 0xE4) immediately transitioned back to Kakariko. ROOT CAUSE:
  SoH spawns Link at the N64 spawn point (-1408,0,330), which is authored against the N64 EXIT
  layout. The OoT3D scene collision (spot02_info.zsi) puts its scene-exit triangles at different XZ
  extents — the N64 spawn poly landed on OoT3D poly472 (type 22, exit index 1 = "back to Kakariko")
  → Link triggered the exit on frame 0. PROVEN: under `collision 0` (N64) the same spawn poly has
  exit=0 and Link stays; under OoT3D collision it had exit=1. The OoT3D cam+exit *indices* also
  point into OoT3D's own camera/exit lists (we load N64's), a second reason not to trust them. FIX
  (soh3d.c `SoH3D_BuildSceneCollision`): build one SurfaceType PER POLY and, for each FLOOR poly,
  re-source the cam+exit bits (surfaceType.data[0] low 13 bits = 0x1FFF) from the N64 floor at that
  triangle's centroid (`SoH3D_N64FloorData0`, manual point-in-triangle over the N64 header, closest
  plane-Y for multi-level). Floor type/material/flags (data[1], high bits of data[0]) stay OoT3D;
  walls untouched; OoT3D-only floors with no N64 floor under them get exit bits zeroed (no stray
  bounce). VERIFIED headless: graveyard (0xE4) now STAYS (scene 0x53); the real exit-1 is preserved
  at the N64 location (-1600,z); spawn now reads exit=0 cam=4 == N64; Hyrule Field (3753 polys)
  still loads in ~2s, Link grounded (the per-poly N64 scan is one-time at scene load). Also fixes
  per-region cameras to match N64. soh only (Shipwright/soh/src/soh3d/soh3d.c). The same mechanism
  should clear #13(a)/(c) — re-test.

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
