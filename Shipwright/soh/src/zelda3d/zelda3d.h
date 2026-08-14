// Zelda3D — render OoT3D (3DS) models in place of N64 assets.
// See repo-root PROGRESS.md for the overall design.
#ifndef ZELDA3D_H
#define ZELDA3D_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns true when OoT3D-model rendering is enabled (env SOH3D=1). Cached.
int Zelda3D_Enabled(void);

// Generalised per-actor divert, called from Actor_Draw for every actor. If Zelda3D
// is enabled and the actor's id has an OoT3D model registered in the table, draws
// that model (via the direct-GL path) and returns 1 so the caller skips the actor's
// N64 draw; returns 0 otherwise (caller draws the N64 model as normal). Replaces
// the old per-actor `if (Zelda3D_Enabled())` edits in each actor's Draw.
int Zelda3D_TryDrawActor(PlayState* play, Actor* actor);

// Pure predicate (no drawing): does this actor have an OoT3D replacement right now? Used by the
// engine draw-distance check so replaced actors keep drawing/updating past the N64 cull distance.
int Zelda3D_ActorHasReplacement(PlayState* play, Actor* actor);

// Called from Actor_Draw immediately AFTER an actor's N64 draw (only when
// Zelda3D_TryDrawActor returned 0, i.e. the N64 model drew). Closes the auto-scale
// measure bracket opened by Zelda3D_TryDrawActor so the ZELDA3D_AUTO path can measure the
// actor's drawn world size on this frame and derive its OoT3D model scale. No-op unless
// a measurement was opened for this actor.
void Zelda3D_AfterActorDraw(PlayState* play, Actor* actor);

// N64-animation port hook, called at the top of the common SkelAnime draw choke points
// (SkelAnime_DrawSkeletonOpa / SkelAnime_DrawSkeleton2). When the actor currently being
// drawn is registered for N64-anim replacement (sModelTable n64anim flag, enabled via
// ZELDA3D_N64ANIM) this retargets the OoT3D model's skeleton from the live N64 jointTable and
// draws it, returning 1 so the caller SKIPS the N64 limb draw. Returns 0 otherwise (the N64
// skeleton draws as normal — also the fallback for actors whose draw path isn't hooked).
// This is what makes N64-anim replacement generic: no per-actor jointTable accessor needed.
int Zelda3D_SkelAnimeDraw(PlayState* play, SkelAnime* skelAnime);

// Record the limb-draw override callback (+arg) the actor passed to its SkelAnime_Draw* call, so
// the N64-anim auto-replace path can replay a PROCEDURAL per-limb rotation the override adds (e.g.
// the cucco wing-flap, which lives in EnNiw_OverrideLimbDraw, not in any animation) onto the
// matching OoT3D bones. `kind`: 0 = OverrideLimbDrawOpa (6 args), 1 = OverrideLimbDraw (7 args).
// Call right before Zelda3D_SkelAnimeDraw / ...Raw at each choke point that has an override on hand;
// NULL override clears it. Consumed once per retarget. #23.
void Zelda3D_SetLimbOverride(void* overrideFn, void* arg, int kind);

// #5 cucco-flap diagnostic: when set, EnNiw_Update holds every cucco in its agitated wing-spread
// pose (func_80AB5BF8 arg 2) each frame, so the spread flap can be A/B'd deterministically headless
// (N64 model via `enable 0` vs OoT3D replay) to derive/verify the multi-axis wing mapping. REPL
// `cuccopose <0|1>`. The agitated flap drives the wing bones on local X+Y+Z, not just Z like idle.
extern int gZelda3dForceCuccoAgitate;

// #5 cucco WING-STATE machine (the one genuinely cucco-specific control — no generic equivalent):
//   gZelda3dCuccoState : -1 = live AI; >=0 = force func_80AB5BF8(this,play,STATE) every frame on every
//                      cucco (0=calm, 1=mild flap, 2=agitated/held spread, 3, 5=...). REPL
//                      `cuccostate <n|off>`; `cuccopose 1` is the legacy alias for state 2.
// Diagnostic read-back (last cucco drawn this frame) so a captured frame's flap phase is known:
//   gZelda3dCuccoDbgPhase : that cucco's unk_29C (the two-phase flap toggle, 0/1).
//   gZelda3dCuccoDbgWing  : the per-limb binang actually applied — [0..2]=limb7 xyz, [3..5]=limb11 xyz.
// REPL `flapinfo` prints these, so two distinct flap phases can be captured. Hold the cucco STILL
// and FRAME it with the GENERIC actor controls below (`asel 0x19` + `afreeze 1` + `acam`).
extern int gZelda3dCuccoState;
extern int gZelda3dCuccoDbgPhase;
extern short gZelda3dCuccoDbgWing[6];
// gZelda3dCuccoHeld : 1 = force every cucco into the HELD-BY-LINK carried action (func_80AB6BF8) —
//   the frantic body shake (shape.rot ±5000/frame) + feather bursts + wing flap — without needing
//   Link to actually grab it (#9/#6 pickup is broken). REPL `cuccoheld <0|1>`. Pair with `afreeze 2`
//   (position-only freeze) so the body still jitters while the cucco stays framed; `afreeze 1` would
//   pin the rotation and kill the shake.
extern int gZelda3dCuccoHeld;

// Generic actor-control debug surface (works on ANY actor, not just cuccos). One selected actor is
// driven each frame from Zelda3D_ActorPostUpdate (called per-actor at the end of Actor_UpdateAll).
//   `asel <id> [n]` / `asel any [n]` : select the n-th nearest live actor of that id (0xHEX or dec)
//                                      to Link (default nearest); captures its current pos/rot.
//   `afreeze <0|1>`   : pin the selected actor's transform every frame (no wander/hop/flee/AI drift).
//   `apos <x y z>`    : set + pin the selected actor's world position.
//   `arot <x y z>`    : set + pin its rotation (binang, x y z).
//   `aparams <v>`     : set actor->params.
//   `acam [dist] [ax]`: frame the selected actor as a side profile (ax 0=+X, 1=+Z; dist default 110).
//   `ainfo`           : dump the selected actor (id, pos, rot, params, velocity, update/draw ptrs).
void Zelda3D_ActorPostUpdate(PlayState* play, Actor* actor);

// Raw variant of the N64-anim hook for draw choke points that don't have a SkelAnime* on hand
// (SkelAnime_DrawFlexOpa / SkelAnime_DrawOpa, called directly by many actors). Same effect as
// Zelda3D_SkelAnimeDraw; derives limbCount from the skeleton tree. Returns 1 if it drew the OoT3D
// model (caller skips the N64 limbs).
int Zelda3D_SkelAnimeDrawRaw(PlayState* play, void** skeleton, Vec3s* jointTable);

// Set while a replaced actor's N64 limb walk is being re-run purely for its SIDE EFFECTS — the
// per-limb postLimbDraw callback, which is the only writer of the collision spheres (#107/#108) and
// of Player's shape.feetPos (#206, the missing feet shadow). While it is set the hooks above return
// 0 so the real N64 walk runs instead of the OoT3D replacement; the caller rewinds polyOpa.p/polyXlu.p
// afterwards so none of that walk's geometry reaches the screen.
extern int gZelda3dColliderPass;

// Record the live N64 animation pointer (an OTR path string in SoH) AND the live playhead
// (curFrame / animLength) for the actor currently deferred for replacement, so the auto CSAB
// resolver can map it to the matching OoT3D CSAB and the auto path can PHASE-LOCK the OoT3D CSAB to
// the actor's real N64 tempo. Called from the SkelAnime-bearing draw wrappers (func_80034BA0/CC4)
// whose inner SkelAnime_DrawFlex (the raw hook) has only the skeleton, not the SkelAnime — so
// without this capture those actors (e.g. En_Ko Kokiri kids) never reach the phase-lock branch and
// free-run at the global rate (#76: wrong/frozen anims). No-op when no replacement is pending.
void Zelda3D_SetCurAnim(void* animation, float curFrame, float animLength, float morphWeight);

