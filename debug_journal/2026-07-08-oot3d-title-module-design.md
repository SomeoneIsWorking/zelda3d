# OoT3D title screen as a first-class module — architecture + migration plan

Status: DESIGN ONLY. No code changed. This document is the blueprint for follow-up agents to
execute against; it does not itself close any task.

## 0. Problem statement (recap)

The 3DS title is currently reconstructed by hijacking SoH's ordinary `spot00` (Hyrule Field)
`Play` state and layering N64-title-cs overrides on top of it, all keyed off one global flag,
`gZelda3dInTitleDemo`, set/cleared from a single function, `Zelda3D_ApplyTitleCam` (in
`Shipwright/soh/src/zelda3d/zelda3d.c`), called once per frame from `Zelda3D_ReplPoll`. Every
piece of the title presentation — camera, lighting, sky, rider, logo — is an independent
`if (gZelda3dInTitleDemo)` bolt-on scattered through `zelda3d.c` / `zelda3d_cutscene.cpp` /
`behaviors/title/title_logo.cpp`, each patched in isolation as divergences were found. There is
no single owner of "the 3DS title" as a presentation; there's a flag and a pile of readers.

Consequences of the current shape (motivating the pivot):
- No single place to reason about "what does the title look like at frame N" — the answer is
  spread across ~6 functions with independent entry/exit logic that must each reimplement the
  guard (`Zelda3D_TitleCamEnabled() && !AutoWarpEnabled() && sceneNum==SCENE_HYRULE_FIELD`).
- The cs cursor (`sFrame` in `zelda3d_cutscene.cpp`) is advanced by whichever caller happens to
  call `Zelda3D_TitleCsAdvance()`, but consumers (camera, rider, dayTime, lighting) each
  independently call `Zelda3D_TitleCsFrame()` at their own call site — nothing guarantees they
  observe the same frame value within one game-frame if the order of calls changes.
- `title_logo.cpp` had to explicitly step outside the project's own `ActorBehavior` registry
  because there's no live actor to key off (`En_Mag` doesn't spawn under the spot00 hijack) —
  a sign the *actor-id-keyed* dispatch pattern doesn't fit title's needs, and title needs its
  own first-class module with its own lifecycle, not another entry bolted onto an actor-shaped
  registry.
- A wrong asset (`common_bg01.ctxb` "bg-card overlay") was committed and had to be retracted
  (`debug_journal/2026-07-08-title-overlay-wrong-asset-RETRACTION.md`) partly because there was
  no single reviewable surface for "everything the title draws" — it's easy to bolt on one more
  unreviewed draw call when the pattern already is one more bolt-on function.

## 1. Module: responsibilities + interface

### Name and location

`Zelda3D::Title` — class `TitlePresentation`, in a new dedicated directory:

```
Shipwright/soh/src/zelda3d/behaviors/title/
    title_presentation.h       // class TitlePresentation, public API
    title_presentation.cpp     // update()/draw() driver, owns lifecycle + per-frame state
    title_logo.cpp             // KEEPS drawing the logo; becomes a component the driver calls
    title_logo.h               // (new) expose a proper draw(frame, camBasis) entry point
```

This mirrors the existing `behaviors/actor/*.cpp` convention (one file per concern) but under
`behaviors/title/` since title is not actor-id-dispatched. `zelda3d_cutscene.cpp`
(the spot99 cs parser/evaluator) stays where it is — it's a *data* layer the module consumes,
not part of the module itself (see §5).

### Class shape

