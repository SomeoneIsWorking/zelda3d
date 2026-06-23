// SoH3D — render OoT3D (3DS) models in place of N64 assets.
// See repo-root PROGRESS.md for the overall design.
#ifndef SOH3D_H
#define SOH3D_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

// Returns true when OoT3D-model rendering is enabled (env SOH3D=1). Cached.
int SoH3D_Enabled(void);

// Generalised per-actor divert, called from Actor_Draw for every actor. If SoH3D
// is enabled and the actor's id has an OoT3D model registered in the table, draws
// that model (via the direct-GL path) and returns 1 so the caller skips the actor's
// N64 draw; returns 0 otherwise (caller draws the N64 model as normal). Replaces
// the old per-actor `if (SoH3D_Enabled())` edits in each actor's Draw.
int SoH3D_TryDrawActor(PlayState* play, Actor* actor);

// Pure predicate (no drawing): does this actor have an OoT3D replacement right now? Used by the
// engine draw-distance check so replaced actors keep drawing/updating past the N64 cull distance.
int SoH3D_ActorHasReplacement(PlayState* play, Actor* actor);

// Called from Actor_Draw immediately AFTER an actor's N64 draw (only when
// SoH3D_TryDrawActor returned 0, i.e. the N64 model drew). Closes the auto-scale
// measure bracket opened by SoH3D_TryDrawActor so the SOH3D_AUTO path can measure the
// actor's drawn world size on this frame and derive its OoT3D model scale. No-op unless
// a measurement was opened for this actor.
void SoH3D_AfterActorDraw(PlayState* play, Actor* actor);

// N64-animation port hook, called at the top of the common SkelAnime draw choke points
// (SkelAnime_DrawSkeletonOpa / SkelAnime_DrawSkeleton2). When the actor currently being
// drawn is registered for N64-anim replacement (sModelTable n64anim flag, enabled via
// SOH3D_N64ANIM) this retargets the OoT3D model's skeleton from the live N64 jointTable and
// draws it, returning 1 so the caller SKIPS the N64 limb draw. Returns 0 otherwise (the N64
// skeleton draws as normal — also the fallback for actors whose draw path isn't hooked).
// This is what makes N64-anim replacement generic: no per-actor jointTable accessor needed.
int SoH3D_SkelAnimeDraw(PlayState* play, SkelAnime* skelAnime);

// Record the limb-draw override callback (+arg) the actor passed to its SkelAnime_Draw* call, so
// the N64-anim auto-replace path can replay a PROCEDURAL per-limb rotation the override adds (e.g.
// the cucco wing-flap, which lives in EnNiw_OverrideLimbDraw, not in any animation) onto the
// matching OoT3D bones. `kind`: 0 = OverrideLimbDrawOpa (6 args), 1 = OverrideLimbDraw (7 args).
// Call right before SoH3D_SkelAnimeDraw / ...Raw at each choke point that has an override on hand;
// NULL override clears it. Consumed once per retarget. #23.
void SoH3D_SetLimbOverride(void* overrideFn, void* arg, int kind);

// #5 cucco-flap diagnostic: when set, EnNiw_Update holds every cucco in its agitated wing-spread
// pose (func_80AB5BF8 arg 2) each frame, so the spread flap can be A/B'd deterministically headless
// (N64 model via `enable 0` vs OoT3D replay) to derive/verify the multi-axis wing mapping. REPL
// `cuccopose <0|1>`. The agitated flap drives the wing bones on local X+Y+Z, not just Z like idle.
extern int gSoH3dForceCuccoAgitate;

// #5 cucco WING-STATE machine (the one genuinely cucco-specific control — no generic equivalent):
//   gSoH3dCuccoState : -1 = live AI; >=0 = force func_80AB5BF8(this,play,STATE) every frame on every
//                      cucco (0=calm, 1=mild flap, 2=agitated/held spread, 3, 5=...). REPL
//                      `cuccostate <n|off>`; `cuccopose 1` is the legacy alias for state 2.
// Diagnostic read-back (last cucco drawn this frame) so a captured frame's flap phase is known:
//   gSoH3dCuccoDbgPhase : that cucco's unk_29C (the two-phase flap toggle, 0/1).
//   gSoH3dCuccoDbgWing  : the per-limb binang actually applied — [0..2]=limb7 xyz, [3..5]=limb11 xyz.
// REPL `flapinfo` prints these, so two distinct flap phases can be captured. Hold the cucco STILL
// and FRAME it with the GENERIC actor controls below (`asel 0x19` + `afreeze 1` + `acam`).
extern int gSoH3dCuccoState;
extern int gSoH3dCuccoDbgPhase;
extern short gSoH3dCuccoDbgWing[6];
// gSoH3dCuccoHeld : 1 = force every cucco into the HELD-BY-LINK carried action (func_80AB6BF8) —
//   the frantic body shake (shape.rot ±5000/frame) + feather bursts + wing flap — without needing
//   Link to actually grab it (#9/#6 pickup is broken). REPL `cuccoheld <0|1>`. Pair with `afreeze 2`
//   (position-only freeze) so the body still jitters while the cucco stays framed; `afreeze 1` would
//   pin the rotation and kill the shake.
extern int gSoH3dCuccoHeld;