// Generalised per-room scene divert, called from Room_Draw. If Zelda3D is enabled and
// the current scene has an OoT3D mapping (kZelda3dSceneNames) with a room CMB for
// room->num, draws that room geometry at the world origin (identity model matrix +
// the game camera, depth-correct via the scene pass) and returns 1 so the caller
// skips the N64 room mesh; returns 0 otherwise (caller draws the N64 room as normal).
int Zelda3D_TryDrawRoom(PlayState* play, Room* room);

// True when Zelda3D is going to draw the OoT3D room for the current scene — used to suppress the N64
// pre-rendered background (e.g. Link's House interior). That bg image is drawn from TWO paths: the
// Room_Draw handler (already guarded by Zelda3D_TryDrawRoom) and the SkyboxDraw_Draw call in Play_Draw
// for bg-image skyboxes (unk_140 != 0). Without a guard on the second path the 2D bg image draws
// over our CMB whenever the camera setting isn't CAM_SET_PREREND_FIXED (which triggered the second
// path). #134.
int Zelda3D_ShouldSuppressBgImageSkybox(PlayState* play);

// #111: the exact env-light blend z_kankyo just performed this frame, captured at the blend site so
// Zelda3D_SceneLightSettingsOverride can re-run the SAME indices + weights against the OoT3D rows —
// no schedule logic is duplicated and no guessing which branch (outdoor time-of-day vs indoor
// settings) ran. z_kankyo.c writes it in all three lightSettings branches.
typedef struct {
    unsigned char valid;     // 1 once z_kankyo has captured a blend for the CURRENT scene's palette;
                             // reset on palette switch (Zelda3D_UpdateLight) so a stale record from
                             // the previous scene is never applied to the new scene's rows
    unsigned char timeBased; // 1 = outdoor time-of-day path (colors only; sun/moon dirs stay computed)
    unsigned char idx[4];    // settings-list indices: time path {cfgA.from, cfgA.to, cfgB.from, cfgB.to};
                             // settings path {prev, cur, prev, cur} (wConfig = 0 collapses the 2nd pair)
    float wTime;             // within-config time lerp (sp8C) / indoor blend weight (unk_D8)
    float wConfig;           // cross-config lerp (sp88); 0 when no config transition
} Zelda3dEnvBlend;
extern Zelda3dEnvBlend gZelda3dEnvBlend;

// The OoT3D per-scene env COLOR blend for this frame (ambient + the two directional light
// colors, 0..1), computed by Zelda3D_SceneLightSettingsOverride from the OoT3D ZSI rows under
// the SAME schedule/indices/weights z_kankyo just used for the N64 rows (gZelda3dEnvBlend).
//
// WHY A SEPARATE SINK AND NOT envCtx.lightSettings (unified-shading decision, 2026-07-23):
// both render paths evaluate the SAME per-vertex light equation — clamp(ambientSum +
// Σ lightCol·max(0, N·L)) — the N64 F3DEX one on the CPU (interpreter.cpp gSPVertex) and the
// PICA CmbVShader one on the GPU (zelda3d_sdl3gpu.cpp kVert). What differs is CALIBRATION:
// the OoT3D env magnitudes are authored against CMB materials (matAmbient/matDiffuse
// coefficients ~0.4/0.5 on characters, baked-AO vertex colours, double-ambient scene
// binding), while N64 materials have an implicit matAmb=matDif=1 and vColor=shade, authored
// against the N64 rows. Writing the OoT3D colors into envCtx.lightSettings (the pre-2026-07-23
// design) fed 3DS-calibrated magnitudes into N64-calibrated materials: at Kokiri day the
// N64 ambient rows were replaced by OoT3D's (181,181,160) and every lit N64-format draw
// (N64 Link, doors) blew out toward saturation. FALSIFIED premise recorded: "one env feed for both paths" is
// NOT unification — it mixes calibration spaces. The unified model is ONE env STATE (same
// schedule, blend weights, light DIRECTIONS, fog) driving TWO calibration palettes, each
// material corpus lit by the rows its assets were authored for. So: colors for CMB draws go
// HERE (renderer feed), colors for N64 draws stay the N64 rows via the stock
// lightSettings -> lightCtx path; dirs + fog (shared world truth) are still overridden.
typedef struct {
    unsigned char valid; // 1 = gameplay frame with an OoT3D palette applied; 0 = title / no
                         // palette / no blend captured yet -> consumers fall back to
                         // envCtx.lightSettings (which then carries the intended values)
    float amb[3];        // scene ambient, 0..1
    float l1col[3];      // light-slot-1 (sun/moon key) color, 0..1
    float l2col[3];      // light-slot-2 (opposed fill) color, 0..1
} Zelda3dEnvColors;
extern Zelda3dEnvColors gZelda3dEnvColors;

// Title-demo: override envCtx.lightSettings from the ported 3DS title
// palette + time schedule (no-op outside the title demo). Called by
// z_kankyo before the lightSettings -> lightCtx application.
void Zelda3D_TitleLightSettingsOverride(PlayState* play);
// Gameplay counterpart (#111): re-runs the gZelda3dEnvBlend record against the OoT3D rows.
// COLORS go to gZelda3dEnvColors (CMB renderer feed only — see its comment above); light
// DIRS (settings path) + FOG color/window still go into envCtx.lightSettings, shared by
// both render paths.
void Zelda3D_SceneLightSettingsOverride(PlayState* play);

// Draw the OoT3D sky (BlueSky.zar tenkyu gradient dome) in place of the N64 normal-sky skybox.
// Called from Play_Draw at the skybox point; returns 1 if it drew the OoT3D sky (caller skips the
// N64 SkyboxDraw_Draw), 0 otherwise (caller draws the N64 skybox as normal). #28.
int Zelda3D_TryDrawSky(PlayState* play);

// Query form of Zelda3D_TryDrawSky (no draw, no side effects): 1 if the Zelda3D OoT3D sky dome is
// handling the skybox this frame (so the N64 SkyboxDraw_Draw is bypassed and its sSkyboxDrawMatrix
// is never allocated). Callers MUST then skip the N64 SkyboxDraw_UpdateMatrix to avoid a NULL deref
// (#16 first-person early-load crash). #28.
int Zelda3D_SkyActive(PlayState* play);

// Draw the OoT3D sun/moon discs (BlueSky.zar fine_sun.ctxb / fine_moon0.ctxb billboards) in place
// of the N64 Environment_DrawSunAndMoon sprites. Called from Play_Draw at that call site; returns 1
// if it drew the OoT3D sun/moon (caller skips the N64 path), 0 otherwise (caller draws N64). #28e.
int Zelda3D_TryDrawSunMoon(PlayState* play);
// Task #16 title atmosphere: composite the OoT3D menu-bg + ura CTXBs OVER the 3D scene at
// title. This is what paints the visible grass / mountain / sky colours on Az's title-demo
// (the 3D geometry alone draws effectively black through the game's TEV combiner). Returns
// 1 when the atmosphere was drawn (title-demo active), 0 otherwise.
int Zelda3D_TryDrawTitleAtmos(PlayState* play);