```cpp
namespace Zelda3D {

// One title-cs-frame's worth of fully-resolved presentation state.
// Every downstream system (camera, lighting, rider, sky, logo) reads from
// THIS struct, not from ad-hoc per-function TitleCs* calls. Computed once
// per frame by TitlePresentation::update().
struct TitleFrameState {
    int      csFrame;
    f32      dayTime;          // resolved 0..0x10000 daytime for this frame
    Vec3f    eye, at, up;       // resolved camera basis (already LH->RH converted)
    f32      fov;
    EnvLightSettings light;     // resolved ambient/light1/light2/fog (already blended)
    Vec3f    riderPos;          // resolved world position for the mounted pair
    f32      riderYaw;
    bool     riderCueDiscontinuity; // shot-cut flag, so Player/Epona can teleport not lerp
    int      skyDomeVariant;    // resolved skybox1Index/skybox2Index/blend inputs
};

class TitlePresentation {
public:
    static TitlePresentation& Instance();

    // Entry/exit — replaces the guard duplicated in every current override.
    bool shouldBeActive(PlayState* play) const;   // camera-enabled && !autowarp && scene==field
    bool isActive() const { return mActive; }
    void enter(PlayState* play);   // one-time setup: load cs, save+disable N64 lighting, reset cursor
    void exit(PlayState* play);    // one-time teardown: restore lighting, reset rider cue state

    // Per-frame driver — the ONE call site from the title path (see §1.3).
    void update(PlayState* play);  // advances cs cursor, resolves TitleFrameState, applies
                                    // camera+envCtx+rider transform+sky selection for this frame
    void draw(PlayState* play);    // draws logo + (future) fire-glow + copyright + fade

    const TitleFrameState& frame() const { return mFrame; }

private:
    bool mActive = false;
    TitleFrameState mFrame{};
    // ...saved-lighting-state, rider cue cache, etc. (today's module-level statics in
    // zelda3d.c, moved here as members)
};

} // namespace Zelda3D

// C bridge for callers still in zelda3d.c / z_kankyo etc:
extern "C" {
    int  Zelda3D_Title_Update(PlayState* play);  // returns 1 if title took over this frame
    int  Zelda3D_Title_Draw(PlayState* play);
    int  Zelda3D_Title_IsActive(void);           // replaces direct gZelda3dInTitleDemo reads
}
```

Key design choices and why:

- **`TitleFrameState` is the seam.** Today, camera/lighting/rider/sky/dayTime each independently
  call into `zelda3d_cutscene.cpp` and re-derive their slice of state from `csFrame`, so nothing
  guarantees they agree on which frame they're looking at if call order changes, and there's no
  single place to log/dump/diff "the title's state at frame N" for a harness A/B (which is
  exactly what `tools/title_ab.py` had to reconstruct externally, piecemeal). Resolving
  everything once in `update()` and handing every consumer the same immutable struct makes the
  presentation frame-coherent by construction and gives the harness one struct to dump.
- **`update()` vs `draw()` split** mirrors the project's existing `applyDrawOverrides` /
  `tryDrawModel` split in `actor_behavior.h` and the general engine `Update`/`Draw` phase split
  — `update()` runs where `Zelda3D_ApplyTitleCam` runs today (per-frame, pre-draw), `draw()` runs
  where `Zelda3D_TryDrawTitleLogo`/`Zelda3D_TryDrawSky`/`Zelda3D_TryDrawSunMoon` run today
  (`Play_DrawOverlayElements`-adjacent).