// Generic actor-control debug surface (works on ANY actor, not just cuccos). One selected actor is
// driven each frame from SoH3D_ActorPostUpdate (called per-actor at the end of Actor_UpdateAll).
//   `asel <id> [n]` / `asel any [n]` : select the n-th nearest live actor of that id (0xHEX or dec)
//                                      to Link (default nearest); captures its current pos/rot.
//   `afreeze <0|1>`   : pin the selected actor's transform every frame (no wander/hop/flee/AI drift).
//   `apos <x y z>`    : set + pin the selected actor's world position.
//   `arot <x y z>`    : set + pin its rotation (binang, x y z).
//   `aparams <v>`     : set actor->params.
//   `acam [dist] [ax]`: frame the selected actor as a side profile (ax 0=+X, 1=+Z; dist default 110).
//   `ainfo`           : dump the selected actor (id, pos, rot, params, velocity, update/draw ptrs).
void SoH3D_ActorPostUpdate(PlayState* play, Actor* actor);

// Raw variant of the N64-anim hook for draw choke points that don't have a SkelAnime* on hand
// (SkelAnime_DrawFlexOpa / SkelAnime_DrawOpa, called directly by many actors). Same effect as
// SoH3D_SkelAnimeDraw; derives limbCount from the skeleton tree. Returns 1 if it drew the OoT3D
// model (caller skips the N64 limbs).
int SoH3D_SkelAnimeDrawRaw(PlayState* play, void** skeleton, Vec3s* jointTable);

// Record the live N64 animation pointer (an OTR path string in SoH) AND the live playhead
// (curFrame / animLength) for the actor currently deferred for replacement, so the auto CSAB
// resolver can map it to the matching OoT3D CSAB and the auto path can PHASE-LOCK the OoT3D CSAB to
// the actor's real N64 tempo. Called from the SkelAnime-bearing draw wrappers (func_80034BA0/CC4)
// whose inner SkelAnime_DrawFlex (the raw hook) has only the skeleton, not the SkelAnime — so
// without this capture those actors (e.g. En_Ko Kokiri kids) never reach the phase-lock branch and
// free-run at the global rate (#76: wrong/frozen anims). No-op when no replacement is pending.
void SoH3D_SetCurAnim(void* animation, float curFrame, float animLength, float morphWeight);

// Generalised per-room scene divert, called from Room_Draw. If SoH3D is enabled and
// the current scene has an OoT3D mapping (kSoH3dSceneNames) with a room CMB for
// room->num, draws that room geometry at the world origin (identity model matrix +
// the game camera, depth-correct via the scene pass) and returns 1 so the caller
// skips the N64 room mesh; returns 0 otherwise (caller draws the N64 room as normal).
int SoH3D_TryDrawRoom(PlayState* play, Room* room);

// Draw the OoT3D sky (BlueSky.zar tenkyu gradient dome) in place of the N64 normal-sky skybox.
// Called from Play_Draw at the skybox point; returns 1 if it drew the OoT3D sky (caller skips the
// N64 SkyboxDraw_Draw), 0 otherwise (caller draws the N64 skybox as normal). #28.
int SoH3D_TryDrawSky(PlayState* play);

// Query form of SoH3D_TryDrawSky (no draw, no side effects): 1 if the SoH3D OoT3D sky dome is
// handling the skybox this frame (so the N64 SkyboxDraw_Draw is bypassed and its sSkyboxDrawMatrix
// is never allocated). Callers MUST then skip the N64 SkyboxDraw_UpdateMatrix to avoid a NULL deref
// (#16 first-person early-load crash). #28.
int SoH3D_SkyActive(PlayState* play);

