#include "../core/zelda3d_runtime.h"
#include "../anim/skeleton_draw_bridge.h"
#include "actor_auto_replacement.h"
#include "../diagnostics/model_tuning_query.h"
#include "actor_model_submission.h"
#include "bone_map_lookup.h"
#include "model_queries.h"
#include "replacement_calibration.h"
#include "replacement_catalog.h"
#include "replacement_control.h"
#include "special_replacement_measurements.h"

#include "../tables/zelda3d_object_zars.inc"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// ===========================================================================
// ZELDA3D_AUTO — programmatic actor replacement with auto-scale.
//
// Instead of hand-listing every actor, an actor whose loaded object has a matching
// OoT3D ZAR (kZelda3dObjectZars[objectId]) is replaced by that ZAR's main model, drawn
// via the same direct-GL path. The world scale is NOT a magic constant: it is MEASURED
// per object. The first time such an actor is seen, we let its N64 model draw and bracket
// that draw with the OTR_G_ZELDA3D_MEASURE opcode; the interpreter accumulates the actor's
// eye-space (== world-space) bbox and reports its HEIGHT back via Zelda3D_MeasureResult.
// scale = measured_N64_world_height / OoT3D_model_local_height (Zelda3D_AutoModelHeight, the
// bind-pose local Y extent). NOT a diagonal: the interpreter deliberately projects onto the view-up
// axis and takes the range, because height is what the manual scales were calibrated against and a
// diagonal carries an aspect-ratio bias (see the opcode in libultraship interpreter.cpp). This
// comment previously said "diagonal" on both sides, which is a ~1.5-2x systematic error in the
// reader's head even though the code was always right. The next frame the OoT3D
// model draws at that scale. Explicit sModelTable entries always win (they carry
// calibrated scale + anim resolvers) unless ZELDA3D_AUTO=2 (validation: route ALL through
// the auto path so the derived scale can be checked against the hand-tuned values).
//
// Gated behind env ZELDA3D_AUTO (0=off default, 1=fill non-table actors, 2=auto for ALL)
// + REPL `auto`. Static props only animate correctly (no skeleton); skinned characters
// come out in bind pose (frozen) — acceptable per the session-13 plan; sModelTable still
// drives the calibrated/animated ones at AUTO=1.
// ===========================================================================
int gZelda3dAuto = -1; // -1 = uninit (read env), 0=off, 1=fill, 2=all (validation)

int Zelda3D_AutoMode(void) {
    if (gZelda3dAuto < 0) {
        // Default ON (mode 1: replace non-table actors with their OoT3D object models). Part of the
        // no-flags unified default; ZELDA3D_AUTO=0 still disables, =2 routes ALL actors through auto.
        const char* v = getenv("ZELDA3D_AUTO");
        gZelda3dAuto = (v != NULL && v[0] != '\0') ? atoi(v) : 1;
    }
    return gZelda3dAuto;
}

// Per-object auto-replace cache, indexed by object id.
//   state: 0 unseen, 1 measuring (bracket emitted, awaiting result), 2 ready, 3 failed
static Zelda3D_AutoEntry sAuto[ARRAY_COUNT(kZelda3dObjectZars)];

// Two DIFFERENT reasons an auto slot stops trying, which used to share state 3 and therefore shared
// its permanence:
//   state 3 = STRUCTURALLY unreplaceable (no ZAR, no such CMB, model has no geometry, skinned with
//             n64anim off). Nothing about another scene changes this, so it is correctly permanent.
//   state 4 = NEVER GOT A MEASUREMENT. The bbox measure needs the actor's N64 draw to actually
//             happen; if the actor is culled or off-screen every frame we look at it, the measure
//             bracket never closes. `tries` therefore counts FRAMES WAITED, not failures -- an actor
//             that happens to be off-screen for its first 8 frames was marked unreplaceable FOR THE
//             WHOLE PROCESS, including in later scenes where it stands in plain view. sAuto is never
//             cleared, so nothing ever undid it.
// Splitting them lets the second kind expire at a scene change, which is the natural scope: the
// evidence ("never drawn while we watched") was gathered in one scene and says nothing about another.
const Zelda3D_AutoEntry* Zelda3D_AutoCalibrationAt(int objectId) {
    if (objectId < 0 || objectId >= (int)ARRAY_COUNT(sAuto)) {
        return NULL;
    }
    return &sAuto[objectId];
}