- **`gZelda3dInTitleDemo` becomes `Zelda3D_Title_IsActive()`**, a read-only query on the
  singleton, not a free-floating global any function can set. Only `enter()`/`exit()` (called
  from inside `update()`'s own guard check) flip `mActive`.
- **No dependency on `ActorBehavior`.** Title doesn't key off `actor->id`; the existing registry
  pattern is *the* other example of a first-class module in this codebase, so the shape (a base
  interface + owning driver + explicit call sites replacing scattered ifs) is copied, but the
  dispatch key is different (game-state phase, not actor id) so it does not literally register
  into `findActorBehavior`.

### 1.3 Invocation from the title path

Two call sites replace the current scattered ones:

1. **Update** — in `Zelda3D_ReplPoll` (`zelda3d.c` ~line 7230), replace:
   ```c
   if (!Zelda3D_ApplyTitleCam(play)) { Zelda3D_ReconcileCutsceneCam(play); }
   ```
   with:
   ```c
   if (!Zelda3D_Title_Update(play)) { Zelda3D_ReconcileCutsceneCam(play); }
   ```
   `Zelda3D_Title_Update` internally does: `shouldBeActive()` check → `enter()`/`exit()` on edge
   → `TitleCsAdvance()` → resolve `TitleFrameState` → apply camera to `play->view`/active
   `Camera*` → apply `envCtx.lightSettings` → apply rider transform to `ACTOR_PLAYER`/Epona →
   set sky enable flags. Returns whether title was active this frame (same contract as today's
   `Zelda3D_ApplyTitleCam` return value).

2. **Draw** — wherever `Zelda3D_TryDrawSky` / `Zelda3D_TryDrawSunMoon` /
   `Zelda3D_TryDrawTitleLogo` are currently called from the overlay-draw path, replace the three
   separate call sites with one `Zelda3D_Title_Draw(play)` that internally sequences: sky dome →
   sun/moon → logo → (future) fire-glow → (future) copyright → (future) fade. This makes draw
   *order* an explicit, reviewable sequence in one function instead of implicit call-site order
   in whatever file happens to invoke each `TryDraw*`.

`Zelda3D_TitleLightSettingsOverride`'s call site inside z_kankyo's `Environment_Update` is
replaced by having `Environment_Update` ask `Zelda3D_Title_IsActive()` and, if true, pull
`TitlePresentation::Instance().frame().light` directly — no separate override function
re-deriving light from `dayTime` a second time.

## 2. Migration table — every current override → its new home

| Current piece (file : function) | What it does | New home | Notes |
|---|---|---|---|
| `zelda3d.c : gZelda3dInTitleDemo` (global) | master active flag | `TitlePresentation::mActive`, read via `Zelda3D_Title_IsActive()` | all direct reads of the global across the codebase (rider post-update, HUD suppress, sun/moon draw, title_logo) switch to the accessor |
| `zelda3d.c : Zelda3D_TitleCamEnabled()` | env/REPL gate | `TitlePresentation::shouldBeActive()` (folds in scene+autowarp checks too) | unify 3 separate guard conditions into one predicate |
| `zelda3d.c : Zelda3D_ApplyTitleCam()` | top-level per-frame driver: entry/exit, camera, dayTime, sky-enable, rider, light-save/restore | split across `TitlePresentation::enter()` / `update()` / `exit()` | this is the function being decomposed; its body becomes the reference implementation moved almost verbatim into the three lifecycle methods |
| `zelda3d.c : Zelda3D_TitleLightSettingsOverride()` | writes `envCtx.lightSettings` + light1Dir/light2Dir from dayTime | folded into `TitlePresentation::update()`'s `TitleFrameState.light` resolution; z_kankyo's `Environment_Update` reads `frame().light` instead of calling an override function | removes the double-derivation of dayTime→light that currently exists as a second call path |
| `zelda3d.c : Zelda3D_RiderStepCue()` + `Zelda3D_ActorTurnToPoint`/`Zelda3D_PathFollowUpdate`/`Zelda3D_ActorMoveXZByYawSpeed` | cue-driven rider position/yaw integration | move to `behaviors/title/title_rider.cpp` (`Zelda3D::TitleRider` helper class), invoked from `TitlePresentation::update()`; result written into `TitleFrameState.riderPos/riderYaw/riderCueDiscontinuity` | pure-math ports (already RE-verified) move unchanged; only the call site changes |
| `zelda3d.c : Zelda3D_ActorPostUpdate` rider-transform block (lines ~438-441) | applies `gZelda3dRiderPos/Yaw` to `ACTOR_PLAYER` | stays in `Actor_UpdateAll`'s post-update hook (that hook point is correct — actors must be moved during actor update, not during title's own update), but reads `TitlePresentation::Instance().frame().riderPos/riderYaw` instead of the module-level globals `gZelda3dRiderPos`/`gZelda3dRiderYaw` | this is the one piece that legitimately stays outside `title_presentation.cpp` because it's *applying* title state to a generic actor-update hook, not producing it; it becomes a **reader** of `TitleFrameState`, mirroring how any other consumer would |
| `zelda3d.c : Zelda3D_TryDrawSky()`, `Zelda3D_ActiveSkyIndex`, `Zelda3D_SkyActive`, `Zelda3D_SkyBoxToTenkyuIndex` | sky dome + cloud/star draw primitives | **stay in `zelda3d.c`/a shared sky module** — these are the generic sky renderer used by non-title scenes too (see §5); title only calls them and feeds `skyDomeVariant` selection, doesn't own the draw code | `TitlePresentation::draw()` calls `Zelda3D_TryDrawSky(play)` unchanged, after setting the resolved variant/blend into the fields it already reads (`skybox1Index`/`skybox2Index`/`skyboxBlend`) |
| `zelda3d.c : Zelda3D_TryDrawSunMoon()`, `Zelda3D_TryDrawTitleAtmos()` (stub) | sun/moon disc + halo draw; atmosphere stub | stay as shared draw primitives, called from `TitlePresentation::draw()`; the title-demo-bypasses-scene-gate special case inside `TryDrawSunMoon` (line ~3940) becomes `if (Zelda3D_Title_IsActive() || ...)` (same logic, just via the new accessor) | the stub nature of `TryDrawTitleAtmos` is a genuine open RE gap (§4), not a structural issue — no change needed beyond the flag-read |
| `behaviors/title/title_logo.cpp : Zelda3D_TryDrawTitleLogo()` | draws the wordmark billboard | becomes `TitleLogo::draw(const TitleFrameState&)`, called from `TitlePresentation::draw()` | logic unchanged (CSAB frame counter, camera-facing basis math, `gSPZelda3DDraw` unlit call); only the gate (`gZelda3dInTitleDemo` direct read) and camera-basis source (currently reads `play->view.*` directly — should read `frame().eye/at/up` for frame-coherence with everything else this frame) change |
| `zelda3d.c : Zelda3D_TitleLightSlotsConvert`/`TitleLightSlots`/`TitleLightSlotCount` | title-specific 4-slot palette override for `gZelda3dScenePalette` | stays adjacent to the palette system it feeds (not title-owned data, it's a scene-palette-selection concern) but its *gate* (`gZelda3dInTitleDemo && ...`) becomes `Zelda3D_Title_IsActive() && ...` | no logic move; flag-read only |
| `zelda3d_cutscene.cpp` : all of it (`Zelda3D_TitleCs*`) | spot99 cs parse + per-frame evaluation (camera spline, rider cues, dayTime anchor+extrapolation, light schedule) | **stays as-is**, becomes the module's data/evaluation dependency, called *only* from inside `TitlePresentation::update()` (today it's called from ~4 independent sites: `Zelda3D_ApplyTitleCam`, `Zelda3D_RiderStepCue`, `Zelda3D_TitleLightSettingsOverride`, and indirectly wherever `Zelda3D_TitleCsFrame()` is read elsewhere) | this consolidation is the concrete fix for the frame-coherence risk in §0 — same data layer, single caller |

