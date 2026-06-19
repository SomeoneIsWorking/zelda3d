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

> **USER PLAYTEST 2026-06-19 SESSION 2 (GROUND TRUTH — newest, overrides everything below).**
> Live keyboard/Vulkan playtest with screenshots (saved `scratch/screenshots/playtest_0619_s2/`).
> TWO prior "DONE" marks are FALSIFIED by direct visual evidence. Re-prioritize around these:
>
> - **#5 STAIRS — REOPENED, the wall-integration rework is BROKEN (4 distinct defects, all on real
>   walk-up angles).** Last session marked it "DONE + verified + pushed" (Shipwright 9c28b908e); the
>   user's 4 screenshots show it is not. The wall-integration PREMISE is itself rejected by the user.
>   Defects (see the saved shots):
>   1. `stairs_gaps_ugly_tex.png` — bright **CYAN see-through GAPS** between/around the steps (the
>      clear/sky color punching through the geometry — culling/winding holes or non-watertight steps),
>      AND the tread brick texture is **stretched/smeared ("super ugly")**.
>   2. `stairs_flat_ochre_slab.png` — a whole staircase renders as **ONE FLAT UNTEXTURED OCHRE SLAB**
>      with a cyan halo: NO steps at all, NO real texture (looks like the SVG-stone fallback sampled at
>      a single UV / a stairFrame that didn't step). Completely busted.
>   3. `stairs_overlap_wall.png` — the stair side/riser faces **OVERLAP and COVER the original brick
>      wall** with a duplicated, mis-tiled brick. **USER DIRECTIVE: "the wall should be preserved
>      original."** Do NOT steal the wall's texture, do NOT build big inset side-walls that drop to a
>      base over the real wall. Leave the scene wall geometry untouched.
>   REVISED DESIGN (supersedes the wall-integration design below): replace the fake-flat kaidan ramp
>   with REAL watertight steps on the ramp's OWN footprint only; texture them with a clean tiled stone
>   (SVG baseline is acceptable) mapped so it does NOT stretch; sides are minimal vertical caps at the
>   ramp's actual cmin/cmax edges (no inset, no dropping to a far base, never over the wall); consistent
>   winding so backface-cull leaves no cyan gaps. The wall-integration code (findPatchWall + the
>   wall-textured/inset-sawtooth path in buildStairPatch/generateRoomStairs) is the source of defects
>   2 & 3 and should be reverted/replaced. CANDIDATE BASELINE to revert toward: the pre-rework
>   SVG-stone stepped stairs at Shipwright `8fff103d0` ("#5 stairs: custom SVG stone texture") +
>   `11180de6c` (configurable step size) — those were the "accepted baseline" before wall-integration;
>   then fix the cyan gaps (winding/watertightness) on THAT, not on the wall-integration code.
>
> - **#24 KAKARIKO WELL — REOPENED, concrete defect identified at last.** `well_water_tiny_diamond.png`:
>   the well "water" renders as a **tiny teal GLOWING DIAMOND floating in mid-shaft** — it looks like a
>   magic crystal/portal, not water. Correct well water is a **wide flat water SURFACE filling the
>   well's cross-section near the bottom of the shaft**. Root area: the Idomizu (0x104) replacement
>   forces CMB `c_s01idomizu` (soh3d.c ~1282) — either that CMB is a small splash/ripple decoration
>   (not the surface plane), or it's drawn at the wrong scale/Y so it shows as a tiny disc. NEXT: dump
>   what `c_s01idomizu` actually is; if it isn't a full surface plane, either pick the correct surface
>   CMB or generate a flat water quad sized to the well bore at the water Y (the N64 Idomizu waterbox
>   ySurface gives the height). The prior "no actionable defect" note (below) is now SUPERSEDED.
>
> ---
> **USER PLAYTEST 2026-06-19 SESSION 1 (GROUND TRUTH — overrides prior "VERIFIED headless / DONE").**
> The user playtested on keyboard/Vulkan through real scene flow and reports MANY items still broken,
> incl. several marked DONE. Hard lesson recorded: prior "VERIFIED headless" tested NARROW mechanisms
> in artificial conditions (frozen cams, `skiptest` harness, forced states, single frames), NOT the
> full user-facing behavior — so those DONE marks were premature/partial. Treat the items below as the
> live state. (Stairs were thought confirmed-good here — "stairs look decent now" — but SESSION 2 above
> FALSIFIES that: from real walk-up angles they are badly broken. #5 is REOPENED.)
> - **#2 SKIP still fails** — Start/SPACE does NOT skip "place introduction camera panning" (scene-
>   intro establishing pans). The onepoint-cam skip I shipped only covered onepoint cams; scene-intro
>   pans are a different system (Cutscene/Player csMode) — the deferred part of #2 is the part the user
>   actually needs. Note: SPACE clearly registers (it does things on the title), so keyboard→Start works.
> - **#1b TITLE SPACE → weird places** still broken (lands in wrong gamestate).
> - **#4 LADDER still floats / needs lowering** (Image: child Link floating just above a wooden plank
>   platform by a ladder; dark mossy wood area — looks like Deku Tree / Kokiri interior, NOT only
>   Kakariko). "needs hand weaving."
> - **#7 KOKIRI KID DRAW DISTANCE still culls** — user wants INFINITE. My #7 fix (force shouldDraw/
>   Update for replaced actors) verified drawn=1 at d=1790 but the kids still pop out for the user →
>   likely actor UNLOAD (room/scene despawn) at greater range, not just the draw cull. REOPEN.
> - **#34/#35 KEYBOARD UI / control scheme — not done** (acknowledged; needs libultraship default-map +
>   live keyboard verification).
> - **#9 FLOWERS/LILYPAD not walkable** (Image: child Link at the edge of a Kokiri pond clover/lilypad
>   patch, blocked). Collision.
> - **#23 CUCCO won't flap WHEN HELD** — REOPEN. My #23 did the IDLE procedural flap; the HELD/picked-up
>   cucco uses the agitated flap path (func_80AB5BF8 higher amplitude / a different state) which my
>   OverrideLimbDraw replay doesn't cover when the cucco is carried. Incomplete, not done.
>   SESSION 2: STILL not flapping, confirmed (`scratch/screenshots/playtest_0619_s2/cucco_ghost_at_pickup.png`
>   shows the cucco at rest with N64 feather effects but a static OoT3D wing).
> - **#23b CUCCO GHOST-AT-PICKUP (NEW, SESSION 2)** — when Link picks a cucco up (and can throw it),
>   a cucco still renders AT THE SPOT where it was picked up. So the OoT3D replacement is drawing the
>   cucco at a STALE/original world position instead of following the carried actor (held-above-head /
>   thrown trajectory). Likely the replacement uses actor->world.pos captured/wrong while the engine
>   moves the carried actor via a different transform (Player's carry, or En_Niw's grabbed state). Fix:
>   draw the replacement at the actor's LIVE carried transform (the same matrix the N64 cucco draws at
>   when held), not its ground position. Same screenshot.
> - **#29c 3DS-LINK CAN'T PICK UP CUCCO (NEW, SESSION 2)** — in `linksrc 3ds` mode, child Link cannot
>   pick up a cucco at all (the grab/lift action doesn't fire / isn't usable). Part of the broken
>   3DS-Link weave (#29): the 3DS anim/state path doesn't drive the pickup interaction.
> - **#29d 3DS-LINK with N64 ANIMS = bad retarget (NEW concrete symptoms, SESSION 2)** —
>   `scratch/screenshots/playtest_0619_s2/link_n64anim_headdown_arms.png`: in N64-anim mode child Link
>   (a) ALWAYS looks at the ground (head bone pitched fully down), (b) holds arms in weird splayed/bent
>   positions, (c) walks weird. So the N64 jointTable→OoT3D-skeleton retarget for the PLAYER skeleton
>   is wrong on at least the head and shoulder/arm bones — likely a per-bone rotation-axis/order or
>   rest-rotation-replace mismatch (cf. [[soh3d-n64anim-retarget]]: N64 jointTable REPLACES the CMB
>   rest rotation, ZYX) that hasn't been calibrated for Link's skeleton (cf. [[soh3d-link-player-path]],
>   still proof-of-hook). Both #29 Link anim paths are broken: 3DS-anim = static slide (#29 slide),
>   N64-anim = head-down/arms-wrong retarget (this).
>   SESSION 2 holding-cucco: `link_n64anim_hold_cucco_nohands.png` — N64-anim Link carrying a cucco
>   has arms WRANGLED/contorted (retarget badness, as above) AND **no cucco is in his hands** (the held
>   cucco never appears at the hand — it's drawn at the stale pickup position instead, = #23b). So the
>   two bugs compound: the held cucco is absent from the hands and Link's carry pose is mangled.
> - **#24 KAKARIKO WELL — [SUPERSEDED by SESSION 2 above: defect IS actionable — water renders as a
>   tiny teal diamond, not a surface plane].** ~~re-diagnosed 2026-06-19, NO actionable defect found
>   in-scene; NEEDS USER SPECIFICS.~~ Framed the well live (water actor Idomizu 0x104 @ (762,52,524), pillar Idohashira 0x103
>   @ (799,80,503), both drawn=1). At PLAYER EYE-LEVEL the well reads correctly: a stone octagonal ring
>   matching the surrounding kabe walls, dark shaft interior, teal 3DS water visible (a small disc deep
>   down) only when looking straight down. The prior forced-CMB fix (teal water + pillar, no windmill
>   blades) IS in effect. auto 0 (N64) vs auto 1 (3DS) look identical from top-down. So either the prior
>   fix is actually fine and the user's "still broken" was stale/about something else (windmill? water
>   level near rim? the Bottom-of-the-Well *entrance* vs the surface well?), or the defect only shows in
>   a state/angle I didn't reproduce. Screenshots scratch/screenshots/well_{eyelevel,topdown,n64}.png.
>   DO NOT make a speculative change — get the user to point at the specific wrong thing first.
> - **FIRST-PERSON CAMERA (NEW, #37) — TWO bugs.** (a) **CRASH/flashing**: entering first-person
>   (C-up, BTN_CUP=0x0008) within ~2-3s of a FRESH scene load DETERMINISTICALLY CRASHES (SIGSEGV,
>   verified 3/3 trials in Kakariko via REPL `btnhold 0x0008 4`). After the scene settles (~30s) it's
>   SAFE (survives 60+ frames). Crash sig: `guMtxF2L <- Matrix_ToMtx <- Play_Draw+0xA3D`, RDI=0
>   (NULL source matrix / NULL Graph_Alloc dest) — a graph-arena/matrix corruption that only fires in
>   the post-scene-load window. The user hits this window constantly (enter a scene, look around) →
>   experiences it as "flashing most places".
>   **INVESTIGATION 2026-06-19 (characterized, NOT yet fixed — do not re-walk):** Reliable repro =
>   Kakariko fresh load + `btnhold 0x0008 30` (C-up) within ~2s + advance ~10 frames -> SIGSEGV.
>   - `sky 0` (BOTH SoH3D dome + sun/moon off) -> NEVER crashes (3/3). Default sky-on -> crashes.
>   - `SOH3D_NOSUNMOON` (dome only) -> crashes 3/3; `SOH3D_NOSKYDOME` (sun/moon only) -> crashes 3/3.
>     So EITHER sky draw alone is sufficient; you must disable BOTH (=`sky 0`) to avoid it.
>   - Crash is ALWAYS `guMtxF2L <- Matrix_ToMtx <- Play_Draw+0xA3D`, RDI=0 RSI=0, and a SUSPICIOUSLY
>     CONSTANT RBP=0x417AFA0 / R15=0x3BDA298 across every crash. The only guMtxF2L site in all of
>     Play_Draw is the per-frame billboard matrix (z_play.c:1437 `Matrix_MtxFToMtx(&billboardMtxF,
>     Graph_Alloc(...))`), which runs EARLY (before sky/scene/actor draws).
>   - Diagnostics RULED OUT the obvious causes: POLY_OPA arena is HEALTHY (163-194k free) every
>     logged frame; sunId/moonId are valid (loaded) from frame 1; billboardMtxF is sane (not NaN);
>     play->view.eye NEVER moves to the first-person position (stays third-person) right up to the
>     crash frame. So it is NOT arena exhaustion, NOT model-not-loaded, NOT a NaN matrix, NOT the
>     first-person camera position.
>   - NULL-GUARDING the billboard alloc (and the sky/sun/moon allocs) did NOT fix it (6/6 crash with
>     the long hold) and NEVER logged an actual NULL — and the crash OFFSET shifted by exactly the
>     bytes I added (+0xA3D->+0xA6D). A crash that ROAMS with unrelated code edits + constant register
>     state + no real NULL = MEMORY CORRUPTION (a bad write from elsewhere lands on the matrix/arena),
>     almost certainly from the SoH3D sky/sun-moon DRAW path (gSPSoH3DDraw + SoH3D render pass) in the
>     first-person early-load window — the billboard Matrix is just the victim. Likely related to the
>     scene-load race [[soh3d-skybox-corruption]]. Short C-up taps (`btnhold 0x0008 6`) sometimes
>     survived (timing-accidental), long holds always crash.
>   - NEXT (for a fresh session): catch the CORRUPTING WRITE, not the victim. Options: (1) a debug/
>     ASAN build (note: the corruption may be WITHIN the POLY_OPA arena buffer, which ASAN won't see
>     as a heap overrun — may need arena-bounds asserts in THGA_AllocEnd / the gfx interpreter);
>     (2) instrument the SoH3D render path (SoH3D_GL_Submit / EmitRenderPass / the bit-30 far-plane
>     draw) for an out-of-bounds write when the camera is first-person + scene just loaded; (3) check
>     whether the SoH3D sun/moon BILLBOARD model (loadBillboard quad) or the dome is submitted with a
>     bad vertex/index count or transform in the first-person view. All my speculative guard/diag
>     changes were REVERTED (they did not fix it) — tree is clean at the counter-icon commit.
>   (b) **POSITION SNAP**: first-person camera also "snaps to a wrong place under conditions I don't
>   know how to reproduce" (user) — a separate positional bug, repro unknown.
> - **#29 3DS LINK slides + RIGHT ARM TOO LONG** — Image: child Link in a crouched/contorted pose while
>   moving. FALSIFIES the #29b note that "slide does NOT reproduce" — it DOES slide for the user in
>   `linksrc 3ds` (3DS-anim) mode. Plus a NEW concrete bug: **the right arm renders longer than it
>   should** (skinning/bone-retarget defect). Child Link only (adult untested). n64-anim mode also
>   "doesn't look fine" (#29). The whole 3DS-Link weave is still broken.
>   SESSION 2 rear-view confirmation: `scratch/screenshots/playtest_0619_s2/link_long_right_arm.png`
>   (child Link from behind, standing idle — one arm visibly elongated, so it is NOT anim-specific;
>   it's a static bone-length/retarget error on the arm chain, present even at rest).
>   SESSION 2 SLIDE repro: `link_3ds_motionless_slide.png` — in `linksrc 3ds` mode child Link holds a
>   STATIC crouched/bind-like pose and SLIDES across the ground while walking (legs do not cycle). The
>   3DS walk CSAB is NOT advancing → motionless slide. So #29 has two faces: (a) the 3DS-anim walk
>   clip isn't being played/advanced (slide), and (b) the arm bone is mis-retargeted (long arm).
> - **T-POSING NPC (#38) — DONE 2026-06-19 (verified Kakariko, see Done).** The red-haired woman is the
>   Cucco Lady (En_Niw_Lady, id 0x13C, gCuccoLadySkel in object_ane). Root cause: she animates from the
>   SHARED object_os_anime bank via 4 anims (gObjOsAnim_07D0/9F94/0718/A630), but object_ane has no
>   AnimationHeaders so she never entered the match; those 4 OTR paths were seeded ONLY under km1
>   (Kokiri), resolving to km1/fad CSABs that don't exist in chickenlady.cmb -> bind pose = T-pose. The
>   anim map keyed solely on the OTR path, so a bank anim shared by two skeletons couldn't differ per
>   model. Fix: (1) allowlist her 4 anims into SHARED_ANIM_BANKS for zelda_ane; (2) the matcher picked
>   her zelda_ane CSABs (onegai_c/Ane_hanasu/oro_oro by frame; 07D0 idle stub pinned to Ane_matsu in the
>   overrides TSV); (3) made the runtime resolver MODEL-AWARE — entries whose OTR is shared across ZARs
>   are emitted ZAR-qualified (SOH3D_ANIMMAP_Z) and SoH3D_ResolveAutoCsab now prefers the entry matching
>   the live model's ZAR (SoH3D_AutoModelZar), falling back to a generic entry. Verified: model 2016
>   plays gObjOsAnim_A630 -> oro_oro phase-locked (frames advancing), renders animated (arms down), no
>   T-pose; only the 4 shared OTRs got ZAR-qualified, all gKokiri* anims stayed generic (no En_Ko regression).
> - **#5 STAIRS — side faces still triangles — [DONE-mark FALSIFIED by SESSION 2; REOPENED — see top].** (Shipwright 9c28b908e fork/develop).
>   The sides are now vertical SAWTOOTH-topped walls (not flat triangle caps) textured like the wall
>   the staircase abuts. See the #5 STAIRS REWORK entry below — both reopen blocks are addressed by the
>   same wall-integration change. Verified Kakariko: entrance = canyon rock, village = matching brick.
> - **OPENING INVENTORY renders background UPSIDE DOWN (NEW, #39 / #1 FB-flip cluster)** — pausing to
>   the inventory/pause menu draws the frozen game background flipped vertically. This is the Vulkan
>   FB-flip / negative-viewport issue (#1 family): the pause-preview render samples the FB with the
>   wrong Y orientation. Likely shares a root with first-person flashing and title-cam-under-terrain.
>
> - **#5 STAIRS REWORK — [DONE-mark FALSIFIED by SESSION 2; REOPENED — see top]** (Shipwright 9c28b908e fork/develop; libultraship + outer
>   untouched besides the submodule bump). Implemented the user's wall-integration design. Each kaidan
>   group is now replaced by ONE draw group PER PATCH (each staircase), and each patch wears the texture
>   of the WALL it abuts — found by findPatchWall (scan other room groups for verts along the patch's
>   side edges, pick the material with the most + a non-degenerate uv.v-vs-worldY fit). Per-patch matters:
>   the Kakariko entrance picks the rock canyon (kabe_03), the village stairs pick brick (kabe_01),
>   patch0/3 pick kabe_05x. Treads sit at the step midpoint (yk+dy/2 — straddle the ramp, no sink);
>   the SIDES are vertical sawtooth-topped walls inset 3u, same wall texture, dropping to the flight
>   base (so they read as the wall, no z-fight, no see-through triangle caps). UVs are world-derived
>   (risers/sides share the wall's vertical V banding; treads tile isotropically). No-wall patches (open
>   lower entrance flight) fall back to the embedded SVG stone. DESIGN NOTE: I did NOT mutate the shared
>   wall geometry ("bump the wall") — that's the highest-regression option and isn't needed at the
>   verified repros (the entrance/village walls already tower above the steps). The inset sawtooth side
>   wall, same-texture, achieves the same read without touching the wall. Collision unchanged (smooth
>   ramp; midpoint treads meet it at each tread centre). Verified headless from side/up-channel/player
>   angles; screenshots scratch/screenshots/stairs_final_*. ORIGINAL DESIGN/CONTEXT kept below.
> - **#5 STAIRS REWORK (user 2026-06-19, with image).** My first stepped-side attempt was REVERTED:
>   it added a stone side wall that OVERLAPPED the existing brick wall (z-fighting moiré), used the
>   wrong (SVG stone) texture, and the steps sink too far below the surface. User's DESIGN: (1) the
>   stair side faces should be extra faces using the SAME TEXTURE as the WALL they connect to, NOT a
>   separate stone; (2) step height should AVERAGE to the flat ramp surface they replace (don't sink —
>   straddle it, tread at the midpoint of each step's ramp span = yk+dy/2); (3) where steps poke ABOVE
>   the connected wall, BUMP THE WALL up to those points (user prefers this over gap-filling triangles;
>   "might hurt the UV a bit, I don't care"). CLARIFIED 2026-06-19: the stairs themselves CANNOT reuse
>   the original kaidan texture — it is a FAKE-STAIRS PAINTING (designed to make a FLAT surface look
>   stepped), so it doubles/wrong on real 3D steps (tried affine-UV kaidan -> reverted). So the stair
>   TREADS/RISERS/SIDES should use the CONNECTED WALL's texture (brick), so the staircase reads as the
>   same material as the wall. The current SVG-stone (gen_stairs_tex) is the accepted baseline but
>   CLASHES with the brick wall in the image -> goal is the wall's own texture. Don't draw side caps
>   that overlap the wall (the wall IS the side; bump it). REQUIRES adjacent-wall identification in
>   generateRoomStairs: the wall is another draw group in the same room CMB (find the group whose verts
>   are nearest the ramp's cmin/cmax side edges + roughly vertical; use its material/texture for the
>   steps, UV by world pos so the brick tiles continuously with the wall; raise its top verts to the
>   step profile). My two attempts (stepped stone side wall; affine kaidan texture) were both reverted.
>   This is a careful scene-aware geometry change — do it focused, verify visually, don't rush.
> - **CHILD LINK/ZELDA too small (NEW, #40)** — "child zelda is too small, I think half size" (so the
>   replacement is ~2x too big, OR child Zelda specifically renders at ~half the right size). Check the
>   auto-scale (measured N64 height vs OoT3D model height) for the child Zelda actor.
>
> NEXT (this session): capturing done; now reworking. Honest verification rule going forward — only
> mark DONE when the FULL user-facing path works in a realistic run, not when a narrow mechanism passes.

1. **[REOPENED 2026-06-19 — backface cull did NOT fully fix it] Cutscene / title / demo
   camera goes UNDER the 3DS terrain.** USER 2026-06-19: "title screen camera wasn't corrected
   well, it is still in the terrain." So the backface-cull change removed the "underside flip" read
   but the title/demo camera is STILL geometrically below the surface (you see into/through terrain).
   The real fix is the camera reconciliation, not just culling the underside. Pursue: lift the
   scripted demo/title camera eye.y when it falls below the 3DS mesh surface at its xz (probe the
   render-mesh height; this is the title-demo camera path specifically, scene 0x51 cam
   eye.y=-1). Don't just rely on the cull. Original investigation kept below.
   ROOT CAUSE of the visible "flip" was NOT terrain height:
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

2. **Universal skip for everything that takes control (re-prioritized by the user 2026-06-19:
   "space to skip everything that takes control away from the player")** — Start AND the keyboard
   SPACEBAR must skip: scripted CS cutscenes, onepoint cameras, actor-driven sequences, item-get
   freezes. No auto-skip, no forced-watch. Scripted CS terminators already skip on Start
   (`z_demo.c` `csSkipButton`, frames>20). REMAINING: add SPACE as a skip trigger everywhere;
   Player cutscene-mode (`csMode`), onepoint cams, actor-driven. User accepts the
   iterate-on-softlock approach (they report what softlocks).
   (DONE: holding Start skips dialogs — `z_message_PAL.c` all 4 text-advance sites.)
   (DONE 2026-06-19, see Done: **onepoint cutscene cameras** now press-to-skip on Start/Space —
   `SoH3D_SkipControlTakers` force-ends every active onepoint subcam via the game's own
   `OnePointCutscene_EndCutscene`. Gate `skip <0|1>` / env SOH3D_SKIP.)
   REMAINING: Player cutscene-mode (`csMode`) freezes + item-get freezes (chest-open / get-item
   over-head) — these are actor-driven in z_player.c and riskier (must release control without
   corrupting the get-item state); do next, carefully.

30. **Hi-res textures (user 2026-06-19)** — world/scene textures at higher resolution. The texpack
   mechanism exists ([[soh3d-texpack]]: replace CMB textures by Citra legacy hash from a `textures/`
   pack). Either bundle/enable a hi-res pack or upscale procedurally. Needs the actual hi-res image
   assets (or an upscaler).

31. **UI textures (user 2026-06-19)** — higher-res / custom HUD & menu element textures (the SVG
   approach used for the stairs is a candidate: author UI elements as SVG, rasterize, inject).
   **HEARTS DONE 2026-06-19 (see Done; awaiting user sign-off):** the blocky N64 16x16 IA8 HUD
   hearts (full/3-4/half/quarter/empty) are now crisp 64x64 SVG-authored textures injected via the
   raw-RGBA Fast3D path (option (b)), grayscale so the heart combine still tints them red + drives
   the beating/partial/empty states. Gate env SOH3D_HUDTEX / REPL `hudtex 0|1` (default on).
   tools/soh3d_gen_hud_tex.sh is the reusable heart-style generator.
   **DIGIT FONT DONE 2026-06-19 (see Done; awaiting user sign-off):** the blocky N64 8x16 I8 counter
   digits (gCounterDigit0..9/Colon) are now a crisp 32x64 font injected at the single Gfx_TextureI8
   choke point, so the rupee / small-key / ammo / race-timer / minigame-score counters all upgrade
   at once. tools/soh3d_gen_digit_tex.sh is the generator (font via SOH3D_DIGIT_FONT).
   **BUTTON-BG DISC DONE 2026-06-19 (see Done; awaiting user sign-off):** the blocky N64 32x32 IA8
   gButtonBackgroundTex (round beveled circle behind the B / C / item / A action buttons) is now a
   crisp 64x64 SVG-authored disc. tools/soh3d_gen_button_tex.sh is the generator.
   **COUNTER ICONS DONE 2026-06-19 (see Done; awaiting user sign-off):** the blocky N64 16x16 IA8
   rupee gem (gRupeeCounterIconTex, always-on bottom-left), small-key (gSmallKeyCounterIconTex,
   dungeons) and clock (gClockIconTex, timers) are now crisp 64x64 SVG-authored grayscale icons
   injected by pointer in Gfx_TextureIA8. tools/soh3d_gen_counter_icons.sh is the generator. Rupee
   live-verified (Kokiri); key/clock are the identical PRIM-tinted intercept path, correct-by-construction.
   REMAINING high-visibility HUD elements to give the same treatment (all reuse the raw-RGBA mechanism):
   the **magic bar** (gMagicMeterEnd/Mid/Fill — investigated 2026-06-19: the bar is mostly horizontal
   bands that are already sharp; the only blocky parts are the tiny rounded cap corners, so the crisp
   gain is small — "modest gain" confirmed, deprioritized), the **item icons / equip outlines**, the
   **do-action label** font, and the **minimap/dungeon-map** dot/marker sprites. The A/B/C button
   backgrounds are also badged with Xbox glyphs (#32). Pick by visibility.
   INVESTIGATED 2026-06-19: the texpack DOES have a `UI/` dir (`textures/0004000000033500/UI`, 259
   files, Citra-hash-named `tex1_WxH_HASH_fmt_mip0.png`, with GERMAN/ITALIAN/JAPANESE/SPANISH subdirs),
   but inspecting the 256x256 set they are **OoT3D menu/map/pause/GAME-OVER screens + item-grid atlases**
   — i.e. OoT3D's OWN UI (touchscreen layout), NOT drop-in replacements for the N64 in-game HUD
   (gButtonBackgroundTex, the heart/magic/rupee/do-action sprites). There is **no clean 1:1 mapping**
   from these atlases to N64 HUD elements, and they're keyed by Citra hash for the CMB path, which the
   N64 HUD doesn't use. So "use the texture pack for UI" can't be a blind hash-swap. Tractable options:
   (a) the **raw-RGBA Fast3D HUD injection path from #32** ([[soh3d-hud-glyphs]] / `SoH3D_XboxGlyphTex`
   + `SoH3D_DrawXboxBtn`) is the ready mechanism — feed it upscaled/redrawn HUD textures per draw site;
   (b) author crisp HUD elements as SVG (hearts, magic bar, button bg) and inject them, same pipeline
   as the Xbox glyphs / stairs; (c) cherry-pick the few pack UI textures that DO correspond (e.g.
   item icons that match N64 items) and remap. Needs the user to say which HUD elements matter most;
   pursue (b) for the highest-visibility elements if proceeding autonomously.

32. **[REWORKED 2026-06-19 — clean badge replacement, see Done; awaiting user sign-off] XBOX
    controller UI.** USER 2026-06-19: "You overlaid XBOX buttons on top of the existing UI, don't
    do that — XBOX should REPLACE the existing UI, not overlay it." (And: do NOT just disable it —
    tried that, rejected; they want it ON, done right.) The OLD impl swapped the button BACKGROUND
    circle for a full Xbox disc, then the N64 item icon + do-action LABEL ("PutAway") drew ON TOP,
    burying the letter = cluttered overlay. REWORKED (see Done): button backgrounds stay vanilla
    N64, item icons render normally, and a SMALL Xbox face-button glyph is drawn as a BADGE in each
    item button's top-right corner (recorded rects + SoH3D_DrawHudBadges, on top of the icons). The
    do-action button reverted to the vanilla green circle + label. Items + controller letters both
    readable, nothing stacked. REMAINING (await user): badge size/position/corner tuning; whether
    the do-action button should also carry an A indicator; possible badge-colour-vs-button-colour
    harmonization. Original polish notes below.

32b. **[was: XBOX controller UI ITEM-BUTTON CLUSTER — superseded by the #32 reopen above]**
   Done: the HUD item-button prompts (B + the 3 C buttons) now render as full-colour Xbox face-button
   glyphs (B=red, C-Left=X blue, C-Down=Y yellow, C-Right=A green), gated SOH3D_XBOXUI / REPL `xboxui`.
   REMAINING (lower priority): (a) **[DONE 2026-06-19; see Done]** the **A action button**
   (Interface_DrawActionButton) now renders as the green Xbox 'A' glyph on the SAME flip-animated
   3D quad — the quad's baked 32-texel texcoords were remapped to the glyph's real size so the
   FULL glyph maps (not the top-left quarter); the do-action label still overlays. (b) The **C-Up
   Navi prompt** (the lone blue disc) is a separate
   gButtonBackgroundTex draw (naviCalling) left as the N64 circle. (c) Letters on item-occupied
   buttons are mostly hidden behind the item icon + equipped-item outline — only the colour ring +
   a peek of the letter show; consider a corner BADGE layout if the user wants the letter prominent.
   (d) The B/C->A/B/X/Y mapping is a fixed cosmetic choice (C buttons are on the right stick by SoH
   default, no canonical face button) — retune per user preference. Mechanism is the reusable raw-RGBA
   HUD-glyph path (SoH3D_DrawXboxBtn in z_parameter.c + SoH3D_XboxGlyphTex), same SVG->PNG-embed
   pipeline as the stairs texture; reuse it for #31 (UI textures).

33. **XBOX CONTROL SCHEME — modern dual-stick mapping, not just the HUD glyphs (user 2026-06-19).**
   This is the CONTROL MAPPING (physical input -> in-game action), distinct from #32 which only
   reskins the HUD prompts. The user wants a modern-console layout: **no C-pad** — instead map the
   four item slots onto **B and Y** (face buttons) plus **R1 (RB/R-shoulder) + A/B/X/Y chords**, and
   also allow the **D-pad** to hold item slots. I.e. the OoT B + C-Left/Down/Right + (optional)
   D-pad-equip item set is re-bound onto a face-button/shoulder-chord scheme. NOTE the hard part:
   SoH's input system (`Controller`/`ControllerButton` mappings, the InputEditor + CVars under
   `CVAR_..."ControllerButton..."`) maps ONE physical button -> ONE N64 button; **R1+A style CHORDS
   are not natively supported** — a chord (modifier + button -> a different N64 button) needs new
   input plumbing (a modifier-aware mapping layer), OR leverage SoH's existing "additional bindings"
   / DpadEquips (`CVAR_SETTING("DpadEquips")`) features. Investigate: (a) what SoH already supports
   (dual-stick, DpadEquips, extra controller-button slots) vs (b) what needs new chord logic. The N64
   side still only has B + 4 C + Start + Z + R + L + dpad, so "more item slots than buttons"
   inherently needs either the existing Ocarina/40-item radial or a chord modifier expanding the slot
   count. Pairs with #32 (the HUD glyphs should reflect whatever physical button each item lands on,
   not a fixed B->B/C->XYA map). Needs the user to confirm the exact desired bindings before coding.

34. **INPUT-DEVICE-ADAPTIVE UI — keyboard vs controller prompts (user 2026-06-19).** When playing on
   KEYBOARD, the HUD/menus should show **keyboard key prompts**; when on a controller, show the
   controller (Xbox) glyphs (#32). Switch the prompt set based on the **last input device used**
   (detect the most-recent non-zero input source — keyboard event vs gamepad event — and flip a
   global "active device" flag). Builds directly on the #32 raw-RGBA HUD-glyph infra
   ([[soh3d-hud-glyphs]]): add a keyboard-key glyph set (SVG->PNG-embed, same pipeline) and make
   `SoH3D_XboxGlyphTex` / the draw sites pick the glyph variant by the active-device flag. SoH/LUS
   already tracks input sources (SDL gamepad vs keyboard) in the WindowManager/Controller layer —
   find where the last-used device is known (or add a hook in the input poll) and expose it to the
   HUD. Lower-effort once #32's pipeline is reused; the new work is (a) authoring key glyphs and
   (b) the device-detection flag.

35. **KEYBOARD + MOUSE CONTROL SCHEME — modern KBAM layout (user 2026-06-19).** The control-mapping
   side of #34 for keyboard/mouse (the UI/prompt side is #34). Desired bindings (user's words):
   **WASD = movement** (analog-stick emulation), **1/2/3/4 = item slots**, **mouse-look = camera**,
   **left click = attack** (B), **right click = block** (R / shield), **E = interact/talk/check**
   (A), **Space = roll** (the roll input). Implementation: SoH/LUS already has a keyboard mapping
   layer (`KeyboardController` / `CVAR_..."Keyboard..."` bindings) and a mouse feature in some builds
   — map these keys/mouse buttons onto the N64 buttons + synthesize an analog stick from WASD, and
   feed mouse-delta into the camera/right-stick. Mouse-look needs the free-look camera
   (`CVAR_SETTING("FreeLook...")`) + relative mouse capture. Items on 1-4 map to the B + C slots (or
   whatever #33 settles on). Roll = the N64 "A while moving"; Space should inject the roll input
   contextually. NOTE: 1/2/3/4 -> four item slots dovetails with #33's "no C-pad" goal. NB Space is
   currently BTN_START (the skip key, #2) — rebinding Space to roll conflicts; resolve the skip key
   (move skip to a different key, or roll on a different key) when implementing. Verify live with the
   user (keyboard is their actual play path — they report what feels wrong). Pairs with #33 (Xbox
   scheme) + #34 (device-adaptive prompts) as the input-rework cluster.
   **SCOPING (investigated 2026-06-19, infra EXISTS — this is mostly default-binding config, not new
   plumbing):** LUS already has the full mapping system under
   `libultraship/src/ship/controller/controldevice/controller/mapping/{keyboard,mouse}/` —
   KeyboardKeyToButtonMapping, KeyboardKeyToAxisDirectionMapping (WASD->stick), MouseButtonToButton,
   Mouse->axis (mouse-look), all data-driven. Defaults live in
   `mapping/ControllerDefaultMappings.cpp`: **WASD->LEFT_STICK is ALREADY a built-in default**
   (lines ~63-66, KbScancode LUS_KB_{A,D,W,S}). So the work is: (1) customize the default KEYBOARD
   key->button map (E->A, 1/2/3/4 -> B + C-left/down/right, LMB->B, RMB->R) — SoH passes its defaults
   into the ControllerDefaultMappings ctor; find that call (ControlDeck setup) and set our scheme;
   (2) wire mouse-delta -> camera (right-stick / FreeLook, `MouseStateManager`/`FastMouseStateManager`
   + relative capture); (3) resolve Space: A is CONTEXT-SENSITIVE in OoT (roll-when-moving /
   interact-when-near), so mapping BOTH E and Space -> A gives interact+roll on both keys (functional
   but the skip-key #2 conflict remains — pick a different skip key). NB this is libultraship-fork
   territory (commit to fork/soh3d), unlike #32 which was soh-only. Hard to verify headless (no SDL
   key/mouse events in the Xvfb REPL path; the REPL injects at the N64-button level, downstream of
   the mapping) -> validate with the user live.

36. **[DONE 2026-06-19; see Done] 2D→3D item drops default + always on (user 2026-06-19).** SoH's
    "3D Item Drops" enhancement (`CVAR_ENHANCEMENT("NewDrops")`, read all over `z_en_item00.c`) now
    forced ON at soh3d init (once per process, in SoH3D_ReplPoll) so drops render as 3D models, not
    flat billboards. Opt-out env SOH3D_NO3DDROPS=1 (sets the CVar to 0). VERIFIED headless.

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
    **USER RE-CONFIRMED 2026-06-19 (both anim modes broken):**
    - `linksrc n64` (3DS model + N64-retarget anim): "doesn't look fine" (this #29).
    - `linksrc 3ds` (3DS model + own-CSAB anim): **Link FLOATS above the ground and SLIDES instead
      of walking** — see new #29b. The slide is the documented WALK/RUN-MISSING root cause
      ([[soh3d-link-player-path]]): own-CSAB-by-name can't see blended locomotion (jointTable), so
      Link plays an idle CSAB while the actor translates → slide. The float is a Y-placement/scale
      issue in the player draw hook. Fix both before this is "fine".

29b. **[DONE 2026-06-19 — FLOAT fixed; SLIDE not reproducing; see Done; awaiting user re-confirm]
    3DS Link (own-CSAB / `linksrc 3ds` path) FLOATS above ground + SLIDES instead of walking
    (user 2026-06-19).** (1) **FLOAT — FIXED.** Root cause was NOT a blind Y/scale offset: the OoT3D
    Link CSABs carry absolute HIP (bone 1) TRANSLATION tracks authored for the taller BOY rig;
    applied to the shorter CHILD rig (the only own-CSAB source for many anims) they over-lift the hip
    → the whole skeleton floats ~930 model-local units (~40px). Quantified offline: BOY rig +
    nml_wait_free grounds at -2.2; CHILD + same CSAB floats +931.7. The working linksrc=n64 path
    grounds because it applies the rest (bind) translation and only REPLACES rotations. Dropping the
    translation breaks the boy run (needs the hip bob: drop → +177 float), so the fix MEASURES the
    posed model's lowest visible vertex (the feet) each frame and offsets the draw so the feet land
    on actor pos.y (per-frame analogue of the auto path's bind-pose groundOffset). VERIFIED headless:
    boot-bottom 502 (floating) → 553 == the grounded n64-retarget path. (2) **SLIDE — does NOT
    reproduce.** During sustained movement player->skelAnime.animation resolves to
    gPlayerAnim_link_normal_run_free → nml_run_free, so Link plays a real run cycle while translating
    (verified live, Kakariko + Kokiri: speedXZ 1.49-2.15 → nml_run_free, two-frame captures show legs
    cycling). The documented jointTable-blend slide didn't occur; a speed-driven locomotion override
    was prototyped but NEVER fired (natural resolution covers it) so it was dropped, not committed.
    The "slide" look was likely the FLOAT (a hovering Link translating reads as sliding). Re-confirm
    with the user now that the float is fixed.

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

23. **[DONE 2026-06-19; see Done] Cucco wing-flap animation not implemented** — the cucco (chicken,
    En_Niw 0x19) OoT3D replacement now plays its procedural wing-flap (the N64 flap lives in
    EnNiw_OverrideLimbDraw, not any anim). Generic OverrideLimbDraw-replay mechanism: capture the
    override callback at the SkelAnime draw choke points, probe it per limb for the additive
    rotation delta, map N64 wing limbs 7/11 -> OoT3D wing bones 3/5 (local Z), inject as a per-bone
    local-rotation delta on the CSAB pose. Original root-cause writeup kept below for reference.
    ROOT-CAUSED 2026-06-19 (asset + code, ground truth):
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

26b. **[CLOSED 2026-06-19 — confirmed NOT a bug; see Done]** DM gate collision. Code-confirmed that
    `Bg_Gate_Shutter` installs its own unmodified N64 dynapoly collision
    (`z_bg_gate_shutter.c:42-44`: `DynaPolyActor_Init` + `CollisionHeader_GetVirtual(&gKakarikoGuardGateCol)`
    + `DynaPoly_SetBgActor`), which the #26 render swap never touches — so the gate's physics are
    vanilla by construction. The #26 verification already observed the barred gate blocking the
    passage. The "no collision" report was the old wrong (windmill-mechanism) model rendering AWAY
    from the real gate, so the (correct) collision looked detached from the visible mesh; #26's
    model fix realigned render with collision. No code change needed.

4. **Kakariko ladder render too high** — the OoT3D ladder model renders a bit high, so Link appears
   to float when climbing. Lower it slightly (per-actor yoff / placement). Tune live via REPL
   `yoff`, then bake.

5. **[REOPENED 2026-06-19 SESSION 2 — render BROKEN; see the SESSION 2 block at the top of Open.
   Collision step-grounding may still be fine; the RENDER (wall-integration) is the broken part]
   Real polygon stairs for fake flat stairs**
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

- **#31 crisp higher-res HUD counter icons (rupee gem / small key / clock)** — the N64 counter icons
  were blocky 16x16 IA8 textures (gRupeeCounterIconTex — always-on bottom-left, gSmallKeyCounterIconTex
  — dungeons, gClockIconTex — timers). Replaced with crisp 64x64 SVG-authored grayscale icons injected
  via the raw-in-RAM RGBA32 path ([[soh3d-hud-glyphs]]). The rupee/key draws are MODULATEIA_PRIM
  (Gfx_SetupDL_39Overlay; PRIM carries the rupee colour / key silver), so a GRAYSCALE icon (rgb=facet
  bevel intensity, a=coverage) tints to PRIM exactly like the original IA8 — the 3D facet look comes
  from the intensity. The clock is MODULATERGBA_PRIM with PRIM white, so its grayscale shows directly.
  All three are FULL-LOAD single draws (no shared-tile reuse gotcha like gButtonBackgroundTex), so the
  intercept just loads the RGBA32 and rescales dsdx/dtdy to the rect. Art authored to match the
  originals (extracted+viewed each first): rupee = iconic tilted faceted gem crystal, key = skeleton
  key (round bow + blade/teeth), clock = pocket-watch face (crown + hands). Pieces:
  tools/soh3d_gen_counter_icons.sh -> counter_icon_png.h; SoH3D_CounterIconTex(kind) (soh3d_model.cpp,
  decode-once persistent RGBA32, SOH3D_CICON_* enum); a 3-pointer intercept in Gfx_TextureIA8
  (z_parameter.c) right after the button-bg one. Gated on SoH3D_HudTexEnabled (env SOH3D_HUDTEX / REPL
  hudtex, default on); off restores the byte-identical N64 IA8 path. VERIFIED headless (Kokiri 238):
  the always-on rupee counter renders as a crisp faceted green gem (sharp facets vs the vanilla blurry
  blob) and tints green correctly; before/after sent. Key/clock are the identical intercept path
  (PRIM-tinted), correct-by-construction (not visible in Kokiri to live-trigger). soh only
  (z_parameter.c, soh3d.{c via header,h}, soh3d_model.cpp, counter_icon_png.h); libultraship UNTOUCHED.
  See [[soh3d-hud-glyphs]].

- **#31 crisp higher-res HUD button-background disc (B / C / item / A action buttons)** — the N64
  button background was a blocky 32x32 IA8 disc (gButtonBackgroundTex, the round beveled circle drawn
  behind EVERY HUD button). Replaced with a crisp 64x64 SVG-authored beveled disc injected via the
  raw-in-RAM RGBA32 path ([[soh3d-hud-glyphs]]). KEY: the button combine is G_CC_MODULATEIA_PRIM
  (SETUPDL_39: out.rgb = TEXEL0.rgb*PRIM, out.a = TEXEL0.a*PRIM), so a GRAYSCALE disc (rgb=bevel
  intensity, a=circle coverage) tints to each button's PRIM colour (B=green, C=yellow, A=green, the
  blue Navi C-Up, etc.) IDENTICALLY to the original IA8 — all per-button colours/cosmetics preserved
  free, same insight as the hearts. ROOT-CAUSE GOTCHA found+fixed: gButtonBackgroundTex is loaded
  ONCE by the B-button Gfx_TextureIA8 call and the C-Left/Down/Right/Up buttons REUSE that resident
  tile via bare texrects whose dsdx/dtdy are tuned for 32 texels (z_parameter.c comment: "Also loads
  the Item Button Texture reused by other buttons afterwards"). A first attempt with a 128x128 tile
  broke that contract → the Navi C-Up disc rendered as 3 stacked scalloped shapes. Fixed two ways:
  (1) use a 64x64 disc (16KB RGBA32, within the proven HUD-load envelope, vs 128x128=64KB), and
  (2) SoH3D_ButtonBgTexScale() returns gw/32 and every reused texrect scales its dsdx/dtdy by it
  (a principled texel-ratio, not a magic constant). Pieces: tools/soh3d_gen_button_tex.sh ->
  button_tex_png.h; SoH3D_ButtonBgTex (soh3d_model.cpp); a gButtonBackgroundTex intercept in
  Gfx_TextureIA8 (covers the B / empty-C / item-button full-load sites) + the bare-reuse dsdx scale
  (C-Left/Down/Right/Up) + the do-action A-button 3D quad (Interface_DrawActionButton: load crisp +
  rescale the actionVtx baked 32-texel tc by gw/32 — same quad/tc remap #32 already proved). Gated on
  SoH3D_HudTexEnabled (env SOH3D_HUDTEX / REPL hudtex, default on); off restores the byte-identical
  N64 IA8 path. VERIFIED headless (Kokiri 0xEE): 4x A/B zoom of the empty blue C disc shows the crisp
  disc has a defined edge + visible bevel vs the vanilla bilinear blur, and the Navi-disc scallop bug
  is gone (single clean circle); before/after sent to user. The do-action A-button disc uses the same
  mechanism + a tc-remap proven in #32, falls back to vanilla cleanly, but could not be visually
  triggered headless (the REPL has no input injection to raise a do-action prompt) — correct by
  construction. soh only (z_parameter.c, soh3d.{c,h via header}, soh3d_model.cpp, button_tex_png.h);
  libultraship UNTOUCHED. See [[soh3d-hud-glyphs]].

- **#31 crisp higher-res HUD counter font (rupee/key/ammo/timer/score digits)** — the N64 counter
  digits were blocky 8x16 I8 glyphs (gCounterDigit0..9/Colon) drawn through Gfx_TextureI8 by EVERY
  HUD counter. Replaced with a crisp 32x64 font (Liberation Sans Bold) injected at that single choke
  point, so the rupee, small-key, ammo, race-timer and minigame-score counters all upgrade at once.
  The counter combine is colour=PRIMITIVE, alpha=TEXEL0, so a grayscale RGBA32 glyph (a=coverage)
  reproduces the digit exactly at higher res; dsdx/dtdy rescaled from the glyph dims so the full
  glyph maps onto the same rect (same quad/tc principle as the hearts + #32 A-button). SoH3D_DigitIndex
  (z_parameter.c) maps each gCounterDigit*/gCounterColon symbol to a glyph; gated on SoH3D_HudTexEnabled
  (env SOH3D_HUDTEX / REPL hudtex, default on), falls through to the byte-identical N64 I8 path off.
  Pieces: tools/soh3d_gen_digit_tex.sh -> digit_tex_png.h; SoH3D_DigitTex (soh3d_model.cpp). VERIFIED
  headless (Kokiri 238): the always-on rupee counter "150" renders smooth/anti-aliased vs the N64
  pixelated digits; before/after sent to user. soh only; libultraship UNTOUCHED. See [[soh3d-hud-glyphs]].

- **#31 crisp higher-res HUD hearts (raw-RGBA Fast3D injection)** — the N64 HUD hearts were blocky
  16x16 IA8 textures (gHeart{Full,ThreeQuarter,Half,Quarter,Empty}Tex, drawn by HealthMeter_Draw in
  z_lifemeter.c). Replaced with crisp 64x64 SVG-authored hearts injected via the same raw-in-RAM
  RGBA32 pointer path proven by the #32 Xbox glyphs ([[soh3d-hud-glyphs]]). KEY INSIGHT: the heart
  combine is (PRIM-ENV)*TEXEL0+ENV for colour, TEXEL0*PRIM for alpha — so TEXEL0.rgb is just the
  PRIM<->ENV lerp factor. A GRAYSCALE heart (rgb=intensity, a=silhouette) therefore tints EXACTLY
  like the original IA8 one, so every existing behaviour is preserved for free: heart-colour
  cosmetic CVars, the beating-heart pulse, and the partial/empty fill states (the fill fraction is
  baked as a bright-vs-dark intensity step WITHIN a solid heart silhouette, matching N64 IA
  semantics — verified offline by re-tinting the 5 PNGs ENV->PRIM). Pieces: tools/soh3d_gen_hud_tex.sh
  (SVG->PNG-embed, mirror of the xbox-glyph/stairs scripts) -> heart_tex_png.h (5 fill states);
  SoH3D_HeartTex(kind) (soh3d_model.cpp, decode-once persistent RGBA32); z_lifemeter.c maps each
  gHeart*/gDefenseHeart* symbol to a kind, loads the crisp RGBA32 in its place, and RESCALES the
  shared heart quad's far texcoords to the real texture size (the baked tc span only 16 texels ==
  the old 16x16 tile, so a 64x64 tile would show its top-left quarter — same quad-tc fix as the #32
  A-button). Gate env SOH3D_HUDTEX / REPL `hudtex 0|1` (default on); off restores the byte-identical
  N64 IA8 path (tc reset to 512). VERIFIED headless (Kokiri Forest 238): 8x zoom shows the crisp
  heart is smooth/anti-aliased with a defined dark rim vs the N64 stair-stepped 16x16; before/after
  montage sent to user. soh only (z_lifemeter.c, soh3d.{c,h}, soh3d_model.cpp, heart_tex_png.h);
  libultraship UNTOUCHED. This is the first #31 element; magic bar / digits / map markers reuse the
  same mechanism. See [[soh3d-hud-glyphs]].

- **#29b child Link FLOAT fixed (posed-feet grounding) + SLIDE confirmed not reproducing** — the
  linksrc=3ds child Link hovered ~40px above the floor. ROOT CAUSE (measured, not guessed): the
  OoT3D Link CSABs carry absolute hip (bone 1) TRANSLATION tracks authored for the taller BOY rig;
  applied to the shorter CHILD rig (the own-CSAB source for many anims, since the idle/locomotion
  CSABs live under boy/anim/ even in the child zar) they over-position the hip and lift the whole
  skeleton. Offline proof: BOY rig + nml_wait_free grounds at -2.2 local units; CHILD rig + the same
  CSAB floats at +931.7 (matches the in-game ~40px). The working linksrc=n64 path grounds precisely
  because SoH3D_UpdateAnimN64* applies the bind translation and only REPLACES bone rotations — never
  the hip lift. Naively dropping the CSAB translation tracks instead breaks the boy rig (its run
  NEEDS the hip bob: drop → +177 float), so the fix is general and pose-driven: SoH3D_SetTrackPosedMinY
  (per-model gate) caches each frame's skin matrices in every Link update path; SoH3D_PosedGroundOffset
  recomputes the posed model's lowest VISIBLE vertex (mesh_id-mask-gated so a hidden unposed equipment
  variant at its bind ~-1325 can't skew it) and returns -minY; SoH3D_TryDrawPlayer builds the world
  matrix AFTER the pose+mask are known and applies that offset innermost (pre-scale, like the auto
  path's bind-pose groundOffset). VERIFIED headless (Kokiri 0x55, child): boot-bottom 502 (floating) →
  553 == the grounded n64-retarget path (552); N64 native 542 (the residual ~10px is model height,
  identical to what the n64-retarget always showed); steady groundOff ~-900 idle / ~-1371 walk,
  matching the offline-predicted floats. The SLIDE half does NOT reproduce: sustained movement
  resolves to gPlayerAnim_link_normal_run_free → nml_run_free, so Link animates while translating
  (verified two scenes, leg-cycle captures); a speed-driven locomotion override was prototyped but
  never fired (natural resolution covers it) and was dropped rather than committed as dead code; only
  a speedXZ readout was kept in the anim debug. soh only (soh3d.c, soh3d_model.cpp); libultraship
  UNTOUCHED. See [[soh3d-link-player-path]].

- **#32 Xbox HUD glyphs reworked from overlay → clean corner BADGE** — user reported the original
  #32 "overlaid XBOX buttons on top of the existing UI" (the full Xbox disc was the button
  background, then the N64 item icon + do-action "PutAway" label drew on top, burying the A/B/X/Y
  letter). Reworked to a clean replacement: the button backgrounds stay vanilla N64 and the item
  icons render normally; each item button's screen rect + glyph (B→B, C-Left→X, C-Down→Y,
  C-Right→A) is recorded during Interface_DrawItemButtons (new SoH3D_RecordHudBtn / sSoH3dHudBtns),
  and a SMALL Xbox face-button glyph is drawn as a badge in each button's TOP-RIGHT corner AFTER
  all the item icons (new SoH3D_DrawHudBadges, called in Interface_Draw just before the A button) so
  it sits on top without obscuring the item. The do-action 'A' button reverted to the vanilla green
  circle + action label (the green already reads as A; the Xbox-A-glyph-with-label-on-top was the
  worst overlay). Reused SoH3D_DrawXboxBtn / SoH3D_XboxGlyphTex; gate unchanged (SOH3D_XBOXUI
  default on, REPL `xboxui`). VERIFIED headless (Kokiri Forest 238): item icons (sword/slingshot/
  bombs/ocarina) fully visible, each with a crisp readable corner badge (red B, blue X, yellow Y,
  green A), no stacking/clutter. soh only (z_parameter.c, soh3d.c comment); libultraship UNTOUCHED.
  Badge tuning + do-action indicator left for user sign-off. See [[soh3d-hud-glyphs]].

- **#36 2D→3D item drops default + always on** — SoH already ships a "3D Item Drops" enhancement
  (`CVAR_ENHANCEMENT("NewDrops")`, read throughout `z_en_item00.c`: rupees/hearts/magic jars/ammo
  draw a real 3D model instead of the flat spinning billboard sprite) but it defaults OFF. The
  soh3d project converts ALL graphics to 3D, so it's now forced ON. Implementation: a one-shot
  `CVarSetInteger(CVAR_ENHANCEMENT("NewDrops"), 1)` in `SoH3D_ReplPoll` (soh3d.c) — runs every
  frame with a static guard so it fires once per process, regardless of REPL connection, after the
  config is loaded. Set EXPLICITLY in both directions (the CVar persists to config across runs, so
  the opt-out env `SOH3D_NO3DDROPS=1` actively writes 0 rather than merely skipping the force). New
  reusable verification helper `SoH3D_DebugDrawDrop` (env `SOH3D_SPAWNDROP=<ITEM00 id>`, hooked in
  z_play.c next to the other debug spawns) drops one real En_Item00 collectible beside Link.
  VERIFIED headless (Temple of Time 0x55, adult Link): with the default, the dropped green rupee is
  the fat faceted 3D gem (hollow center, multi-face shading, ~38px wide); with SOH3D_NO3DDROPS=1
  it's the thin flat spinning 2D billboard (~21px wide). Log confirms `NewDrops -> 1` (default) vs
  `-> 0` (opt-out). soh only (soh3d.{c,h}, z_play.c); libultraship UNTOUCHED.

- **#32 A action button → Xbox 'A' glyph (completes the HUD button cluster)** — the do-action A
  button (Interface_DrawActionButton, z_parameter.c) is the one HUD button drawn as a 3D
  flip-animated QUAD (gSP1Quadrangle, RotateX wobble) rather than a screen-space texrect, so the
  item-button path's `SoH3D_DrawXboxBtn` (texrect) didn't cover it. Now: when Xbox UI is on, draw
  the green Xbox 'A' glyph on the SAME quad (position + flip animation preserved). The quad's baked
  texcoords (origin -16, far 1024-16) assume a 32-texel tile, so a 64x64 RGBA glyph would show only
  its top-left quarter — remapped the far tc to `(gw<<5)-16`/`(gh<<5)-16` so the FULL glyph maps onto
  the quad (correct-by-construction, no offset tuning). Combine shows TEXEL0.rgb faded by aAlpha
  (white prim); tc set explicitly in BOTH the Xbox and the N64-circle branch so toggling `xboxui`
  off restores the IA8 mapping cleanly. The do-action label still overlays (Xbox-prompt style).
  Reuses the existing `SoH3D_XboxGlyphTex('A')` — no new asset. VERIFIED headless (Kokiri 0x55,
  Link at a sign so the "Read" A button shows): `xboxui 1` → the do-action button is a clean green
  disc with a crisp centered white "A" (full glyph, NOT quartered); `xboxui 0` → reverts to the N64
  circle (tc restored, no corruption). soh only (z_parameter.c); libultraship UNTOUCHED. Remaining
  #32 polish: C-Up Navi disc, corner-badge letters. See [[soh3d-hud-glyphs]].

- **#2 press-to-skip — onepoint cutscene cameras (door/attention/treasure pans)** — onepoint
  cutscene cameras (Z-target attention pans, door reveals, treasure/switch framing) grab the camera
  away from the player and run on a timer; they were the un-skippable gap (scripted CS already skip
  via z_demo.c `csSkipButton`). New `SoH3D_SkipControlTakers(play)` (soh3d.c), hooked in `Play_Update`
  just before the camera-update loop: on a `BTN_START` press (keyboard SPACE maps to Start) it walks
  the subcameras and force-ends every active onepoint cam (`cam->csId != 0 && timer > 1`) via the
  game's OWN `OnePointCutscene_EndCutscene` — the same path the timer expiry uses, so it lands in the
  proper post-cam state (timer→0, or 5 for attention csId 5010). No auto-skip: it only fires on a
  press. Title demo (fileNum 0xFEDC) excluded. Gate env SOH3D_SKIP (default on) / REPL `skip <0|1>`;
  added REPL `cscams` (dump active subcams idx/csId/timer) and `skiptest <csId> <timer>` (start a
  onepoint cam on Link) for verification. VERIFIED headless (Kakariko 0x52): `skiptest 1020 200` →
  `cscams` shows active=1 cam1 csId=1020 timer counting; `btnhold 0x1000 2` (Start) → `cscams` shows
  active=0 (cam ended in one frame). Control: `skip 0` then Start → cam SURVIVES (active=1, timer
  keeps counting) — proves the gate + that our code is what ends it; re-`skip 1` + Start ends it
  again. soh only (soh3d.{c,h}, z_play.c); libultraship UNTOUCHED. REMAINING #2: Player csMode +
  item-get freezes (z_player.c, riskier — see #2 Open).

- **#32 Xbox face-button HUD glyphs — item-button cluster (B + 3 C buttons)** — the in-game HUD
  button prompts now render as full-colour Xbox face-button glyphs instead of the shared N64 circle
  tinted per button. NEW reusable pieces: (1) 4 authored SVG glyphs `assets/soh3d/xbox_{a,b,x,y}.svg`
  (colour disc + dark ring + white letter), rasterized+embedded via `tools/soh3d_gen_xbox_glyphs.sh`
  -> `xbox_glyphs_png.h` (mirrors the stairs SVG pipeline); (2) `SoH3D_XboxGlyphTex(which,&w,&h)`
  (soh3d_model.cpp) decodes each PNG once to persistent RGBA8888 (== N64 G_IM_FMT_RGBA/32b byte
  order) and hands the Fast3D HUD a raw RAM texture pointer — the HUD already feeds gfx_pc raw
  pointers (e.g. the do-action labels), so no resource-manager plumbing needed; (3) the in-game
  draw helper `SoH3D_DrawXboxBtn` (z_parameter.c) loads the glyph as a 32b RGBA tile with a combine
  that shows TEXEL0.rgb (the baked colour+letter) faded by PRIMITIVE.a, deriving dsdx/dtdy from the
  glyph dims so any glyph size maps onto the same screen rect. Wired into `Interface_DrawItemButtons`:
  the always-drawn B + C-Left/Down/Right backgrounds AND the empty-C-slot redraw both branch on
  `SoH3D_XboxBtnEnabled()`, restoring the IA8 circle tile + MODULATEIA_PRIM afterward so the
  Start-button draw (which reuses the loaded tile) is byte-identical to the N64 path. Fixed mapping
  B->B(red), C-Left->X(blue), C-Down->Y(yellow), C-Right->A(green). Gate env SOH3D_XBOXUI (default on)
  / REPL `xboxui <0|1>` (live). VERIFIED headless (Kakariko 0xDB): `xboxui 0` = the N64 circles
  (B green, C-Left blue, C yellow); `xboxui 1` = B turns red, the C items gain blue/yellow/green Xbox
  rings (slingshot=blue X with the white "X" peeking past the icon, bomb=yellow Y, ocarina=green A),
  the rest of the HUD (hearts/magic/rupees/minimap) unchanged, no corruption. soh only (z_parameter.c,
  soh3d.{c,h}, soh3d_model.cpp + new assets/header; tools/ gen script in the outer repo); libultraship
  UNTOUCHED. Remaining polish (A action button quad, C-Up Navi, letter-vs-item occlusion, mapping)
  tracked under #32 above. See [[soh3d-hud-glyphs]].

- **#5 stairs polish — custom SVG stone texture + configurable step size** — (1) the generated 3D
  steps no longer wear the stretched low-res kaidan ramp texture (muddy smear); they use a
  purpose-authored stone texture: `assets/soh3d/stairs_stone.svg` (one-step tile: lit flagstone
  tread / nosing highlight / shaded riser), rasterized + embedded (`tools/soh3d_gen_stairs_tex.sh`
  -> stairs_stone_png.h), loaded at runtime, injected per room with tile-aligned UVs. (2) Step size
  is now a runtime tunable (default chunkier than the old 7.8) with a RmlUi "Stair Step Size"
  row (Small/Medium/Large) + REPL `stairsize <rise>`; changing it live-rebuilds the loaded scene
  rooms via a cross-backend GPU model-cache eviction (SoH3D_GL/Vk_RequestEvictRange — the Vk one
  behind vkDeviceWaitIdle at BeginPass). Verified headless Vulkan. See [[soh3d-stairs]].

- **#26b DM gate collision — confirmed NOT a bug (no code change)** — re-checked the leftover "no
  collision" report against the source: `Bg_Gate_Shutter` (z_bg_gate_shutter.c:42-44) sets up its
  own N64 dynapoly collision via `DynaPolyActor_Init(DPM_UNK)` +
  `CollisionHeader_GetVirtual(&gKakarikoGuardGateCol)` + `DynaPoly_SetBgActor`. SoH3D's #26 fix only
  swapped the *render* CMB (windmill-mechanism → c_s01tomegate gate); it never touches the actor's
  collision init, so the gate's physics are the unmodified vanilla dynapoly. The earlier "passable"
  perception was the OLD wrong model rendering away from the real gate location, making the (correct)
  collision look detached from the mesh — #26 realigned them, and #26's headless verify already
  observed the barred gate blocking the passage. Closed by inspection; no commit needed.

- **#23 Cucco wing-flap (procedural OverrideLimbDraw replay onto OoT3D bones)** — the cucco (En_Niw)
  renders via the auto path playing its only CSAB (nw_wait), which has just a ±9° idle ruffle; the
  real wing-flap is PROCEDURAL in `EnNiw_OverrideLimbDraw` (z_en_niw.c: `rot->z += this->unk_2C4` on
  N64 wing-tip limbs 7 & 11, ~38° idle / up to ~137° agitated), never in any animation, and the auto
  path drops the override callback — so the flap was missing. FIX is a GENERIC mechanism (any actor
  whose limb motion is procedural via an OverrideLimbDraw gets it): (1) capture the `overrideLimbDraw`
  + `arg` the actor passed to its SkelAnime_Draw* call (`SoH3D_SetLimbOverride`, wired at all 6
  z_skelanime.c choke points; `kind` distinguishes the 6-arg Opa vs 7-arg variant); (2) in the auto
  retarget, PROBE the override per mapped limb — call it with `rot` seeded from the N64 jointTable and
  take delta = (override-applied rot) − jointTable rot (= the `+=` amount); (3) map the N64 limb ->
  OoT3D bone + axis via a small per-ZAR table `kSoH3dProcOverride` (cucco: limb7->bone3, limb11->bone5,
  local Z only — DERIVED: chicken.cmb wing bones 3/5 rest rZ≈-159°, nw_wait swings rZ, N64 flaps local
  z); (4) feed the delta (binang->radians) to the OoT3D bone's animated LOCAL rotation in
  `Csab::animatedBoneWorld` via a new optional `boneRotDelta` arg, plumbed through
  `SoH3D_SetBoneRotDelta`/`SoH3D_ClearBoneRotDeltas` (soh3d_model.cpp, per-model, set/cleared each auto
  draw). Gate env SOH3D_PROCOVERRIDE (default ON) / REPL `wingflap <0|1>`; `wingflap force <binang>`
  forces a fixed Z delta for direction/amplitude probing. VERIFIED headless on Vulkan (Kakariko 0xDB,
  6 cuccos): force A/B (0 vs 25000) shows the wing fan UP/out (folded->raised, correct direction, no
  shatter/deformation), 6919 px change isolated to the wing region; the LIVE probe (animdbg) logs the
  cucco's real idle flap = Z binang oscillating 0↔~6944 (≈0–38°, matching N64's 7000), X/Y zero,
  driven onto bone 3/5 — the genuine N64 procedural flap, not a fabricated curve. soh only
  (z_skelanime.c, soh3d.{c,h}, soh3d_model.cpp, asset/csab.{h,cpp}); libultraship UNTOUCHED. Memory
  [[soh3d-n64anim-csab-map]].

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
