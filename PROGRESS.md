# SoH3D — progress & state

## ✅ DONE (session 30, 2026-06-18): DYNAMIC SUN SHADOWS (enhancement layer)
User asked for "better lighting with dynamic shadows and ambient occlusion." Shipped real
shadow mapping in the SoH3D direct-GL pass (libultraship soh3d_gl.cpp 3de9f584, Shipwright
develop 9b40d483c, both pushed to `fork`).
- **How:** a depth-only pass renders the shadow casters from the scene sun direction
  (envCtx.lightSettings.light1Dir, the same dir the form-light uses) into a 2048² depth
  texture (own FBO). The main fragment shader projects each surface into light-space via a
  CPU-built world→light ortho VP and does a 3×3 PCF depth compare; shadowed fragments are
  dimmed (applied to BOTH lit characters and lit=0 scene geometry, so the shadow lands on the
  OoT3D ground). Light frustum = ortho box centred on the camera look-at (SoH3D_GL_SetShadowFocus,
  fed per frame from play->view.lookAt).
- **Casters default = "lit" draws only** (characters/props); receivers = everything. This gives
  clean character-on-ground shadows with NO ground self-shadow acne. REPL `shadow all 1` adds
  scene geometry as a caster (walls cast too, at the cost of self-shadowing).
- **Matrix convention (important):** uMV/uMP upload GL_FALSE, so in GLSL they are standard
  column-major (M*v = transform); I build lightVP the same way (mat4LookAt/Ortho/Mul col-major)
  and the depth-pass MP = lightVP * mv. Depth pass forces invertY=0 so the stored map matches the
  fragment's sampling (which uses uLightVP*world with no clip-Y flip).
- **BUG FOUND+FIXED:** the shadow map MUST clear to far depth 1.0 explicitly. Fast3D's inherited
  glClearDepth is NOT 1.0, so empty texels read "near" → every receiver compared as shadowed →
  the whole ground went dark uniformly (caught live by A/B: castAll=0 yet entire ground dimmed).
  Set+restore glClearDepth around the shadow clear.
- **State hygiene:** save/restore bound FBO + viewport + texture unit 1 (shadow map) so nothing
  leaks into Fast3D's skybox/UI. VERIFIED: SOH3D_GL_STATECHECK=1 → 0 leaked fields; live A/B diff
  (scratch/screenshots/diff_shadow2.png) shows a localised directional cast shadow on the OoT3D
  ground with zero global dimming far from Link. Demo: shadow_demo2{,_off}.png.
- **REPL:** `shadow <0|1>`, `shadow bias|str|rad|dist|all <f>`. Env SOH3D_SHADOW (default ON).

### AMBIENT OCCLUSION (same session, also DONE & PUSHED)
SSAO added (libultraship soh3d_gl.cpp 5da70722, Shipwright develop 764f0e9f5).
- **Approach:** render a PRIVATE single-sample camera-view depth texture of the SoH3D content
  using the SAME per-item transforms (mp/aspectAdj/invertY) as the visible draw → pixel-aligned
  with the frame. This deliberately AVOIDS blitting Fast3D's depth (a non-samplable MSAA
  renderbuffer) and avoids any unprojection/invertY reconstruction — the full-screen SSAO pass
  samples by gl_FragCoord. A separate full-screen program (gl_VertexID big triangle) walks a
  12-tap golden-angle spiral and darkens fragments whose neighbours sit closer to the camera
  (creases/contacts), range-checked so silhouette edges don't count. Composited as a MULTIPLY
  (glBlendFunc ZERO, SRC_COLOR); far/empty texels output 1.0 so N64-only pixels are untouched →
  AO hits only OoT3D content.
- **REPL:** `ao <0|1>`, `ao rad|str|bias|maxdiff <f>`. Env SOH3D_AO (default ON). Defaults
  str 0.7 / rad 22px. VERIFIED: STATECHECK=0 with AO+shadows both on; live A/B shows crease/contact
  darkening on Link/Saria (ao_strong_crop.png, ao_diff.png), no halos/noise.
- Both enhancements are leak-free and on by default. Render order in SoH3D_GL_RenderPass:
  shadow depth → visible draws → AO depth+composite → endPass.