// Draw the OoT3D sun/moon discs (BlueSky.zar fine_sun.ctxb / fine_moon0.ctxb billboards) in place
// of the N64 Environment_DrawSunAndMoon sprites. Called from Play_Draw at that call site; returns 1
// if it drew the OoT3D sun/moon (caller skips the N64 path), 0 otherwise (caller draws N64). #28e.
int SoH3D_TryDrawSunMoon(PlayState* play);

// Emit the once-per-frame SoH3D render-pass marker (drains all SoH3D draws collected this frame
// in one GL-state-bracketed pass). Call from Play_Draw right after the actor draw-all.
void SoH3D_EmitRenderPass(PlayState* play);
// Per-frame, before the display list is built: drop any SoH3D draws left unrendered from a prior
// frame. Call once per frame ahead of Play_Draw (e.g. alongside the REPL poll).
void SoH3D_FrameBegin(void);

// Floor-height callback: returns the N64 collision floor Y at world (x,z), or a value
// <= -31000 if there is no floor. Provided by soh3d.c (it has the PlayState/colCtx).
typedef float (*SoH3D_FloorFn)(float x, float z);

// Compute & cache an OoT3D scene-room's ground-delta field D(x,z)=N64_floor-OoT3D_floor (the
// render mesh is left untouched; actors are offset by -D to stand on the visible OoT3D
// ground). Idempotent per model. Call from the room-draw hook before the room is drawn.
void SoH3D_ComputeRoomGroundDelta(int modelId, SoH3D_FloorFn floorFn);

// Sample that field: *outD = N64_floor - OoT3D_floor at world (x,z). Returns 1 on success.
int SoH3D_RoomGroundDeltaAt(int modelId, float x, float z, float* outD);

// OoT3D render-mesh floor at (x,z) for a scene room, the floor hit closest to `target`.
// Returns 1 + *outY on a hit. Grounds actors exactly on the visible OoT3D ground (exact, no
// grid approximation; XZ-bbox-culled so per-actor-per-frame is cheap).
int SoH3D_RoomOoT3DFloorAt(int modelId, float x, float z, float target, float* outY);

// Render-Y offset for an actor so it stands on the visible OoT3D ground instead of floating
// at the N64 collision height (= OoT3D_ground - N64_ground at the actor's XZ, i.e. -D).
// Returns 0 when SoH3D is off, the scene has no OoT3D room, or no delta covers the actor.
// Called from Actor_Draw: add to actor->world.pos.y around the draw, then subtract (physics
// stays N64). The inverse of the old render warp (which distorted the mesh).
float SoH3D_ActorRenderYOffset(PlayState* play, Actor* actor);

// Query the (warped) OoT3D room render-mesh floor Y at world (x,z). Returns 1 + *outY
// on a floor hit, else 0. For verifying the warp aligned the drawn ground to N64.
int SoH3D_RoomMeshFloorAt(int modelId, float x, float z, float* outY);

// #5 — real stepped-polygon stairs: replace OoT3D fake-flat "kaidan" ramps with actual
// treads+risers. SetStairs evicts cached scene-room models so the scene rebuilds (live A/B).
void SoH3D_SetStairs(int on);
int SoH3D_GetStairs(void);
void SoH3D_SetStairRiserY(float v); // generated step rise (world-units/step); larger = bigger steps
float SoH3D_GetStairRiserY(void);

// #32 — Xbox face-button HUD glyphs. Returns a persistent RGBA8888 (G_IM_FMT_RGBA/32b) buffer for
// the glyph 'A'/'B'/'X'/'Y' (case-insensitive) + its dims, or NULL on failure. The in-game Fast3D
// HUD draws this raw pointer in place of the shared N64 button circle when gSoH3dXboxBtn is set.
const void* SoH3D_XboxGlyphTex(char which, int* w, int* h);
extern int gSoH3dXboxBtn;       // env SOH3D_XBOXUI / REPL `xboxui` gate (-1=uninit, 0/1)
int SoH3D_XboxBtnEnabled(void); // lazily resolves the env on first call; HUD draws gate on this

// #32 hotswap — keyboard-key HUD glyphs. Returns a persistent RGBA8888 buffer for the key glyph
// for the given HUD slot: 'B'=B-button (C key), 'X'=C-Left (←), 'Y'=C-Down (↓), 'A'=C-Right (→).
// Same 64x64 dims as SoH3D_XboxGlyphTex. The draw path is identical; only the texture changes.
const void* SoH3D_KbdGlyphTex(char which, int* w, int* h);