// Once-per-frame Zelda3D bookkeeping after the actor draw-all: advance the hand-flap frame counter
// and update the scene light direction. (The 3DS model draws are appended INLINE during the actor
// draws — G_ZELDA3D_DRAW — into the one unified render pass; there is no render-pass marker to emit.)
void Zelda3D_FrameEndUpdate(PlayState* play);
// Per-frame, before the display list is built: drop any Zelda3D draws left unrendered from a prior
// frame. Call once per frame ahead of Play_Draw (e.g. alongside the REPL poll).
void Zelda3D_FrameBegin(void);

// Floor-height callback: returns the N64 collision floor Y at world (x,z), or a value
// <= -31000 if there is no floor. Provided by zelda3d.c (it has the PlayState/colCtx).
typedef float (*Zelda3D_FloorFn)(float x, float z);

// Compute & cache an OoT3D scene-room's ground-delta field D(x,z)=N64_floor-OoT3D_floor (the
// render mesh is left untouched; actors are offset by -D to stand on the visible OoT3D
// ground). Idempotent per model. Call from the room-draw hook before the room is drawn.
void Zelda3D_ComputeRoomGroundDelta(int modelId, Zelda3D_FloorFn floorFn);

// Sample that field: *outD = N64_floor - OoT3D_floor at world (x,z). Returns 1 on success.
int Zelda3D_RoomGroundDeltaAt(int modelId, float x, float z, float* outD);

// OoT3D render-mesh floor at (x,z) for a scene room, the floor hit closest to `target`.
// Returns 1 + *outY on a hit. Grounds actors exactly on the visible OoT3D ground (exact, no
// grid approximation; XZ-bbox-culled so per-actor-per-frame is cheap).
int Zelda3D_RoomOoT3DFloorAt(int modelId, float x, float z, float target, float* outY);

// Render-Y offset for an actor so it stands on the visible OoT3D ground instead of floating
// at the N64 collision height (= OoT3D_ground - N64_ground at the actor's XZ, i.e. -D).
// Returns 0 when Zelda3D is off, the scene has no OoT3D room, or no delta covers the actor.
// Called from Actor_Draw: add to actor->world.pos.y around the draw, then subtract (physics
// stays N64). The inverse of the old render warp (which distorted the mesh).
float Zelda3D_ActorRenderYOffset(PlayState* play, Actor* actor);

// Query the (warped) OoT3D room render-mesh floor Y at world (x,z). Returns 1 + *outY
// on a floor hit, else 0. For verifying the warp aligned the drawn ground to N64.
int Zelda3D_RoomMeshFloorAt(int modelId, float x, float z, float* outY);

// #5 — real stepped-polygon stairs: replace OoT3D fake-flat "kaidan" ramps with actual
// treads+risers. SetStairs evicts cached scene-room models so the scene rebuilds (live A/B).
void Zelda3D_SetStairs(int on);
int Zelda3D_GetStairs(void);
void Zelda3D_SetStairRiserY(float v); // generated step rise (world-units/step); larger = bigger steps
float Zelda3D_GetStairRiserY(void);

// #32 — Xbox face-button HUD glyphs. Returns a persistent RGBA8888 (G_IM_FMT_RGBA/32b) buffer for
// the glyph 'A'/'B'/'X'/'Y' (case-insensitive) + its dims, or NULL on failure. The in-game Fast3D
// HUD draws this raw pointer in place of the shared N64 button circle when gZelda3dXboxBtn is set.
const void* Zelda3D_XboxGlyphTex(char which, int* w, int* h);
extern int gZelda3dXboxBtn;       // env ZELDA3D_XBOXUI / REPL `xboxui` gate (-1=uninit, 0/1)
// Zelda3D_XboxBtnEnabled() declared in input/zelda3d_input.h (moved there, Phase 1 input
// consolidation); lazily resolves the env on first call, HUD draws gate on this.

// #32/#203 hotswap — keyboard-key HUD badge. Composites a keycap carrying `label` (an uppercase
// key name from Zelda3D_KeyLabelForButton, e.g. "1" or "LSHFT") and returns a persistent RGBA8888
// buffer + its dims, or NULL on failure. Height is always 64; the WIDTH grows for multi-character
// labels (the cap is 9-sliced horizontally), so callers must draw at the returned aspect rather
// than assume a square — a wide label on a square rect would squash the text. Results are cached
// per label string, so calling this every HUD frame costs a hash lookup.
const void* Zelda3D_KeyCapTex(const char* label, int* w, int* h);

// The character set Zelda3D_KeyCapTex can actually draw (the generated glyph atlas's alphabet).
// input/zelda3d_keymap.cpp folds SDL key names against this rather than keeping a second copy.
const char* Zelda3D_KeyCapAlphabet(void);

// #32 hotswap — last-used input device. 0 = gamepad (Xbox glyphs), 1 = keyboard (key-label glyphs).
// Updated from the C++ LUS input layer (Ship::Controller::ProcessKeyboardEvent for keyboard events,
// LUS::Controller::ReadToOSContPad for gamepad events). The HUD reads this each frame to pick the
// glyph set. Always 0 or 1 -- there is no unset state; ZELDA3D_INPUTDEV seeds it once at startup via
// Zelda3D_InputDeviceInit. REPL `inputdev <0|1>` overrides for testing.
extern int gZelda3dInputDevice;
// Zelda3D_InputDevice() declared in input/zelda3d_input.h (moved there, Phase 1 input
// consolidation) — a plain read of gZelda3dInputDevice above.

// #31 — crisp higher-res HUD textures (hearts first). Returns a persistent RGBA8888
// (G_IM_FMT_RGBA/32b) buffer for a heart kind + its dims, or NULL on failure. The buffer is
// grayscale (rgb=intensity, a=silhouette) so the N64 heart combine ((PRIM-ENV)*TEXEL0+ENV) tints
// it exactly like the original IA8 heart — just at a much higher resolution. The Fast3D HUD draws
// this raw pointer in place of gHeart{Full,ThreeQuarter,Half,Quarter,Empty}Tex when gZelda3dHudTex.
enum {
    ZELDA3D_HEART_FULL = 0,
    ZELDA3D_HEART_THREEQUARTER,
    ZELDA3D_HEART_HALF,
    ZELDA3D_HEART_QUARTER,
    ZELDA3D_HEART_EMPTY,
};
const void* Zelda3D_HeartTex(int kind, int* w, int* h);
// Decode an OoT3D standalone romfs .ctxb atlas (the real 3DS HUD textures: hud_all00,
// icon_item_menu00, num_all00) to RGBA8, cached by path. texIdx selects the texture entry (these
// menu files carry one atlas at index 0). NULL on failure.
const void* Zelda3D_OoT3dAtlas(const char* romfsPath, int texIdx, int* w, int* h);
// Local AABB of one uploaded Zelda3D draw group; 1 on success. Room CMBs are world-space under
// an identity matrix, so for scene geometry this is the WORLD box. Backs the REPL `camdraw`.
int Zelda3D_Sg_GroupBounds(int modelId, int groupIdx, float* outMin, float* outMax);
// ROM-authored dims of a CTXB atlas (sprite rects are in this space; the atlas itself may have
// been replaced by a hi-res pack, so scale rects by atlasW/nativeW).
void Zelda3D_OoT3dAtlasNativeSize(const char* romfsPath, int texIdx, int* w, int* h);
extern int gZelda3dHudTex;       // env ZELDA3D_HUDTEX / REPL `hudtex` gate (-1=uninit, 0/1)
int Zelda3D_HudTexEnabled(void); // lazily resolves the env on first call; HUD draws gate on this