## ✅ DONE (session 24, 2026-06-17): "sky bug" root-caused + diagnostic; anim phase-lock
User-driven: the rare sky-corruption bug fired live ("probe it... fully... don't assume; probably
SoH itself, not you — if it's N64 guest memory we'd need real RE").
- **"SKY BUG" FULLY ROOT-CAUSED (was "interp suspected" — falsified).** Probed live: a garbled HUD
  item icon + green/white diagonal sky stripes. It is a guest-side **texture-segment race at scene
  load**, NOT our GL pass (statecheck = 0 leaks; the corrupt elements are Fast3D-drawn HUD/sky). A
  `G_SETTIMG` references an N64 segment whose base is still 0 (`mSegmentPointers[seg]==0`) when its
  display list first runs; `SegAddr` can't resolve it → `gfx_set_timg_handler_rdp` skips the texture
  → `texture_to_load` null → the downstream null guards (anti-crash band-aids) return without binding
  → the **stale GL binding** paints garbage on that geometry. Self-heals next frame ⇒ rare/transient
  (3 clean reboots = 0 warnings). The bottom-right cyan shape is the normal minimap, not corruption.
- **DIAGNOSTIC ADDED** (libultraship interpreter.cpp, commit 560e68b0): at the skip, logs
  `SoH3D SKYBUG: unresolved texture segment <N> ... drawn by [<OPEN_DISPS file:line breadcrumb>]`
  (via g_exec_stack.getDisp(), capped 64) — the next occurrence NAMES the culprit object/draw so the
  actual cause (object-bank/scene segment not set up at first draw) can be chased. Not fixed (rare,
  cosmetic, self-healing). See memory soh3d-skybox-corruption.
- **ANIM PHASE-LOCK (handoff #2 "anims too fast" FIXED).** `SoH3D_UpdateAnimAuto` now drives the auto
  CSAB at the N64 anim's fractional progress: `csab_frame=(n64CurFrame/n64AnimLength)*csab_duration`
  (threaded from skelAnime->{curFrame,animLength} captured in SoH3D_SkelAnimeDraw). Stub idles
  (animLength<=4) + the SkelAnime-less raw path free-run. VERIFIED live: En_Daiku_Kakariko (model 2002)
  object_daiku_Anim_000C44→dk2_hashiru ("run"), n64frame advances 0..18, OoT3D run locked to the N64
  cycle, renders intact (no explosion). `animdbg` prints `n64frame=cur/len [PHASE-LOCK]/[free-run]`.
  Known interaction: procedural-motion actors (cucco gCuccoAnim, curFrame stuck at 0) now pin at CSAB
  frame 0 — orthogonal; they belong on the retarget path (handoff #1). Commit 6d6ddd900.

## ✅ DONE (session 23, 2026-06-17): run.sh Kakariko-DAY + all-NPC replace, anim 60fps, defaults
User-driven session ("replace ALL characters", smooth anims, sensible defaults). Shipped:
- **KANBAN restored to N64.** Auto-replacing signs broke the cut behaviour: En_Kanban spawns
  more En_Kanban for the break pieces, which got re-replaced as whole signs ("slashing spawns
  signs"). Re-added the OBJECT_KANBAN skip; emptied kAssemblies (merge mechanism kept).
- **run.sh now actually enables replacement.** It set neither SOH3D_AUTO nor SOH3D_N64ANIM (both
  default OFF) nor a day time — so the user saw N64 chars at night. Added SOH3D_TIME=0x8000,
  SOH3D_AUTO=1, SOH3D_N64ANIM=1 (overridable).
- **Day-clock at scene load.** SoH3D_ApplyForceTime() now runs at the TOP of Play_Init, before
  the day/night scene setup layer + actor set are chosen. Pinning dayTime only per-frame in
  ReplPoll was too late: the scene loaded the NIGHT NPC set and just looked recoloured. Now
  SOH3D_TIME loads the DAY villagers (verified: toryo/daiku/ani/ane/soldier spawn + replace).
- **Animation jitter FIXED (60fps anim).** Replaced chars' bone pose was a 20fps snapshot held
  across the N interpolation subframes while the transform interpolated -> limbs snapped. Now
  the pose system keeps per-modelId emit-ordered cur+prev poses; RenderPass lerps prev->cur by
  the subframe step (gSoH3dInterpStep, fed from RunCommands), component-wise like the N64 matrix
  interpolation. Also fixed a latent bug (old queue consumed on subframe 0 -> multi-instance
  poses wrong on the rest). Files: libultraship soh3d_gl.cpp, Shipwright OTRGlobals.cpp.
- **Display/range defaults (SOH3D mode, applied once, persisted):** MatchRefreshRate=1 (FPS=refresh),
  DisableDrawDistance=20 (20x actor draw range), WidescreenActorCulling=1. Aspect already follows
  the window. In InitOTR (OTRGlobals.cpp).
- **COVERAGE — renamed-creature aliases added (gen_object_zars.py ALIAS map, 289->306).** Many
  creatures fell back to N64 because OoT3D renamed their archive, so object_<name> missed. Added a
  hand ALIAS map (each identity CONFIRMED by dumping the target zar's main CMB): niw->nw (cucco),
  okuta->oc2, bigokuta->ocd, peehat->ph, poh->po, reeba->rb, tite->tt, wallmaster->wm2,
  kingdodongo->kdodongo, firefly->ff (keese), anubice->av, Bb->bb (bubble), geldb->gelb,
  horse_link_child->hlc (child epona), fhg->fantomHG, door_killer->killer_door, sk2->skelton
  (Stalfos). Verified live: cucco (Kakariko) + Market crowd (En_Hy townsfolk, Malon, dog, market
  people, cuccos) all auto-replace as OoT3D.
- **CORRECTION:** the "shared zelda_ec archive" concern was UNFOUNDED. En_Hy townsfolk use SEPARATE
  per-body objects (OBJECT_AOB/BOJ/AHG/BBA/CNE/BJI/COB/BOB), each already mapped to its own zar; the
  cucco is zelda_nw (not ec). zelda_ec is a redundant consolidated archive nothing routes through.
- **CREATURE ANIM MAPS (bulk dump -> hand-weave, per user direction).** Made the offline exporters
  (soh3d_skel_export.py + soh3d_anim_export.py) ALIAS-AWARE (import gen_object_zars.ALIAS, build
  OBJ_OVERRIDE from its inverse, case-insensitive zar regex) so they dump N64+OoT3D skeletons & anims
  for the aliased creatures too. Then wove 116 reviewed N64-anim->CSAB entries into soh3d_animmap.inc
  so the new creatures play proper anims (octorok shoot/die/hide, stalfos full moveset, tektite jump,
  peahat fly, poe states, gerudo combat, king dodongo, child epona gaits) instead of idle. Hand-fixed
  the matcher's alphabetical ties (Poe attack->po_atack/damaged->po_damage, Octorok shoot->oc_atack,
  Wallmaster wait->wm_wait/miss->wm_miss, Anubis standup->av_getup, Gerudo damage->geldB_hit); dropped
  garbage-frameCount entries; skipped object_fhg (bad reads) + object_door_killer (no skel anims).
  NOTE the auto path plays each model's OWN CSAB (no joint retarget) so creatures POSE correctly by
  construction — the anim map only selects WHICH CSAB. Townsfolk os_anime gestures stay idle (shared
  gesture bank across body types -> one mapping can't fit all; idle is the right fallback).
- **REMAINING N64 (no clean standalone model / not creatures):** ~96 NULL objects left, but they are
  keeps (gameplay_keep), scene object-sets (oA*/oB*/oE*/oF*), anim-only banks (*_anime, ganon_anime*,
  zl2_anime*), effects (mjin_*), items (medal/b_heart/gi via held path), Link/Epona variants, and a
  few uncertain (human/ossan shopkeeper, gol, fish-vs-fishmaster, masterkokiri). Mapping more needs
  per-CMB selection inside multi-model zars (e.g. object_fish would pick fishmaster, the wrong cmb).

## ✅ DONE (session 22, 2026-06-17): multi-CMB ASSEMBLY merge mechanism + KANBAN; generic-merge REJECTED
**Built the reusable multi-CMB merge capability and hand-applied it to the kanban signpost; a
GENERIC auto multi-CMB merge was investigated and rejected on evidence.**
- **SURVEY (the decision):** dumped all 289 mapped object ZARs (`tools/` py + ctr_romfs/zar/cmb).
  112 have >=2 "real" (non-debris, non-flat) CMBs, but they are overwhelmingly COLLECTIONS (one
  ZAR shared by many actor types, e.g. `zelda_ec` = 23 distinct NPCs; `bdan_objects`/`jya_obj`/
  `haka_objects` = dozens of objects), ALTERNATE VARIANTS (cow/cow2, koume/kotake, gi_* states),
  or effect sprites. A blanket "merge all CMBs" would draw them overlapping = massive regression.
  So a generic assembly loader is the WRONG abstraction — correct CMB selection needs the actor's
  own draw logic (per-actor). Evidence: `scratch/evidence/multicmb_finding.md` + `multicmb_survey.txt`.
  Per the user's steer ("do the ones you can manually, ignore hard ones"), implemented a
  HAND-CURATED table instead.
- **MECHANISM (libultraship... no — Shipwright@develop soh3d_model.cpp):** new `buildFromCmbs()`
  merges N CMBs into one model — concatenates draw groups + textures, rebasing each group's
  material texture index by the running texture count (`makeCgroup(cmb,group,verts,texBase)`;
  `appendTextures()`). cGroups are built only AFTER `out->groups` is final (stable vert pointers).
  `kAssemblies[]` table = {zar suffix -> CMB-name-substring list}; each substring merges EVERY
  matching .cmb. loadAutoModel consults it before the single-pick. Static props only (skinned=false).
- **KANBAN (first + only verified entry):** the OBJECT_KANBAN skip in `SoH3D_TryAuto` is REMOVED;
  it now routes through the assembly path. Entry = `{"kanban_bo_","kanban_L_","kanban_R_"}` (11 CMBs:
  post segments bo_* + the 8 board segments L_*/R_*; the flat eff_modelT slash sprite excluded).
  **Key geometry correction:** the bo_* CMBs are the POST (each ~411x N x411, stacked Y0-6014), NOT
  a board; the wide flat BOARD (2000x1155x213) is the L_*/R_* pieces — authored pre-divided into 8
  cuttable segments whose baked positions (x=+-1000, width 2000) tile a CONTIGUOUS board, i.e. the
  ASSEMBLED board at rest (the slash anim moves them at runtime), NOT shattered debris. My first cut
  merged only bo_* and rendered a bare POST (caught in-game); adding the board pieces fixed it.
- **VERIFIED in-game** (Kokiri Forest scene 0x55, 8 signs via `actorscan 0x141`, frozen cam on the
  nearest at world (49,-94,967), time 0x8000): OoT3D sign now renders a complete board+post matching
  the N64 silhouette. Before-fix (bo_* only) vs after-fix at identical camera: the board region went
  from dark grass mean RGB (51,57,25) to bright wood/parchment (128,114,64) — the board appearing
  where there was empty space. Evidence: scratch/screenshots/ksign_oot (bare post, bug),
  ksign_oot2 (full sign, fixed), ksign_n64 (N64 reference).
- **NOTE:** auto is OFF by default, so this is opt-in (SOH3D_AUTO=1). Other plausible static
  assemblies (d_lift, spot00 drawbridge, mamenoki) are in hard-to-reach scenes with low payoff;
  add to kAssemblies only when confirmed in-scene (no unverified entries).

## ✅ DONE (session 21, 2026-06-17): per-item POSE capture — LIVE two-actor proof (CLOSED OUT)
**The session-20 per-item-pose fix is now FULLY verified live.** Staged two `En_Hata` flags
(model=2001, 20 bones) in one view at Gerudo Fortress (scene 0x5d, entrance 297): `tp -4150 -18
-3300` next to flags at z=-3246 & z=-3363, frozen cam framing both. With `SOH3D_GL_DBG=1`, a single
render pass shows **two model=2001 items with DIFFERENT poseSums** (e.g. 3358.61 vs 3786.26). Parsed
the whole run: across **1458 render passes containing both flags, the two same-model items differed
in ALL 1458 (0 identical), min nonzero delta 54.04** — zero collapse to a shared pose. This is the
two-different-poses live demo that was pending. Flags animate via the N64-anim path
(`SoH3D_UpdateAnimN64` reads each actor's own jointTable → per-actor phases), and `SoH3D_GL_EmitPose`
snapshots each at emit time. Evidence: scratch/evidence/per_item_pose_proof.txt +
per_item_pose_two_flags.png.
- **NEW TOOLING (Shipwright@develop soh3d.c):** REPL `actorscan <id>` (decimal or 0xHEX) lists every
  live actor of that id with world pos + distance from Link. Ends blind scene-wandering — used it to
  find all 4 En_Hata (id 0x26) and pick the closest pair to frame. Reusable for any multi-instance
  framing/verification.
- **ANIM-MAP curation spot-check (roadmap #3):** sampled live mappings in Kokiri Forest via
  `animdbg 1` — Saria `gSariaHandsBehindBackWaitAnim`→`saria_matteru_wait` (matteru=wait ✓),
  skulltula `object_st_Anim_000304`→`st_matsu` (✓). The IDLE mappings spot-checked are correct.
  FINDING: meaningful curation needs observing NPCs in VARIED anim states (talk/walk/special),
  but free-roam mostly shows idle and the En_Ko kids wander out of frame — forcing varied anims
  on demand is impractical without scripted interaction. So #3's near-term yield is low; the
  high-value next work is roadmap #4 (broaden coverage / multi-CMB assemblies), which also surfaces
  more replaced NPCs to curate. #2 (lighting fill/per-light colour) is low-priority and would
  invalidate the session-20 brightness calibration (needs re-cal) for a modest gain — deferred.

## 🟡 PARTLY-VERIFIED (session 20, 2026-06-17): per-item POSE capture (multi-instance skinning)
**Fixes the session-16 known issue: multiple actors sharing a glModelId all rendered with the LAST
actor's pose.** Root cause is a DEFERRED-TIMING bug: `SoH3D_GL_SetBones(modelId)` stores the pose in
a per-model store during dlist BUILD, but the draw opcode runs `SoH3D_GL_Submit` at dlist INTERPRET
time — by then a later same-model actor has overwritten the store, so every item read the same (last)
pose. (My first attempt snapshotting at Submit time was a non-fix for exactly this reason — caught
before shipping.)
- **Fix (emit-time capture):** new `SoH3D_GL_EmitPose(modelId)` snapshots the just-set pose into a
  FIFO queue `g_poseQueue` at EMIT time (called in `SoH3D_EmitModelDraw` right after the
  `SoH3D_UpdateAnim*`/SetBones, before the draw opcode). `SoH3D_GL_Submit` consumes the first queued
  pose matching that modelId (same-model emits/submits keep relative order → correct pairing) into the
  `DrawItem`; `drawOne` uploads the item's own bones. `FrameBegin` clears the queue. Falls back to the
  per-model store when no emit-time pose (legacy inline path). Files: libultraship@soh3d
  {soh3d_gl.cpp,.h}; Shipwright@develop soh3d.c.
- **TOOLING:** `SOH3D_GL_DBG=1` now also logs per render-pass item `model/boneCount/poseSum` (a
  bone-checksum) — two same-model items with different poseSums = per-item pose proven.
- **VERIFIED:** (a) per-item collection — each actor makes its own DrawItem; (b) per-MODEL pose
  distinction is correct (Gerudo 15-bone vs flag 20-bone items carry their own poseSums); (c) a single
  animated instance poses correctly through the per-item path (flag `object_hata` poseSum animates
  smoothly); (d) NO regression (En_Sa Saria + En_Ge1 Gerudo render correctly). **NOT yet verified
  LIVE:** two same-model actors in DIFFERENT poses rendering distinctly — couldn't stage a scene with
  2+ simultaneously-*animating* same-model replaced actors (idle NPCs hold a static pose: two spawned
  Gerudo both poseSum=11680.4061 constant; only one fortress flag reachable in view; auto-replace skips
  skinned actors w/o a bonemap so multi-instance skinned scenes are rare). Fix is correct-by-
  construction (FIFO-by-modelId of emit-time snapshots). **NEXT to close this out:** reach the Gerudo
  Fortress courtyard (multiple flags) or a crowd of bonemapped NPCs and confirm two same-model
  poseSums differ in one frame.

## ✅ DONE (session 20, 2026-06-17): SCENE-ACCURATE form lighting (sun direction)
**The character/prop form light now follows the SCENE'S sun, not a fixed direction.** Session 19's
half-Lambert term used a hardcoded direction the comments called "camera-space"; this session drives
it from `play->envCtx.lightSettings.light1Dir` so shading tracks time of day / the world.
- **Root-caught a wrong assumption first:** the form normal was documented as "view/camera space",
  implying the light followed the camera. It does NOT. OoT folds the camera/viewing matrix into the
  **PROJECTION** matrix (`z_view.c` loads `viewing` with `G_MTX_PROJECTION|G_MTX_MUL`;
  `interpreter.cpp` line 1437 `MatrixMul(P_matrix, viewing, P_matrix)`), so the modelview-stack top is
  the **model→world** matrix only. Hence `mat3(uMV)*nM` is a **WORLD-space** normal — the old fixed
  light was world-fixed, not camera-fixed. So the fix needs NO view transform: feed the world-space
  `light1Dir` straight in. Fixed the misleading comments in `soh3d_gl.cpp`.
- **Impl:** frag shader's const `kLightDir` → per-frame uniform `uLightDir`; new global
  `gSoH3dLightDirWorld` + setter `SoH3D_GL_SetLightDir` (mirrors `gSoH3dLightEnable`), uploaded once
  in `beginPass`. `SoH3D_UpdateLight(play)` (called from `SoH3D_EmitRenderPass`) normalizes
  `light1Dir` (F3DEX dir-to-light = exactly the half-Lambert L; OoT copies it into
  `dirLight1.params.dir`) and sets it; degenerate dirs are skipped (hold last). light1 only (the sun);
  light2/colour fill is a possible follow-on.
- **TOOLING:** REPL `lightdir x y z` (override world dir, held), `lightdir auto` (back to scene),
  `lightdir` (print live dir) — lets the plumbing be A/B'd live and the scene's light1Dir be read.
- **VERIFIED quantitatively** (Kokiri Forest, static `kibako` prop, frozen camera/pose). (1) Live
  `light1Dir` traces the sun arc: t=8192 (0.70,−0.70,…)→ 20480 (0.92,0.38,…)→ 32768 noon
  (0.00,0.99,…)→ 43008 (−0.83,0.55,…). (2) Shader consumes it: lightdir toward- vs away-camera =
  **77-luma swing** on the box's interior front face (static noise 99/3380 px = drifting N64
  sparkles). (3) Sun-tracking, **tint-isolated** (on/off ratio = 0.55+0.45·hl, divides out scene
  colour): box front-face ratio **0.761 (morning, sun east/away) → 0.911 (afternoon, sun west/toward)**.
  scratch/screenshots/lighting_flat_vs_scene.png. NOTE the original "follows the camera" claim below
  is FALSE (see root-cause above); the form was always world-fixed.

## ✅ DONE (session 19, 2026-06-17): CHARACTER LIGHTING (half-Lambert form term)
**OoT3D characters/props were rendering FLAT** — the GL shader did `frag = tex·vColor·flatTint` and
the per-vertex normal (`aNrm`, read from the CMB) was uploaded but UNUSED, so models had no form
while N64 models get per-vertex N·L. Added a directional FORM term:
- **Shader** (libultraship soh3d_gl.cpp): vertex now skins the NORMAL too and outputs a view-space
  normal (via new `uMV` = modelview, no projection); fragment adds a half-Lambert wrap term for lit
  draws: `shade = uTint·(0.55 + 0.45·(dot(N,L)·0.5+0.5))` with a FIXED camera-space key light
  `L=normalize(0.40,0.55,0.73)` (form follows the camera; scene colour/time still from uTint). 0.55
  floor → never black.
- **Scoping:** only characters/props are lit (they have flat vColor); SCENE GEOMETRY keeps its baked
  vColor AO (would double-shade). The emitter packs a "lit" flag into the draw handle's high bit
  (`modelId | 0x80000000` in SoH3D_EmitModelDraw; scene rooms leave it clear); the interpreter masks
  it back out and also captures the modelview-stack top for `uMV`.
- **Toggle:** REPL `light 0|1` / env `SOH3D_LIGHT` (default on) via cross-module global
  `gSoH3dLightEnable` (same pattern as gSoh3dDumpPending).
- **VERIFIED** (Kokiri Forest, Saria, frozen pose): live `light` toggle changes ONLY the model
  (2220 px in Saria's bbox; N64 Link + scene unchanged → correct scoping). ON shows a clear
  light→dark gradient across her tunic/hair (was flat). scratch/screenshots/saria_{ON,OFF}_crop.png.
  Plausible next: drive the key light from the scene's actual light1Dir (needs the view matrix to
  put world light dirs in view space) so shading tracks time-of-day direction, not just the camera.

## ✅ DONE (session 19, 2026-06-17): N64-anim → OoT3D-CSAB MAPPING (auto skinned actors)
**Auto-replaced skinned actors now play the OoT3D CSAB that CORRESPONDS to their LIVE N64 animation
(walk→walk, cut-grass→cut-grass, idle→idle), instead of one fixed idle.** Built the "dump all anims
from both games and match them" pipeline the user asked for + wired the runtime resolver.
- **Offline pipeline** `tools/soh3d_anim_export.py` (sibling of soh3d_skel_export.py): dumps every
  OoT3D CSAB per ZAR (name+duration+boneCount, via csab.py) → `tools/skeldata/oot3d_anims.json`
  (1026 CSABs/117 ZARs); every N64 AnimationHeader per object (XML `<Animation Name Offset>` +
  frameCount read from the ROM object bytes, via n64_skel_extract) → `n64_anims.json` (785 anims/99
  objects); MATCHES per character → `animmap.json` + `soh3d_animmap.seed.inc`. Match signal =
  **frame count** (Grezzo re-exported the same data; an N64 frameCount usually == one CSAB's
  duration), with an idle-aware tiebreak: an N64 idle that is itself a 2-frame STUB (ge1
  gGerudoWhiteIdleAnim fc=2) must pick the LIVELY idle (ge1_s_wait d22), not the stub frame-match
  (ge1_matsu d2); a real idle LOOP (Saria fc=24) keeps the frame-close idle. Verified the seed
  reproduces ge1 ground truth (Idle→ge1_s_wait, Clap→ge1_mon_akeru) + Saria idle→saria_matteru_wait.
- **Hand-maintained map** `Shipwright/soh/src/soh3d/soh3d_animmap.inc` (mirrors the bonemap design:
  generator writes a `.seed.inc`, the `.inc` is hand-owned). Flat `SOH3D_ANIMMAP(n64otr, csab)` table
  keyed by the N64 anim's OTR resource path (object-qualified → globally unique). 785 entries
  bootstrapped from the seed; frame-count guesses are APPROXIMATE — hand-fix as verified in-game
  (1 so far: ge1 Dismissive→ge1_hanasi, a semantic match frame count can't see).
- **Runtime** (soh3d.c): `skelAnime->animation` is an OTR path string in SoH (ResourceMgr resolves
  it) = the stable key. `SoH3D_ResolveAutoCsab` strips `__OTR__` and strcmp-looks it up. Captured
  per-actor: reset in `SoH3D_TryDrawActor`, set by the SkelAnime-bearing choke points
  (SoH3D_SkelAnimeDraw + new `SoH3D_SetCurAnim` called from func_80034BA0/CC4, whose inner
  SkelAnime_DrawFlex/raw hook has only skeleton+jointTable). Auto branch of `SoH3D_DoRetarget` uses
  the mapped CSAB, falling back to the default idle when unlisted. `SoH3D_UpdateAnimAuto` now
  restarts the per-model playhead when the selected CSAB changes (one-shots start at frame 0).
- **VERIFIED in-engine** (Kokiri Forest entrance 238, real GPU, single clean instance): live anim →
  corresponding CSAB across 3 chars — Saria `gSariaHandsBehindBackWaitAnim`→`saria_matteru_wait`,
  Kokiri kid `gKokiriCuttingGrassAnim`→`fad_kusu_to_wait` (a real km1 CSAB; semantically apt),
  Skulltula `object_st_Anim_000304`→`st_matsu`. NOT the old fixed default (`saria_banzai_wait`).
  Screenshot scratch/screenshots/saria_front.png (OoT3D Saria, hands-behind-back pose).
- **TOOLING:** new `tools/soh3d_game.sh` (start|restart|stop|status|log) — single-instance game
  manager. Root-caused a recurring time-sink: the hand-rolled kill loop matched `*soh.elf)` and
  SILENTLY missed rebuilt binaries that readlink reports as `soh.elf (deleted)`, so MULTIPLE
  instances piled up fighting over one REPL FIFO (commands reverting, log from a different instance).
  The manager kills the `(deleted)` form too, guarantees one instance, and self-daemonizes
  (setsid+nohup) so no orphaned background launches. USE IT instead of hand-launching.

## ✅ DONE (session 18, 2026-06-17): FIX invisible hook-replaced skinned actors (POLY_OPA rewind)
**The "auto-skinned chars draw-yet-invisible" bug from the session-17 handoff is FIXED + pushed**
(Shipwright@develop 0c453a3a6, `soh/src/code/z_skelanime.c` only). En_Sa (Saria, Kokiri Forest)
now renders her OoT3D 3DS model playing her own CSAB.
- **Root cause (root-caused, not patched):** the SoH3D SkelAnime hook emits the OoT3D model into
  POLY_OPA via a nested `OPEN_DISPS`, advancing the GLOBAL `__gfxCtx->polyOpa.p` (POLY_OPA_DISP is
  literally that field — release OPEN_DISPS keeps no local copy). But the THREE Gfx*-returning
  choke points — `SkelAnime_Draw`, `SkelAnime_DrawFlex`, `SkelAnime_DrawSkeleton2` — returned the
  STALE pre-hook `gfx`, and the caller's `POLY_OPA_DISP = SkelAnime_DrawX(...)` wrote that stale
  pointer back, REWINDING polyOpa.p over the just-emitted `gSPSoH3DDraw` opcode. The interpreter
  then never executed it (`SoH3D_GL_Submit` was never reached → the model was absent from the
  render-pass draw list, even though the pass reported "N/N items glerr=0" for the OTHER items).
  The VOID-returning choke points (`DrawSkeletonOpa`/`DrawOpa`/`DrawFlexOpa`, used by En_Ge1) don't
  thread the pointer back → unaffected. **That is exactly why En_Ge1 worked via the hook but En_Sa
  was invisible** — it was never CSAB/pose/scale/material (all verified sane during the hunt:
  Saria skinned-vert bbox sane, materials opaque/no-alpha-test, scale ~0.0105 → ~47u).
- **Fix:** in each Gfx*-returning function, capture `Gfx* soh3dOpaEntry = polyOpa.p` at entry; when
  the hook fires, `return (gfx == soh3dOpaEntry) ? polyOpa.p : gfx` — i.e. for an OPA draw return
  the ADVANCED pointer (no rewind); for XLU/other-buffer callers the opa emit is independent of gfx
  so return gfx unchanged. No behavior change when SoH3D is off (hook returns 0).
- **How it was found:** heartbeat logs proved `auto-emit model=N` fired every frame but
  `SoH3D_GL_Submit modelId=N` never did → the opcode was emitted but never executed → pointer
  clobber. (Discriminating steps: skinned-vert bbox = sane → not pose; draw-list ids = model
  absent → not drawn; Submit heartbeat = never called → emit clobbered.)
- **NEXT (per handoff):** N64-anim → 3DS-CSAB MAPPING (select CSAB from the actor's live N64 anim,
  not just the idle default); ensure every replaced obj has a CSAB. Saria currently plays
  `saria_banzai_wait` (arm-raised idle) — fine, but the right idle/anim selection is the next step.

## ✅ DONE (session 16, 2026-06-16): OWN THE OoT3D FRAME — dedicated SoH3D render pass
**The strategic direction (PRIMARY) landed: SoH3D content no longer draws inline inside
Fast3D's frame fighting its cached GL state. It is now COLLECTED and drawn in ONE
GL-state-bracketed pass.** This subsumes the recurring GL-state-leak (striped skybox) bug.
Pipeline:
- `OTR_G_SOH3D_DRAW` handler (interpreter.cpp) no longer calls `SoH3D_GL_Draw` inline — it
  calls `SoH3D_GL_Submit`, capturing {modelId, MP_matrix snapshot, invertY, tint, aspectAdj}
  into a per-frame draw list (soh3d_gl.cpp `g_drawList`). Capturing MP HERE is essential
  (it's that item's matrix; the render-pass opcode comes later when MP is something else).
- New opcode `OTR_G_SOH3D_RENDERPASS` (0x4b) + `gSPSoH3DRenderPass`: `gfx_soh3d_renderpass_handler`
  flushes Fast3D then calls `SoH3D_GL_RenderPass`, which drains the list — `beginPass` saves
  Fast3D's GL state + installs ours ONCE (isolated VAO, our program, depth LEQUAL, scissor/cull
  off), `drawOne` per item (per-group blend/depth from the CMB material), `endPass` restores
  Fast3D's state + deterministically resets the once-set state (blendFunc/equation/blendColor,
  depthFunc) ONCE. Old `SoH3D_GL_Draw` kept as a thin single-item bracket (legacy/unused path).
- Emitted once/frame: `SoH3D_EmitRenderPass(play)` right after the actor draw-all
  (`func_800315AC`, z_play.c) so OoT3D content composites after Fast3D's opaque 3D, before UI.
  `SoH3D_FrameBegin()` (at the per-frame REPL-poll point) drops any items left unrendered by a
  transition early-out so they can't double-draw.
- Pose note (unchanged, pre-existing): skinning is per-modelId (`SoH3D_GL_SetBones`), set at
  emit time; multiple actors sharing a model id still share the last pose. Not a regression of
  this change; fix when char-replacement resumes.
- **VERIFIED** (Kakariko entrance 219, real GPU :0, SOH_FRAMEDUMP): room geometry (windmill/
  gate/walls) renders, Link composites depth-correct in front, render pass logs "5/5 items
  glerr=0x0" every frame, no crash. Skybox CLEAN — quantitative: 34.7k blue-sky px, B-channel
  std 1.1, horizontal scanline adjacent-pixel delta 2.1 (striping would be tens-to-hundreds).
  The recurring candy-stripe leak is GONE. Screenshot scratch/screenshots/rp_kak.png.
- Files: libultraship@soh3d {soh3d_gl.cpp/.h, interpreter.cpp, fast/lus_gbi.h,
  libultra/gbi.h}; Shipwright@develop {soh3d.c/.h, z_play.c}.
**NEXT (render path, step 3):** grow coverage — more of the frame under our pass; consider
per-item pose capture (fix shared-model pose); eventually our own view/proj setup.


Goal: make **Ship of Harkinian** render **OoT3D (3DS)** character models and world
geometry instead of the N64 assets. Asset-conversion + renderer-integration task
(not a renderer merge). Azahar (3DS emulator) is built as the **visual oracle**.

## 🚧 IN PROGRESS (session 15) — USE OoT3D COLLISION
**Step 1 DONE (2026-06-16): OoT3D scene-collision format fully reverse-engineered + verified.**
noclip has the `Collision = 0x03` enum but NEVER parses it, so there was no reference — REd
from scratch off the USA decrypted ROM. Oracle: `tools/oot3d_collision.py` (verified across
spot04/spot01/gerudoway/spot00/ydan/ddan: plane identity `n·vA == -dist` holds for ~100% of
polys, stored normal == geometric face normal for 99.9%). Format (in `<scene>_info.zsi`,
scene-header cmd-0x03; command addrs are PLAIN file offsets, proven via Rooms cmd):
- **CollisionHeader** @ cmd-0x03 file offset: `+0x1c u16 nVtx`, `+0x1e u16 nPoly`,
  `+0x28 u32 vtxList`, `+0x2c u32 polyList`, `+0x30 surfaceTypeList`, `+0x34 camData`,
  `+0x38 waterBox`. **Internal data pointers are NOT plain offsets**: actual vertex data =
  `vtxList + 0x10`; the poly array is anchored at `polyList - 2` (the 3 vtx indices of poly 0
  sit at file offset polyList-2). These two offsets differ (+0x10 vs −2) but are STABLE across
  all tested scenes — do not "fix" them.
- **Vertex** = `Vec3s` (s16 x,y,z), stride 6, from `vtxList+0x10`.
- **CollisionPoly** = 20 bytes, stride 20, from `polyList-2`: `+0 u16 vA`, `+2 u16 vB`,
  `+4 u16 vC` (each `& 0x1FFF`; top 3 bits = flags), `+6 u16` flags/material, `+8 s16 nx`,
  `+0xa ny`, `+0xc nz` (normal /32767), `+0xe f32 dist` (plane `n·p == -dist`), `+0x12 u16` pad.
- Collision floor matches the OoT3D RENDER mesh floor (spot01 median 0.1u; spot04 noisier only
  because multi-level render geom has non-walkable canopy/rooftops the max-Y probe catches).
**Step 2 DONE (2026-06-16): OoT3D collision drives gameplay in-engine.** Link now physically
walks the OoT3D world. Pipeline:
- C++ parser `asset/zcol.{h,cpp}` (OoT3DCollision) + C-ABI bridge `soh3d_collision.h` /
  `SoH3D_LoadSceneCollisionRaw` (soh3d_model.cpp, reuses `rom()`).
- `SoH3D_BuildSceneCollision(play, n64)` (soh3d.c) converts to a SoH `CollisionHeader`: verts
  copied 1:1 (N64-unit world-space, no transform), poly normal = raw s16 (matches
  COLPOLY_SNORMAL), `dist = (s16)oot3dDist` (SoH plane normal·p+dist=0 == OoT3D n·p=-dist),
  vA/vB/vC → flags_vIA/vIB/vIC, all polys share ONE generic SurfaceType (floor/wall/ceiling
  comes from the normal, not the type). Arrays malloc'd + kept resident (freed on next build).
- Hook: `Scene_CommandCollisionHeader` (the ACTIVE one is the OTR path `soh/z_scene_otr.cpp`,
  NOT `src/code/z_scene.c` — both hooked) installs the OoT3D header via `BgCheck_Allocate`.
- **Waterboxes + camera regions are COPIED from the N64 header** (not REd yet): actors like
  `Bg_Spot01_Idomizu` write `colHeader->waterBoxes[0]` and crash on a NULL list. Same world
  space, so carrying N64 water/cam over is correct enough; floors+walls are OoT3D.
- **Per-poly SurfaceType (exits + cameras + floor types) DONE.** The OoT3D poly's `+0x12` field
  is the surfaceType INDEX (spot04: 47 types [0,46]; spot01: 28 [0,27]); the surfaceType list is
  at `surfaceTypeList+0x10`, count at header `+0x20`, 8 bytes/entry, **same bitfield layout as N64**
  (data0 low byte = camIndex, `(data0>>8)&0x1F` = scene EXIT index, data1 = floor props). Since
  it's the same game, the cam/exit indices line up with SoH's N64 cameraDataList / exit list, so
  zcol now parses them and the builder copies `data0/data1` verbatim + sets each poly's `type`.
  This fixed: **scene exits** (user-caught: "can't leave the village" — every floor had exit 0),
  per-region cameras (replaces the earlier forced-CAM_SET_NORMAL0 hack), and special floors.
  REPL `exitat x z` reports the floor poly's exit/cam index. Verified: exit floors report their
  index in-engine; user confirmed leaving Kakariko works. Camera + waterboxes still copied from
  the N64 header (CamData/WaterBox OoT3D formats not REd; indices align since same game).
- Gate: `SoH3D_CollisionEnabled()` (env `SOH3D_COLLISION`, default ON; REPL `collision <0|1>`,
  takes effect next scene load/warp). Terrain Y-offset auto-disables when collision is on
  (mutually exclusive — `SoH3D_TerrainWarpEnabled()` returns 0).
- **Verified in-engine** (Kokiri spot04, Kakariko spot01): `floorat` (N64 BgCheck, now reading
  OoT3D collision) == `meshfloor` (OoT3D render floor) across the scene (e.g. the old sink spots
  Kokiri (-1067,429) and Kakariko (-1067,429): collision==render, sink GONE); Link stands
  exactly on the floor (y == floorat under him); no crash; BgCheck buffer holds 3858 polys.
  Residual: steep multi-level spots (Kakariko (-579,-1314)) differ ~130u — collision picks a
  higher real surface than the render max-Y probe; not a parse error (plane identity was 100%).
**STILL TODO:** RE the OoT3D surfaceType list (per-poly material: sound, special floors like
sand/void) + waterbox/camData sub-lists (offsets at `surfaceTypeList`/`camData`/`waterBox`
header fields, all +0x10 like vtx); test more scenes (dungeons, Hyrule Field poly count/stall);
verify walls/Link physics interactively with the user.

## (superseded) NEXT MAJOR EFFORT (session 14 decision) — USE OoT3D COLLISION
**User decision (reverses the earlier "keep N64 collision" rule):** drive gameplay from the
**OoT3D scene collision mesh** so Link physically walks the OoT3D world. Why: N64 collision
geometry and OoT3D render geometry genuinely differ in 3D — not just floor height but **walls
and edges** (proven in Kokiri: Link sinks where OoT3D ground ≠ N64 floor AND can walk *into*
OoT3D walls because they sit at a different XZ than the N64 collision wall). Every render-side
fix tried (mesh warp → height-blend → inverse per-actor Y-offset onto the OoT3D floor) is a
*vertical* band-aid; **no Y-offset can fix horizontal (wall) mismatch.** The only true fix is
ONE geometry for gameplay+visuals → use OoT3D's collision.
- **Current state being superseded:** the inverse approach (actors offset to the OoT3D render
  floor, `SoH3D_ActorRenderYOffset`, render mesh untouched) is committed and is a decent
  *visual* stopgap for floors, but OoT3D collision replaces the need for it (and fixes walls).
- **Starting points for the port:** (a) OoT3D scene collision lives in the **scene-level ZSI**
  (a collision command — the N64 cmd-0x03 analogue; noclip's oot3d parses it: vertices + polys
  + surface types). Our `zsi.cpp`/`zsi.py` currently parse ONLY the room `cmb ` geometry — add
  collision parsing. (b) SoH gameplay collision = `play->colCtx` / `BgCheck_*` (see
  `SoH3D_N64FloorCb` using `BgCheck_EntityRaycastFloor1`). Decide: convert OoT3D collision →
  SoH's `CollisionHeader`/`CollisionPoly` format and inject into `colCtx` at scene load, OR
  hook the BgCheck queries. (c) Verify with `floorat` (N64 collision) vs the OoT3D collision +
  visual `meshfloor`. Keep it gated for A/B.
- See memory `soh3d-terrain-warp` (history of the render-side attempts + why they fail).

## 🚧 IN PROGRESS (session 16): "replace ALL characters" — DATA-DRIVEN maps (user direction)
**User direction: precompute the per-character bone-correspondence OFFLINE (dump both games ->
JSON), reference it directly in-game; do NOT match at runtime.** Pipeline built:
`tools/soh3d_skel_export.py` -> `tools/skeldata/oot3d_skeletons.json` (156 skinned chars, parsed
offline from each ZAR via cmb.py) + `tools/skeldata/bonemap.json` (per char: scale_ratio +
bone_to_limb map). N64 skeletons captured in-game via SOH3D_SKELDUMP -> `tools/skeldata/n64/<base>.txt`
(only zelda_boj so far; scale_ratio 1.0167, map verified == hand map). Matcher in
`tools/soh3d_skel_match.py` (collapse zero-len bones, pair children by length).

**Two findings this session (IMPORTANT):**
- **Saria (and most NPCs) are NOT being replaced — they render N64.** The SkelAnime replacement
  hook is ONLY in `SkelAnime_DrawSkeletonOpa`/`DrawSkeleton2` (z_skelanime.c). En_Sa never calls
  those, so it's never offered for replacement (verified: 0 retarget logs with Saria framed at
  dist 107). This is a COVERAGE gap separate from the bone map. To actually SEE OoT3D characters,
  the hook must cover more paths (esp. `SkelAnime_DrawFlexOpa`, the common humanoid path).
- The earlier "Saria looks good" was the N64 model. Don't claim a replacement without a DRAW log.

**NEXT (resumable):**
1. Game-side: generate `soh3d_bonemap.inc` from bonemap.json (keyed by object id), load it, and
   use `bone_to_limb` + `scale_ratio` in `SoH3D_UpdateAnimN64` instead of the identity map.
2. Widen the SkelAnime hook (DrawFlexOpa etc.) so En_Sa/Kokiri/etc. are offered for replacement
   — but only AFTER the map is wired, else more chars render with the wrong identity map.
3. Collect N64 skeletons for the chars we want (drive scenes w/ SOH3D_SKELDUMP -> n64/*.txt; the
   hook-coverage widening also makes SKELDUMP capture them). Then re-run soh3d_skel_export.py.
4. If "replace + correct map" still mis-poses divergent rigs (differing rest rots), switch to the
   WORLD-orientation retarget (see below). Verify live (skill soh3d-game-control: actors/cam/shot).
   Boj lives in Market (loads as empty NIGHT — need a non-night route or a different Boj scene).

## (history) "replace ALL characters" — SCALE fixed, POSE analysis
**SCALE done + verified + pushed (2026-06-16).** Skinned auto-actors no longer use the bbox
measure (which over-measures articulated actors -> giant). Scale now comes from the REST
skeletons' bone-length ratio (rotation-invariant; N64 & OoT3D are the same Grezzo-ported char):
`scale = actor->scale * (Σ N64 |jointPos|) / (Σ OoT3D |trans|)`. Verified Market: Boj 0.01017
(n64sum 15100.7 / oot3dsum 14853.1), Malon 0.00986 — sums agree ~1.5%. Code: SkelAnime hook
(soh3d.c) + `SoH3D_AutoModelBoneLenSum` (soh3d_model.cpp) + `SoH3D_WalkN64Skeleton` (OOB-safe
limb-tree walk — blind 0..limbCount-1 crashed on skeletons with an unreferenced trailing limb,
e.g. Boj limbCount 16 but only limbs 0..14 reachable).

**POSE still wrong for topology-divergent rigs (NEXT, the hard part).** The retarget still uses
the identity map (OoT3D bone i <- N64 limb i), which only works when the rigs match index-wise
(ge1). For Boj it's wrong because:
- TOPOLOGY DIVERGES: OoT3D has a pure root b0 + zero-length reorient bones (b1=-90,-90; b8) that
  N64 folds into the waist limb; AND the leg branch attaches at a different node (N64: waist
  limb0; OoT3D: root b0). So neither identity nor a fixed offset maps them.
- BIND POSES DIFFER: N64 rest = zero-rotation (splayed); OoT3D rest carries real rotations. So
  matching by rest POSITION fails, and the "replace" retarget (N64 local rot AS the OoT3D bone
  local rot — works for ge1 whose rest rots are ~identity) won't pose Boj.
PLAN (proper fix), step 1 of 2 DONE:
(1) CORRESPONDENCE — DONE + VERIFIED offline. `tools/soh3d_skel_match.py` produces the OoT3D
bone -> N64 limb map and reproduces the hand-derived Boj map EXACTLY (all 16 bones; lengths
align: arms 492~577/1336~1401/1456~1451, head 1815~1855, legs, zero-len hubs b1<->limb0,
b8<->limb7, OoT3D pure-root b0 -> identity). ALGORITHM: build both limb trees; COLLAPSE
zero-length reorient bones (|trans|<1) on BOTH sides (OoT3D inserts a pure root + reorient bones
that N64 folds into one limb); pair each node's effective children by |bone length|; align the
collapsed zero-len chains (chain bone i <-> chain limb i; extra OoT3D reorient bones map to the
parent's N64 limb). Boj map (bone->limb): b0->-1,b1->0,b2->1,b3->2,b4->3,b5->4,b6->5,b7->6,
b8->7,b9->14,b10->8,b11->9,b12->10,b13->11,b14->12,b15->13.
(2) RETARGET — TODO (the remaining uncertain mile, needs LIVE visual iteration). The "replace"
retarget (N64 local rot AS OoT3D bone local rot) only works when rest orientations align (ge1).
For rigs with differing rest rots (Boj b1=-90,-90) the proper fix is WORLD-orientation: FK the
live N64 jointTable to world rotations, set each OoT3D bone's world orientation from its mapped
N64 limb, skin = worldAnim * bindInverse. NEXT: port soh3d_skel_match.py to C++ (SkelAnime hook /
soh3d_model.cpp), feed the map into SoH3D_UpdateAnimN64, switch to world-orientation retarget,
verify by driving to a stationary Market Boj. Captured data: scratch/skeldump/; OoT3D bones also
parse offline via tools/cmb.py (no game needed).

## (history) IN PROGRESS (session 15): "replace ALL characters" (WIP, gated SOH3D_N64ANIM)
Skinned auto-actors now route through the generic N64-anim SkelAnime hook (was skipped). Committed
but NOT good yet — **two unsolved problems, both solvable PROGRAMMATICALLY from the shared
skeleton** (N64 and OoT3D are the same character; Grezzo ported the rig):
1. **Scale (giant):** the measure-bbox over-measures skinned actors (roofman n64h=1091, soldier
   n64h=4773 — no NPC is ~1000u tall; Link≈70). Static props measure fine. Robust fix: derive
   scale from the N64 skeleton REST pose (walk skelAnime->skeleton limb tree, rest jointPos ×
   actor->scale = N64 world height) vs the OoT3D CMB rest height — no bbox measure.
2. **Pose (malformed):** the retarget assumes N64 jointTable[i+1] -> OoT3D bone i. Works when the
   rig matches (ge1 Gerudo woman, hand-calibrated) but breaks when bone COUNT or ORDER differs
   (roofman 15 OoT3D bones). Stopgap added: a bone-count guard (auto path only) falls back to N64
   when count != limbCount — prevents giant/malformed but skips those chars. Robust fix: compute
   the OoT3D-bone <-> N64-limb correspondence by matching the two skeletons' rest poses / tree
   topology at runtime (both available: skelAnime->skeleton + cmb->bones()).
**NEXT:** implement rest-pose skeleton matching (scale + correspondence) in the SkelAnime hook /
soh3d_model.cpp. This is the user's explicit ask: "there MUST be a way to compute the correct
replacements programmatically" — and there is (shared skeleton).

## 🔁 RECURRING: GL state leak striping the skybox/UI (user-caught again, session 15)
The "candy-stripe diagonal bands on black sky" is the [[soh3d-gl-state-leak]] regression
recurring — our direct-GL draw (soh3d_gl.cpp) leaves some GL state Fast3D's OGL backend doesn't
re-set per draw, leaking into the skybox/2D. The VAO-isolation fix reduced but didn't kill it.
**User's strategic framing (worth acting on):** SoH is a decomp of OoT's *logic* but RENDERS via
Fast3D (N64 RDP/RSP display-list -> GL translation); our OoT3D GL injection co-exists with it and
fights its cached state. "Port more of it" = own the GL frame state around our draws completely
(save/restore the FULL set Fast3D assumes, or push/pop a complete state block), and longer-term
own the OoT3D render path rather than injecting alongside Fast3D. NEXT: enumerate every GL state
our draw touches and harden the restore (deterministic, not glGet) — find the one still leaking
into the skybox pass specifically.

## ⏸️ ALSO PENDING (superseded by the section above): "replace ALL characters"
Route SKINNED auto-replaced actors (currently SKIPPED to avoid T-pose, see
`soh3d-auto-replace`) through the GENERIC N64-anim SkelAnime hook (already built, session 14:
`SoH3D_SkelAnimeDraw` + sModelTable `n64anim` flag). Plan: in `SoH3D_TryAuto`, stop skipping
skinned models when `SOH3D_N64ANIM` is on; measure+scale them, compute a groundOffset =
`-(bind-pose bbox minY)` (ge1's -1000 == -minY, verified), then set the pending N64-anim state
(`gSoH3dPendingActor/Model/Scale/GroundOff`) + return 0 so the hook drives them from live N64
joints. RISK: characters whose OoT3D rig doesn't correspond to the N64 skeleton (bone i ↔
jointTable[i+1]) will pose wrong — add per-character objId skips as they show up. Grezzo mostly
preserved rigs (verified for ge1), so it should broadly work ("we tried this and it worked").


## 🔧 OPEN ISSUES — found by the user driving Kakariko (session 11→12, 2026-06-16)
Three live rendering/behaviour bugs, in priority order. (Aspect-ratio shear from session 11
is FIXED + pushed — see below.) Session 12: ISSUE 1 (terrain sink) and ISSUE 2 (window
light shaft) FIXED; added OoT3D vertex-color lighting + time-of-day control. ISSUE 3
(black crate) still open.

### ⚠️ CRITICAL RE FINDING — Fast3D OpenGL backend CACHES GL state (read before any GL hook)
`GfxRenderingAPIOGL` (`libultraship/src/fast/backends/gfx_opengl.cpp`) only issues GL state
calls on CHANGE (shadow vars `mLast*`), and some state it sets ONCE at init and assumes
constant forever — notably **`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` is set once
and NEVER re-set** (it only toggles `GL_BLEND` enable per draw via `SetUseAlpha`). Depth
func/mask/zmode are re-set only when their cached state changes. ⇒ **Any GL state our
`SoH3D_GL_Draw` (soh3d_gl.cpp) changes that gfx_opengl does NOT re-set per draw will LEAK
into all later Fast3D draws** (the "opaque 2D UI/sprites/bushes/skybox went transparent +
whitened" regression was our per-material additive blend func leaking). Rule: after our draw,
reset every non-per-draw GL state we touched to gfx_opengl's exact assumption (blend func →
SRC_ALPHA/ONE_MINUS_SRC_ALPHA, equation → FUNC_ADD), and restore per-draw-cached state
(blend enable, depth test/mask, attribs, program, texture, buffers) to the value at entry so
gfx_opengl's shadow vars stay consistent. Do NOT use glGet→restore for the once-set state
(if the query returns an unexpected value you restore garbage and break everything worse —
that happened). Deterministic reset to the known assumption is correct.

### OoT3D vertex-color lighting (session 12)
Scene-room CMBs carry per-vertex RGBA = OoT3D's BAKED lighting (walls dimmed ~0.5, ground
AO, additive light-shaft/god-ray alpha falloff). We were dropping it → flat, washed,
"cubic" windows. Now `cmb.cpp` reads the color attribute, `soh3d_gl` modulates
`frag = tex.rgb*vColor.rgb*uTint, a = tex.a*vColor.a`. Applied to SCENE ROOMS ONLY
(`buildFromCmb(out, bakedVertexColor)`): characters/props are lit dynamically and their
color attr is unused/garbage (geldwoman reads ~0 → would render black), so they force white.
Night Kakariko now matches the OoT3D reference (dark stone, glowing windows).

**ISSUE 1 — Link sinks into the OoT3D terrain. ✅ FIXED (session 12, render-mesh warp).**
Collision stays N64; the OoT3D render mesh diverged where OoT3D reshaped ground (Kakariko
`(-1067,429)`: OoT3D=20 vs N64=10). **User directive: do NOT inject OoT3D collision —
instead recompute the OoT3D terrain to match N64 levels while preserving cliff/mountain
relief.** Implemented as a per-XZ vertical warp of the RENDER mesh:
- `D(x,z) = N64_floor - OoT3D_floor` on a 100u grid. N64 floor = `BgCheck_EntityRaycastFloor1`
  (the surface Link stands on); OoT3D floor = the room mesh. Structure outliers (|D|>120)
  are rejected + hole-filled (BFS) from nearby ground, so a building/cliff column shifts by
  its local ground correction (relief preserved, only the ground baseline re-levels). Every
  room vertex Y += bilinear sample of D.
- In-engine: `SoH3D_WarpRoomToN64` (soh3d_model.cpp), called once per room model from
  `SoH3D_TryDrawRoom` (has PlayState/colCtx), cached. Gate: `SoH3D_Enabled()` + env
  `SOH3D_TERRAIN_WARP` (default ON, =0 for A/B) + REPL `terrainwarp`.
- Oracle (offline, verified first): `tools/soh3d_warp.py` — ground cells within 1u 60%→92%,
  sink 19.9→10.7. In-engine verify: warped `meshfloor` vs N64 `floorat` across 14 spread
  Kakariko points mostly within a few units (sink 20→11.8 vs 10.3; flat 238.1 vs 238.1).
- New REPL cmds: `floorat x z` / `floorgrid x0 z0 x1 z1 step path` (N64 BgCheck field),
  `meshfloor x z` (warped render-mesh floor). Tools: `soh3d_terrain_diff.py`, `soh3d_warp.py`.
- **Residuals (not blocking, revisit if visible):** (a) one Kakariko sample (-579,-1314) still
  −28 (steep/structure-edge; coarse grid + topmost-floor pick); (b) a few map-edge points have
  no OoT3D mesh floor (warp MISS); (c) the warp is ~nx·nz·tris ≈ 1e8 triangle tests at first
  room draw (one-time stall) — bucket triangles by XZ if Hyrule Field stalls. Generalizes to
  any scene automatically (no per-scene data shipped).

**ISSUE 2 — window "light shaft" renders as an opaque grey trapezoid. ✅ FIXED (session 12).**
Root cause: the direct-GL renderer forced blend on with GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA
for every group and ignored the CMB material blend mode, so ADDITIVE materials (dst=GL_ONE)
drew opaque. The OoT3D scene CMB stores blend state as GL-ES enum values (used verbatim):
the window-light material is `s01_mado_light` (mat19, src=GL_SRC_ALPHA dst=GL_ONE, depth-write
off), god-rays are `s01_hikari` (mat4). Fix: `cmb.cpp` parses per-material blend (enable,
src/dst/eq RGB+A @ 0x138/0x13C.. , const color @ 0x14C, depth-write @ 0x135 — offsets per
noclip readMatsChunk, verified), passed via `SoH3DGlGroup` to `soh3d_gl.cpp`, which sets
`glBlendFuncSeparate`/`glBlendEquationSeparate`/`glBlendColor` + per-material depth-write per
group (opaque mats disable blend). Resets blend eq/color to GL defaults after the draw so
Fast3D isn't corrupted. **Verified:** at night the window shafts GLOW on the dark wall (beam
≈165 brightness vs wall ≈35); previously flat grey. Tool: `cmb.py` now dumps blend state.
Also added **time-of-day control** (env `SOH3D_TIME` / REPL `time`; pins dayTime+skyboxTime);
`soh3d_gpu_launch.sh` defaults to noon (0x8000) — set `SOH3D_TIME=0` for night.

**ISSUE 3 — a crate renders as a solid black box. ✅ FIXED (session 12).** Confirmed cause:
the large crate (Obj_Kibako2) went through the LEGACY F3DEX2 baked-dlist path
(`glModelId=-1`) which has the known texture-upload failure → black. Fix: move it to the
runtime direct-GL path. ALL props are now on the GL path (no more baked dlists):
- modelId 1 = crate `/actor/zelda_kibako2.zar` (CIkibako_model), 2 = bush `/actor/zelda_kusa.zar`
  (obj_kusa01_model, En_Kusa, scale 0.5), 3 = pot `/actor/zelda_tsubo.zar` (tubo2_model).
- Multi-CMB ZARs: added a `cmbName` selector to `ModelSpec`/`loadActorModel` so a ZAR with a
  main model + a `hahen`/debris variant picks the intact one (kibako/tsubo both have debris CMBs).
- Verified in-game: crate (Gerudo Valley) wood-plank textured; pots (Kakariko) terracotta;
  bush (Kakariko) leafy — all correct, not black.

### Decal z-fighting on ground/walls — ✅ FIXED (session 14, CMB polygon offset)
OoT3D scene decals (sand/symbol detail coplanar with the base ground/wall) flickered when
the camera moved: our direct-GL path ignored the CMB material's **polygon offset** (the depth
bias OoT3D uses to pull decals toward the camera so they win the depth test cleanly). Fix:
parse `isPolygonOffsetEnabled` (material +0x05) + signed `polygonOffsetUnit` (+0x07), confirmed
against noclip `readMatsChunk`; `polygon_offset = unit/0xFFFE`. Plumbed through `SoH3DGlGroup`;
the GL fragment shader applies `gl_FragDepth = gl_FragCoord.z + uDepthOffset` (standard-Z
equivalent of noclip's reverse-Z depth offset — negative unit pulls the decal forward; 0 for
normal materials). Verified in Gerudo's Fortress (decal flicker gone on camera motion). Ruled
out the terrain warp as the cause first (warp on/off ground crops were identical).

### ⚠️ TERRAIN WARP breaks in Kokiri — thorough tooling investigation (session 14)
Tool: `tools/soh3d_warp_audit.py` (per-scene BENEFIT vs HARM from a REPL `floorgrid` CSV +
the OoT3D room mesh). Kokiri (spot04_0) vs Kakariko (spot01_0, warp known-good):
- **Sparse data → 90% hole-filled.** Kokiri: only 21% of the grid has an N64 floor, 202 cells
  have BOTH floors → 6479/7200 grid cells are BFS hole-filled (vs Kakariko 983 valid). The
  correction field is mostly guessed, so it's unreliable across most of the scene.
- **Under-correction (the "Link floats above ground / fence in the air" report).** At the fence
  spot (-892,715): raw OoT3D ground=120, N64=160 (needs +40), but the warp applied only +27.8
  (→147.8) — 100u grid + bilinear + hole-fill dilutes the correction, so ground lands ~12u low
  and Link (on N64=160) floats above it. This is "the LACK of [enough] warp," not over-warp.
- **Reject leaves big divergences floating.** Cells where OoT3D ground is >120u above N64 (the
  z≈1535 region, +130…+204) are REJECTED as "structure" by `kWarpReject=120` and never lowered
  → that ground + fences on it float at OoT3D height while Link walks at N64 height below.
- **Over-displacement (the real warp HARM).** 361 elevated (structure) verts displaced >50u
  (vs Kakariko 25): a tree/post on a hole-filled cell inherits a neighbor's D (up to ±120) and
  the whole column lifts/sinks off the real ground → floating posts/trunks.
- **Why Kakariko works, Kokiri doesn't:** Kakariko has dense valid coverage + a large genuine
  sink, so benefit dominates (elevated-vert p90 disp 4u, 25 floats). Kokiri is sparse + mixes
  small ground divergences, big genuine divergences, and many structures → the crude
  reject/smooth/hole-fill misbehaves (elevated p90 37u, 361 floats).
- **✅ RESOLVED (session 14) — INVERTED the approach: offset actors, don't warp the mesh.**
  The render-mesh warp (any variant, incl. the height-blend that was tried first) has a
  fundamental limit: it can only apply a SMOOTH per-XZ height field, but N64 collision has
  SHARP steps (ledges, fenced platforms) that OoT3D ground doesn't mirror — so near every step
  it smears the correction into adjacent already-correct ground and floats fences/posts.
  Proven at the Kokiri fence (-867,737): raw OoT3D ground=120 ALREADY = N64=120, yet the warp
  lifted it to 146 (+26 smeared from a 40u N64 ledge 30u away). The height-blend can't fix this
  (it's the GROUND being lifted) and worse, stretches attached fences (a flawed offline metric
  rewarded structures NOT moving, but a fence on lowered ground SHOULD move down with it).
  - **Fix (user-chosen):** leave the OoT3D render mesh UNTOUCHED (pixel-faithful) and instead
    offset each actor's RENDER Y by `OoT3D_ground - N64_ground` (= -D) so it stands on the
    visible ground; physics stays N64. `computeRoomGroundDelta` now just computes+caches the D
    grid (no mesh edit); `SoH3D_RoomGroundDeltaAt` samples it; `SoH3D_ActorRenderYOffset`
    returns -D for an actor; Actor_Draw bumps `world.pos.y` by it around the draw then restores.
  - **Verified in-engine (user, Kokiri):** "very good" — fences/ground no longer float; the
    render is faithful and Link/actors sit on the visible ground. No smearing (no smooth field
    applied to the mesh at all).
  - **Remaining floating things** are NOT terrain — they're incorrect actor/model replacements
    (auto-scale/position), a separate issue.
  - Tools kept for reference: `soh3d_warp_audit.py`, `soh3d_warp_proto.py` (proved the warp's
    limits + that no smooth-warp variant wins).

### ⚠️ KNOWN RESIDUAL — terrain warp under-corrects at sloped wall-edge cells (Link sinks)
At Gerudo's Fortress (-560,-2812) the N64 collision floor is y=204.6 but the warped OoT3D
render ground is y=221.6 (+17u), so Link's lower body is buried in the render terrain. This is
the terrain-warp residual class already noted (steep / structure-edge cells: the 100u grid +
bilinear smoothing + topmost-floor pick under-corrects near walls). NOT caused by the decal
fix (Link is on the N64 draw path). Proper fix = finer warp grid / better N64↔render floor
correspondence near structures. Measure with REPL `floorat` vs `meshfloor`.

### "Replace more objects" — pattern + remaining candidates
To add an object: find its `/actor/*.zar` in the romfs, pick the main `.cmb` (avoid `hahen`
debris), add a `kModels[]` row (zarPath, worldScale, cmbName) + an `sModelTable` row mapping
the `ACTOR_*` id to that `glModelId`, then calibrate worldScale live via REPL `scale <name>`.
Still N64 (candidates seen in romfs): gossip stone is on the older `gs` mapping; **sign
(zelda_kanban) needs multi-CMB assembly** (the sign is split into bottom/center/top + slice
pieces — the loader currently draws one CMB, so a sign needs combining several); bombable
rock (zelda_bombiwa), torch (zelda_torch2), treasure chest (zelda_box, animated lid).

## SOH3D_AUTO — programmatic actor replacement + auto-scale (session 13, 2026-06-16)
Stop hand-listing actors: any actor whose loaded object has a matching OoT3D ZAR is
replaced automatically, at a MEASURED scale. Two halves + the framework:
- **object id -> ZAR** (`tools/gen_object_zars.py` -> `soh3d_object_zars.inc`): an actor's
  object dependency id (`play->objectCtx.status[actor->objBankIndex].id`) indexes a
  generated positional table to `/actor/zelda_<name>.zar` (289/402 object ids map 1:1).
  Paths only — safe to commit.
- **AUTO-SCALE by measuring the N64 actor (height, NOT diagonal).** No universal scale
  exists (OoT3D models are authored at per-object-inconsistent scales). First time an
  auto-eligible actor is seen, its N64 draw is bracketed by the `OTR_G_SOH3D_MEASURE`
  opcode (gbi 0x4a); the Fast3D interpreter accumulates the actor's drawn **world-space
  height** (projects each vertex's eye-space pos onto the world-up axis = top-modelview ·
  (0,1,0); rigid view => eye-space range == world height) and reports it via
  `SoH3D_MeasureResult`. Next frame: `worldScale = measured_N64_height / OoT3D_model_local_Yextent`,
  cached per object id, drawn via the GL path. **Height — not bbox diagonal:** diagonal
  gave a consistent ~+22% bias (pot 0.147 vs hand 0.12, crate 0.122 vs 0.10) because the
  manual scales were calibrated by matching visible HEIGHT and the OoT3D remodels have a
  different aspect ratio than the N64 models; height removes the bias.
- **Framework:** explicit `sModelTable` entries WIN (calibrated scale + anim resolvers);
  else SOH3D_AUTO fills in. Auto model ids allocated in a 3rd range (`kAutoModelBase=2000`,
  keyed by ZAR path) in soh3d_model.cpp; main CMB picked by "largest non-debris" (skip
  hahen/modelT/broke/...). Gate: env `SOH3D_AUTO` (0=off default, 1=fill non-table, 2=ALL/
  validation) + REPL `auto`/`autostate`. After-draw hook `SoH3D_AfterActorDraw` (z_actor.c)
  closes the measure bracket.
- **Skinned characters are SKIPPED on the auto path** (CMB bones>1 => articulated): with no
  animation they render in a frozen T-pose (user saw guards T-posing at AUTO=2). They fall
  back to N64. Animating them via the actor's live N64 SkelAnime pose on the OoT3D skeleton
  is a SEPARATE effort (user: "we tried this and it worked" — geldwoman's sModelTable
  N64-anim->CSAB resolver is the hand-built precedent). **TODO (separate task).**
- **Main-CMB pick (largest-non-debris) refined:** reject FLAT/degenerate CMBs (one bbox
  dim ~0 => billboard/sprite/decal) and pick the CMB with the MOST VERTICES among non-debris
  non-flat candidates. Fixed the Kakariko tree rendering as a white flat quad: wood02's
  `wd_model` is a flat [800,655,0] billboard the old "largest diagonal" pick chose; now it
  picks `tree05_model` (489-vert 3D tree). Verified in log (no crash, real mesh loaded).
- **Known auto limitations:** multi-CMB objects assemble only their main piece (sign
  `zelda_kanban` -> center only; no multi-part assembly); the auto path picks ONE tree
  variant (tree05) regardless of the actor's tree-type param; keep-object actors (cuttable
  grass `En_Kusa` lives in gameplay_keep) have no per-object ZAR so the object-id path can't
  map them — `sModelTable` (by actor id) still does. Never crashes: a bad/missing ZAR or
  empty/flat-only model falls back to N64.

## Animated characters: N64 anim -> OoT3D skeleton (session 14, ✅ POSE CORRECT for Gerudo)
Port animated characters by driving the OoT3D skeleton from the actor's LIVE N64 SkelAnime
joint pose (no per-actor CSAB mapping). `SoH3D_UpdateAnimN64` (soh3d_model.cpp) mirrors
csab.cpp::skinMatrices but driven by N64 joint rotations; wired for En_Ge1
(`SoH3D_Joints_EnGe1`). Bone correspondence VERIFIED exact for Gerudo (OoT3D bone i <- N64
jointTable[i+1]; same 15-limb rig). Gate: env `SOH3D_N64ANIM` / REPL `n64anim` (default OFF).

**✅ FIXED (session 14) — the rotation CONVENTION, derived QUANTITATIVELY (not by guessing).**
The contortion was a structural compose error: the old formula multiplied the N64 joint
rotation INTO the CMB rest rotation (`T·R_n64·R_rest·S`). But the N64 jointTable already
encodes each limb's FULL local orientation (the standing pose's big rotations — e.g. En_Ge1
limb1 = (-90,0,-90), matching OoT3D bone0's rest), so composing double-applies that
orientation. The correct rule is the SAME one csab.cpp uses: the animated rotation REPLACES
the bone's rest rotation. Fix: `L = T(rest)·Rz·Ry·Rx(n64)·S(rest)` (no rest-rot compose).
- **How it was derived (the quantitative gate the user asked for):** added a REPL `jointdump`
  cmd (dumps the live En_Ge1 jointTable) + `tools/soh3d_anim_derive.py`, which diffs the CSAB
  ge1_s_wait skin matrices against the N64-joint-driven ones across all 15 bones × 864
  candidate conventions (3 compose structures × 6 euler orders × axis perms × sign flips). The
  top 12 by Frobenius residual were ALL `struct=replace`; winner `replace, ZYX, identity,
  +++` beat every compose variant decisively. (Residuals nonzero because the N64 idle
  `gGerudoWhiteIdleAnim` is a 2-frame fidget stub — a slightly different pose than CSAB
  ge1_s_wait — but the STRUCTURE is unambiguous.)
- **Verified:** at Gerudo's Fortress the OoT3D Gerudo stands upright with natural proportions
  (arms at sides), matching the CSAB A/B (`scratch/screenshots/ge1_AB_n64.png` vs `_csab.png`).
- **✅ GENERALIZED (session 14) via a SkelAnime_Draw hook.** No more per-actor jointTable
  accessor: `SoH3D_TryDrawActor` defers an `n64anim`-flagged table actor (returns 0 so its own
  Draw runs); the `SoH3D_SkelAnimeDraw` hook at the top of `SkelAnime_DrawSkeletonOpa` /
  `SkelAnime_DrawSkeleton2` (used by ~139 animated actors) grabs the live `SkelAnime*`
  (jointTable + limbCount), retargets the OoT3D model and draws it, returning 1 to skip the N64
  limbs. Unhooked actors fall back to the N64 draw. Adding an animated actor = one table row
  with `n64anim=1` (verify the rig corresponds: bone i <- jointTable[i+1]). `SoH3D_EmitModelDraw`
  shares the world-transform+draw emit between the table and hook paths. Verified: En_Ge1
  renders upright via the generic hook, matching the CSAB A/B. Gated by SOH3D_N64ANIM.

### GL state leak (striped UI/skybox/magic-bar corruption) — ✅ FIXED (session 14, own VAO)
The recurring non-deterministic 2D corruption was our `SoH3D_GL_Draw` mutating **Fast3D's
VAO**: gfx_opengl creates one VAO at init (`mOpenglVao`) and assumes its attrib config
persists; we ran with it bound, changed its attrib state, then hand-restored 6 attribs field
by field — any miss corrupted Fast3D's persistent vertex state, so later draws (incl. the
skybox/2D UI drawn before our scene draw → next frame) fetched garbage → stripes. Fix: give
our draw its **own VAO** (`g_vao`), bind it for the draw, restore the previous VAO binding
after; deletes the fragile per-attrib save/restore. (The old comment "interpreter renders on
the DEFAULT VAO" was wrong.) Structural fix — the leak mechanism is eliminated, not patched.

## ⭐ ARCHITECTURE PIVOT (2026-06-15, session 7) — read this FIRST
The "convert CMB → N64 F3DEX2 dlist (cmb_to_c.py) → bake C arrays into soh.elf →
draw via libultraship's Fast3D interpreter" approach is being **REPLACED**. User
directive: *"Mod SoH so it reads 3DS textures and models directly and can replace
N64 models with them, no LUS, no elf."* Decisions (locked via AskUserQuestion):
- **Render path:** a NEW **direct-OpenGL renderer inside SoH** that does NOT go
  through the Fast3D interpreter/dlist path. Own vertex+texture upload, own shader,
  matrices hooked to the game camera, drawing into the game framebuffer with depth
  test so 3DS models occlude correctly against the N64 scene.
- **Assets:** a runtime **C++ parser for raw .cmb/.zar/decrypted-.3ds** (port of the
  Python tools/ parsers), reading 3DS files directly at load time. NO pre-converted
  C arrays compiled into the binary.
- **Replacement:** at the actor-divert point (sModelTable), draw the runtime-loaded
  3DS model via the new GL path instead of the N64 actor.

**Why the pivot (root cause that triggered it):** baking models as `static const`
C arrays makes their texture pointers land at ~0x03xxxxxx in the **non-PIE** soh.elf
(`readelf` Type=EXEC). Those addresses are ≤ 0x0FFFFFFF, which collides with the N64
**segment-address range** — `gfx_set_timg_handler_rdp`'s `addr <= 0x0FFFFFFF` guard
rejects them as "unresolved N64 segment", so geldwoman's textures never upload
(in-game = flat tan skin; the dlist harness only worked because it mmap'd textures
high). Verified via TEXLOG instrumentation: all 6 geldwoman RGBA32 textures
"REJECTED by guard: addr=0x31e6b40…". Rather than fight the N64 segment scheme
(relocate-to-heap / PIE / resource packaging), use SoH as the PC engine it is:
load assets at runtime (heap = high addrs, no guard) and render them directly in GL.

**What was REVERTED this session:** the TEXLOG/texfix debug hacks in libultraship
(interpreter.cpp, gfx_sdl2.cpp) — that fork is back to its committed baseline ("no
LUS" edits). The cmb_to_c.py / generated `soh3d_*_model.{c,h}` path is now legacy
(kept for reference until the new path renders, then removed).

**Plan / phases** (see also `debug_journal/` if present):
1. **C++ asset loader** (port tools/ → `soh/src/soh3d/asset/`): `ctr_rom` (NCSD→NCCH
   →IVFC romfs), `zar`, `cmb` (skeleton, bind-pose matrices, SEPD/PRMS/PRM geometry
   assembly, smooth-vs-rigid skinning), `pica_texture` (ETC1/ETC1A4 + tiled formats).
   Verify byte/vertex-exact vs the Python tools (verify-quantitatively).
2. **GL renderer** (`soh/src/soh3d/soh3d_gl.{cpp,h}`): upload decoded textures + a
   VBO per model once; per draw, set MVP from the game camera + actor matrix, render
   into the game FBO with depth. Reuse libultraship's GL *context/FBO* (unavoidable)
   but NOT its Fast3D interpreter.
3. **Divert wiring:** SoH3D_TryDrawActor → enqueue (model, matrix); flush via the new
   renderer. Orientation/scale tuned live over the existing REPL.
4. Then: animation (bone matrices), more characters, **world/scene geometry**.
   World geometry is the SAME pipeline — OoT3D scenes/rooms are CMB models inside
   ZAR archives (e.g. the scene `*_info.zar` / room CMBs). The loader (CtrRom→Zar→
   Cmb) and the GL renderer are kept GENERAL (not character-specific): a scene model
   is just a static, skeleton-less CMB placed at world origin. Design must not bake
   in character-only assumptions — this is an explicit goal, not an afterthought.

ROM path: read the decrypted .3ds at runtime from **env `SOH3D_3DS_ROM`** (see
`soh3d-rom-paths` memory; NEVER commit the absolute path).

### Phase 1 DONE (session 7) — runtime C++ asset loader, VERIFIED
`soh/src/soh3d/asset/{ctr_rom,zar,cmb,pica_texture}.{h,cpp}` (pure C++, no SoH/LUS
deps) load a model straight from the .3ds. Verified byte-identical to the Python
tools on zelda_ge1→geldwoman: bones=15/meshes=6/materials=6/textures=6, 1086 tris,
exact bbox, all 6 texture-decode FNV checksums match (Python decode was oracle-exact
vs Azahar). Standalone verifier: `tools/build_asset_test.sh` →
`scratch/bin/asset_test [/actor/<x>.zar]`. Committed+pushed (Shipwright fork develop
993b7ec9e; parent main 3a6715a). `Cmb::buildDrawGroups()` returns per-material
interleaved {pos,nrm,uv} triangle lists (bind pose); `CmbMaterial`/`CmbTexture`
carry wrap/alpha/tex metadata; `PicaDecode()` → RGBA8.

### Phase 2 NEXT — direct-GL renderer (design notes, START HERE)
**Execution model (researched):** SoH renders RETAINED-mode but SYNCHRONOUS on the
MAIN thread — `graph.c` → `Graph_ProcessGfxCommands` (OTRGlobals.cpp:1800) →
`RunCommands` → `Fast3dWindow::DrawAndRunGraphicsCommands` → `Interpreter::Run()`.
GL is current only DURING `Run()`, not during `Actor_Draw` (which only RECORDS the
POLY_OPA dlist). So we CANNOT `glDraw*` in the divert; we must inject at dlist-EXEC
time. Note: the dlist is Run() once PER mtx_replacement (frame interpolation), so any
hook fires multiple times/frame — fine (idempotent redraw), but don't accumulate.

**Clean injection point:** embed a CUSTOM GBI opcode in the POLY_OPA dlist at the
divert (carrying a model handle); register a handler in the interpreter's opcode
dispatch table (interpreter.cpp ~4560, same mechanism as `G_REGBLENDEDTEX` 0x3f).
At execution the handler runs on the main thread with GL current and the interpreter's
current modelview/projection available → do `glUseProgram`+VBO+texture draw into the
bound game FBO with depth test, save/restore GL state so Fast3D isn't corrupted.
⚠️ This is a SMALL libultraship hook (a generic "call native draw" opcode) — tension
with "no LUS". Decide: is "no LUS" = don't route our MODELS through the N64
Fast3D/texture path (satisfied — we draw raw GL), or literally zero libultraship
edits (then need an existing hook / a different injection)? **Pending user call.**

### Phase 2 DONE (session 7) — direct-GL renderer, VERIFIED correct
The Gerudo renders fully + correctly textured through the new path, loaded from the
.3ds at runtime. Committed+pushed: libultraship fork soh3d 7b3b6c9d; Shipwright fork
develop 31563fa26. Key files: `libultraship/src/fast/soh3d_gl.cpp` (renderer),
`OTR_G_SOH3D_DRAW` opcode (gbi.h/lus_gbi.h/interpreter.cpp), `soh/src/soh3d/
soh3d_model.cpp` (bridge), `soh3d.c` SoH3D_DrawModelGL. Bugs fixed via the harness:
(1) crash — the interpreter assumed mOpenglVbo stays bound; now LoadShader+DrawTriangles
bind it explicitly. (2) textures wrong — UVs need V-flip (PICA top-origin vs GL
bottom-origin). Harness: `soh3d_dlist_harness --soh3d [--rotx 180] [--zar <p>]` →
EGL render of a .3ds model in ~1s (needs SOH3D_3DS_ROM + SOH3D_O2R). Clean render:
`scratch/render/soh3d_gl_clean.png`. NOTE: rotx 180 makes it upright in the HARNESS;
the in-game orientation (SoH3D_DrawModelGL uses live gSoH3dRotX/Y/Z) still needs an
in-game check (harness ≠ in-game orientation proxy — see earlier).

### Phase 4 DONE (session 8) — CSAB skeletal animation, VERIFIED through GL
The Gerudo's idle "wait" animation renders fully textured + correctly skinned
through the direct-GL path. **`scratch/render/gerudo_idle_f0.png`** = the iconic
arms-crossed Gerudo idle; `gerudo_idle_f11.png` = the idle sway (torso lean). End
to end: CSAB parse → per-bone animated TRS → world matrices → skinMatrix =
animWorld·bindInverse → per-vertex weighted blend → GL.

**Files.** Python oracle: `tools/csab.py` (parser + sampling + `skinned_triangles`),
`tools/csab_render.py` (software rasterizer for quick pose checks),
`tools/csab_xcheck.py` (element-wise C++↔Python diff). C++: `soh/src/soh3d/asset/
csab.{h,cpp}` (mirror of csab.py), `asset/mat4.h` (shared 4x4 helpers, extracted
from cmb.cpp + general inverse), `asset/cmb` gained `buildDrawGroupsSkinned(skinMats)`
+ `boneMatrices()` (the old `buildDrawGroups()` is the identity case = bind pose,
unchanged). Wiring: `soh3d_model.cpp` provider + `dlist_harness` honor env
`SOH3D_ANIM=<csab base>` `SOH3D_FRAME=<float>`.

**Key design (the property that makes it safe):** the animated bone world matrix uses
the SAME T·Rz·Ry·Rx·S convention as the CMB bind-pose `computeBoneMatrices`, so with
no anim (rest TRS) animWorld == bindWorld and skinMatrix = animWorld·bindInverse = I —
the bind-pose render is byte-unchanged. Rigid (bone_dim==1) and smooth (bone_dim>1)
meshes are UNIFIED: every vertex is first taken to MODEL space exactly as the bind-pose
path does (rigid: ·bindWorld; smooth: raw), then skinned by the weighted blend of its
bones' skinMatrix. Rigid = single bound bone weight 1.

**Verified (verify-quantitatively):** Python — rest-pose skin matrices = I (3.9e-13);
keyframe-exactness = 0 (sampling at a keyframe returns its value → track parse + hermite
correct); loop continuity pose(0)==pose(duration) = 0 (duration + REPEAT wrap correct);
csab=None skinned == bind pose. C++↔Python element-wise (`csab_xcheck.py`): ge1_s_wait
frames 0/11/21 + ge1_matsu (linear) max|Δpos|~1e-3 (float32 vs float64 on coords ≤6500),
max|Δnrm|~3e-7. Commits: Shipwright fork develop 25a03176b; libultraship fork soh3d
835f6a1e; parent main e871b7f.

**Format (Ocarina subversion 3), confirmed on ge1_s_wait:** header `csab`@0,
subver=3@8, anod-base=0x18 (@0x14), duration-1@0x28, anodCount@0x30, boneCount@0x34,
then int16 boneToAnimTable[boneCount], align(4), u32 anod-offset table (rel to 0x18).
Each `anod`: boneIndex u16@4, isRotInt16 u16@6, nine u16 track offsets@8 (tX tY tZ rX
rY rZ sX sY sZ, rel to anod start). Track: type u32@0 (0 const/1 linear/2 hermite),
nKf@4, tStart@8, tEnd@0xC; linear kf = (u32 time, f32 val) stride 8; hermite kf = (u32
time, f32 val, f32 tIn, f32 tOut) stride 0x10. Rotations are radians.

### Phase 4 LIVE in-game (session 8) — GPU skinning + live playback, VERIFIED in-game
The Gerudo (En_Ge1) plays her CSAB idle LIVE in-game, fully textured + upright —
`scratch/render/ingame_anim_f0.png` (arms-crossed idle, from behind: elbows out + the
green Gerudo-belt emblem) vs `ingame_anim_f11.png` (idle sway: torso lean/head tilt).
**This closes the old "in-game Gerudo is UPSIDE-DOWN + UNTEXTURED" bug** (that was the
legacy N64-dlist path; the direct-GL path textures correctly, and upright in-game
matches harness `--rotx 180` per the documented harness-readback flip).

**GPU skinning (the chosen design).** `SoH3DGlVtx`/`CmbVertex` gained `boneIds[4]`+
`weights[4]`; `buildDrawGroups` uploads MODEL-space (bind-pose) verts + bindings ONCE.
The GL vertex shader blends `pos = Σ weight_i · uBones[boneId_i] · pos` — `uBones`
defaults to identity (bind pose, so un-animated models are unchanged), uploaded with
`transpose=GL_TRUE` (row-major M·v → GLSL column-major m·v). `SoH3D_GL_SetBones
(modelId, mats16, n)` stores per-model matrices (≤`SOH3D_GL_MAX_BONES`=32); attribs 3/4
save/restored so Fast3D state isn't corrupted. Verified in the harness pixel-equal to
the CPU oracle (bind + ge1_s_wait f0/f11 ≤2 px, `scratch/render/gpu_*.png`).

**Live driver.** `soh3d_model.cpp` keeps the Zar+Cmb resident + caches parsed CSABs;
`SoH3D_UpdateAnim(modelId, animName, frame)` recomputes skin matrices per call.
`soh3d.c` `SoH3D_DrawModelGL` advances a free-running `gSoH3dAnimFrame` per Actor_Draw
and calls it before the draw opcode; `sModelTable` carries a per-entry CSAB name
(geldwoman → `ge1_s_wait`). REPL: `animrate` (0=pause) / `animframe` (scrub), in `state`.

**REMAINING (integration polish, NOT animation bugs):** (1) **placement** — En_Ge1
renders floating ABOVE Link (model origin not grounded at the actor world pos) and
**world-scale** needs calibration (0.011 too small; 0.02 framed the verification shot).
Fix the origin/ground offset + calibrate scale vs the N64 En_Ge1. (2) Hook the actor's
ACTUAL current animation (the game picks ge1_s_wait/matsu/hanasi by state) instead of
the fixed table anim. (3) Frame RATE: `gSoH3dAnimRate`=1/Actor_Draw is a guess; match
the OoT3D logic tick. (4) Generalise beyond one global anim frame if >1 GL character.

### Phase 4 polish DONE (session 9) — En_Ge1 grounded + live anim state, VERIFIED in-game
En_Ge1 now stands **grounded** on the floor playing her **real** animation, fully
textured + upright (`scratch/render/ge1_placed_anim_a.png` = mid-sway, `..._b.png` =
arms-crossed idle; A/B against the N64 En_Ge1 in Gerudo Fortress). **Closes ALL of
REMAINING (1)(2)(3)(4).**

**(4) Multi-GL-char generalisation.** The free-running playhead is now PER GL MODEL
(`gSoH3dGlAnim[glModelId]` = {frame, lastCsab}, cap `SOH3D_GL_MODEL_MAX`=16) instead of
one global `gSoH3dAnimFrame`, so distinct GL characters animate on independent playheads
(`gSoH3dAnimRate` stays the shared speed knob; the global frame remains the scrub-mode
playhead for REPL `animframe`/`animrate`). Still per-MODEL not per-instance: two En_Ge1
instances share one pose (skin matrices upload per modelId) — independent per-instance
poses would need per-actor bone buffers, deferred. Regression-verified: lone geldwoman
still grounds + idles (figure-band motion ~2-3k px/0.5s, bg settled).

### Phase 5 DONE — world/scene geometry (session 10) ⭐
**OoT3D scene ROOM geometry now renders in-game through the direct-GL path**, world-space
aligned with the N64 scene and depth-correct. Verified A/B in TWO scenes (general, not
one-off): Gerudo's Fortress (`spot12_0`, entrance 297) and Gerudo Valley (`spot09_0`,
entrance 279). Quantitative: scene-1 wall-top silhouette median |Δrow|=0 vs the N64 room
(95.7% central sky/geometry agreement); scene-2 the N64 Link actor anchors to the SAME
screen pixel (951,549 vs 964,550) in both renders → identical camera, room grounded.

**What was built (all committed):**
- `tools/zsi.py` (oracle) + `Shipwright/soh/src/soh3d/asset/zsi.{h,cpp}` (C++): parse the
  ZSI, walk the command list, require a 0x0A Mesh command, extract the single embedded
  room CMB by its `cmb ` magic. Byte/vertex-EXACT C++↔Python (verified across rooms +
  scenes via `tools/soh3d_zsi_test.cpp`, wired into `build_asset_test.sh`).
- **KEY FINDING (don't re-derive):** every one of the game's 390 room files holds EXACTLY
  ONE embedded CMB — OoT3D rooms are a single multi-material CMB; the opaque/transparent
  split N64 puts in separate mesh entries lives in the CMB's per-material alpha flags
  (renderer already honours it). The ZSI mesh-header→entries→opaque/transparent pointer
  chain (noclip zsi.ts) does NOT resolve at plain file offsets on the USA decrypted ROM
  (the data-section addresses carry a base/segment not pinned down), and is unnecessary:
  locating the lone `cmb ` blob (anchored to a 0x0A command, NOT hardcoded — gerudoway is
  at 96, spot00 at 996) is robust. See `tools/zsi.py` docstring for the full rationale.
- `tools/gen_scene_names.py` → committed `soh3d_scene_names.inc`: SoH sceneNum (== SceneID
  enum == scene_table.h row) → OoT3D scene folder name. 101/110 mapped (case-insensitive
  match + overrides for renamed dungeons/bosses/houses/shops; 9 NULL = test/beta → N64).
  Names only, no ROM assets.
- `soh3d_model.cpp`: scene-room models in a separate id range (`kSceneModelBase=1000`),
  allocated on demand by ZSI path (`SoH3D_RoomModelId`), loaded via ZSI→CMB→`buildFromCmb`
  (shared with the actor path; no skeleton/anim). 
- `soh3d.c`: `SoH3D_TryDrawRoom` (gate by SOH3D → scene name → `room->num` → model id →
  `SoH3D_DrawRoomGL`). Room draws at the WORLD ORIGIN with an **identity model matrix**
  (scene CMB verts are already world-space) + scene tint; MP = identity·view·proj = the
  game camera, so no actor-translate. REPL knobs `scenescale`/`sceneoff` (defaults 1.0 / 0
  — N64 unit scale & origin match directly, confirmed). Hook: `z_room.c` `Room_Draw`
  diverts on the opaque pass (`flags&1`; Room_Draw is called once/room with flags=3) and
  skips the N64 mesh on a hit. Multi-room scenes handled because we hook INSIDE Room_Draw
  (engine calls it per active room with the right `room->num`).

**Polish left for later (not blockers):** the OoT3D room is brighter than the N64 room —
we apply a flat scene tint, not the N64 per-vertex lighting; lighting parity is a
follow-up. Detail/LOD differs (OoT3D is higher-poly + higher-res textures, by design).

### BUG FIXED — OoT3D content not widescreen-corrected (session 11, 2026-06-16) ⭐
**Symptom (user):** "some props move differently via the camera, visible in the initial
Kakariko camera sway" — the OoT3D scene and the N64 actors (trees/doors/Link) sheared
horizontally relative to each other as the camera panned, growing off-center.

**Root cause (quantitatively confirmed, NOT eyeballed):** Fast3D applies a per-vertex
widescreen correction to EVERY N64 vertex — `x = AdjXForAspectRatio(x)` in
`interpreter.cpp` `gfx_sp_vertex`, where `AdjXForAspectRatio(x) = x * (4/3)/(w/h)` for the
resizable game FB (≈0.699 on a 1920×1006 window), squeezing the 4:3-authored clip-X onto
the wider screen. The direct-GL path (`SoH3D_GL_Draw`) uploaded the raw `MP_matrix` with
NO such scale, so the OoT3D scene + diverted props rendered at the un-squeezed 4:3 X while
N64 actors were squeezed. Near screen center (clip x≈0) negligible; off-center the two
coordinate frames diverge linearly with the pan → "moves differently." NOT an
origin/scale mismatch — the spot01 room CMB bbox `x[-6479,3412] z[-9614,2376]` contains
Link's world pos, so the scene IS world-aligned (matches Phase 5).

**Fix:** mirror Fast3D exactly. `gfx_soh3d_draw_handler_custom` passes
`gfx->AdjXForAspectRatio(1.0f)` (the factor, or 1.0 for fixed-aspect FBs — so the headless
harness, which renders to a fixed-size FB, is unaffected) to `SoH3D_GL_Draw`, which scales
the clip-X output column of MP (row-major indices 0,4,8,12) by it before upload. Files:
`libultraship/.../soh3d_gl.{h,cpp}`, `interpreter.cpp`.

**Verification (geometry-independent):** same camera, OoT3D scene before vs after the fix;
Link (N64, unaffected by the fix) is the fixed fiducial. Best-fit horizontal scale that
maps before→after about screen center = **0.695**, predicted `(4/3)/(1920/1006)=0.699`
(Δ0.004, within search step); SAD 10.5→7.7. The applied factor matches Fast3D's exactly.

**TOOLING added (the diagnostic that found/measured this):** REPL camera control for
DETERMINISTIC sweeps, in `soh3d.c` (`cam`/`camorbit`/`camfreeze`) — freeze the world and
orbit the camera about a fixed look point. A pure-rotation orbit is the textbook way to
expose a transform mismatch between two render paths that "share" the camera (the OoT3D GL
draw vs N64 Fast3D both read `mRsp->MP_matrix`, so under orbit they can only drift if their
effective transforms differ — which the missing aspect scale made true). Override is
re-applied every frame in `SoH3D_ReplPoll` (runs post-`Play_Update`, pre-`Play_Draw`) by
writing `play->view.eye/lookAt/up`. `soh3d_zsi_test.cpp` also gained bone-binding stats
(boneId range / weight-sum) for the GPU skinning shader's `uBones[32]` bound.

### Phase 5 (original research — kept for reference)
The plan-item-4 goal. **The whole asset+GL pipeline is reusable for scenes — a room is
a static, skeleton-less CMB.** Validated this session against `gerudoway` (Gerudo's
Fortress, the entrance-297 scene):

**Where scene geometry lives (don't re-derive):**
- `/scene/<name>.zar` holds only per-room `.cmab` (material/UV ANIMATION, tiny 128–544 b)
  + `.ctxb` (the scene NAME-plate textures, per language) — NOT the room mesh.
- Room geometry is an **embedded CMB inside `/scene/<name>_<R>_info.zsi`** (the per-room
  "Zelda Scene Info" file; `<name>_info.zsi` with no number = the SCENE header: room list,
  actor/spawn lists, collision, lighting — N64 scene-header analogue). `.zsi` header magic
  = `ZSI\x01` then an 8-byte name ("ShUnqueen"); a `cmb ` magic follows (offset 96 in the
  gerudoway rooms, but PARSE the ZSI mesh-header command to locate it — do NOT hardcode 96).
- The embedded CMB parses with the EXISTING `Cmb` loader UNCHANGED: gerudoway ROOM0 =
  2334 tris / 7002 verts, has materials + textures, `bone_count` absent (static mesh).
- **Scene coords are already WORLD-space**: ROOM0 bbox x[-1570,1630] y[-160,800]
  z[-3828,-2984] (extent 3200×960×844). So a scene draws at the **world origin with an
  IDENTITY model matrix** (just camera view·proj), NOT translated to an actor pos like
  characters. Likely also at the N64 world scale directly (verify the unit match in-game).

**Impl sketch (next session):** (1) C++ ZSI parser in `soh/src/soh3d/asset/` (port from
noclip OoT3D `zsi.ts`): read the ZSI command list, find the mesh header → embedded CMB
slice; expose room CMB(s). Verify byte/vertex-exact vs a Python `tools/zsi.py` oracle
(write that too) per verify-quantitatively. (2) A scene-render entry on the GL path:
load the scene's room CMBs via `buildDrawGroups` (no skinning — bind pose / identity),
draw each at world origin with the game camera matrices + depth, replacing/over the N64
room. Hook at scene/room load (z_scene / room draw), gated by `SOH3D`. (3) Verify
in-game: SOH3D=1 OoT3D room vs SOH3D=0 N64 room in the SAME scene (entrance 297), aligned
+ depth-correct. Keep it GENERAL (no character assumptions) — explicit roadmap goal.
**Gotcha to expect:** multiple rooms per scene (gerudoway has 6: ROOM0–5); the active
room set is driven by the scene/room-load logic — mirror that, don't draw all rooms always.

**(1) Placement.** `SoH3D_DrawModelGL` gained a per-model `groundOffset` (MODEL units,
applied innermost = pre-scale, so it scales WITH worldScale and re-tuning scale never
desyncs grounding). `SOH3D_GELDWOMAN_GROUND_OFFSET=-1000` drops her soles onto the
actor's shadow — CALIBRATED live via new REPL `yoff geldwoman <f>` (-600 floats, -1000
grounds, -1400 sinks to ankles). **Scale 0.011 is CORRECT, not too small:** quantitative
A/B in the same shot — OoT3D figure 186 px tall (head y399→foot y585) vs N64 En_Ge1
187 px (head→shadow). The earlier "0.011 too small" was a misread of an occluded shot.
REPL `spawn` now offsets front-right (~55u) so the figure clears Link for inspection.

**(2)+(3) Live anim selection + rate.** `sModelTable` gained an `SoH3D_AnimResolver`
fn-ptr; `SoH3D_ResolveAnim_EnGe1` reads the actor's live N64 anim (`EnGe1.animation`,
an OTR-path string in SoH → identify by `strcmp`) and maps it to the CSAB: Idle
(`gGerudoWhiteIdleAnim`)→`ge1_s_wait`, Clap/open-gate→`ge1_mon_akeru`, Dismissive/
post-talk→`ge1_hanasi` (mapping by use site in z_en_ge1.c; `ge1_matsu` unused by this
actor). The resolver picks WHICH CSAB; the CSAB then **free-runs** at its own authored
rate (`gSoH3dAnimRate`=1/Actor_Draw), restarting on anim change. **Key finding (don't
re-derive):** phase-LOCKING the CSAB to the N64 `SkelAnime.curFrame` does NOT work — the
N64 idle `gGerudoWhiteIdleAnim` is a 2-frame stub (`animLength=2`, `curFrame` stays 0);
its visible life is *procedural limb fidget*, not keyframes. So there is no frame motion
to sync to, and the OoT3D CSAB's own 22-frame idle is the faithful motion source. Live
verify (background drift-free): figure-band changed px 388→707→1570→1570→1465 across
0.5 s frames, bg patch ~0 → continuous idle sway. REPL: `animlive <0|1>` (1=resolver,
0=scrub w/ `animframe`), `animdbg <0|1>` (log resolved csab/curFrame each ~20 draws).

### Phase 4 (original scoping) — animation (CSAB skeletal anim):
zelda_ge1.zar contains CSAB anims: `ge1_s_wait` (idle), `ge1_matsu`, `ge1_hanasi`
(talk), + `geldwoman_eye.cmab` (eye texture anim). CSAB header (ge1_s_wait): magic
'csab', version=3 @0x08, ~frame/duration field near 0x14, bone count 15 @0x30 (matches
skeleton), then a per-bone index table, then `anod` chunks each with translation/
rotation/scale keyframe tracks. **Plan:**
1. Port a CSAB parser (Python first in tools/csab.py, verify, then C++ asset/csab) —
   use the noclip OcarinaOfTime3D/csab.ts as the format reference (cmb.py/pica were
   ports of noclip; do the same — don't reverse-engineer keyframe encoding blind).
2. Per frame: anim → each bone's local TRS → world matrices; skin = for smooth meshes
   blend by per-vertex boneIndices+boneWeights using animBoneWorld × bindInverse
   (identity at bind pose, so bind-pose render still matches). Need to ALSO read the
   boneIndices/boneWeights attrs (currently buildDrawGroups ignores them for smooth)
   and the per-bone bind-inverse matrices.
3. Skinning location: GPU (pass bone-matrix array uniform + per-vertex idx/weights in
   the VBO; shader does the blend) is cleanest; CPU (recompute verts/frame) is simpler
   to get correct first. Verify a deformed frame in the harness (--anim ge1_s_wait
   --frame N) before in-game.

**Renderer pieces to build (`soh/src/soh3d/soh3d_gl.{cpp,h}`):**
- One-time per model: upload each draw group's verts to a VBO; decode+upload each
  texture (PicaDecode→glTexImage2D, GL_RGBA8); cache by model id.
- Shader: textured + a flat tint uniform (reuse the scene-tint idea) + alpha test.
- Per draw: MVP = game proj * game view * actor(Translate*RotateY*Scale) — get the
  matrices from the interpreter at hook time (or recompute from play->view).
- Divert: SoH3D_TryDrawActor records (modelId, matrix); the hook draws it.
Orientation/scale tuned live via the existing REPL (rotx/roty/rotz still wired).

## Layout
- `tools/` — 3DS asset toolchain (Python, dependency-free for extraction).
- `scripts/` — dependency install scripts (Fedora; run with sudo yourself).
- `Shipwright/` — SoH fork (own git, gitignored from parent).
- `Azahar/` — 3DS emulator fork, used as oracle (own git, gitignored from parent).
- `3ds/` — boot9/boot11 (not needed for extraction; for emulator). gitignored.
- `scratch/` — extraction output, logs, screenshots. gitignored.

## Done
- ROMs verified: N64 OoT USA v1.0 (`$SOH3D_N64_ROM`, supported hash),
  OoT3D USA decrypted NCSD (`$SOH3D_3DS_ROM`). Provide both via the gitignored `.env`.
- 3DS extraction pipeline, **verified on the pot** (`zelda_tsubo`):
  - `tools/ctr_romfs.py` NCSD→NCCH→RomFS (IVFC), no bootrom needed.
  - `tools/zar.py` ZAR archives.
  - `tools/cmb.py` CMB models: geometry/skeleton/material+texture refs. OBJ export.
  - `tools/pica_texture.py` PICA textures (ETC1 + 8x8 Morton-tiled formats).
  - pot → 130 verts / 160 tris, ETC1 + RGB565 textures decode correctly.
- **SoH builds & RUNS**: `soh.elf` (57 MB). Game archive `oot.o2r` (33 MB) +
  `soh.o2r` generated and staged in `build-cmake/soh/`. Boots headlessly to the
  title screen, **render captured** (scratch/screenshots/soh_vanilla.png).
- **Azahar builds & RUNS OoT3D**: `azahar` (49 MB). Boots the ROM (title id
  0004000000033500), inits DSP/save archive, emulates without crashing. Run with
  `XDG_CONFIG_HOME`/`XDG_DATA_HOME` pointed at `scratch/azahar_{cfg,data}` to keep
  it out of the user's real config. Logs to `<data>/azahar-emu/log/azahar_log.txt`.
- **Headless frame-dump tool** added to libultraship (branch `soh3d` in the
  submodule): `SOH_FRAMEDUMP=<path.ppm> SOH_FRAMEDUMP_FRAME=N ./soh.elf` under
  `xvfb-run`. Reusable for A/B vs the oracle. (exit 139 on teardown after dump is
  harmless — PPM is written before exit.)

### How to run SoH headless + capture
```
cd Shipwright/build-cmake/soh
SOH_FRAMEDUMP=/abs/out.ppm SOH_FRAMEDUMP_FRAME=500 \
  timeout 150 xvfb-run -a -s "-screen 0 1280x720x24" ./soh.elf
```

### MILESTONE — OoT3D pot renders in-game (2026-06-15)
First OoT3D asset rendered through SoH's modern renderer, in a live scene, with
its full-res 3DS texture at the correct world transform. **Approach A** (CMB →
F3DEX2 dlist + full-res RGBA32 texture), which turned out to be the right first
step — see the corrected TMEM finding below.

Pipeline:
- `tools/cmb_to_c.py <model.cmb> <out_base>` converts a CMB to a self-contained
  C source: a `Vtx[]`, the texture decoded to RGBA32 (`pica_texture.decode`),
  and an `Gfx[]` display list with **manual** texture-load commands carrying the
  *real* pixel width (so LUS uploads it at full res — NOT through 4 KB TMEM).
  Geometry is batched <=10 tris / gSPVertex load. Generated files contain
  ROM-derived assets → **gitignored, never committed**; regenerate from the ROM.
- `soh/src/soh3d/` — `soh3d.{c,h}` (env toggles) + the generated
  `soh3d_pot_model.{c,h}`. SoH globs `src/*.c` (GLOB_RECURSE) so new files are
  picked up after a `cmake build-cmake` reconfigure.
- Hook: `ObjTsubo_Draw` (z_obj_tsubo.c) calls `SoH3D_DrawModel(play, dl, actor,
  SOH3D_POT_WORLD_SCALE)` when `SOH3D=1`.

**Draw path — must build its own matrix (root cause).** Do NOT reuse
`Gfx_DrawDListOpa` for OoT3D models: it loads the actor's inherited matrix, which
carries the N64 0.01 actor scale (`Actor_Draw` does `Matrix_Scale(actor->scale)`
before the actor's Draw). With that inherited fixed-point matrix the OoT3D dlist
renders **nothing at all** (not just wrong-sized) — verified: the identical dlist
renders fine through a fresh `MTXMODE_NEW` matrix but is invisible through the
inherited one. `SoH3D_DrawModel` (soh3d.c) builds its own
translate+rotateY+scale matrix at the actor's world pos and emits the dlist, so
SoH3D owns the transform. Calibrated world scale: **0.12** (OoT3D pot ~162 model
units -> matches the N64 pot's on-screen height; tuned via the SOH3D_SPAWNPOT
spawn comparison in Deku Tree). Generated model is baked at --scale 1.0; the
world scale lives in `SOH3D_POT_WORLD_SCALE`.

Regenerate the pot model (from the extracted CMB):
```
python3 tools/cmb_to_c.py scratch/extract/tubo2_model.cmb \
  Shipwright/soh/src/soh3d/soh3d_pot_model
```

### Headless verification path (no input scripting)
Reaching an in-game scene headlessly is solved with an env-gated auto-warp that
reuses SoH's debug Select overlay + `Select_LoadGame`:
- `SOH3D_WARP=1` — boot straight into Select, then auto-warp. Default entrance
  Kakariko Village (`SOH3D_ENTRANCE=<decimal>` to override).
- `SOH3D=1` — enable OoT3D-model rendering (the `ObjTsubo_Draw` divert).
- `SOH3D_SPAWNPOT=1` — spawn one real Obj_Tsubo beside Link (the actual
  ObjTsubo_Draw path). A/B with SOH3D=0 (N64 pot) vs SOH3D=1 (OoT3D pot) in the
  same scene. params=0 needs the dungeon keep object, so use a dungeon
  (`SOH3D_ENTRANCE=0` = Deku Tree).
Verified renders: `scratch/screenshots/final_3ds.png` (OoT3D pot via the real
ObjTsubo_Draw path, Deku Tree, calibrated size) and `full_n64.png` vs
`full_s012.png` (N64 vs OoT3D height match). Code: graph.c (boot→Select),
z_select.c (auto-warp), z_play.c + soh3d.c (spawn helper).

### CORRECTION — there is NO 4 KB TMEM limit in the LUS modern path
PROGRESS/handoff previously said approach A hits "TMEM 4 KB: pot's 64x128 tex
won't fit". **Wrong.** `Interpreter::ImportTextureRgba32` reads the texture
straight from the source pointer at the tile's full dimensions; the 4 KB assert
in `GfxDpLoadBlock` is commented out, and the code explicitly supports
manually-built DLs that set the real pixel width. This is the same path SoH's HD
texture packs use. So approach A renders full-res textures fine — it is the
correct first milestone, not just a stepping stone. Approach B (native opcode)
is only needed later if per-vertex lighting/normals fidelity demands it.

### MILESTONE — multi-material model (Gossip Stone) renders in-game (2026-06-15, session 3)
Generalised the pipeline from the 1-mesh/1-material pot to genuine multi-mesh,
multi-material, multi-texture models, and proved it on the **OoT3D Gossip Stone**
(`zelda_gs.zar` -> `gossip_stone2_model.cmb`: 2 meshes, 2 materials, 2 distinct
fully-opaque 128x128 / 128x64 textures), hooked via `EnGs_Draw` + `SOH3D_SPAWNGS`.

Toolchain changes:
- `cmb.py` now parses **MATS** (per-material primary texture index + wrap modes +
  UV coordinator scale/translate + alpha test) and computes **bind-pose bone
  matrices**; `triangles()` applies each mesh's bound-bone world matrix. Multi-bone
  props (e.g. the treasure-chest lid, bone 2) are stored in bone-LOCAL space and
  render scrambled without this — verified on `tr_box` (lid then sits atop the base).
  The single-bone pot never exposed it (bone 0 = identity).
- `cmb_to_c.py` groups triangles by material and re-binds texture+combiner per
  material in one dlist (multiple RGBA32 texture arrays in the C file).

**Root cause of the "model renders solid black" bug (the real one).** Clearing the
`G_FOG` *geometry-mode* bit stops the RSP computing per-vertex fog, but the RDP
**blender** configured by the caller's SetupDL can still blend the framebuffer
toward the scene fog colour. In a foggy scene (Kakariko at night, fog ~ (0,0,30))
that painted the *entire* model the fog colour regardless of texture/combiner — a
solid black/blue silhouette. Proven by bisection: a solid-red PRIMITIVE combiner
*also* rendered black, so it was downstream of the combiner. Fix: the converter
emits an explicit `gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2)`.
The pot only escaped this because Deku Tree's fog setup didn't tint.

**Verified QUANTITATIVELY (do not eyeball — see `tools/compare_render.py` and the
[[verify-quantitatively]] memory).** Lower-frame pixels matching tex0's gray-green
palette: **0.2% before the fix (fog-black) -> 33.6% after**; the N64 Gossip Stone
scores 0.2% (its stone is gray-blue, so the green is unambiguously the OoT3D
texture). Calibrated world scale ~0.13 (`SOH3D_GS_WORLD_SCALE`).

**Dead ends / corrected notes from this session (don't re-walk):**
- The `lrs` 12-bit truncation in `gsDPLoadBlock` (>4096 texels truncate) is REAL but
  IRRELEVANT to our textures: wide vs non-wide `gsDPLoadBlock` produced a
  pixel-identical pot render, so SoH3D's static textures load fully via the
  resource/cache path regardless. Kept plain `gsDPLoadBlock` (proven primitive).
- The treasure chest (`tr_box`) is a POOR multi-material test: both its main textures
  are ~95-100% transparent decals composited by a PICA multi-texture fragment
  combiner — no single binding-0 texture is the visible surface, so the single-texture
  converter renders it wrong. PICA combiner emulation is future work. Pick models
  whose materials each use a distinct mostly-opaque texture (the Gossip Stone does).

### MILESTONE — flat scene-ambient tint (color fidelity) (2026-06-15, session 4)
Unlit OoT3D models rendered full-bright: they did NOT darken with the room and
sat ~2.5x too bright vs the N64 model in the same scene. Fixed without
reintroducing per-vertex lighting banding.

**How.** The unlit dlist now modulates the texture by the **PRIMITIVE** register
instead of vertex SHADE: `cmb_to_c.py` emits `G_CC_MODULATERGBA_PRIM` (= TEXEL0 *
PRIM) for both the opaque and alpha-test material paths (still `G_CC_MODULATERGBA`
under `--lit`). `SoH3D_DrawModel` (soh3d.c) computes ONE flat tint colour from the
LIVE interpolated scene lights — `play->envCtx.lightSettings`: `ambient + 0.5 *
(light1Color + light2Color)`, clamped — and emits `gDPSetPrimColor` before the
dlist. Reading it live means the model tracks time-of-day automatically; one prim
colour for the whole dlist means it's flat by construction (no banding). The dlist
deliberately emits no prim colour of its own so the caller's wins. Re-cal knobs:
`SOH3D_TINT_DIFF` (diffuse fraction, default 0.5), `SOH3D_TINT_MUL` (overall, 1.0).

**Verified QUANTITATIVELY** (`tools/compare_render.py model`, A/B same scene/spawn;
see [[verify-quantitatively]]). Model-region mean luminance:
- Gossip Stone / Kakariko: full-bright **150.5** -> tinted **62.7**; N64 **60.2**
  (tinted within ~4% of N64; full-bright was 2.5x too bright).
- Pot / Deku Tree (darker scene, single material): tinted **56.6**; N64 **65.4**
  (~13%) — confirms it generalises across scenes/objects, neither black nor
  full-bright.
The frac=0.5 / mul=1.0 DEFAULTS land within tolerance with no per-scene tuning, so
no magic constants are baked. Residual hue gap (OoT3D stone is gray-green, N64 is
gray-blue) is the genuine OoT3D texture palette, not a tint error.

### MILESTONE — generalised table-driven divert (2026-06-15, session 4)
The SoH3D divert was hand-coded into each actor's Draw (`ObjTsubo_Draw`,
`EnGs_Draw`) with an `if (SoH3D_Enabled()) { SoH3D_DrawModel(...); return; }`
block. Replaced with ONE central divert + a table, so adding an object is a
one-row change with no actor-source edits:
- `soh3d.c`: `sModelTable[]` maps `actorId -> { dlist, worldScale }`, and
  `SoH3D_TryDrawActor(play, actor)` looks the actor up; on a hit it draws the
  OoT3D model via `SoH3D_DrawModel` and returns 1.
- `z_actor.c` `Actor_Draw`: the single `actor->draw(actor, play)` call site (the
  chokepoint for EVERY actor) becomes
  `if (!SoH3D_TryDrawActor(play, actor)) actor->draw(actor, play);`.
- The per-actor edits in `z_obj_tsubo.c` / `z_en_gs.c` are reverted to stock.

`Actor_Draw` only runs once an actor has a non-NULL draw, so load/spawn timing
(e.g. the pot's VB_POT_SETUP_DRAW gate) is preserved. **Verified**: the Gossip
Stone rendered through the new central divert is **pixel-identical** (0 differing
px) to the pre-refactor per-actor render.

To register another object: extract+convert its CMB (see the pot/GS recipes),
add `{ ACTOR_<ID>, <model>_dl, <scale> }` to `sModelTable[]`, add the generated
`#include`/`extern` via `soh3d.h`. (GameInteractor `VB_*` hooks were considered
but the `Actor_Draw` chokepoint is simpler and id-driven — no per-actor hook
plumbing.)

### TOOLING — live REPL for a long-lived headless SoH instance (2026-06-15, session 4)
Replaced the env-flag -> rebuild -> 7-min headless render loop with an interactive
REPL so experiments cost seconds. Tooling-first (a hard rule — see memory):
- `tools/soh3d_render.sh` — headless launcher with GUARANTEED Xvfb/soh teardown via
  a trap (soh.elf exits 139 on teardown, which leaked Xvfb under plain xvfb-run —
  that was the "instances left behind" bug).
- `tools/soh3d_repl_launch.sh` — boots ONE long-lived instance with the REPL FIFO
  enabled (default warp Gerudo Valley 0x117, which loads OBJECT_KIBAKO2).
- `soh3d.c SoH3D_ReplPoll` (env SOH3D_REPL=<fifo>) — reads commands each frame:
  `mul/diff/tint` (live scene tint), `scale <name> <f>`, `spawn <name>`, `enable`,
  `dump <path>` (on-demand frame dump, no exit), `state`. Tint params + per-model
  world scales are now live globals; the model table carries a name per entry.
- libultraship `gfx_sdl2.cpp` — on-demand dump trigger (`gSoh3dDumpPending`/Path)
  so the running instance dumps any frame without exiting.
- `tools/soh3d_repl.py` — driver: `ready`, `cmd`, `shot [box]`, `zoom`, `region`,
  `isolate` (diff two shots of the same scene to isolate the one changed object),
  `probe`. Use these; never hand-run xvfb or inline measurement python again.

### RESOLVED (NOT A BUG) — 128x128 RGBA32 upload works fine in LUS (2026-06-15, session 5)
**The previous session's "128x128 RGBA32 textures don't upload" was a MISDIAGNOSIS**,
built on a misread in-game log. Debunked with a new data-driven SoH-side oracle (the
**dlist render harness**, below) that runs the crate's exact generated dlist+texture
through the REAL libultraship Fast3D interpreter with NO game boot, NO window, NO GPU:
- The harness drives `Interpreter::Run()` over `soh3d_kibako_model_dl` with a recording
  `GfxRenderingAPI` stub. Result: `gsDPLoadBlockWide(16383)` dispatches correctly
  (otrHandlers entry `RDP_G_LOADBLOCK_WIDE`=0x47 -> `gfx_load_block_wide_handler_rdp`),
  `GfxDpLoadBlock` computes the FULL `size=65536` (no 12-bit truncation — the wide
  opcode carries lrs in w1), and `ImportTextureRgba32` **uploads the full 128x128 with
  the correct wood texels** (`UploadTexture 128x128, first=213,196,94,255`). The whole
  RGBA32 upload path is correct end-to-end.
- Why the previous session was wrong: (a) it read a `siz=2`(16b)/`32768B`/`131,81,123`
  scene texture as "the crate's 65536B RGBA32 load" — it was an unrelated texture; the
  crate's load never appeared in the in-game log AT ALL. (b) The crate's load was absent
  because the spawned `Obj_Kibako2` actor was **frustum-culled / off-screen** (spawned
  120 units ahead of Link in Gerudo Valley), so `Actor_Draw -> SoH3D_TryDrawActor` never
  ran its dlist. No dlist => no load => no upload. The "teal" was a stale/other surface,
  not a failed upload. The `gsDPLoadBlockWide` change (cmb_to_c.py) IS still correct and
  needed (>4096 texels would truncate under plain `gsDPLoadBlock`); it was a red herring
  only in that it didn't "fix" a bug that was actually elsewhere.
- REMAINING (separate, integration-level, NOT an LUS bug): confirm the crate renders
  in-game when guaranteed on-screen. The debug-draw hooks only *spawn a cullable actor*;
  to verify deterministically, draw the crate dlist directly each frame in front of the
  camera (or spawn closer / point the camera at it). The LUS render path itself is proven.
- FIXED the REPL flakiness this implies: `SoH3D_ReplPoll` moved from `Play_Draw` (after a
  transition `goto` that skipped it) to `Play_Main` after `Play_Update`, so the REPL stays
  responsive during entrance fades.

### TOOLING — SoH-side dlist render harness (the LUS oracle) (2026-06-15, session 5)
The SoH-side counterpart to the Azahar decode oracle: drives libultraship's REAL Fast3D
interpreter over a generated CMB->F3DEX2 model dlist, headless, with NO game boot / NO
window / NO GPU. This is the tool to answer "does LUS actually upload/draw this model
correctly?" deterministically (ms, not a 7-min flaky scene navigation where the actor may
be culled). It already debunked the bogus "128x128 upload" bug (RESOLVED section above).
- `Shipwright/libultraship/tools/dlist_harness/` (CMake target `soh3d_dlist_harness`,
  gated by `-DLUS_BUILD_DLIST_HARNESS=ON`): a recording `GfxRenderingAPI` stub (logs every
  `UploadTexture` w/h + first pixel, and triangle count) + a no-op `GfxWindowBackend`; a
  minimal `Ship::Context` (just `InitConsoleVariables`); links the generated model `.c`
  directly. Injects an ortho projection + modelview via `Run()`'s `mtx_replacements` so
  triangles aren't all clip-rejected (the upload only fires once a tri survives culling).
- GOTCHAS baked in: (1) the harness target MUST define `F3DEX_GBI_2` (libultraship sets it
  PRIVATE, so it doesn't propagate) or the gbi.h opcode macros encode the wrong ucode and
  the dlist desyncs into garbage opcodes. (2) `gfx_set_timg_handler_rdp` rejects texture
  pointers `<= 0x0FFFFFFF` (assumed unresolved N64 segment addrs); in a small standalone
  binary the static texture sits at ~6 MB and is falsely rejected, so the harness mmap's
  the texture to a HIGH address to mimic the in-game (PIE, high-addr) condition.
- Build/run: `cmake -S . -B Shipwright/build-cmake -DLUS_BUILD_DLIST_HARNESS=ON`
  then `cmake --build ... --target soh3d_dlist_harness`; run the resulting binary.

### TOOLING — dlist harness `--gl` mode: REAL rendered pixels (2026-06-15, session 6)
The harness now also drives the **real `GfxRenderingAPIOGL`** to emit actual rasterised
pixels, still fully headless — the both-renderers pixel A/B counterpart to the Azahar
decode oracle. **VERIFIED**: the 128x128 RGBA32 crate renders as a fully textured wood
crate (bright wood highlights to 255,242,174, wood-grain interior, centred bbox) — see
`scratch/render/kibako_lus.png`. Definitively NOT teal/black; closes the "128x128 upload"
question with actual pixels, end-to-end through LUS's GL path.
- Run: `soh3d_dlist_harness --gl [--out scratch/render/kibako_lus.ppm] [--size 640x480]
  [--o2r <path>]` (recording stub stays the default mode). Needs the shader archive
  (`shaders/opengl/default.shader.glsl` lives in `soh.o2r`) — pass `--o2r`/`SOH3D_O2R` or
  it probes `Shipwright/build-cmake/soh/soh.o2r` etc.
- GL context = **EGL surfaceless** (no window, no X server, no Xvfb — directly addresses
  the leftover-Xvfb complaint). KEY GOTCHAS: (1) `EGL_PLATFORM_SURFACELESS_MESA` advertises
  ZERO EGLConfigs, so create a **config-less context** via `EGL_KHR_no_config_context`
  (`EGL_NO_CONFIG_KHR`) + `EGL_KHR_surfaceless_context` (both present on this Mesa). (2)
  Request a **compatibility** profile — the GLSL the OGL backend emits on desktop Linux is
  `#version 130` (varying / gl_FragColor / texture2D) and it draws WITHOUT a VAO; both need
  a non-core context. (3) harness CMake must also define **`ENABLE_OPENGL`** (gates the
  `GfxRenderingAPIOGL` decl in gfx_opengl.h) and link `OpenGL::OpenGL` + `OpenGL::EGL` (LUS
  pulls them PRIVATEly). (4) the rendered image lives in `mGameFb` = the interpreter's FIRST
  `CreateFramebuffer()` (deterministic index 1); read its colour attachment with
  `glGetTexImage` (MSAA=1 default => it's a plain RGB8 texture). fb 0 is re-cleared at frame
  end, so don't read it.
- Prologue adds a full-screen 320x240 viewport + scissor + white PRIM (combiner is
  MODULATE x PRIM => PRIM=0 renders black) so the crate actually rasterises on-screen.
- **`--model {kibako,pot,gs}`** selects which generated model to render (CMake links every
  `soh3d_*_model.c` that exists, exposes them via HAVE_* defines; default out =
  `scratch/render/<model>_lus.ppm`). The modelview is **auto-fit** from the model's vertex
  bbox (scan G_VTX = opcode **0x01** under F3DEX_GBI_2, `n=(w0>>12)&0xFF`, int16 ob[3] at
  Vtx offset 0; centre + uniform-scale into ~80% NDC) — no per-model magic constants.
  VERIFIED all three render correctly: crate=wood, pot=clay tubo2, gossip stone=Sheikah-eye
  with BOTH textures (2-material). PNGs in `scratch/render/`.
- (render_compare vs Azahar was explicitly dropped by the user — "just work on SoH3D".)

### Smooth (per-vertex) skinning in cmb.py — bind-pose = model space (2026-06-15, session 6)
**FIXED**: cmb.py previously only did RIGID skinning (whole prms bound to one bone) and
*scrambled* any `bone_dimension>1` (smooth-skinned) mesh by forcing bone_table[0]'s matrix
onto every vertex. The key finding (verified by rendering `hintstone` through the harness):
- **Rigid meshes (bone_dimension==1)**: vertices are in BONE-LOCAL space → transform by the
  single bound bone's world-bind matrix (unchanged; pot/gs/kibako/tr_box output byte-identical).
- **Smooth meshes (bone_dimension>1)**: vertices are in MODEL space; the per-vertex
  boneIndices/boneWeights are for animation only (runtime applies bone_current ·
  bone_bind_inverse, which is IDENTITY at bind pose). So a static bind-pose render uses the
  RAW positions with NO per-bone transform. Applying the bones' world-bind matrices (whether
  single-bone or a weighted blend) scrambles it — both tried on hintstone, both exploded; raw
  model-space gave a coherent stone (`scratch/render/hintstone_lus.png`).
- Data notes: boneIndices/boneWeights are per-vertex arrays of `bone_dimension` elems, GL data
  types (0x1401 = GL_UNSIGNED_BYTE); boneIndices are LOCAL indices into prms.bone_table;
  boneWeights scaled (e.g. ×0.01) and sum to 1. hintstone: 4 bones stacked vertically
  (world y = 0/2600/5200), bone_dim=3, bone_table=[1,2,3,0].
- **This unblocks CHARACTER models** — OoT3D characters/NPCs are smooth-skinned; they render
  in bind/T-pose. (Done — see next section.)

### First OoT3D CHARACTER rendered — Gerudo woman (2026-06-15, session 6)
**A real OoT3D character renders headless through the LUS GL backend** — see
`scratch/render/geldwoman_upright.png`: a fully-textured Gerudo (geldwoman) in T-pose
(red hair, yellow eyes, magenta top, white harem pants, gold bracelets, sandals), 15 bones,
6 meshes, 6 textures (5 ETC1 + 1 RGBA), 952 verts / 1086 tris.
- Extracted via the existing pipeline: `ctr_romfs.py` (NCSD→RomFS) → read `/actor/zelda_ge1.zar`
  → `zar.Zar` → `Model/geldwoman.cmb`. Other characters seen in romfs: zelda_link_{child,boy}_new
  (Link), zelda_ge1 (Gerudo), zelda_dog/cow, zelda_zl* (Zelda), zelda_ganon*, zelda_horse*.
- Smooth skinning (above) was the key unlock; multi-material (6 distinct opaque textures) and
  the harness load-block fix (plain G_LOADBLOCK for ≤4096-texel textures) were also needed.
- **OPEN (orientation):** character rest meshes are Y-up but **HEAD-DOWN** in model space
  (geldwoman head at y≈0, feet at y≈6524). The bind-pose render is faithful to raw model
  space; the upright PNG was just `magick -rotate 180`. Applying the skeleton ROOT bone's
  world matrix does NOT fix it (it rotates the figure to Z-up — tried, wrong). The correct
  rest→upright reorientation for in-game placement is unresolved and is an INTEGRATION concern
  (the in-game actor/skeleton matrix), not a geometry/texture bug. Revisit when wiring
  characters into SoH in-game.
- **Child Link too** (`zelda_link_child_new.zar` → `child/model/childlink_v2.cmb`): 25 bones,
  55 meshes, 27 materials, 11172 verts — renders fully textured (green tunic/hat, face,
  Kokiri sword + shield, slingshot, Deku shield) via `--model childlink`. See
  `scratch/render/childlink_upright.png`. (Lower legs/boots look slightly compressed — likely
  rigid sub-meshes bound to leg bones interacting with the mixed rigid/smooth model; minor,
  revisit later.)
- NEXT: in-game integration of OoT3D models (divert table `sModelTable[]` in soh3d.c);
  animation (moving bones) and world/scene geometry as later phases.

### Character orientation baking + first character divert wired (2026-06-15, session 6)
Resolved the head-down orientation so characters drop into the in-game pipeline like props:
- `cmb_to_c.py` gained **`--rotx/--roty/--rotz <deg>` + `--ground`** — bakes a PROPER
  rotation (no mirror) into the geometry and drops min-Y to 0 (stand on origin).
  `--rotx 180 --ground` makes the (head-down, model-space) characters UPRIGHT, grounded,
  front-facing, NOT mirrored. Harness-verified: `scratch/render/geldwoman_baked.png` and
  `childlink_baked.png` both upright with NO manual rotate. Baking it (vs a runtime
  transform) means `SoH3D_DrawModel`'s existing Translate*RotateY*Scale handles characters
  with zero special-casing — same path as the props.
- **First character divert wired in-game**: `sModelTable[]` now maps `ACTOR_EN_GE1`
  (white Gerudo) → the OoT3D Gerudo model (`SOH3D_GELDWOMAN_WORLD_SCALE` 0.011, initial).
  SoH compiles + links. (REPL `state` now lists the table generically.)
- **REMAINING (needs a game run):** live in-game verification + world-scale calibration vs
  the N64 En_Ge1 — warp to a Gerudo scene (Gerudo Fortress/Valley), `SOH3D=1`, A/B the
  divert, tune `scale geldwoman <f>` via the REPL (same method as the pot). Generated
  character model .c is regenerated with `--rotx 180 --ground` (ROM-derived → gitignored).

### BUG — in-game Gerudo is UPSIDE-DOWN + UNTEXTURED (harness ≠ game) (2026-06-15, session 6)
First in-game character test (En_Ge1 diverted, spawned in Gerudo Fortress ENTR 0x129=297,
SOH3D=1) rendered the OoT3D Gerudo **upside-down and untextured (flat grey)** — even though
the harness `--gl` render of the SAME generated .c is upright + fully textured. Verified the
running soh.elf has the upright model (geldwoman.c vtx y range 0..6524; elf newer than the .c).
Two distinct in-game-only bugs the harness did NOT catch:
1. **Orientation — the harness readback is vertically FLIPPED vs the game.** Both use
   libultraship's `GfxRenderingAPIOGL`; the only difference is harness-only code: the PPM
   readback **row-flip** (`WritePpmFlipped`) + identity projection. mGameFb is created with
   `opengl_invertY=true`, so the harness's extra flip likely double-inverts. Props (pot/gs)
   are ~vertically symmetric so it went unnoticed; the asymmetric Gerudo exposed it. The game
   is canonical → **the `--rotx 180` bake was BACKWARDS for the game.** TODO: (a) fix the
   harness readback to match the game (after fix, `--rotx 180` geldwoman should show
   head-DOWN in the harness); (b) re-derive the correct orientation bake against the GAME as
   ground truth (likely NO --rotx 180, just grounding so feet sit at the actor origin).
2. **Textures fail in-game (renders flat grey = TEXEL0*PRIM with no texel).** The harness
   mmaps textures to high addresses to pass `gfx_set_timg_handler_rdp`'s `addr<=0x0FFFFFFF`
   guard; in-game pot/gs/kibako texture fine, so soh.elf static data is normally high — but
   geldwoman (6 textures, several using the PLAIN G_LOADBLOCK for <=4096-texel textures, vs
   the props' all-wide loads) shows none. Investigate whether the small/plain-LoadBlock
   textures bind in-game, or whether some of the 6 G_SETTIMG addrs land below the guard.
**Lesson: the dlist harness is NOT a faithful proxy for in-game orientation/texture — verify
characters IN-GAME, not just in the harness.** The --rotx/--ground feature + En_Ge1 divert
wiring are sound; the orientation VALUE + texture path need in-game debugging.

### TOOLING — Azahar texture-decode ORACLE (data-driven) (2026-06-15, session 4)
Built the first piece of the "compare SoH3D vs Azahar" oracle the user asked for,
as a C++ tool in the Azahar fork that needs NO emulator run / in-game navigation:
- `Azahar/src/soh3d_oracle/` (new CMake target `soh3d_oracle`): links Azahar's OWN
  `Pica::Texture::LookupTexture` + `etc1.cpp` + `citra_common` (no GL/Vulkan/core).
  Decodes a raw PICA texture blob -> PPM = the emulator's ground-truth decode.
  Build: `cmake --build Azahar/build --target soh3d_oracle`.
- `tools/oracle_compare.py <cmb> [tex]`: pulls the CMB texture's raw bytes, runs
  BOTH the oracle and the converter's `pica_texture.decode`, and diffs per channel
  (worst/mean |Δ| + histogram), trying both V orientations.

**FOUND + FIXED a real ETC1 decode bug, data-driven (the full oracle loop).**
The oracle showed the converter's ETC1 decode was NOT bit-exact vs Azahar — crate
tex0: ~90.7% channels exact, ~9.3% off by 33-128 (worst 66). The spatial diff
(`oracle_compare.py` histogram by `(x%8,y%8)`) localised the errors to EXACTLY
ETC1 subblock2 (the `c2` half: differ when local x>=2 OR y>=2, exact in the
lx<2&ly<2 corner). That + worst=66 (= 8 five-bit levels × ~8.25 after 5->8
expansion) pinned it to the differential DELTA. Root cause: `pica_texture.py`
`_s3` (3-bit sign-extend) used the C idiom `(n<<29)>>29`, which relies on 32-bit
overflow into the sign bit — but Python ints are arbitrary precision, so it does
NOT sign-extend (`(4<<29)>>29 == 4`). Differential deltas with bit 2 set (n=4..7,
i.e. -4..-1) stayed positive, corrupting every differential-mode block's c2.
Fix: `_s3(n) = n-8 if (n&4) else n`. After the fix ALL OoT3D ETC1 textures
(pot, GS tex0/tex1, crate) decode **bit-exact** vs Azahar: worst|Δ|=0, mean|Δ|=0.

Impact: every OoT3D texture rendered in-game before this was subtly mis-coloured
(~9% of texels, up to 66/255). Regenerate the generated models to pick up the fix.
This is the payoff of the Azahar oracle — a bug invisible to eyeballing, caught and
fixed to provable exactness. (The separate crate 128x128 LUS UPLOAD bug above is
unrelated and still open — correct texels still won't upload until that's fixed.)

## Next phase (implementation)
1. **DONE** — First in-game OoT3D pot via approach A (see MILESTONE above).
2. **DONE** — Real `ObjTsubo_Draw` path renders the OoT3D pot at calibrated size
   (0.12), via `SoH3D_DrawModel`'s own matrix. Root cause of the earlier
   non-render documented above.
3. **DONE (lighting)** — Pot renders unlit / full-bright so the OoT3D texture
   shows at its authored brightness. Root cause of the earlier darkness: SETUPDL_25
   has `G_LIGHTING` on, so F3DEX2 read the white vertex *color* as a *normal* and
   shaded by the (dark) scene. The converter now clears `G_LIGHTING | G_FOG` and
   uses white vertex color (unlit). `--lit` emits real CMB normals for N64
   per-vertex lighting instead, but on this low-poly pot in low ambient that
   bands hard, so unlit is the default. True 3DS-style per-pixel lighting is a
   later, bigger task.
4. **DONE (flat scene tint)** — Unlit models now darken/colour-shift with the room
   via a flat per-draw PRIMITIVE tint (no per-vertex banding). See the MILESTONE
   below. (Materials→textures map was already done in session 3's multi-material
   work; the converter no longer hardcodes texture 0.)
5. **Azahar oracle instrumentation**: headless frame dump (glReadPixels, mirror
   of the LUS one) + draw-call dump of geometry/material/texture for A/B compare.
6. **DONE (generalised divert)** — A central table-driven divert replaces the
   per-actor Draw edits. See the MILESTONE below.

## In progress / next
- **Azahar Qt frontend build**: needs Qt6 — run `scripts/install_azahar_deps.sh`,
  then reconfigure with `-DENABLE_QT=ON` and build `citra_qt`. (Azahar has NO
  standalone SDL frontend; `citra_cli` is just a static helper lib.)
- **SoH game assets**: `GenerateSohOtr` uses `--norom` (only builds `soh.o2r`).
  The game `oot.o2r` is produced by SoH's built-in extractor at first launch
  (point it at the staged ROM `Shipwright/OTRExporter/oot_ntsc10.z64`).
- Then: oracle instrumentation in Azahar `video_core` (dump geometry/material/
  texture state + framebuffer for a target model), and the SoH-side integration
  (new 3DS-model resource + draw path, hooked where the N64 model is drawn).

## Integration design (SoH render path)

Draw path (pot example): `ObjTsubo_Draw` → `Gfx_DrawDListOpa(play, dlist)`
(`soh/src/code/z_cheap_proc.c`). That fn does:
1. `Gfx_SetupDL_25Opa` (state), 2. `gSPMatrix(..., MATRIX_NEWMTX, MODELVIEW|LOAD)`
— loads the actor's already-built world matrix, 3. `gSPDisplayList(dlist)`.
So the actor transform is on the RSP matrix stack before the dlist; any
replacement draws at the correct place for free. SoH already wraps pot draw in
GameInteractor `VB_POT_SETUP_DRAW` hooks — a clean place to divert.

Two integration layers:
- **A. CMB→F3DEX2 conversion** (no LUS changes): convert CMB to an N64 display
  list + full-res RGBA32 texture, feed to `Gfx_DrawDListOpa`. **This is what's
  implemented and verified (see MILESTONE).** The old "TMEM 4 KB limit" worry was
  wrong — LUS uploads textures at full tile size (CORRECTION above). Remaining A
  limits are s16 vertex precision and N64-style lighting, not texture size.
- **B. Native model draw path** (chosen end state): new Fast3D opcode / resource
  in libultraship that, at gfx_pc interpret time, takes the current MV+proj matrix
  and renders a native CMB mesh (full-res RGBA texture bound directly, bypassing
  TMEM) through the modern gfx backend. Hook actor draw (GameInteractor `VB_` or a
  per-object table) to emit it instead of the N64 dlist. Real 3DS quality.

Plan: implement B. First milestone — pot in-game via the native path, A/B'd
against the Azahar oracle render.

To generate the **game** archive (oot.o2r) headlessly:
`python3 OTRExporter/extract_assets.py -z build-cmake/ZAPD/ZAPD.out --non-interactive <rom>`
(run from `Shipwright/soh`-relative as the build does; do after Azahar build to
avoid CPU contention).

## Gotchas learned (CMB; wiki is partly wrong / MM3D-mixed)
- OoT3D cmb version = 6; MM3D = 0x0A. OoT3D has NO tangent attribute.
- SEPD VertexList stride = 0x1C (includes constant vec4), not 0x14.
- VATR slice entry = (size u32, offset u32) — size FIRST.
- Bone struct stride = 0x28. PRM indices are global into VATR (start + idx*stride).
- Texture glFormat = (dataType<<16) | formatConstant.