// #32 hotswap — last-used input device. 0 = gamepad (Xbox glyphs), 1 = keyboard (key-label glyphs).
// Updated from the C++ LUS input layer (Ship::Controller::ProcessKeyboardEvent for keyboard events,
// LUS::Controller::ReadToOSContPad for gamepad events). The HUD reads this each frame to pick the
// glyph set. -1 = uninitialized (lazily resolved from SOH3D_INPUTDEV env on first call).
// REPL `inputdev <0|1>` overrides for testing. SoH3D_InputDevice() is the lazily-initialized getter.
extern int gSoH3dInputDevice;
int SoH3D_InputDevice(void);

// #31 — crisp higher-res HUD textures (hearts first). Returns a persistent RGBA8888
// (G_IM_FMT_RGBA/32b) buffer for a heart kind + its dims, or NULL on failure. The buffer is
// grayscale (rgb=intensity, a=silhouette) so the N64 heart combine ((PRIM-ENV)*TEXEL0+ENV) tints
// it exactly like the original IA8 heart — just at a much higher resolution. The Fast3D HUD draws
// this raw pointer in place of gHeart{Full,ThreeQuarter,Half,Quarter,Empty}Tex when gSoH3dHudTex.
enum {
    SOH3D_HEART_FULL = 0,
    SOH3D_HEART_THREEQUARTER,
    SOH3D_HEART_HALF,
    SOH3D_HEART_QUARTER,
    SOH3D_HEART_EMPTY,
};
const void* SoH3D_HeartTex(int kind, int* w, int* h);
extern int gSoH3dHudTex;       // env SOH3D_HUDTEX / REPL `hudtex` gate (-1=uninit, 0/1)
int SoH3D_HudTexEnabled(void); // lazily resolves the env on first call; HUD draws gate on this

// #31 — crisp HUD counter font. `glyph` 0..9 = the digit, 10 = ':'. Returns a persistent RGBA32
// (grayscale, a=coverage) glyph + dims, or NULL. The counter combine is colour=PRIM, alpha=TEXEL0,
// so the grayscale glyph reproduces the N64 8x16 I8 digit at higher resolution (see Gfx_TextureI8).
const void* SoH3D_DigitTex(int glyph, int* w, int* h);

// #31 — crisp HUD button-background disc (the round beveled circle behind the B / C / A action
// buttons). Returns a persistent RGBA32 (grayscale: rgb=bevel intensity, a=circle coverage) + dims,
// or NULL. The button combine is G_CC_MODULATEIA_PRIM (out.rgb=TEXEL0.rgb*PRIM, out.a=TEXEL0.a*PRIM),
// so the grayscale disc tints to each button's PRIM colour exactly like the N64 32x32 IA8 original.
const void* SoH3D_ButtonBgTex(int* w, int* h);

// #31 — crisp HUD counter icons. `kind` 0=rupee gem, 1=small key, 2=clock. Returns a persistent
// RGBA32 (grayscale, a=coverage) + dims, or NULL. The rupee/key draw MODULATEIA_PRIM (PRIM tints
// the facet shading); the clock draws MODULATERGBA_PRIM with PRIM white (grayscale shown directly).
// Substituted by pointer in Gfx_TextureIA8 for the N64 16x16 IA8 gRupee/SmallKey/Clock icons.
enum {
    SOH3D_CICON_RUPEE = 0,
    SOH3D_CICON_SMALLKEY,
    SOH3D_CICON_CLOCK,
};
const void* SoH3D_CounterIconTex(int kind, int* w, int* h);

// #2 — press-to-skip: on a Start/Space press, force-end any active onepoint cutscene camera
// (door/attention/treasure pans) so it hands control back. Call once per frame from Play_Update
// before the camera update loop. Gate env SOH3D_SKIP (default on) / REPL `skip <0|1>`.
extern int gSoH3dSkip;

// Frame-step harness (transient-capture tooling, e.g. #86 walk-stop snap, #80 boulder spin). When
// gSoH3dFreeze!=0 the per-frame Play_Update in z_play.c is skipped, holding the game logic still while
// the REPL + Play_Draw keep running (so dumped frames stay stable). REPL `step [n]` advances exactly n
// logic ticks on demand, letting `dumpframe` capture every single game frame of a brief transient.
extern int gSoH3dFreeze;
int SoH3D_SkipEnabled(void);
void SoH3D_SkipControlTakers(PlayState* play);

// World scale for the OoT3D pot (OoT3D model units -> N64 world units). Tuned by
// matching the rendered height of the OoT3D pot to the N64 pot at the same spot
// (spawn comparison, Deku Tree). The OoT3D model is ~162 units tall; ~0.12 lands
// it at the N64 pot's height. See PROGRESS.md calibration.
#define SOH3D_POT_WORLD_SCALE 0.12f