## 3. Incremental migration order (no big-bang rewrite)

Each step must build + the title screen must look pixel-identical (verified via
`tools/title_ab.py` content-matched frames) before moving to the next. Order chosen so early
steps are pure refactor (behavior-preserving by construction, nothing to re-verify beyond
"still compiles and matches byte-for-byte") and risk is pushed to the end.

1. **Scaffold + flag wrapper (no behavior change).** Add `title_presentation.h/.cpp` with the
   class skeleton and `Zelda3D_Title_IsActive()` as a thin wrapper that just reads the existing
   `gZelda3dInTitleDemo` global (module doesn't own the flag yet). Replace direct global reads
   at call sites (`Zelda3D_ActorPostUpdate`, HUD suppress, sun/moon gate, title_logo gate) with
   the accessor call. Build + verify: title A/B identical to before (this step touches zero
   logic, only indirection).
2. **Move the cs cursor consumption behind `update()`, keep the cs module untouched.** Move the
   *body* of `Zelda3D_ApplyTitleCam` verbatim into `TitlePresentation::update()` (camera resolve,
   dayTime resolve, sky-enable), still writing to the same globals it writes today
   (`play->view`, `gSaveContext.dayTime`, `play->envCtx.sunMoonDisabled` etc.) — do NOT yet
   introduce `TitleFrameState` as the sole source of truth, just relocate the function body and
   make `gZelda3dInTitleDemo` a true member (`mActive`) set only inside `enter()`/`exit()`.
   Verify: byte-identical A/B (pure move, same statements, same order).
3. **Fold in `Zelda3D_TitleLightSettingsOverride`.** Move its body into the same `update()` (it
   already runs once per frame and depends only on `dayTime`, already resolved in step 2). Have
   z_kankyo's `Environment_Update` call `Zelda3D_Title_IsActive()` and skip its own
   `lightSettings` write when true, letting the values `update()` just wrote stand (rather than
   a separate override call racing/overwriting after). Verify A/B — this is the first step with
   real reordering risk (call-order dependency), so check both the lighting values AND that nothing
   else in `Environment_Update` between the old override call site and end-of-function depended
   on running after the override.
4. **Fold in the rider cue integration.** Move `Zelda3D_RiderStepCue` + its math-port helpers
   into `behaviors/title/title_rider.cpp`; `update()` calls it and stores results into member
   fields (`mFrame.riderPos` etc. — this is where introducing the real `TitleFrameState` struct
   pays off, since rider consumption from `Zelda3D_ActorPostUpdate` needs a stable place to read
   from). Update `Zelda3D_ActorPostUpdate` to read `TitlePresentation::Instance().frame()`
   instead of `gZelda3dRiderPos/gZelda3dRiderYaw`. Verify: rider trajectory A/B unchanged
   (this is the step most likely to regress task #8's in-progress mount work if done carelessly
   — coordinate with whoever owns #8 before this step, or do it after #8 lands).
5. **Introduce `TitleFrameState` fully and freeze the public struct**, replacing the informal
   member fields from steps 2-4 with the documented struct in §1.2. This is a pure refactor
   (rename/regroup fields) if steps 2-4 already routed everything through members.
6. **Migrate `draw()`: consolidate the three `TryDraw*` call sites into `Zelda3D_Title_Draw`.**
   Find the current call sites (overlay-draw path) and replace 3 calls with 1, sequencing
   sky → sun/moon → logo. Verify draw order is unchanged (should be, since we're just wrapping
   the same sequence in one function) via A/B.
7. **Update `title_logo.cpp` to consume `frame().eye/at/up`** instead of re-reading
   `play->view.*` directly, closing the last frame-coherence gap. Verify A/B (should be a no-op
   numerically since `update()` already wrote the same values into `play->view` this frame — this
   step is about removing the redundant read path, not changing output).
8. **Delete now-dead globals** (`gZelda3dRiderPos/Yaw/Speed/CueIdx`, standalone
   `gZelda3dInTitleDemo` if fully subsumed, the old static `kZelda3dTitleRiderPath`/
   `Zelda3D_RiderStep` superseded-path if still present) and dead wrapper functions
   (`Zelda3D_ApplyTitleCam`, `Zelda3D_TitleLightSettingsOverride` as standalone entry points) once
   nothing calls them. Final A/B pass to confirm no behavior rode on a "dead" path actually still
   being reachable.

Each numbered step is one commit, gated on: build succeeds, `tools/title_ab.py` shows no new
divergence vs the immediately-prior commit (not vs the Az oracle — that's the pre-existing
divergence list in `title-divergence-remeasure.md`, which this refactor must not change either
direction).

## 4. Data/RE inventory — what the module needs

**Already exists and can be ported into the module as-is:**
- spot99 cs stream parse + evaluator (`zelda3d_cutscene.cpp`, format solved per
  `title-cs-spot99-format-solved.md`), camera spline (op 0x97/hermite eval, byte-exact vs Az per
  `tools/oot3d_cs_camera.py`).
- Terrain lighting ground truth (`spot00_field_lighting_ground_truth.md`) — the
  `2*texel*vertexColor*sceneAmbient/255` formula; task #6 fix already applied.
- Title lighting schedule (`title-lighting-schedule.md`/`title-lighting-solved.md`) — 9-span
  `kTitleLightSchedule`, 4-slot palette blend.
- Sky dome asset inventory (`title_sky_dome.md`) — `BlueSky.zar` meshes/cmabs, star-brightness
  fix (task #10, L8 decode).
- Moon disc scale/opacity calibration (`title-moon-size.md`).
- Rider position/yaw integration math (turn-to-point, path-follow, move-by-yaw-speed — all
  RE-verified ports) and the world-position address (`title_actor_world_pos.md`,
  `.data VA 0x005AFFB0`).
- Logo asset + placement (`title_2d_overlay_logo.md` for the correct `title_logo_us.cmb` entry;
  the bg-card/copyright claim in that doc is RETRACTED — do not resurrect).
- Camera handedness (LH OoT3D basis → RH SoH conversion) and FOV=48.803° — documented inline in
  `zelda3d.c` (kZelda3dTitleEye/At/Up comments) referencing `title_view_matrix_lh.md`.

**Open gaps, still needed before the module is behaviorally complete (not blockers for the
structural migration itself — the module can house a known-divergent piece same as the scattered
code does today, and get fixed in place post-migration):**
- **dayTime rate/phase (task #11, in progress by a parallel agent per this task's brief):** the
  flat 6-units/frame linear extrapolation past sparse `TimeCue` anchors in
  `Zelda3D_TitleCsTimeOfDay` is the confirmed root cause of the sky-dome color-collapse
  divergence (`skybox2Index` snapping equal to `skybox1Index`). This logic lives entirely inside
  `zelda3d_cutscene.cpp` (the data/eval layer, §5) — the module doesn't need to change to receive
  the fix, it just calls the same `TitleCsTimeOfDay` function and the corrected values flow
  through automatically once that function is fixed. **Coordinate migration step 2-3 timing with
  this fix** — if both land in the same window, do the dayTime fix first (in the old scattered
  code) and let the migration move already-correct behavior, rather than migrating then
  re-verifying against a moving target.
- **Rider mount/pose sync (task #8):** Link and Epona currently move on independent uncoordinated
  tracks (`title_rider_port_spec.md`) — no actual "mounted" pose coupling. This is a real
  behavioral gap in `TitleRider`, not a structural one; the module's job is to expose a clean
  `TitleFrameState.riderPos/riderYaw` that both actors can read consistently, but *deriving Link's
  saddle-relative pose from Epona's transform* is new RE/port work that belongs inside
  `title_rider.cpp` once it exists (step 4), sourced from OoT3D's actual mount-attach code (not
  yet decompiled — decomp this before porting it, per the project's ground-truth rule).
- **Fire-glow CMAB (`g_title_fire.cmab`/`g_title_fire_ura.cmab`):** inventoried in
  `title_2d_overlay_logo.md` but not yet ported/drawn. Slot for a future
  `TitlePresentation::draw()` step, after logo, once the material-anim CMAB player exists for it.
- **Copyright text (`copy_nintendo.cmb`):** inventoried, not yet drawn. Same bucket as fire-glow.
- **Fade in/out:** no RE or implementation yet for how the title demo fades to/from the logo
  phase or file-select. Needs its own RE pass (likely in `title_gamestate.md`'s territory —
  since title isn't a `Play` state on 3DS, the fade logic is probably driven by whatever
  IS running before `Play` exists) before it can be added to `draw()`.
- **`Zelda3D_TryDrawTitleAtmos` stub:** open RE arc (task #16 lineage) — a landscape-atmosphere
  layer that doesn't appear to go through the observable rasterizer draw path. Stays a stub in
  the shared sky module (§5) until resolved; the title module just calls it and gets 0 today.
- **`title_cs_stream.md` citation is stale:** a code comment in `zelda3d.c` (~line 2195)
  references a doc that doesn't exist; the real ground truth is
  `debug_journal/2026-07-07-title-cs-spot99-format-solved.md`. Fix this comment during migration
  step 2 (cheap, avoids future agents chasing a dead doc reference).

## 5. Explicitly NOT part of the module

These stay in shared/generic layers and are *called by* the title module, never owned by it —
because they're used outside title too, or are genuinely separable rendering/decoding primitives:

- **`zelda3d_cutscene.cpp` (the spot99 cs parser/evaluator itself).** This is a data/format layer
  (cs stream decode, hermite curve eval, cue lookup) — the module is a *consumer* of
  `Zelda3D_TitleCs*`, not its owner. Keeping it separate also means the harness A/B tooling
  (`tools/title_ab.py`, `tools/oot3d_cs_camera.py`) keeps a stable, narrow surface to diff against
  independent of however the presentation driver gets refactored.
- **`Zelda3D_TryDrawSky` / sky dome + cloud/star draw primitives, `Zelda3D_SkyBoxToTenkyuIndex`,
  `Zelda3D_ActiveSkyIndex`.** These render `BlueSky.zar` for any scene with a sky, not just
  title — title selects *which variant/blend* to show (via `skybox1Index`/`skybox2Index`
  written by `update()`) but does not own the draw code.
- **`Zelda3D_TryDrawSunMoon` / sun-moon draw primitives.** Same reasoning — generic celestial
  draw, title only toggles the scene-gate bypass and lets the same function run.
- **The L8 texture decode fix (star brightness, task #10)** and any other texture/CMB decode
  fixes — these are format-decode correctness in the shared texture/CMB loader, not title logic;
  title just benefits from correct decode like every other scene using those assets.
- **`ActorBehavior` base class/registry (`behaviors/actor_behavior.h/.cpp`).** Explicitly not
  reused for title dispatch (no live actor to key off, per `title_logo.cpp`'s own header
  comment) — the title module is a *sibling* first-class system, not a subclass or registry
  entry.
- **`Zelda3D_ActorPostUpdate`'s generic actor-transform hook in `Actor_UpdateAll`.** The title
  module *writes* rider state into `TitleFrameState`; applying that state to the live
  `ACTOR_PLAYER`/Epona actors happens in the generic per-actor update hook (correct place for any
  actor-transform write, title or not), which merely *reads* the module's output. This keeps the
  module itself free of direct `Actor*` mutation, matching the read/compute vs
  apply-during-actor-update split the codebase already uses elsewhere.
- **Scene/terrain geometry and collision** (real `spot00` Hyrule Field CMB + collision) — title
  runs on top of the real field, per `spot00_field_lighting_ground_truth.md`; the module changes
  only camera/lighting/sky/rider/overlay, never the terrain itself.

## 6. Summary for follow-up agents

Start at migration step 1 (§3). Each step is a single commit; verify with
`tools/title_ab.py` before moving on. Do not attempt draw-order changes, new assets (fire-glow,
copyright, fade), or the rider-mount fix (task #8) as part of the structural migration — land the
skeleton first (steps 1-8), then resume feature work *inside* the new module. If task #11
(dayTime rate) or task #8 (rider mount) land mid-migration, prefer finishing the in-flight
refactor step first, re-verify A/B still matches the pre-refactor commit, then rebase the feature
fix onto the new module shape (not the other way around) — the goal is a single owner for title,
not simultaneous churn in two directions.