int Zelda3D_AutoCalibrationCount(void) {
    return (int)ARRAY_COUNT(sAuto);
}

void Zelda3D_RecordAutoCalibration(int objectId, float height, float footprintX, float footprintZ) {
    if (objectId < 0 || objectId >= (int)ARRAY_COUNT(sAuto)) {
        return;
    }
    sAuto[objectId].measuredH = height;
    sAuto[objectId].measFootX = footprintX;
    sAuto[objectId].measFootZ = footprintZ;
}

// --- The 1:1 check: is the "derived scale" really just the ACTOR'S OWN scale? ------------------
// Zelda3D_EmitModelDraw applies `worldScale` uniformly and never multiplies actor->scale, so every
// auto-routed prop has to recover the actor's scale from pixels: scale = measured-N64-extent /
// CMB-extent. That inference is only necessary when Grezzo RE-AUTHORED the mesh at different
// proportions. When the CMB is dimensionally 1:1 with the N64 display list -- which the Ice Cavern
// row already proved happens (a RED_ICE_SMALL instance derived exactly 0.06000 = sRedIceScales[SMALL]
// against a 1005-unit CMB) -- the ratio IS actor->scale, a number the engine holds EXACTLY and
// per-axis. Three height-primary misfires (King Zora's block, the Bottom of the Well coffin lid, the
// Shadow Temple guillotine) are all cases where one measured axis dissents from a value the actor
// already knows.
//
// This reports the comparison so the mechanism change can be decided on data instead of on the three
// anecdotes. It prints on EVERY derive, including when nothing matches -- a silent "no hits" here
// would be indistinguishable from "the check never ran", and the re-authored case (Obj_Syokudai's
// chunkier wooden torch, where no axis should match) is exactly the case that must stay visible.
#define ZELDA3D_1TO1_TOL \
    0.02f // 2%: tight enough that 0.0847-vs-0.100 dissents, loose enough for
          // measure quantisation (the agreeing axes land within ~0.5%)
static int Zelda3D_RatioMatches(float ratio, float actorScale) {
    if (!(ratio > 1e-6f) || !(actorScale > 1e-6f))
        return 0;
    const float r = ratio / actorScale;
    return (r > 1.0f - ZELDA3D_1TO1_TOL) && (r < 1.0f + ZELDA3D_1TO1_TOL);
}
static void Zelda3D_Report1to1(int objId, const char* modelKey, Actor* actor, float rh, float rx, float rz,
                               float derived, const char* source, short measYaw) {
    if (Zelda3D_AutoMode() < 1 || actor == NULL)
        return;
    // A ratio of 0 means "that axis had no usable measurement", which is NOT a mismatch -- say which.
    const int hv = (rh > 1e-6f), xv = (rx > 1e-6f), zv = (rz > 1e-6f);
    const int hm = hv && Zelda3D_RatioMatches(rh, actor->scale.y);
    const int xm = xv && Zelda3D_RatioMatches(rx, actor->scale.x);
    const int zm = zv && Zelda3D_RatioMatches(rz, actor->scale.z);
    const int nMatch = hm + xm + zm, nValid = hv + xv + zv;
    const char* verdict;
    if (nValid == 0) {
        verdict = "NO AXIS MEASURED -- this check could not run";
    } else if (nMatch == nValid && nValid == 3) {
        verdict = "1:1 ON ALL THREE AXES -- actor->scale is exact, measurement is redundant";
    } else if (nMatch == nValid) {
        verdict = "1:1 on every MEASURED axis (some axes had no measurement)";
    } else if (nMatch >= 2) {
        verdict = "1:1 on 2 of 3 -- the dissenting axis is a BAD MEASURE, not a re-authoring";
    } else if (nMatch == 1) {
        verdict = "only ONE axis matches -- ambiguous, do not trust actor->scale here";
    } else {
        verdict = "NO AXIS MATCHES actor->scale -- CMB is RE-AUTHORED, keep the derived scale";
    }
    // yaw is printed because the X/Z ratios are only meaningful once the model's footprint has been
    // rotated to match the world-space N64 measure. A non-zero yaw here is the case that used to be
    // silently wrong, so it must be visible on the line that reports the verdict.
    const float yawDeg = measYaw * (360.0f / 65536.0f);
    fprintf(stderr,
            "SOH3D 1TO1: obj 0x%x %s src=%s derived=%.5f | actor->scale=(%.5f,%.5f,%.5f) yaw=%.1fdeg "
            "ratios h=%.5f%c x=%.5f%c z=%.5f%c | %d/%d -> %s\n",
            objId, modelKey ? modelKey : "?", source, derived, actor->scale.x, actor->scale.y, actor->scale.z, yawDeg,
            rh, hv ? (hm ? '=' : '!') : '-', rx, xv ? (xm ? '=' : '!') : '-', rz, zv ? (zm ? '=' : '!') : '-', nMatch,
            nValid, verdict);
    fflush(stderr);
}