// World scale for the OoT3D large wooden crate (Obj_Kibako2). Model is ~600 units
// wide; calibrated against the N64 large crate via the SOH3D_SPAWNKIBAKO A/B spawn
// in Gerudo Valley (ENTR 0x117=279). Third object proving the table-driven divert:
// adding it was one sModelTable[] row + this macro + the generated include.
#define SOH3D_KIBAKO_WORLD_SCALE 0.10f

// World scale for the OoT3D field bush reused for Obj_Hana's bush variant (params&3==2),
// the cuttable shrub the grass-cutting Kokiri picks. Same kusa model as En_Kusa (glModelId 2);
// starts at the kusa scale and is fine-tuned against the N64 Obj_Hana bush. REPL `scale kusa`.
#define SOH3D_HANABUSH_WORLD_SCALE 0.5f

// World scales for the field-keep props (En_Ishi rocks, Obj_Hana flower) reused from
// zelda_field_keep.zar. Starting estimates against the N64 actor scale (rock 0.1/0.4,
// flower 0.01); fine-tune live with REPL `scale rock_s|rock_l|flower`.
#define SOH3D_ROCK_SMALL_WORLD_SCALE 0.12f  // calibrated vs N64 in Kokiri Forest
#define SOH3D_ROCK_LARGE_WORLD_SCALE 0.12f  // UNCALIBRATED: no silver rocks in Kokiri Forest yet
#define SOH3D_FLOWER_WORLD_SCALE 0.12f      // UNCALIBRATED: no field flowers in Kokiri Forest yet

// Kakariko well + windmill (Bg_Spot01_Fusya / _Idohashira / _Idomizu), all from one shared ZAR
// coordinate space (zelda_spot01_objects.zar). Seeded from the auto-derived per-object scale
// (n64h 130 / fusya CMB 10255 ~= 0.0127). Tune live with REPL `gscale 7|8|9 <f>`.
#define ZSPOT01 "/actor/zelda_spot01_objects.zar"
#define SOH3D_SPOT01_WORLD_SCALE 0.01268f

// Kakariko DM-trail gate (Bg_Gate_Shutter, OBJECT_SPOT01_MATOYAB). It shares
// zelda_spot01_matoyab.zar with the windmill mechanism (c_matoate_before), but the two CMBs are
// authored at DIFFERENT unit scales (matoate ~1402 units tall, the gate c_s01tomegate ~111), so the
// gate needs its OWN scale — calibrated to 1.4 in-game (gate fills the DM-trail archway). REPL
// `gscale 10 <f>`.
#define ZMATOYAB "/actor/zelda_spot01_matoyab.zar"
#define SOH3D_MATOYAB_WORLD_SCALE 1.4f

// World scale for the OoT3D Gerudo (En_Ge1). FIRST CHARACTER divert: the OoT3D
// model is smooth-skinned and baked UPRIGHT + grounded (cmb_to_c --rotx 180
// --ground), so it drops into the same Translate*RotateY*Scale path as the props
// with no orientation special-casing. The model is ~6358 units tall; 0.011 lands
// it at the N64 Gerudo's height — CONFIRMED in-game A/B (Gerudo Fortress): the
// OoT3D figure is 186 px tall vs the N64 En_Ge1's 187 px head->shadow. Pair with
// SOH3D_GELDWOMAN_GROUND_OFFSET (vertical grounding). Re-tune via `scale geldwoman`.
#define SOH3D_GELDWOMAN_WORLD_SCALE 0.011f

// Vertical grounding offset for En_Ge1, in MODEL units, applied BEFORE the world
// scale (so it scales together with SOH3D_GELDWOMAN_WORLD_SCALE — re-tuning scale
// never desyncs grounding). The skinned ge1_s_wait model sits with its feet ~1000
// model units (≈11 world units at scale 0.011) above the actor origin; this drops
// the feet onto the actor's ground pos. CALIBRATED in-game (REPL `yoff geldwoman`):
// -600 floats, -1000 grounds the soles on the actor's shadow, -1400 sinks to ankles.
// Height itself is correct: at scale 0.011 the model is 186 px tall vs the N64
// En_Ge1's 187 px in the same Gerudo-Fortress shot (head->shadow), so scale is kept.
#define SOH3D_GELDWOMAN_GROUND_OFFSET -1000.0f