// #31 — crisp HUD counter font. `glyph` 0..9 = the digit, 10 = ':'. Returns a persistent RGBA32
// (grayscale, a=coverage) glyph + dims, or NULL. The counter combine is colour=PRIM, alpha=TEXEL0,
// so the grayscale glyph reproduces the N64 8x16 I8 digit at higher resolution (see Gfx_TextureI8).
const void* Zelda3D_DigitTex(int glyph, int* w, int* h);

// #31 — crisp HUD button-background disc (the round beveled circle behind the B / C / A action
// buttons). Returns a persistent RGBA32 (grayscale: rgb=bevel intensity, a=circle coverage) + dims,
// or NULL. The button combine is G_CC_MODULATEIA_PRIM (out.rgb=TEXEL0.rgb*PRIM, out.a=TEXEL0.a*PRIM),
// so the grayscale disc tints to each button's PRIM colour exactly like the N64 32x32 IA8 original.
const void* Zelda3D_ButtonBgTex(int* w, int* h);

// #31 — crisp HUD counter icons. `kind` 0=rupee gem, 1=small key, 2=clock. Returns a persistent
// RGBA32 (grayscale, a=coverage) + dims, or NULL. The rupee/key draw MODULATEIA_PRIM (PRIM tints
// the facet shading); the clock draws MODULATERGBA_PRIM with PRIM white (grayscale shown directly).
// Substituted by pointer in Gfx_TextureIA8 for the N64 16x16 IA8 gRupee/SmallKey/Clock icons.
enum {
    ZELDA3D_CICON_RUPEE = 0,
    ZELDA3D_CICON_SMALLKEY,
    ZELDA3D_CICON_CLOCK,
};
const void* Zelda3D_CounterIconTex(int kind, int* w, int* h);

// #2 — press-to-skip: on a Start/Space press, force-end any active onepoint cutscene camera
// (door/attention/treasure pans) so it hands control back. Call once per frame from Play_Update
// before the camera update loop. Gate env ZELDA3D_SKIP (default on) / REPL `skip <0|1>`.
extern int gZelda3dSkip;

// Frame-step harness (transient-capture tooling, e.g. #86 walk-stop snap, #80 boulder spin). When
// gZelda3dFreeze!=0 the per-frame Play_Update in z_play.c is skipped, holding the game logic still while
// the REPL + Play_Draw keep running (so dumped frames stay stable). REPL `step [n]` advances exactly n
// logic ticks on demand, letting `dumpframe` capture every single game frame of a brief transient.
extern int gZelda3dFreeze;

// --- OoT/MM launcher ---------------------------------------------------------------------------
// The launcher is a GAMESTATE (zelda3d/launcher/zelda3d_launcher_state.c), entered before any game
// exists rather than drawn over a running one. Registered in gGameStateOverlayTable.
void Launcher_Init(GameState* gameState);
void Launcher_Main(GameState* gameState);
void Launcher_Destroy(GameState* gameState);
// Install this core's hooks into the shared engine (zelda3d/core/zelda3d_hostreg.cpp). Called from
// InitOTR before the first frame; libultraship cannot reach a dlopen'd core by symbol name.
void Zelda3D_RegisterHostHooks(void);

int Zelda3D_LauncherEnabled(void);        // zelda3d/launcher/zelda3d_launcher.cpp -- the ONE gate;
                                          // graph.c asks it whether to enter the launcher gamestate
void Zelda3D_LauncherShow(int show);      // SohRmlUi.cpp -- show/hide the launcher document
int Zelda3D_LauncherIsVisible(void);
int Zelda3D_LaunchMM(void);               // zelda3d/launcher/zelda3d_launcher.cpp; 1 = MM starting
// The core's run lifecycle (zelda3d/core/zelda3d_core_lifecycle.c, which also DEFINES the state).
// Begin resets everything whose lifetime is one run, so a run cannot inherit the previous game's;
// End reports anything the teardown left set, which is how a teardown that stopped running gets
// noticed instead of being silently papered over by the next Begin.
void Zelda3D_CoreRunBegin(void);
int Zelda3D_CoreRunEnd(void);

// "Do this once per RUN" -- the idiom for the `static bool initialized` latch.
//
// Three of the crashes in this arc were a plain function-local `static bool`: written to mean "once",
// which in a program that runs one game and exits is the same thing as "once per run", and under the
// launcher is not. The setup they guarded (audio engine init, the skybox setup that exists to avoid
// the 0xabababab crash, SkelAnime_Init against ResourceManager-owned skeletons) then never happened
// again, and the next run used the previous one's.
//
// Central reset lists are the right answer for STATE -- a pointer, a struct -- because the reset also
// has to say what to free. They are the wrong answer for a latch: the list is somewhere else, and the
// skybox latch proved the failure mode by sitting three lines below a static that WAS hoisted into
// one. So a latch carries its own run stamp instead:
//
//     static Zelda3DOnce sOnce;
//     if (Zelda3D_Once(&sOnce)) { ...setup that belongs to this run... }
//
// Zero-initialised, so the first run is the first call. Nothing to register, nothing to remember.
typedef struct {
    unsigned int epoch;
} Zelda3DOnce;

// Returns 1 the first time it is called in a given run for this latch, 0 afterwards.
int Zelda3D_Once(Zelda3DOnce* once);
void Zelda3D_LauncherExit(void);
int Zelda3D_SkipEnabled(void);
void Zelda3D_SkipControlTakers(PlayState* play);

// World scale for the OoT3D pot (OoT3D model units -> N64 world units). Tuned by
// matching the rendered height of the OoT3D pot to the N64 pot at the same spot
// (spawn comparison, Deku Tree). The OoT3D model is ~162 units tall; ~0.12 lands
// it at the N64 pot's height. See PROGRESS.md calibration.
#define ZELDA3D_POT_WORLD_SCALE 0.12f

// World scale for the OoT3D large wooden crate (Obj_Kibako2). Model is ~600 units
// wide; calibrated against the N64 large crate via the ZELDA3D_SPAWNKIBAKO A/B spawn
// in Gerudo Valley (ENTR 0x117=279). Third object proving the table-driven divert:
// adding it was one sModelTable[] row + this macro + the generated include.
#define ZELDA3D_KIBAKO_WORLD_SCALE 0.10f

// World scale for the OoT3D field bush reused for Obj_Hana's bush variant (params&3==2),
// the cuttable shrub the grass-cutting Kokiri picks. Same kusa model as En_Kusa (glModelId 2);
// starts at the kusa scale and is fine-tuned against the N64 Obj_Hana bush. REPL `scale kusa`.
#define ZELDA3D_HANABUSH_WORLD_SCALE 0.5f

// World scales for the field-keep props (En_Ishi rocks, Obj_Hana flower) reused from
// zelda_field_keep.zar. Starting estimates against the N64 actor scale (rock 0.1/0.4,
// flower 0.01); fine-tune live with REPL `scale rock_s|rock_l|flower`.
// Only the SMALL rock value is a real calibration. The other two were copies of it, and both were
// badly wrong -- they are now SEEDS ONLY: sVariantMeas in zelda3d_render.cpp measures those two
// variants from their own N64 draw and overrides these (selfCalibrate=1). Measured live by spawning
// each variant in Hyrule Field:
//   large rock: 0.38139 (n64h 89.6) -- this 0.12 seed drew it at 0.315x, i.e. a third of its size
//   flower:     0.00952 (n64h  8.2) -- the CMB is ~861 local units tall, so this 0.12 seed drew it
//                                      103 world units tall, taller than Link (~60)
#define ZELDA3D_ROCK_SMALL_WORLD_SCALE 0.12f  // calibrated vs N64 in Kokiri Forest
#define ZELDA3D_ROCK_LARGE_WORLD_SCALE 0.12f  // seed only -- measured at runtime (was 3.2x too small)
#define ZELDA3D_FLOWER_WORLD_SCALE 0.12f      // seed only -- measured at runtime (was 12.6x too big)