// Try the ZELDA3D_AUTO path for an actor with no explicit sModelTable entry. Returns 1 if
// it drew the OoT3D model (caller skips N64), 0 to let the N64 model draw (possibly while
// measuring it this frame). mode is Zelda3D_AutoMode() (>=1).
int Zelda3D_TryAuto(PlayState* play, Actor* actor) {
    int objId = Zelda3D_ActorObjectId(play, actor);
    Zelda3D_AutoEntry* e;
    const char* zar;
    if (objId < 0 || objId >= (int)ARRAY_COUNT(kZelda3dObjectZars)) {
        return 0;
    }
    zar = kZelda3dObjectZars[objId];
    if (zar == NULL) {
        return 0; // no OoT3D model for this object -> N64
    }
    // OBJECT_KANBAN (signpost) stays on N64. The assembly-merge can render the intact sign, but
    // En_Kanban's CUT behaviour spawns more En_Kanban actors for the broken pieces — those get
    // auto-replaced as whole signs again, so slashing a sign "spawns more signs" instead of
    // breaking. Until the break pieces are handled, keep the faithful N64 sign (it breaks right).
    if (objId == OBJECT_KANBAN) {
        return 0;
    }
    // ACTORCAT_DOOR actors (Door_Shutter, En_Door, ...) are articulated and draw animated sub-meshes
    // (the sliding panel + the closing bars/tetugousi grate at a per-frame Matrix_Scale). The
    // auto-replace bbox MEASURE — which assumes one static mesh — captures that transient extent, so
    // for a Spirit Temple shutter it measured n64H~2113 and scaled the small 160u panel CMB 13.2x,
    // drawing a door taller than the whole room (room mesh Y-extent ~863). The bbox measure cannot
    // size articulated doors (same reason skinned actors are excluded below), and there is no reliable
    // static N64 height to derive a scale from here. Render the faithful N64 door instead of a
    // blown-up OoT3D one; a proper OoT3D door port needs the door actor's real scale from the decomp.
    if (actor->category == ACTORCAT_DOOR) {
        return 0;
    }
    // MULTI-DISPLAY-LIST ACTORS STAY ON N64. This path substitutes ONE mesh for the actor's ENTIRE
    // draw -- z_actor.c does `if (!Zelda3D_TryDrawActor(...)) actor->draw(...)` -- so an actor whose
    // Draw emits more than one display list in a single call loses every list except the one we
    // replace. That is not a cosmetic difference; it deletes geometry the game is still drawing.
    //
    // Obj_Syokudai is the found case: ObjSyokudai_Draw emits the torch stand and then, whenever
    // litTimer != 0, gEffFire1DL at its own billboarded matrix. Replacing it drops the FLAME from
    // every lit torch, and via the generic per-object AUTO slot that applied to all torch variants,
    // not just the routed wooden one.
    //
    // HONESTY NOTE ON THE EVIDENCE: the two steps are each read directly from source (the double
    // emission above; the call site that skips actor->draw), but the flame loss itself has NOT been
    // observed in game -- every torch found so far is unlit, so `auto 0` vs `auto 1` renders
    // identically and cannot discriminate. THE TEST is a LIT torch (light one with Din's Fire, or
    // find a scene that spawns them lit) A/B'd on `auto`. The skip is correct either way, because an
    // actor that draws several lists cannot be faithfully replaced by a one-mesh substitution.
    if (actor->id == ACTOR_OBJ_SYOKUDAI) {
        return 0;
    }
    // Per-actor forced-CMB routing (sActorForcedAuto): actors sharing a multi-CMB ZAR must
    // route through their OWN slot with a "<zar>|<cmb>" key so AUTO loads the right mesh
    // (see decl above). NULL slot -> default per-object cache.
    // (the 1:1 check below is emitted from the derive branches; see Zelda3D_Report1to1.)
    Zelda3D_ActorForcedAutoSlot* forced = Zelda3D_FindActorForcedSlot(actor->id, (u16)actor->params);
    char forcedKeyBuf[256];
    const char* modelKey = zar;
    int measKey = objId; // which slot Zelda3D_MeasureResult must deliver the height to
    if (forced != NULL) {
        e = &forced->entry;
        snprintf(forcedKeyBuf, sizeof forcedKeyBuf, "%s|%s", zar, forced->cmbSubstr);
        modelKey = forcedKeyBuf;
        measKey = ZELDA3D_MEASKEY_FORCED_BASE + Zelda3D_ForcedSlotIndex(forced);
    } else {
        e = &sAuto[objId];
    }
    if (e->state == 3 || e->state == ZELDA3D_AUTO_NOMEAS) {
        return 0; // known-unreplaceable -> N64
    }
    if (e->modelId == 0) {
        e->modelId = Zelda3D_AutoModelId(modelKey);
        if (e->modelId < 0) {
            e->state = 3;
            return 0;
        }
        // Skinned characters: drive the OoT3D skeleton from the actor's LIVE N64 SkelAnime
        // joints via the generic SkelAnime hook (same mechanism as the calibrated sModelTable
        // n64anim entries). Requires ZELDA3D_N64ANIM; otherwise a frozen bind pose looks like a
        // T-pose, so skip -> N64. Grezzo mostly preserved the rigs (bone i <-> jointTable[i+1]),
        // so this broadly works; characters whose rig doesn't correspond will pose wrong (add a
        // per-objId skip if one shows up).
        if (Zelda3D_AutoModelSkinned(e->modelId)) {
            if (!Zelda3D_N64AnimEnabled() || !gZelda3dAnimLive) {
                e->state = 3;
                return 0;
            }
            e->skinned = 1;
            e->groundOff = -Zelda3D_AutoModelMinY(e->modelId); // feet -> actor world Y
            // Skinned scale is derived from the rest skeletons (bone-length ratio) in the
            // SkelAnime hook — NOT the bbox measure, which over-measures articulated actors and
            // made them giant (Boj: measured n64h~1235 -> scale 0.18; the true scale is ~0.0102).
            // Go straight to ready; the hook computes the real scale and retargets the pose.
            e->state = 2;
        }
    }
    if (e->state == 2) {
        if (e->skinned) {
            // Defer to the actor's own Draw so the SkelAnime hook retargets the OoT3D skeleton
            // from the live N64 jointTable (returns 0 -> actor->draw runs; the hook draws it).
            gZelda3dPendingActor = actor;
            gZelda3dPendingModel = e->modelId;
            gZelda3dPendingScale = e->scale;
            gZelda3dPendingGroundOff = e->groundOff;
            gZelda3dPendingAuto = 1;
            gZelda3dPendingBoneMap = Zelda3D_FindBoneMap(zar); // precomputed correspondence (or NULL)
            return 0;
        }
        // Ready static prop: base-anchor the model to the actor's world Y with the same
        // "feet -> ground" offset (-minY) the skinned path uses. For a base-anchored model minY==0
        // (no change, correctly-placed props unaffected); for a center/top-origin model it lifts the
        // model so its bottom sits at the actor Y instead of sinking half-underground — #22 En_Goroiwa
        // (the Kokiri sword-maze rolling boulder) is sphere-center-origin and was buried to its
        // equator. REPL `autoyoff <f>` adds a live global nudge on top for tuning.
        float goff = (forced != NULL && forced->noBaseAnchor)
                         ? Zelda3D_ModelAutoYOffsetNudge()
                         : -Zelda3D_AutoModelMinY(e->modelId) + Zelda3D_ModelAutoYOffsetNudge();
        Zelda3D_DrawModelGL(play, e->modelId, actor, e->scale, NULL, goff, NULL, NULL);
        return 1;
    }
    // state 0 or 1: derive scale if the measurement has arrived, else (re)measure.
    // FLAT props are admitted here too: a horizontal plane (water surface, floor web) measures a
    // height of ~0, so gating on measuredH alone left them re-measuring until the try budget ran out
    // and then giving up permanently. Their FOOTPRINT is the usable signal.
    if (e->measuredH > 0.0f || e->measFootX > 0.0f || e->measFootZ > 0.0f) {
        float modelH = Zelda3D_AutoModelHeight(e->modelId);
        // "Flat" is RELATIVE, not absolute. The Deku Tree FLOOR web measured a height of 10.0 against a
        // footprint of thousands of units: that passes an absolute `height > 0` test and yields a scale
        // divided by a near-noise number, which is fragile. Prefer the footprint whenever the height is
        // a tiny fraction of the footprint, and keep the absolute test for a true zero.
        const float foot = (e->measFootX > e->measFootZ) ? e->measFootX : e->measFootZ;
        const int tooFlat =
            (modelH <= 1e-3f) || (e->measuredH <= 1e-3f) || (foot > 1e-3f && e->measuredH < 0.05f * foot);

        // --- 1:1 CONFIRMATION: don't infer a number the engine already holds --------------------
        // Zelda3D_EmitModelDraw applies `worldScale` uniformly and NEVER multiplies actor->scale, so
        // every auto-routed prop has had to recover the actor's scale from pixels: scale = measured
        // N64 extent / CMB extent. That inference is only needed when Grezzo RE-AUTHORED the mesh at
        // different proportions. When the CMB is dimensionally 1:1 with the N64 display list, the
        // ratio IS actor->scale -- a number the engine holds EXACTLY, with no measurement noise.
        // So the measurement's job here is not to PRODUCE the scale but to CONFIRM the 1:1, and any
        // axis whose ratio lands on actor->scale has done exactly that.
        //
        // This replaces a height-primary derive that was wrong whenever height was the bad axis:
        // the Bottom of the Well coffin lid (h=0.0847 against x=z=0.09999 and an actor scale of 0.1,
        // rendering 15% small in plan), the Shadow Temple guillotine (h=0.0300, a 3.3x error big
        // enough that the row was WITHDRAWN as unshippable) and m_Hkenzan, which shipped 6.6% small
        // with its axes recorded as "agrees".
        //
        // IT IS ANCHORED ON actor->scale AND NOT ON AXES AGREEING WITH EACH OTHER, because axis
        // agreement is not the evidence it looks like. A first cut of this gate accepted "two axes
        // within 2% of each other" and it MOVED TWO PROPS IT SHOULD NOT HAVE: Obj_Syokudai's torch
        // (h=0.99363 x=0.73296 z=0.72523, actor scale 1.0) and zelda_d_lift (h=0.10129 x=0.21038
        // z=0.22751, actor scale 0.1) -- both by >25%. The reason is structural: X and Z are NOT
        // independent for a prop with a SQUARE OR ROUND FOOTPRINT. They are one measurement taken
        // twice, so they agree automatically, including when the mesh is genuinely re-authored
        // chunkier in plan -- which is precisely what the 3DS wooden torch is. Height matching the
        // actor's own scale while the footprint does not is the signature of a re-authored asset,
        // and such an asset must keep rendering at ITS OWN proportions rather than be bent toward an
        // N64 shape ([[soh3d-re-and-port-not-fix-diffs]]).
        //
        // A non-uniform actor->scale is REFUSED rather than approximated: a single worldScale cannot
        // express it. That is King Zora's red ice (kzIceScale = {0.18, 0.27, 0.24}) and it stays an
        // open row for a per-axis draw path, not something to average away here.
        // FLAT props run this too. A zero-thickness mesh has no usable height ratio, so r[0] comes out
        // 0 and is skipped as "not measured" -- but its two FOOTPRINT axes are exactly as good a
        // confirmation as any, and taking actor->scale beats averaging two noisy ratios. This matters
        // for a whole class: the Fire Temple breakable walls (m_Fbmwall1..4) and every other
        // zero-Z-extent mesh would otherwise fall to the footprint average and never get the exact
        // number. If neither axis confirms, control falls through to the footprint path unchanged.
        if (actor != NULL) {
            float cmx = 0.0f, cmz = 0.0f;
            // NO rotation correction: the measure is already in the actor's own frame (see measYaw).
            const int haveXZ = Zelda3D_AutoModelExtentXZ(e->modelId, &cmx, &cmz);
            const float r[3] = { (modelH > 1e-3f && e->measuredH > 1e-3f) ? (e->measuredH / modelH) : 0.0f,
                                 (haveXZ && cmx > 1e-3f && e->measFootX > 1e-3f) ? (e->measFootX / cmx) : 0.0f,
                                 (haveXZ && cmz > 1e-3f && e->measFootZ > 1e-3f) ? (e->measFootZ / cmz) : 0.0f };
            const float as[3] = { actor->scale.y, actor->scale.x, actor->scale.z }; // parallel to r
            const char* axisName = "hxz";
            const float sx = actor->scale.x, sy = actor->scale.y, sz = actor->scale.z;
            const int uniform = (sx > 1e-6f) && (fabsf(sy - sx) < 1e-3f * sx) && (fabsf(sz - sx) < 1e-3f * sx);
            char members[8];
            int m = 0;
            for (int i = 0; i < 3; ++i) {
                if (Zelda3D_RatioMatches(r[i], as[i]))
                    members[m++] = axisName[i];
            }
            members[m] = '\0';
            if (m >= 1 && uniform) {
                const float wasHeight = (modelH > 1e-3f && e->measuredH > 1e-3f) ? (e->measuredH / modelH) : 0.0f;
                e->scale = sx; // the engine's own number, exactly -- not a measured approximation
                e->state = 2;
                Zelda3D_Report1to1(objId, modelKey, actor, r[0], r[1], r[2], e->scale, "actor-scale", e->measYaw);
                if (Zelda3D_AutoMode() >= 1) {
                    fprintf(stderr,
                            "SOH3D AUTO: obj 0x%x %s -> scale=%.5f FROM actor->scale, CONFIRMED 1:1 on "
                            "[%s] (h=%.5f x=%.5f z=%.5f; height-primary would have given %.5f, %+.1f%%)\n",
                            objId, modelKey, e->scale, members, r[0], r[1], r[2], wasHeight,
                            (wasHeight > 1e-6f) ? 100.0f * (e->scale / wasHeight - 1.0f) : 0.0f);
                    fflush(stderr);
                }
                return 0; // draw next frame, now that a scale exists
            }
            if (m >= 1 && !uniform && Zelda3D_AutoMode() >= 1) {
                // Say it out loud rather than silently falling through: this is the King Zora class.
                fprintf(stderr,
                        "SOH3D AUTO: obj 0x%x %s CONFIRMED 1:1 on [%s] but actor->scale is NON-UNIFORM "
                        "(%.5f,%.5f,%.5f) -- a single worldScale cannot express it, so the derived "
                        "scale is used and the prop renders at a WRONG ASPECT. Needs per-axis scale.\n",
                        objId, modelKey, members, sx, sy, sz);
                fflush(stderr);
            }
        }
        if (tooFlat) {
            // Too flat to scale by height: match the FOOTPRINT instead, on whichever axis is better
            // determined. Same principle as Bg_Spot01_Idomizu's well water, which needed a bespoke
            // path before this existed.
            float mx = 0.0f, mz = 0.0f;
            if (Zelda3D_AutoModelExtentXZ(e->modelId, &mx, &mz)) {
                float s = 0.0f;
                float sx = 0.0f, sz = 0.0f;
                if (mx > 1e-3f && e->measFootX > 1e-3f && mz > 1e-3f && e->measFootZ > 1e-3f) {
                    sx = e->measFootX / mx;
                    sz = e->measFootZ / mz;
                    s = 0.5f * (sx + sz);
                } else if (mx > 1e-3f && e->measFootX > 1e-3f) {
                    s = e->measFootX / mx;
                } else if (mz > 1e-3f && e->measFootZ > 1e-3f) {
                    s = e->measFootZ / mz;
                }
                if (s > 1e-6f) {
                    e->scale = s;
                    e->state = 2;
                    Zelda3D_Report1to1(objId, modelKey, actor, (modelH > 1e-3f) ? (e->measuredH / modelH) : 0.0f, sx,
                                       sz, e->scale, "footprint", e->measYaw);
                    if (Zelda3D_AutoMode() >= 1) {
                        // AXIS AGREEMENT is the quality signal for a footprint match: the X and Z
                        // ratios are two INDEPENDENT estimates of the same scale, so a large spread
                        // means the N64 footprint and the CMB footprint are not the same shape and the
                        // averaged scale is a guess. Measured examples: the Forest Temple well water
                        // agrees to 0.6% (trustworthy), while zelda_mamenoki disagrees by ~14% (its
                        // N64 footprint reads square at 60x60 against a 1044x1192 model, so something
                        // other than the plane was measured). Flag it rather than hide it in an average.
                        const float spread = (sx > 1e-6f && sz > 1e-6f) ? (sx > sz ? sx / sz : sz / sx) - 1.0f : 0.0f;
                        fprintf(stderr,
                                "SOH3D AUTO: obj 0x%x %s -> scale=%.5f FROM FOOTPRINT "
                                "(n64 %.1fx%.1f model %.1fx%.1f, height was %.2f, axis spread %.1f%%%s)\n",
                                objId, modelKey, e->scale, e->measFootX, e->measFootZ, mx, mz, e->measuredH,
                                100.0f * spread, (spread > 0.08f) ? " *** AXES DISAGREE, scale is a guess ***" : "");
                        fflush(stderr);
                    }
                    return 0; // draw next frame, now that a scale exists
                }
            }
        }
        if (modelH > 1e-3f && e->measuredH > 1e-3f) {
            e->scale = e->measuredH / modelH;
            e->state = 2;
            {
                float qx = 0.0f, qz = 0.0f;
                const int haveXZ = Zelda3D_AutoModelExtentXZ(e->modelId, &qx, &qz);
                Zelda3D_Report1to1(objId, modelKey, actor, e->scale,
                                   (haveXZ && qx > 1e-3f) ? (e->measFootX / qx) : 0.0f,
                                   (haveXZ && qz > 1e-3f) ? (e->measFootZ / qz) : 0.0f, e->scale, "height", e->measYaw);
            }
            // CROSS-CHECK the height-derived scale against the FOOTPRINT. Height, X and Z are three
            // INDEPENDENT estimates of the same scale, so agreement is strong evidence the CMB is the
            // right mesh for this actor. Worked example: l_bigst's CMB is 300x90x300 against a measured
            // h=90 foot=300x300, all three ratios 1.0.
            //
            // BUT DISAGREEMENT IS NOT PROOF OF A WRONG MESH, and the first case this check flagged shows
            // why. It assumes Grezzo RE-AUTHORED the asset at the N64 proportions, which is often true
            // but need not be. Obj_Syokudai's wooden torch measures h=58 foot=16x16 on N64, while
            // syokudai_ki_model ("ki" = wood) is 20x61x21 -- so the ratios disagree ~20%, and
            // syokudai_isi_model ("isi" = STONE) at 16x58x16 matches 1.000/1.000/1.000 exactly. Routing
            // the wooden torch to the stone mesh to satisfy the numbers would be chasing an N64 shape
            // instead of porting the OoT3D asset, which is backwards for this project. The 3DS wooden
            // torch is simply chunkier than the N64 one.
            // So: treat a disagreement as "check this pairing", not as "this pairing is wrong". A
            // MISIDENTIFICATION shows up as a gross mismatch (Bg_Ydan_Maruta's wrong candidate was off
            // by 200x on Z); a re-authoring shows up as a modest, CONSISTENT one across both axes.
            if (Zelda3D_AutoMode() >= 1) {
                float mx = 0.0f, mz = 0.0f;
                if (Zelda3D_AutoModelExtentXZ(e->modelId, &mx, &mz) && mx > 1e-3f && mz > 1e-3f &&
                    e->measFootX > 1e-3f && e->measFootZ > 1e-3f) {
                    const float rx = (e->measFootX / mx) / e->scale;
                    const float rz = (e->measFootZ / mz) / e->scale;
                    const float wx = rx > 1.0f ? rx : 1.0f / rx;
                    const float wz = rz > 1.0f ? rz : 1.0f / rz;
                    if (wx > 1.25f || wz > 1.25f) {
                        fprintf(stderr,
                                "SOH3D AUTO: obj 0x%x %s -- height scale %.5f differs from the footprint "
                                "(x %.2fx, z %.2fx). CHECK the pairing: a gross mismatch means the wrong "
                                "mesh, a modest consistent one may just be a Grezzo re-authoring\n",
                                objId, modelKey, e->scale, wx, wz);
                        fflush(stderr);
                    }
                }
            }
            if (Zelda3D_AutoMode() >= 1) {
                // Log modelKey, not zar: for a forced-CMB slot the key carries the "|<cmb>" suffix
                // that says WHICH mesh was picked. Printing the bare zar made a forced slot and the
                // default per-object slot log identically, so the log could not show that a forced
                // entry had resolved at all.
                fprintf(stderr, "SOH3D AUTO: obj 0x%x %s -> scale=%.5f (n64h=%.1f modelh=%.1f)%s\n", objId, modelKey,
                        e->scale, e->measuredH, modelH, e->skinned ? " [n64anim]" : "");
                fflush(stdout);
            }
            if (e->skinned) {
                // Defer to the SkelAnime hook (drive the OoT3D skeleton from live N64 joints).
                gZelda3dPendingActor = actor;
                gZelda3dPendingModel = e->modelId;
                gZelda3dPendingScale = e->scale;
                gZelda3dPendingGroundOff = e->groundOff;
                gZelda3dPendingAuto = 1;
                return 0;
            }
            Zelda3D_DrawModelGL(play, e->modelId, actor, e->scale, NULL, 0.0f, NULL, NULL);
            return 1;
        }
        e->state = 3; // model has no geometry -> cannot scale -> N64
        return 0;
    }
    // Need a measurement: bracket this actor's N64 draw (begin here, end in AfterActorDraw).
    if (e->tries >= 8) {
        e->state = ZELDA3D_AUTO_NOMEAS; // never drawn while we watched -> retried next scene
        return 0;
    }
    e->tries++;
    e->state = 1;
    // Recorded only so the 1TO1 diagnostic can show the measured instance's orientation. It is NOT a
    // correction input -- the measurement is already in actor-local yaw space.
    e->measYaw = actor->shape.rot.y;
    Zelda3D_BeginReplacementMeasurement(play, measKey);
    return 0; // let the N64 model draw so it can be measured
}