// Headless verification: when env SOH3D_WARP is set, boot straight into the
// debug Select overlay and auto-warp into a scene so pots are reachable without
// scripting title/file-select input. Entrance defaults to Kakariko Village
// (lots of pots); override with env SOH3D_ENTRANCE (decimal entrance index).
int SoH3D_AutoWarpEnabled(void);
int SoH3D_AutoWarpEntrance(void);
// 1 = cold boot the auto-warp save as a clean NEW game (not the vanilla debug save). env SOH3D_COLDBOOT.
int SoH3D_ColdBoot(void);

// OoT3D get-item replacement, called from GetItem_Draw (the single get-item draw choke).
// When SoH3D + items are enabled and the drawId has an OoT3D /actor/zelda_gi_*.zar model,
// draws that model at the caller's current matrix and returns 1 so GetItem_Draw skips the
// N64 item DL; returns 0 otherwise (N64 item draws as normal). Covers chest contents,
// held-aloft rewards, shop displays and cutscene items uniformly (all route through here).
int SoH3D_TryDrawGetItem(PlayState* play, s16 drawId);

// Verification helper, called each frame from Play_Draw (before SoH3D_EmitRenderPass).
// When env SOH3D_SPAWNGI=<gid> is set, draws that get-item in front of Link via the real
// GetItem_Draw path so SOH3D=0 (N64) vs SOH3D=1 (OoT3D) can be A/B'd. No-op otherwise.
void SoH3D_DebugDrawGetItem(PlayState* play);

// OoT3D Link (player) replacement, called from Player_Draw just before the N64 body draw
// (Player_DrawGameplay). PROOF-OF-HOOK STAGE: when SoH3D + the link sub-toggle (env
// SOH3D_LINK, default OFF) are enabled, draws the OoT3D link_boy/child_new body CMB at the
// player's world transform in BIND POSE and returns 1 so the caller skips the N64 body.
// Returns 0 otherwise (N64 Link draws). Animation (N64-joint retarget) + held equipment are
// the next stage — see scratch/handoff_link.md.
int SoH3D_TryDrawPlayer(PlayState* play, Actor* actor);

// Verification helper, called each frame from Play_Draw. When env
// SOH3D_SPAWNPOT=1, spawns one real Obj_Tsubo beside Link so the actual
// ObjTsubo_Draw path can be A/B'd (SOH3D=0 N64 pot vs SOH3D=1 OoT3D pot) in the
// same scene. No-op otherwise.
void SoH3D_DebugDrawPot(PlayState* play);

// Verification helper, called each frame from Play_Draw. When env
// SOH3D_SPAWNGS=1, spawns one real En_Gs (Gossip Stone) in front of Link so the
// actual EnGs_Draw path runs. SOH3D=0 draws the N64 Gossip Stone, SOH3D=1 the
// OoT3D multi-material one — a true same-scene comparison. Needs OBJECT_GS
// loaded (a scene with Gossip Stones, e.g. the default Kakariko warp). No-op
// otherwise.
void SoH3D_DebugDrawGs(PlayState* play);

// Verification helper, called each frame from Play_Draw. When env
// SOH3D_SPAWNKIBAKO=1, spawns one real Obj_Kibako2 (large wooden crate) in front
// of Link so the actual crate draw path runs (SOH3D=0 N64 crate vs SOH3D=1 OoT3D
// crate). Needs OBJECT_KIBAKO2 loaded (a scene with large crates, e.g. Gerudo
// Valley ENTR 0x117). Logs whether the spawn succeeded. No-op otherwise.
void SoH3D_DebugDrawKibako(PlayState* play);
void SoH3D_DebugDrawDrop(PlayState* play);

// OoT3D collision: build a SoH CollisionHeader from the current scene's OoT3D scene-collision
// mesh, or NULL when disabled/unavailable (caller then uses the N64 collision). Called from
// Scene_CommandCollisionHeader at scene load; if it returns non-NULL the engine installs that
// header into play->colCtx via BgCheck_Allocate, so ALL gameplay collision (Link physics,
// floor/wall/ceiling) runs on OoT3D geometry — one geometry for visuals + gameplay. The header
// + arrays are kept resident for the scene lifetime (freed on the next build). Gated by
// SoH3D_CollisionEnabled() (env SOH3D_COLLISION, default ON; REPL `collision`). `n64` is the
// scene's N64 CollisionHeader (its waterboxes + camera regions are copied into the OoT3D header
// since those sub-lists aren't REd yet; pass NULL to skip).
CollisionHeader* SoH3D_BuildSceneCollision(PlayState* play, CollisionHeader* n64);
int SoH3D_CollisionEnabled(void);