// Kakariko well + windmill (Bg_Spot01_Fusya / _Idohashira / _Idomizu), all from one shared ZAR
// coordinate space (zelda_spot01_objects.zar). Seeded from the auto-derived per-object scale
// (n64h 130 / fusya CMB 10255 ~= 0.0127). Tune live with REPL `gscale 7|8|9 <f>`.
#define ZSPOT01 "/actor/zelda_spot01_objects.zar"
#define ZELDA3D_SPOT01_WORLD_SCALE 0.01268f

// Kakariko DM-trail gate (Bg_Gate_Shutter, OBJECT_SPOT01_MATOYAB). It shares
// zelda_spot01_matoyab.zar with the windmill mechanism (c_matoate_before), but the two CMBs are
// authored at DIFFERENT unit scales (matoate ~1402 units tall, the gate c_s01tomegate ~111), so the
// gate needs its OWN scale — calibrated to 1.4 in-game (gate fills the DM-trail archway). REPL
// `gscale 10 <f>`.
#define ZMATOYAB "/actor/zelda_spot01_matoyab.zar"
#define ZELDA3D_MATOYAB_WORLD_SCALE 1.4f

// Lake Hylia water body (Bg_Spot06_Objects, LHO_WATER_PLANE). The OoT3D water surface is the
// actor model c_s06beforewater (translucent blue body + additive caustics) in
// zelda_spot06_objects.zar — NOT the room's additive-only s06_uvwater overlay (which alone reads as
// flat fluorescent cyan; kanban #103). It has 2 bones (the Water-Temple raise/lower pose) so the
// auto path skips it as "skinned"; route it here at its rest/bind pose like a scene-aligned prop.
// The mesh is authored in the actor's local space at N64 unit scale, so worldScale = 1.0; REPL
// `gscale 11 <f>` overrides for re-cal.
#define ZSPOT06 "/actor/zelda_spot06_objects.zar"
#define ZELDA3D_SPOT06_WATER_WORLD_SCALE 1.0f

// Field-keep ZAR (OBJECT_GAMEPLAY_FIELD_KEEP's OoT3D counterpart) — grass05_model.cmb (field
// grass tuft, En_Kusa type 0 / Obj_Mure2 clusters), obj_isi01/obj_ginbure (En_Ishi rocks),
// flower1 (Obj_Hana flower) all live here. Scale is self-calibrated per prop, not a shared
// constant (see sFieldGrassMeas in zelda3d.c).
#define ZKEEP_FIELD "/actor/zelda_field_keep.zar"

// World scale for the OoT3D Gerudo (En_Ge1). FIRST CHARACTER divert: the OoT3D
// model is smooth-skinned and baked UPRIGHT + grounded (cmb_to_c --rotx 180
// --ground), so it drops into the same Translate*RotateY*Scale path as the props
// with no orientation special-casing. The model is ~6358 units tall; 0.011 lands
// it at the N64 Gerudo's height — CONFIRMED in-game A/B (Gerudo Fortress): the
// OoT3D figure is 186 px tall vs the N64 En_Ge1's 187 px head->shadow. Pair with
// ZELDA3D_GELDWOMAN_GROUND_OFFSET (vertical grounding). Re-tune via `scale geldwoman`.
#define ZELDA3D_GELDWOMAN_WORLD_SCALE 0.011f

// Vertical grounding offset for En_Ge1, in MODEL units, applied BEFORE the world
// scale (so it scales together with ZELDA3D_GELDWOMAN_WORLD_SCALE — re-tuning scale
// never desyncs grounding). The skinned ge1_s_wait model sits with its feet ~1000
// model units (≈11 world units at scale 0.011) above the actor origin; this drops
// the feet onto the actor's ground pos. CALIBRATED in-game (REPL `yoff geldwoman`):
// -600 floats, -1000 grounds the soles on the actor's shadow, -1400 sinks to ankles.
// Height itself is correct: at scale 0.011 the model is 186 px tall vs the N64
// En_Ge1's 187 px in the same Gerudo-Fortress shot (head->shadow), so scale is kept.
#define ZELDA3D_GELDWOMAN_GROUND_OFFSET -1000.0f

// Headless verification: when env ZELDA3D_WARP is set, boot straight into the
// debug Select overlay and auto-warp into a scene so pots are reachable without
// scripting title/file-select input. Entrance defaults to Kakariko Village
// (lots of pots); override with env ZELDA3D_ENTRANCE (decimal entrance index).
int Zelda3D_AutoWarpEnabled(void);
int Zelda3D_AutoWarpEntrance(void);
// 1 = cold boot the auto-warp save as a clean NEW game (not the vanilla debug save). env ZELDA3D_COLDBOOT.
int Zelda3D_ColdBoot(void);

// OoT3D get-item replacement, called from GetItem_Draw (the single get-item draw choke).
// When Zelda3D + items are enabled and the drawId has an OoT3D /actor/zelda_gi_*.zar model,
// draws that model at the caller's current matrix and returns 1 so GetItem_Draw skips the
// N64 item DL; returns 0 otherwise (N64 item draws as normal). Covers chest contents,
// held-aloft rewards, shop displays and cutscene items uniformly (all route through here).
int Zelda3D_TryDrawGetItem(PlayState* play, s16 drawId);

// Verification helper, called each frame from Play_Draw (before Zelda3D_EmitRenderPass).
// When env ZELDA3D_SPAWNGI=<gid> is set, draws that get-item in front of Link via the real
// GetItem_Draw path so SOH3D=0 (N64) vs SOH3D=1 (OoT3D) can be A/B'd. No-op otherwise.
void Zelda3D_DebugDrawGetItem(PlayState* play);

// OoT3D Link (player) replacement, called from Player_Draw just before the N64 body draw
// (Player_DrawGameplay). PROOF-OF-HOOK STAGE: when Zelda3D + the link sub-toggle (env
// ZELDA3D_LINK, default OFF) are enabled, draws the OoT3D link_boy/child_new body CMB at the
// player's world transform in BIND POSE and returns 1 so the caller skips the N64 body.
// Returns 0 otherwise (N64 Link draws). Animation (N64-joint retarget) + held equipment are
// the next stage — see scratch/handoff_link.md.
int Zelda3D_TryDrawPlayer(PlayState* play, Actor* actor);

// Verification helper, called each frame from Play_Draw. When env
// ZELDA3D_SPAWNPOT=1, spawns one real Obj_Tsubo beside Link so the actual
// ObjTsubo_Draw path can be A/B'd (SOH3D=0 N64 pot vs SOH3D=1 OoT3D pot) in the
// same scene. No-op otherwise.
void Zelda3D_DebugDrawPot(PlayState* play);

// Verification helper, called each frame from Play_Draw. When env
// ZELDA3D_SPAWNGS=1, spawns one real En_Gs (Gossip Stone) in front of Link so the
// actual EnGs_Draw path runs. SOH3D=0 draws the N64 Gossip Stone, SOH3D=1 the
// OoT3D multi-material one — a true same-scene comparison. Needs OBJECT_GS
// loaded (a scene with Gossip Stones, e.g. the default Kakariko warp). No-op
// otherwise.
void Zelda3D_DebugDrawGs(PlayState* play);

// Verification helper, called each frame from Play_Draw. When env
// ZELDA3D_SPAWNKIBAKO=1, spawns one real Obj_Kibako2 (large wooden crate) in front
// of Link so the actual crate draw path runs (SOH3D=0 N64 crate vs SOH3D=1 OoT3D
// crate). Needs OBJECT_KIBAKO2 loaded (a scene with large crates, e.g. Gerudo
// Valley ENTR 0x117). Logs whether the spawn succeeded. No-op otherwise.
void Zelda3D_DebugDrawKibako(PlayState* play);
void Zelda3D_DebugDrawDrop(PlayState* play);