// Clear the measurement-give-ups when the scene changes. Deliberately does NOT touch state 3.
void Zelda3D_AutoRetryOnSceneChange(PlayState* play) {
    static s16 sLastScene = -1;
    const s16 cur = (s16)play->sceneNum;
    if (cur == sLastScene) {
        return;
    }
    sLastScene = cur;
    int revived = 0;
    for (size_t i = 0; i < ARRAY_COUNT(sAuto); i++) {
        if (sAuto[i].state == ZELDA3D_AUTO_NOMEAS) {
            sAuto[i].state = 0;
            sAuto[i].tries = 0;
            sAuto[i].measuredH = 0.0f;
            revived++;
        }
    }
    for (int i = 0; i < Zelda3D_ForcedSlotCount(); i++) {
        Zelda3D_AutoEntry* e = &Zelda3D_ForcedSlotAt(i)->entry;
        if (e->state == ZELDA3D_AUTO_NOMEAS) {
            e->state = 0;
            e->tries = 0;
            e->measuredH = 0.0f;
            revived++;
        }
    }
    revived += Zelda3D_SpecialReplacementRetryNoMeasurement();
    if (revived > 0 && Zelda3D_AutoMode() >= 1) {
        fprintf(stderr, "SOH3D AUTO: scene %d -- retrying %d slot(s) that never got a measurement\n", (int)cur,
                revived);
        fflush(stderr);
    }
}