// Interactive REPL poll, called once per frame from Play_Draw. When env
// SOH3D_REPL=<fifo path> is set, reads control commands from that FIFO and applies
// them live (tint, world scale, model spawn, on-demand frame dump) so a single
// long-lived headless instance can be poked without a rebuild/restart. No-op when
// SOH3D_REPL is unset. Drive it with tools/soh3d_repl.py.
void SoH3D_ReplPoll(PlayState* play);

// Inject the `walkhold` REPL control-stick value into player input. Call from Play_Main right
// BEFORE Play_Update so the player reads it (input is re-sampled each frame). No-op unless active.
void SoH3D_WalkInject(PlayState* play);

// Force Link to grab-climb the wall he is flush against (#79/#74 climb repro). Lives in z_player.c
// (needs the static touched-wall flags + the static climb-entry func). Returns 1 grabbed, 0 declined,
// -1 if not touching a wall. REPL `forceclimb`.
s32 SoH3D_PlayerForceClimb(Player* player, PlayState* play);

// Reliable teleport (#79 repro): snap Link to the raycast floor at (x,z), zero all velocity, and
// force the standing-idle action so he can't slide/void away after a plain `tp`. setYaw!=0 aims him.
// Returns the floor Y used. REPL `tpf x z [yawDeg]`. Lives in z_player.c (needs the idle setup fn).
f32 SoH3D_PlayerForceTeleport(Player* player, PlayState* play, f32 x, f32 z, s16 yaw, s32 setYaw);

// Action-state injection (#70 roll / #83 talk repro). Drive Link's real player action directly so the
// LIVE pose/blend reproduces headlessly (natural triggers are context-gated). Live in z_player.c.
// REPL `linkstate roll|talk`. ForceTalk returns the nearest-NPC actor id (0 if none within `range`).
s32 SoH3D_PlayerForceRoll(Player* player, PlayState* play);
s32 SoH3D_PlayerForceTalk(Player* player, PlayState* play, f32 range);
// Safe reset out of forced talk/roll (avoids the talkActor-null CLOSING crash). REPL `linkstate idle`.
s32 SoH3D_PlayerForceIdle(Player* player, PlayState* play);

// Per-frame pose-scan LOGGER (anim QA). Active=on records each drawn player frame's max bone-rotation
// jump + bone + resolved csab + frame into a log the REPL reads back (`posescan on|off|dump`). Sampled
// in the DRAW path (where the pose updates), so it works at normal speed, not under frame-step.
void SoH3D_PoseScanSetActive(int on);
int SoH3D_PoseScanCount(void);
float SoH3D_PoseScanGet(int i, int* bone, float* frame, const char** csab);

// Pose-discontinuity scanner (anim QA). SoH3D_PoseDiscontinuity returns the largest per-bone rotation
// jump (degrees) between this frame's pose and the previous, plus that bone via outBone — a hard cut /
// missing-morph pop shows as a big value. SoH3D_LinkModelId returns the player body's modelId (the
// scanner's target). Reset between transitions with SoH3D_PoseScanReset. REPL `posescan`.
int SoH3D_LinkModelId(void);
float SoH3D_PoseDiscontinuity(int modelId, int* outBone);
void SoH3D_PoseScanReset(int modelId);

// Force the env SOH3D_TIME time-of-day into the save context. Call from Play_Init before the
// day/night scene setup layer is chosen, so the loaded actor set matches the forced clock.
void SoH3D_ApplyForceTime(void);

// #79 diagnostic (DEFINED in soh3d_link.cpp): groundOff for Link's current cached pose + the resolved
// CSAB name. Compare idle vs a `linkanim`-forced climb clip to see if the climb pose's lowest vertex
// shifts groundOff (the suspected upward-teleport-while-climbing cause). REPL `linkground`.
float SoH3D_LinkGroundDiag(PlayState* play, const char** outCsab);