// OoT3D collision: build a SoH CollisionHeader from the current scene's OoT3D scene-collision
// mesh, or NULL when disabled/unavailable (caller then uses the N64 collision). Called from
// Scene_CommandCollisionHeader at scene load; if it returns non-NULL the engine installs that
// header into play->colCtx via BgCheck_Allocate, so ALL gameplay collision (Link physics,
// floor/wall/ceiling) runs on OoT3D geometry — one geometry for visuals + gameplay. The header
// + arrays are kept resident for the scene lifetime (freed on the next build). Gated by
// Zelda3D_CollisionEnabled() (env ZELDA3D_COLLISION, default ON; REPL `collision`). `n64` is the
// scene's N64 CollisionHeader (its waterboxes + camera regions are copied into the OoT3D header
// since those sub-lists aren't REd yet; pass NULL to skip).
CollisionHeader* Zelda3D_BuildSceneCollision(PlayState* play, CollisionHeader* n64);
int Zelda3D_CollisionEnabled(void);
// SoH sceneNum -> OoT3D scene folder name, or NULL if unmapped. DEFINED in zelda3d.c; exposed here
// (Phase 2 codebase reorg) so zelda3d/scene/zelda3d_collision.cpp can resolve the scene it's
// building collision for.
const char* Zelda3D_SceneName(PlayState* play);

// Interactive REPL poll, called once per frame from Play_Draw. When env
// ZELDA3D_REPL=<fifo path> is set, reads control commands from that FIFO and applies
// them live (tint, world scale, model spawn, on-demand frame dump) so a single
// long-lived headless instance can be poked without a rebuild/restart. No-op when
// ZELDA3D_REPL is unset. Drive it with tools/zelda3d_repl.py.
void Zelda3D_ReplPoll(PlayState* play);

// Zelda3D_WalkInject() declared in input/zelda3d_input.h (moved there, Phase 1 input
// consolidation) — injects the `walkhold`/`btnhold`/ztarget/pause-nav/FP_REPRO state into player
// input. Call from Play_Main right BEFORE Play_Update so the player reads it (input is re-sampled
// each frame). No-op unless one of those harnesses is active.

// Force Link to grab-climb the wall he is flush against (#79/#74 climb repro). Lives in z_player.c
// (needs the static touched-wall flags + the static climb-entry func). Returns 1 grabbed, 0 declined,
// -1 if not touching a wall. REPL `forceclimb`.
s32 Zelda3D_PlayerForceClimb(Player* player, PlayState* play);

// Reliable teleport (#79 repro): snap Link to the raycast floor at (x,z), zero all velocity, and
// force the standing-idle action so he can't slide/void away after a plain `tp`. setYaw!=0 aims him.
// Returns the floor Y used. REPL `tpf x z [yawDeg]`. Lives in z_player.c (needs the idle setup fn).
f32 Zelda3D_PlayerForceTeleport(Player* player, PlayState* play, f32 x, f32 z, s16 yaw, s32 setYaw);

// Action-state injection (#70 roll / #83 talk repro). Drive Link's real player action directly so the
// LIVE pose/blend reproduces headlessly (natural triggers are context-gated). Live in z_player.c.
// REPL `linkstate roll|talk`. ForceTalk returns the nearest-NPC actor id (0 if none within `range`).
s32 Zelda3D_PlayerForceRoll(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceTalk(Player* player, PlayState* play, f32 range);
// Safe reset out of forced talk/roll (avoids the talkActor-null CLOSING crash). REPL `linkstate idle`.
s32 Zelda3D_PlayerForceIdle(Player* player, PlayState* play);

// Discrete-state force helpers for the per-state Link parity sweep (tools/parity_state_sweep.py): each
// installs the real OoT3D action func + the canonical anim for the state, bypassing only the natural
// entry gate (equipment/water/ledge/hit) headless can't satisfy. REPL `linkstate jump|swim|damage|
// shield|attack`. Live in z_player.c (need the static action funcs + anim-group table).
s32 Zelda3D_PlayerForceJump(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceSwim(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceDamage(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceShield(Player* player, PlayState* play);
s32 Zelda3D_PlayerForceAttack(Player* player, PlayState* play);
// Wall-hang (jump_climb) SELECTION force (no wallPoly needed) — exercises the jump_climb->hang map.
s32 Zelda3D_PlayerForceHang(Player* player, PlayState* play);

// 2026-07-15 (6-state Link parity expansion, see docs/link_parity_checklist.md):
// each of these installs the real OoT N64 action func (kept byte-faithful from the community
// decomp base this z_player.c is built from) that OoT3D's own equivalent state machine drives,
// bypassing ONLY the entry gate headless control can't satisfy (repeated timed input, deep-water
// depth, a chest/pickup trigger actor, HP reaching 0) — same "Force*" contract as the existing
// jump/swim/damage/shield/attack helpers above. REPL `linkstate attack2|dive|getitem|death`.
// Combo-swing 2: sets meleeWeaponAnimation to the REAL PLAYER_MWA_FORWARD_COMBO_1H table row
// (D_80854190) that live combo-chain advancement (func_80837818/func_80837948) would itself pick
// after a second well-timed A-press — same CSAB-selection path, entry gate bypassed.
s32 Zelda3D_PlayerForceAttackCombo2(Player* player, PlayState* play);
// Underwater dive: Player_Action_8084DC48 (the SAME action func Player_TryEnteringWater's A-press
// branch installs) + func_8083D330's representative in-dive pose (link_swimer_swim, looping),
// distinct from the surface-tread ForceSwim (link_swimer_swim_wait).
s32 Zelda3D_PlayerForceSwimDive(Player* player, PlayState* play);
// Get-item raised-arm pose: the same Player_SetupWaitForPutAway(func_8083A434) + demo_get_itemB
// sequence func_8083E4C4's caller installs on a real pickup, without needing a live chest/item
// actor interaction headless can't trigger reliably.
s32 Zelda3D_PlayerForceGetItem(Player* player, PlayState* play);
// Death: zeroes gSaveContext.health only — func_8083D53C's existing per-frame HP==0 check (already
// running every Player_Update, see z_player.c ~12246) then drives the REAL death entry
// (func_80836448 -> gPlayerAnim_link_derth_rebirth or the swim/shock variants) with no new hook
// needed; this just supplies the natural trigger condition.
s32 Zelda3D_PlayerForceDeath(Player* player, PlayState* play);

// 2026-07-16 (item_bottle_use/pickup_carry/throw expansion, docs/link_parity_checklist.md): same
// Force* contract — install the REAL OoT N64 action func + canonical anim a real trigger would,
// bypassing only the entry gate headless can't satisfy (a spawned liftable actor / a caught bottle
// item). REPL `linkstate carry|throw|itemuse`.
// Pickup carry-hold: Player_Action_80846050 + PLAYER_ANIMGROUP_carryB — the SAME action+anim the
// real lift path (z_player.c ~5573, Player_SetupWaitForPutAway(func_8083A0F4)'s generic-actor
// branch) installs once the lift animation reaches its grab frame. Skips only the requirement for a
// live interactRangeActor (no liftable spawned headlessly); see zelda3d.c REPL comment for the
// resulting on-frame(4) NULL-interactRangeActor caveat (harmless under the sweep's freeze protocol).
s32 Zelda3D_PlayerForceCarry(Player* player, PlayState* play);
// Throw-release: literally the same call (func_8083EA94: Player_Action_80846578 +
// PLAYER_ANIMGROUP_throw) the real throw branch of Player_ActionHandler_9 (z_player.c ~7486)
// makes once func_8083EAF0 (moving fast enough, or a bomchu) selects THROW over PUT_DOWN. No new
// action-func/anim derivation needed — this just calls the existing internal helper directly.
s32 Zelda3D_PlayerForceThrow(Player* player, PlayState* play);
// Put-down: the PUT_DOWN branch of the same func_8083EAF0 gate (Player_Action_808464B0 +
// PLAYER_ANIMGROUP_put) — the state selected over THROW when the held actor is slow and not
// throw-only. Distinct sweep row from ForceThrow (the else branch). REPL `linkstate putdown`.
s32 Zelda3D_PlayerForcePutDown(Player* player, PlayState* play);
// Item-use (bottle raise/swing): Player_Action_SwingBottle + gPlayerAnim_link_bottle_bug_miss (the
// dry-land entry of sBottleSwingInfo[0]) — the SAME action+anim func_8083C6B8's C-button
// "use held bottle" dispatch (z_player.c ~6564) installs when a bottle is the held item. Forces
// av2.inWater=0 so the deterministic dry-land swing anim is selected (not the water-scoop variant).
s32 Zelda3D_PlayerForceItemUse(Player* player, PlayState* play);

// Backward walk: calls the real func_8083CBF0 directly with a forced dead-behind yawTarget,
// reproducing exactly the state func_8083FC68's -1 branch installs (Player_Action_808423EC +
// gPlayerAnim_link_anchor_back_walk / CSAB ac_back_walk) — see the z_player.c implementation
// comment for why the live input-decoded route (`ztarget` + backward stick) doesn't reliably land
// in this branch headlessly.
s32 Zelda3D_PlayerForceBackwalk(Player* player, PlayState* play);
// Climb traversal (up/down): installs the REAL ladder-traversal action func (Player_Action_8084BF1C,
// via func_8083A3B0 — the identical call func_8083EC18/`forceclimb` makes) and the moving Fclimb_upL/R
// family, bypassing only the yDistToLedge>=79 geometric requirement (no genuine tall wall found near
// a headless spawn). dir>=0 selects the up-playback CSAB; dir<0 flips skelAnime.playSpeed negative,
// which is the SAME anim played in reverse (down), per the real function's own encoding.
s32 Zelda3D_PlayerForceClimbMove(Player* player, PlayState* play, s32 dir);

// ztarget-as-its-own-state query (docs/link_parity_checklist.md "ztarget" row, separate scope
// from the locomotion-gate primitive above): true iff Link's action func is the REAL OoT N64
// Z-hold/standing-aim state (Player_Action_80840450 — decomp ground truth: oot3d-decomp
// docs/player_anim_states.md "Standing-aim / Z-hold (#88-aim), FUN_00488b40"), i.e. the native
// lock-on idle stance entered automatically via func_80839E88/func_80839F90 once `ztarget 1`
// holds a HOSTILE-category focusActor and Link's stick returns to neutral. REPL `ztargetstate`.
s32 Zelda3D_PlayerIsZTargetIdleStance(Player* player);
// Richer variant of the above: func_80839F90 dispatches THREE ways, not one. Returns 0 (none),
// 1 (hostile stance waitR), 2 (hostile stance waitL), 3 (friendly/parallel stance,
// Player_Action_808407CC — a distinct state the bare idle-stance check collapses). REPL `ztargetstate`.
s32 Zelda3D_PlayerZTargetStanceVariant(Player* player);

// Per-frame pose-scan LOGGER (anim QA). Active=on records each drawn player frame's max bone-rotation
// jump + bone + resolved csab + frame into a log the REPL reads back (`posescan on|off|dump`). Sampled
// in the DRAW path (where the pose updates), so it works at normal speed, not under frame-step.
void Zelda3D_PoseScanSetActive(int on);
int Zelda3D_PoseScanCount(void);
float Zelda3D_PoseScanGet(int i, int* bone, float* frame, const char** csab);

// Pose-discontinuity scanner (anim QA). Zelda3D_PoseDiscontinuity returns the largest per-bone rotation
// jump (degrees) between this frame's pose and the previous, plus that bone via outBone — a hard cut /
// missing-morph pop shows as a big value. Zelda3D_LinkModelId returns the player body's modelId (the
// scanner's target). Reset between transitions with Zelda3D_PoseScanReset. REPL `posescan`.
int Zelda3D_LinkModelId(void);
float Zelda3D_PoseDiscontinuity(int modelId, int* outBone);
void Zelda3D_PoseScanReset(int modelId);

// Force the env ZELDA3D_TIME time-of-day into the save context. Call from Play_Init before the
// day/night scene setup layer is chosen, so the loaded actor set matches the forced clock.
void Zelda3D_ApplyForceTime(void);

// #79 diagnostic (DEFINED in zelda3d_link.cpp): groundOff for Link's current cached pose + the resolved
// CSAB name. Compare idle vs a `linkanim`-forced climb clip to see if the climb pose's lowest vertex
// shifts groundOff (the suspected upward-teleport-while-climbing cause). REPL `linkground`.
float Zelda3D_LinkGroundDiag(PlayState* play, const char** outCsab);

// --- Shared internals exposed for zelda3d_link.cpp (the Link policy split out of zelda3d.c) ----------
// These were file-static in zelda3d.c; un-static'd + declared here so zelda3d_link.cpp can call/reference
// them. They remain DEFINED in zelda3d.c (still used by non-Link code there).
void Zelda3D_ReplReply(const char* outPath, const char* fmt, ...);        // REPL reply line (stdout + .out)
void Zelda3D_SceneTint(PlayState* play, u8 out[3]);                       // flat per-scene tint colour
const char* Zelda3D_ResolvePlayerCsab(const char* otr);                  // player anim OTR -> link CSAB base
const char* Zelda3D_LinkWalkRunGate(const char* csab, float speedXZ);    // #117 walk/run selection gate
void Zelda3D_EnsureModelProvider(void);                                   // lazy GL model provider init
int Zelda3D_AutoModelId(const char* zarPath);                            // get-or-alloc a GL model id
float Zelda3D_AutoModelHeight(int modelId);                              // bind-pose local-space Y extent
float Zelda3D_AutoModelMinY(int modelId);                                // bind-pose local-space min Y (feet)
int Zelda3D_ModelGroupCentroid(int modelId, int materialIndex, float out[3]); // centroid of one material's groups
// Bridges for the structured model-REPLACEMENT behaviors (behaviors/actor/<actor>.cpp):
int Zelda3D_TryActorModelDraw(PlayState* play, Actor* actor);            // dispatch actor->behavior->tryDrawModel
int Zelda3D_TryActorDeferredDraw(PlayState* play, Actor* actor);         // prepare skeletal replacement, keep actor Draw
int Zelda3D_ActorDrawSpaceTransform(void* actor, float* outLiftY, float* outLocalOff); // faithful draw-space offset
int Zelda3D_ActorHasBehaviorModule(s16 actorId); // 1 if a behaviors/actor/<x>.cpp module is registered for this id
// Boss_Goma climb-state tooling (REPL `gohmaclimb`, #123): drive Gohma into her REAL wall-climb so the
// draw-space-offset fix can be judged in genuine (not forced) play. Defined in behaviors/actor/boss_goma.cpp.
int Zelda3D_BossGomaForceClimb(Actor* actor, float climbY, int hold); // enter the climb (1=applied)
void Zelda3D_BossGomaClimbTick(Actor* actor);                         // per-frame hold (self-gated)
int Zelda3D_BossGomaClimbHeld(void);                                  // 1 if the hold is active
int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale); // draw OoT3D CMB at actor xform
int Zelda3D_DrawModelTransform(PlayState* play, int modelId, const Vec3f* pos,
                               const Vec3f* rotYXZ, const Vec3f* scale,
                               float postRotX); // arbitrary multipart transform
int Zelda3D_DrawModelBillboard(PlayState* play, int modelId, const Vec3f* pos,
                               const Vec3f* scale); // arbitrary effect-pool sprite
int Zelda3D_BossFd2DrawManeSegment(PlayState* play, Actor* actor, int chain, int segment,
                                  const Vec3f* pos, const Vec3f* rot, const Vec3f* scale);
int Zelda3D_BossFd2ForceGround(Actor* actor);
int Zelda3D_BossFdForceFly(Actor* actor);
int Zelda3D_BossFdForceDeath(Actor* actor, int liveSegments, int actionState);
int Zelda3D_BossFdForceEffects(Actor* actor, int type3ds, int count);
int Zelda3D_BossFdHistoryInfo(Actor* actor, int* bodyLead, int* maneLead, Vec3f* minPos,
                             Vec3f* maxPos);
int Zelda3D_BossFd2ForceIdle(PlayState* play, Actor* actor, int hold);
int Zelda3D_BossFd2ForceDamageState(PlayState* play, Actor* actor, int state);
void Zelda3D_BossFd2IdleTick(PlayState* play, Actor* actor);
int Zelda3D_BossFd2ResolveAnim(PlayState* play, Actor* actor, const char** outCsab,
                              float* outFrame, const char** outMorphCsab, float* outMorphFrame,
                              float* outMorphWeight);
void Zelda3D_UpdateAnimAuthoredMorph(int modelId, const char* inName, float inFrame,
                                    const char* outName, float outFrame, float weight);
// Camera-facing sprite emit at (actor.world.pos + off); uses sun/moon billboardMtxF path.
int Zelda3D_EmitActorBillboard(PlayState* play, int modelId, Actor* actor,
                               float xOff, float yOff, float zOff, float scale,
                               u8 r, u8 g, u8 b, u8 a);
float Zelda3D_GScale(int slot, float def);                              // live REPL `gscale <slot>` or def
// Per-model anim-translation scale for ANIMATED CSAB translation tracks (rig-vs-clip space ratio).
// The player path sets the N64 age root scale (child 0.64 / adult 1.0, z_player_lib.c
// Player_OverrideLimbDrawGameplayDefault; 3DS keeps the literal — FUN_002bc768 DAT_002bc8b8).
void Zelda3D_SetAnimTransScale(int modelId, float scale);
// Pin the root-motion bone's translation to the rig REST value for the components the ENGINE is
// consuming into the actor position this frame (mask bit0/1/2 = x/y/z; mask 0 = no pin). Mirrors
// N64 SkelAnime_UpdateTranslation's `jointTable[0].c = baseTransl.c` (z_skelanime.c:2025-2040):
// without it a clip that moves its actor (ladder climb, ledge vault, door open) has its root
// motion applied TWICE — once by the engine, once by the drawn pose.
void Zelda3D_SetAnimRootPin(int modelId, int boneId, unsigned mask);
// Low-level retarget primitives (DEFINED in zelda3d_model.cpp); forwarded here so link.cpp sees them.
void Zelda3D_SetTrackPosedMinY(int modelId, int enable);                  // per-frame posed-feet grounding
float Zelda3D_PosedGroundOffset(int modelId, unsigned long long midMask); // model-local Y to ground feet
int Zelda3D_PosedBoneWorldPos(int modelId, int boneId, float* outModelPos); // posed bone origin (model-local), #6 held-actor attach
// En_Horse hoof-dust Y reconciliation: given a hoof's ALREADY-COMPUTED native world position
// (ioPos, from z_en_horse.c's own Skin_GetLimbPos), lift/drop its Y onto the OoT3D-warped render
// terrain at the hoof's own XZ (same class of fix as the title tree-grounding commit 36525326) so the
// dust doesn't punch through / float above hill relief the N64 collision mesh never had. Modifies
// ioPos[1] in place. Returns 1 if a correction was applied, 0 if none was needed/available (terrain
// warp inactive, no OoT3D room mesh here, etc — ioPos is left unchanged either way). See
// oot3d-decomp/docs/en_horse_hoof_dust.md and debug_journal/2026-07-15-epona-hoof-dust-depth.md.
// Defined in behaviors/actor/en_horse.cpp.
int Zelda3D_HoofDustWorldPos(PlayState* play, Actor* horseActor, float* ioPos);
// #152 rider seat: actor-relative offset of the DRAWN 3DS horse's rider-attach bone (bone 14),
// through the exact replaced-draw transform. Returns 0 when the horse isn't Zelda3D-drawn (caller
// falls back to the native N64 Skin_GetLimbPos path). Defined in behaviors/actor/en_horse.cpp.
int Zelda3D_HorseSaddleOffset(Actor* horseActor, float out[3]);
void Zelda3D_UpdateAnim(int modelId, const char* animName, float frame);
// Sample an authored CSAB, then replace a contiguous range of animated bone-world matrices with
// explicit 3x4 row-major matrices before skinning. Boss_Fd's OoT3D draw callback uses this for its
// 18 procedural body-history bones; no N64 animation identity or joint table enters this path.
void Zelda3D_UpdateAnimWorldBones(int modelId, const char* animName, float frame, int firstBone,
                                 const float* worldMatrices3x4, int matrixCount);
void Zelda3D_SkinDumpArm(int modelId, const char* path, int frames); // #117 resolved-pose capture
void Zelda3D_UpdateAnimAuto(int modelId, const char* animName, float rate, float n64CurFrame,
                          float n64AnimLength, float morphWeight);
// #85 carry-WALK two-source per-limb blend (lower=loco clip free-run by speed, upper=carry pose).
void Zelda3D_UpdateAnimTwoSource(int modelId, const char* lowerAnim, float lowerRate,
                               const char* upperAnim, float upperCurFrame, float upperAnimLength,
                               const unsigned char* upperMask, int maskCount);
void Zelda3D_GL_SetMidMask(int modelId, unsigned long long mask);
void Zelda3D_GL_EmitPose(int modelId);
// Walk a live N64 skeleton tree, cb per non-root limb (shared with the linkskeldump REPL).
typedef void (*Zelda3D_LimbCb)(int limbIndex, StandardLimb* limb, void* ud);
void Zelda3D_WalkN64Skeleton(void** skeleton, int limbCap, Zelda3D_LimbCb cb, void* ud);

// Non-Link globals referenced by the moved Link code.
extern Actor* gZelda3dSelActor;   // generic actor-control selection (asel)
extern Actor* gZelda3dHideActor; // REPL `ahide`: skip this actor's draw (baked-vs-actor geometry test)
extern s32 gZelda3dActorFreeze;   // generic actor pin mode (afreeze)
extern float gZelda3dAnimRate;    // shared CSAB playback speed
extern float gZelda3dAnimFrame;   // free-running CSAB scrub frame
extern int gZelda3dAnimDebug;     // REPL `animdbg` log gate

// Link toggles/sources — DEFINED in zelda3d_link.cpp; referenced by the menu integration in
// Zelda3D_ReplPoll (zelda3d.c), so exported rather than file-static.
extern int gZelda3dLinkOn;
extern int gZelda3dLinkAnimSrc;
int Zelda3D_LinkEnabled(void);
int Zelda3D_LinkAnimSrc(void);

#ifdef __cplusplus
}
#endif

#endif