// --- Shared internals exposed for soh3d_link.cpp (the Link policy split out of soh3d.c) ----------
// These were file-static in soh3d.c; un-static'd + declared here so soh3d_link.cpp can call/reference
// them. They remain DEFINED in soh3d.c (still used by non-Link code there).
void SoH3D_ReplReply(const char* outPath, const char* fmt, ...);        // REPL reply line (stdout + .out)
void SoH3D_SceneTint(PlayState* play, u8 out[3]);                       // flat per-scene tint colour
const char* SoH3D_ResolvePlayerCsab(const char* otr);                  // player anim OTR -> link CSAB base
void SoH3D_EnsureModelProvider(void);                                   // lazy GL model provider init
int SoH3D_AutoModelId(const char* zarPath);                            // get-or-alloc a GL model id
// Low-level retarget primitives (DEFINED in soh3d_model.cpp); forwarded here so link.cpp sees them.
void SoH3D_SetTrackPosedMinY(int modelId, int enable);                  // per-frame posed-feet grounding
float SoH3D_PosedGroundOffset(int modelId, unsigned long long midMask); // model-local Y to ground feet
int SoH3D_PosedBoneWorldPos(int modelId, int boneId, float* outModelPos); // posed bone origin (model-local), #6 held-actor attach
void SoH3D_UpdateAnim(int modelId, const char* animName, float frame);
void SoH3D_UpdateAnimAuto(int modelId, const char* animName, float rate, float n64CurFrame,
                          float n64AnimLength, float morphWeight);
void SoH3D_GL_SetMidMask(int modelId, unsigned long long mask);
void SoH3D_GL_EmitPose(int modelId);
// Walk a live N64 skeleton tree, cb per non-root limb (shared with the linkskeldump REPL).
typedef void (*SoH3D_LimbCb)(int limbIndex, StandardLimb* limb, void* ud);
void SoH3D_WalkN64Skeleton(void** skeleton, int limbCap, SoH3D_LimbCb cb, void* ud);

// Non-Link globals referenced by the moved Link code.
extern Actor* gSoH3dSelActor;   // generic actor-control selection (asel)
extern s32 gSoH3dActorFreeze;   // generic actor pin mode (afreeze)
extern float gSoH3dAnimRate;    // shared CSAB playback speed
extern float gSoH3dAnimFrame;   // free-running CSAB scrub frame
extern int gSoH3dAnimDebug;     // REPL `animdbg` log gate

// Link toggles/sources — DEFINED in soh3d_link.cpp; referenced by the menu integration in
// SoH3D_ReplPoll (soh3d.c), so exported rather than file-static.
extern int gSoH3dLinkOn;
extern int gSoH3dLinkAnimSrc;
int SoH3D_LinkEnabled(void);
int SoH3D_LinkAnimSrc(void);

// ---- PC HUD (RmlUi replacement for the N64 Fast3D HUD) ------------------------------------
// Gate: 1 = PC HUD active (default ON); env SOH3D_PCHUD=0 disables it (falls back to N64 HUD).
// When enabled, Interface_Draw and HealthMeter_Draw are skipped and SoH3D_HudUpdateFrame fills
// gSoH3dHudState each frame for SohRmlUi to render.
extern int gSoH3dPcHud;            // -1=uninit, 0=off, 1=on
int SoH3D_PcHudEnabled(void);      // lazily resolves env, then returns gSoH3dPcHud

// Snapshot of game HUD state for the RmlUi pass. Filled by SoH3D_HudUpdateFrame() once per
// frame (soh3d.c), consumed by SohRmlUi::UpdateHud() (libultraship). The struct is pure POD
// so it crosses the C/C++ boundary without issues.
// buttonItems[0]=B, [1]=C-Left, [2]=C-Down, [3]=C-Right (index into gItemIcons names, = item id).
// ITEM_NONE (0xFF) = empty slot.
typedef struct {
    int     health;           // current health in quarter-hearts (gSaveContext.health)
    int     healthCapacity;   // max health in quarter-hearts (gSaveContext.healthCapacity)
    int     magic;            // current magic 0..magicCapacity (gSaveContext.magic)
    int     magicCapacity;    // max magic (gSaveContext.magicCapacity; 0 if no magic)
    int     magicLevel;       // 0=none, 1=normal, 2=double (gSaveContext.magicLevel)
    int     rupees;           // current rupee count (gSaveContext.rupees)
    int     buttonItems[4];   // item ids for B/C-L/C-D/C-R (0xFF=empty)
    int     inputDevice;      // 0=gamepad, 1=keyboard (gSoH3dInputDevice)
    int     valid;            // 1 when this struct has been filled at least once
} SoH3dHudState;

extern SoH3dHudState gSoH3dHudState;

// Called once per frame from SoH3D_ReplPoll (has PlayState) to snapshot game state into
// gSoH3dHudState. SohRmlUi reads the snapshot and updates the RML document elements.
void SoH3D_HudUpdateFrame(PlayState* play);

#ifdef __cplusplus
}
#endif

#endif
