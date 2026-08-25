#include "player_draw.h"
#include "functions/animation.h"
#include "functions/math.h"
#include "functions/player.h"
#include "functions/rendering.h"

#include "../anim/automatic_playback.h"
#include "../anim/authored_playback.h"
#include "../anim/pose_tracking.h"
#include "../anim/skeleton_draw_bridge.h"
#include "../core/zelda3d_log.h"
#include "../core/zelda3d_runtime.h"
#include "../render/model_queries.h"
#include "../render/scene_tint.h"
#include "player_animation_policy.h"
#include "player_behavior.h"
#include "player_draw_policy.h"
#include "player_pose_scan.h"
#include "zelda3d_link.h"
#include "zelda3d_link_face.h"

#include "fast/zelda3d_material_overrides.h"
#include "fast/zelda3d_pose.h"
#include "overlays/actors/ovl_En_Horse/z_en_horse.h"

#include <cmath>
#include <cstdio>
#include <cstring>

static inline Zelda3D::PlayerBehavior& P() {
    return Zelda3D::PlayerBehavior::instance();
}

// Draw body kept as an extern "C" GLOBAL function (not a Zelda3D-namespace method) because
// OPEN_DISPS/CLOSE_DISPS inject an unqualified FrameInterpolation_RecordOpenChild declaration that
// must bind to the C-linkage symbol (a namespaced/C++-mangled binding is undefined at link). The
// PlayerBehavior::tryDrawModel method below delegates here. Returns 1 if it drew.
extern "C" int Zelda3D_PlayerDrawImpl(PlayState* play, Actor* actor) {
    const char* zar;
    const char* csab = NULL; // set only in the own-CSAB (linksrc 3ds) branch; NULL in N64-retarget
    Player* player;
    int modelId;
    u8 tint[3];
    // The general OoT3D Link body replacement is still WIP and stays gated behind ZELDA3D_LINK
    // (default off) for ordinary on-foot gameplay. The MOUNTED case is scoped ON unconditionally:
    // it is not a toggle for "N64-original vs 3DS" (the no-gates rule bans exactly that) — it is a
    // code-level narrowing of an already-3DS-default feature to the one state (horseback) that is
    // verified correct (position sync via the native z_player.c mount code + the groundOff fix
    // above). Riding N64-blocky-Link floating off Epona is the reported bug; this closes it without
    // waiting on the rest of the on-foot player port to finish.
    int mountedForDraw = (((Player*)actor)->stateFlags1 & PLAYER_STATE1_ON_HORSE) != 0;
    if (!Zelda3D_Enabled() || (!Zelda3D_LinkEnabled() && !mountedForDraw)) {
        return 0;
    }
    // Use the *_new (link_v2/childlink_v2) body: a single CMB with FULL embedded textures
    // (the body skin atlas, 128x128 ETC1) and a 25-bone rig — renders correctly textured. The
    // *_ultra rigs store the body skin in external CTXB files bound at runtime (the embedded
    // CMB texture is just a 32x32 'cube_01' placeholder -> renders untextured/red), and carry
    // the held-equipment CMBs; wiring up ultra's external textures + equipment is the next stage.
    zar = (LINK_AGE_IN_YEARS == YEARS_CHILD) ? "/actor/zelda_link_child_new.zar" : "/actor/zelda_link_boy_new.zar";
    Zelda3D_EnsureModelProvider();
    modelId = Zelda3D_AutoModelId(zar); // auto-picks the largest single CMB = the body (link.cmb)
    if (modelId < 0) {
        return 0; // model unavailable -> fall back to the N64 body
    }
    P().modelId = modelId; // expose to the REPL pose-discontinuity scanner (Zelda3D_LinkModelId)
    // N64 age root-translation scale, applied to the CSAB's ANIMATED translation tracks (the hip):
    // all Link clips are authored in the BOY rig's translation space, and the engine scales the
    // anim-provided root translation per age — z_player_lib.c Player_OverrideLimbDrawGameplayDefault
    // (child *= 0.64f), literal kept on 3DS (FUN_002bc768 DAT_002bc8b8). Must be set BEFORE the
    // Zelda3D_UpdateAnim* call below so THIS frame's pose uses it.
    Zelda3D_SetAnimTransScale(modelId, (LINK_AGE_IN_YEARS == YEARS_CHILD) ? 0.64f : 1.0f);
    // Player is Actor-first so the cast is valid (see z64player.h).
    player = (Player*)actor;
    // ROOT-MOTION SPLIT — which components of the clip's root translation the ACTOR consumes and
    // which stay in the drawn pose. Faithful mirror of SkelAnime_UpdateTranslation
    // (z_skelanime.c:2025-2040): whenever anim-movement is running it overwrites the root joint
    // with `baseTransl` — x/z unconditionally, y as well when ANIM_FLAG_UPDATEY is set — right
    // after taking the delta into `actor.world.pos`. So those components must NOT be drawn from
    // the clip. `movementFlags & ANIM_FLAG_ENABLE_MOVEMENT` is the exact condition under which the
    // consumption runs every frame: Player_UpdateCommon queues AnimationContext_SetMoveActor on it
    // (z_player.c:12600), and AnimationContext_Update flushes that queue before the draw.
    // MEASURED (Kokiri ladder, child): nml_Fclimb_upL bone1 tY 4772->6272 and upR 6272->7772, i.e.
    // +1500 local (+15 world) per rung — exactly what the engine also adds to world.pos.y — and the
    // next upL restarts its track at 4772, a -30 world-unit snap. Drawn unpinned that is the
    // user-reported "floats off the ladder and resets on clip loop".
    {
        // The Link rig carries its root motion on bone 1 (bone 0 is the skeleton root at the
        // origin); same bone the mounted-seat anchor reads as the root joint
        // (oot3d-decomp/docs/en_horse_rider_pos.md FUN_002b7fd0).
        enum { kLinkRootMotionBone = 1 };
        // z64animation.h names only UPDATEY/NOMOVE; z_player.c spells this one as a raw `& 8`.
        enum { kAnimFlagEnableMovement = 1 << 3 };
        unsigned pinMask = 0;
        if (player->skelAnime.movementFlags & kAnimFlagEnableMovement) {
            pinMask = (1u << 0) | (1u << 2); // x/z: reset unconditionally
            if (player->skelAnime.movementFlags & ANIM_FLAG_UPDATEY) {
                pinMask |= (1u << 1); // y: reset only when the actor consumes it
            }
        }
        Zelda3D_SetAnimRootPin(modelId, kLinkRootMotionBone, pinMask);
    }
    gZelda3dLogicFrame = (int)play->gameplayFrames; // logic-frame clock for the walk-stop synthetic morph
    // #16c: in FIRST-PERSON (C-up) the camera eye sits AT Link's head bone, so drawing the 3DS body
    // renders the head mesh around/in front of the camera — "you see inside his head". Vanilla OoT
    // hides the head/torso here via Player_OverrideLimbDrawGameplayFirstPerson (z_player.c ~12576),
    // selected when `unk_6AD != 0` AND the projected head is in front of the camera (projHead.z<-4).
    // The 3DS draw path has no per-limb override, so suppress the WHOLE body in first-person (held
    // items in 3DS-Link FP aren't handled yet, so dropping everything is acceptable and matches "I
    // don't want to see my head"). Same condition vanilla uses, so it's true exactly while FP is
    // engaged. Return 1 (NOT 0) so the N64 fallback in z_player.c is also skipped -> draw nothing.
    if (player->unk_6AD != 0) {
        Vec3f projectedHeadPos;
        SkinMatrix_Vec3fMtxFMultXYZ(&play->viewProjectionMtxF, &actor->focus.pos, &projectedHeadPos);
        if (projectedHeadPos.z < -4.0f) {
            return 1; // first-person: draw nothing (no head clipping the camera)
        }
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Zelda3D_SceneTint(play, tint);
    // Keep posed-skin caching on for this model: Zelda3D_PosedBoneWorldPos (held-actor hand anchor,
    // focus.pos head anchor, mounted seat root) and the REPL groundDiag all read the cached pose.
    // (The old per-frame feet-GROUNDING consumer of this cache is gone — see the falsified-mechanism
    // note above the world-matrix build below.)
    Zelda3D_SetTrackPosedMinY(modelId, 1);
    // linkjointdump capture: append this frame's live jointTable + phase, for offline retarget fitting.
    P().jointDump.capture(player);
    // #85 carry-walk: in N64, walking-while-carrying merges TWO anims per-limb — the BASE skelAnime's
    // locomotion anim drives the LOWER body (legs cycle) while the upper carry anim (carryB_wait, arms
    // raised) is copied onto only the upper-body limbs (z_player.c ~3611, SetCopyTrue +
    // sUpperBodyLimbCopyMap; "moving" = linearVelocity != 0). The 3ds own-CSAB path now reproduces
    // this directly with a TWO-SOURCE per-limb blend (Zelda3D_UpdateAnimTwoSource + kLinkUpperBodyMask):
    // lower body plays the loco CSAB, upper body the carry CSAB — see the 3ds branch below. This drops
    // the former carry-walk detour through the N64 retarget path (the last retarget dependency in the
    // 3ds Link path). Carry-IDLE (linearVelocity == 0) plays the carry CSAB on the WHOLE rig (N64
    // SetCopyAll there — a single whole-rig pose, no leg cycle). The N64-retarget path (linksrc n64)
    // still handles carry-walk for free via the already-merged jointTable.
    int carryWalk = (player->heldActor != NULL && player->linearVelocity != 0.0f &&
                     player->skelAnime.jointTable != NULL && player->skelAnime.limbCount > 0);
    // Two user-selectable animation sources (REPL `linksrc`), both kept working:
    if (Zelda3D_LinkAnimSrc() == 1 && gZelda3dLinkForceCsab[0] == '\0' && player->skelAnime.jointTable != NULL &&
        player->skelAnime.limbCount > 0) {
        // N64 RETARGET: drive the OoT3D rig from Link's LIVE blended jointTable (captures walk/run and
        // every blended state, which the named-CSAB path is blind to). jointTable[0] = root translation,
        // skip it. The per-bone kLinkChildBoneCorr table maps each OoT3D bone to its N64 limb and how to
        // apply it: legs/head use pure "replace" (their rest frame matches the N64 rig), the divergent
        // spine/upper-arm bones get a constant rest-frame correction C (Grezzo re-rigged Link's torso).
        if (gZelda3dAnimDebug) {
            static int dbg = 0;
            if ((dbg++ % 30) == 0) {
                fprintf(stderr, "SOH3D LINK: src=N64 jointTable limbCount=%d (live blended pose, per-bone corr)\n",
                        player->skelAnime.limbCount);
                fflush(stderr); // these lines go to stderr; flushing stdout never flushed them
            }
        }
        P().retarget.ensure();
        // Hand-weave pose-freeze: latch the live idle jointTable on request, then feed the frozen copy
        // so tuning `linkcorr` is the only variable (the idle fidget otherwise moves the pose).
        const s16* jrots = (const s16*)&player->skelAnime.jointTable[1];
        int jcount = player->skelAnime.limbCount;
        if (P().retarget.freezeReq && jcount > 0 && jcount < 39) {
            for (int i = 0; i <= jcount; i++)
                P().retarget.frozenJoints[i] = player->skelAnime.jointTable[i];
            P().retarget.frozenCount = jcount;
            P().retarget.freezeReq = 0;
        }
        if (P().retarget.frozenCount > 0) {
            jrots = (const s16*)&P().retarget.frozenJoints[1];
            jcount = P().retarget.frozenCount;
        }
        Zelda3D_UpdateAnimN64Corr(modelId, jrots, jcount, P().retarget.table, (int)ARRAY_COUNT(P().retarget.table));
    } else {
        // 3DS OWN-CSAB: pick the link CSAB matching Link's named anim, phase-locked to curFrame/animLength.
        // Unmapped -> idle so it never freezes in bind pose. NOTE (#29b): the documented "slide" (idle
        // played while translating because OoT blends locomotion into jointTable without naming an anim)
        // does NOT reproduce here — during sustained movement player->skelAnime.animation resolves to
        // gPlayerAnim_link_normal_run_free, so this maps to nml_run_free and Link animates while moving
        // (verified live, Kakariko + Kokiri). speedXZ shown in the debug for future locomotion work.
        csab = Zelda3D_ResolvePlayerCsab((const char*)player->skelAnime.animation);
        // #117 walk/run SELECTION parity (see Zelda3D_LinkWalkRunGate / ZELDA3D_LINK_WALKRUN_SPEED).
        csab = Zelda3D_LinkWalkRunGate(csab, player->actor.speedXZ);
        // DOOR-EXIT / scripted auto-walk slide fix (user bug (b), 2026-07-23): during scripted
        // forward moves — the door-exit walk-out and entrance walk-ins (Player_Action_80845CA4 /
        // func_80845964) — N64 keeps skelAnime.animation on the IDLE header (wait_free) and drives
        // the LEG CYCLE separately through func_80841EE4, the unk_868 leg-phase accumulator (same
        // mechanism as normal ground locomotion, z_player.c:10703). Keying locomotion off the
        // resolved anim NAME alone therefore froze the idle pose while the body glided forward at
        // lin=2.0 (measured: 16 frames x 3 units of idle-pose slide on the market mask-shop door
        // exit, warp 0x1D1). The engine signal that the ground-locomotion leg driver ran THIS
        // frame is unk_868 advancing; when it does and the resolved clip is not already a
        // locomotion clip, select walk/run by speed exactly like the walk/run gate above — the
        // phase-locked loco path below then plays it at phase unk_868, which IS what N64 renders.
        {
            static float sPrevUnk868 = -1.0f;
            static int sPrevUnk868Gf = -1;
            int gf = (int)play->gameplayFrames;
            int legDriverRan = (sPrevUnk868Gf >= 0 && gf != sPrevUnk868Gf && player->unk_868 != sPrevUnk868);
            if (gf != sPrevUnk868Gf) {
                sPrevUnk868 = player->unk_868;
                sPrevUnk868Gf = gf;
            }
            if (legDriverRan && player->actor.speedXZ > 0.5f && player->heldActor == NULL && csab != NULL &&
                strstr(csab, "walk") == NULL && strstr(csab, "run") == NULL) {
                csab = (player->actor.speedXZ > ZELDA3D_LINK_WALKRUN_SPEED) ? "nml_run_free" : "nml_walk_free";
            }
        }
        // #6/#85/#117 carry: OoT3D (oracle, GROUND TRUTH — tools/oracle_carry_id.py 2026-06-25) does
        // NOT layer carry onto locomotion via the N64 sUpperBodyLimbCopyMap. It plays a SINGLE unified
        // whole-body clip per carry state:
        //   carry-WALK  -> nml_carryB_free  (legs 3-8 AND arms 9-21 both match it at the SAME frame,
        //                  mean 0.9°/1.1° across 50 captured frames — a complete walk-while-carrying
        //                  loop, 17f). The old TWO-SOURCE blend (lower nml_walk_free + upper carryB_wait
        //                  masked b9..21) was a reconstruction of the N64 copy-map, but OoT3D simply
        //                  authored a dedicated clip. Play carryB_free whole-rig, free-run by ground
        //                  speed exactly like the walk/run loco cycle.
        //   carry-IDLE  -> the upper carry CSAB resolved from player->upperSkelAnime (carryB_wait),
        //                  whole rig (N64 SetCopyAll while standing).
        //   PICKUP/LIFT -> the LOWER skelAnime itself plays the lift clip whole-body (N64
        //                  LinkAnimation_PlayOnce(&skelAnime, carryB...) at z_player.c:5578 — the upper
        //                  copy is NOT yet engaged). So the upper override below must ONLY fire when the
        //                  upper body is GENUINELY holding the carry pose (carryB_wait). Gating on
        //                  upperSkelAnime being a *carry* anim is exactly N64's SetCopy condition
        //                  (it copies the upper carry pose, meaningful only when the upper IS carry).
        //                  #117 BUG (fixed): without this gate, the instant heldActor is set mid-lift
        //                  the upper (still wait_free) clobbered the lower's lift clip -> instant snap
        //                  to carry-hold instead of OoT3D's ~0.5s raise. Live-traced 2026-06-25.
        const char* upperCarryCsab = NULL;
        if (player->heldActor != NULL && player->upperSkelAnime.animation != NULL &&
            strstr((const char*)player->upperSkelAnime.animation, "carry") != NULL) {
            upperCarryCsab = Zelda3D_ResolvePlayerCsab((const char*)player->upperSkelAnime.animation);
        }
        if (carryWalk && gZelda3dLinkForceCsab[0] == '\0') {
            csab = "nml_carryB_free"; // carry-WALK: unified whole-body carry-locomotion clip
        } else if (upperCarryCsab != NULL && !carryWalk) {
            csab = upperCarryCsab; // carry-IDLE: SetCopyAll -> whole rig plays the carry pose
        }
        if (csab == NULL) {
            csab = ZELDA3D_LINK_IDLE_CSAB;
        }
        if (gZelda3dLinkForceCsab[0] != '\0') {
            csab = gZelda3dLinkForceCsab; // REPL `linkanim` override (verification)
        }
        if (Zelda3D_LogEnabled(Z3D_LOG_LINK)) {
            // #117 pickup diagnosis: trace the exact fields that decide the carry/lift CSAB per draw.
            const char* lo = (const char*)player->skelAnime.animation;
            const char* lob = lo ? strrchr(lo, '/') : NULL;
            lob = lob ? lob + 1 : (lo ? lo : "(null)");
            const char* up = (const char*)player->upperSkelAnime.animation;
            const char* upb = up ? strrchr(up, '/') : NULL;
            upb = upb ? upb + 1 : (up ? up : "(null)");
            // Mounted extras: the horse anim index the Player's D_80854944 selector keys on, the
            // Player's latched copy (av2.actionVar2), and the horse frame Link syncs to (z_player.c
            // ~14048/14081) — the title-demo riding-pose diagnosis needs exactly these three.
            EnHorse* rh = (EnHorse*)player->rideActor;
            Z3D_LOG(LINK,
                    "held=%d carryWalk=%d lin=%.2f spd=%.2f lower=%s(cf=%.1f/%.1f mw=%.3f mr=%.3f) upper=%s(cf=%.1f)"
                    " unk868=%.2f horseAnimIdx=%d av2=%d horseCurFrame=%.1f -> csab=%s\n",
                    player->heldActor != NULL, carryWalk, player->linearVelocity, player->actor.speedXZ, lob,
                    player->skelAnime.curFrame, player->skelAnime.animLength, player->skelAnime.morphWeight,
                    player->skelAnime.morphRate, upb, player->upperSkelAnime.curFrame, player->unk_868,
                    rh ? (int)rh->animationIdx : -1, (int)player->av2.actionVar2, rh ? rh->curFrame : -1.0f, csab);
        }
        if (gZelda3dAnimDebug) {
            // Log on CHANGE (plus the first time), not every 30th frame. The interesting event here is
            // the CLIP SELECTION changing; a fixed 1-in-30 sample can miss a switch entirely and then
            // reads as "it never changed", which is the failure mode where a diagnostic quietly
            // becomes evidence for the wrong conclusion. Frame/speed still advance every frame and are
            // not worth a line each.
            static const char* sLastOtr = nullptr;
            static const char* sLastCsab = nullptr;
            const char* otr = (const char*)player->skelAnime.animation;
            if (otr != sLastOtr || csab != sLastCsab) {
                sLastOtr = otr;
                sLastCsab = csab;
                fprintf(stderr, "SOH3D LINK: src=3DS n64=%s -> csab=%s frame=%.1f/%.1f speedXZ=%.2f\n",
                        otr ? otr : "(none)", csab, player->skelAnime.curFrame, player->skelAnime.animLength,
                        player->actor.speedXZ);
                fflush(stderr);
            }
        }
        // carry-WALK rides the same speed-driven loco free-run as walk/run (nml_carryB_free has no
        // "run"/"walk" substring so it is gated in explicitly here).
        int isLoco = (strstr(csab, "run") != NULL) || (strstr(csab, "walk") != NULL) ||
                     (carryWalk && gZelda3dLinkForceCsab[0] == '\0');
        if (gZelda3dLinkForceTwoLower[0] != '\0' && gZelda3dLinkForceTwoUpper[0] != '\0') {
            // REPL `linktwo`: forced two-source capture (no live grab needed). Lower legs cycle at
            // animrate; upper held as a free-run carry pose. csab tracked as the lower for poseScan.
            // Retained as a DEBUG path only — OoT3D's real carry-walk is the unified carryB_free clip
            // selected above (oracle-verified); the live carry path no longer uses two-source.
            csab = gZelda3dLinkForceTwoLower;
            Zelda3D_UpdateAnimTwoSource(modelId, gZelda3dLinkForceTwoLower, gZelda3dAnimRate, gZelda3dLinkForceTwoUpper,
                                        0.0f, 0.0f, kLinkUpperBodyMask, 25);
        } else if (strcmp(csab, "rest") == 0) {
            Zelda3D_UpdateAnim(modelId, NULL, 0); // diagnostic: force bind pose (linkanim rest)
        } else if (isLoco && gZelda3dLinkForceCsab[0] == '\0' && player->actor.speedXZ > 0.5f) {
            // #117 / #7 SLIDE FIX: Link's run/walk advances its pose via a player-internal accumulator
            // (unk_868) + LinkAnimation_BlendToJoint writing jointTable directly, NOT skelAnime.curFrame
            // (RE'd: z_player.c func_80841860/func_80841EE4; oot3d-decomp player_anim_states.md). So
            // curFrame is pinned 0 the whole run AND morphWeight is pinned 1.0 — both are STALE design
            // artifacts of the one-time run-entry morph (func_8083C858 -> Player_AnimChangeLoopMorph,
            // morphFrames=-6), never updated during the steady cycle because the cycle bypasses the
            // curFrame/morph machinery. Phase-locking to that dead curFrame froze the CSAB at frame 0;
            // so we free-run the leg cycle by ground speed (the CSAB wraps internally). CRUCIALLY we
            // pass morphWeight=0 here, NOT the live (stale 1.0) value: feeding 1.0 makes the morph
            // blend render 100% the frozen OUTGOING idle pose every frame (csab.cpp weight=1 -> full
            // outgoing), which froze the legs while the root slid — the actual #117 slide. Verified
            // A/B (posescan over a run): morphWeight live=1.0 -> mean per-frame leg jump 0.6 deg
            // (frozen/slide); morphWeight=0 -> 38.9 deg (legs cycle). The loco free-run is a continuous
            // speed-driven cycle, not a cross-fade, so a morph blend is semantically wrong here.
            // FAITHFUL PHASE SOURCE (2026-07-23): OoT3D drives every ground-locomotion cycle from the
            // player's own leg-phase accumulator `unk_868` in [0,29), advanced per logic frame by
            // func_8084029C (speed-scaled, R_UPDATE_RATE-aware, footstep-SFX-synced) — byte-exact on
            // 3DS (the whole ring-1..4 sweep found Grezzo did not rewrite Link). The anim frame IS
            // that phase: walk = unk_868 (29f clip), run = unk_868*(20/29) (z_player.c:9270 — and
            // nml_run_free is exactly 20 frames), side-walk = *(16/29). Passing unk_868/29.0f through
            // the locked path of Zelda3D_UpdateAnimAuto reproduces this for ANY loco clip length
            // (f = phase * clipDur). This replaces the per-DRAW free-run at speedXZ*gZelda3dLinkLocoGain,
            // whose tuned gain was a decoupled approximation of this accumulator (and whose drift vs
            // the game's own phase forced the walk-stop endR/endL pick to be re-derived from a baked
            // gap table instead of the real leg phase). Measured 2026-07-23 (parity_pose_sweep +
            // oracle vs plain-clip diff): the oracle's live walking jointTable IS nml_walk_free
            // (median 1.15 deg), so phase = unk_868 is the complete mechanism — no blend layer.
            // Carry-walk (nml_carryB_free, a Grezzo-authored 17f clip with no N64 twin) keeps the
            // speed free-run: no evidence yet that OoT3D scales it from the same accumulator.
            if (carryWalk && gZelda3dLinkForceCsab[0] == '\0') {
                Zelda3D_UpdateAnimAuto(modelId, csab, player->actor.speedXZ * gZelda3dLinkLocoGain, 0.0f, 0.0f, 0.0f);
            } else {
                Zelda3D_UpdateAnimAuto(modelId, csab, 0.0f, player->unk_868, 29.0f, 0.0f);
            }
        } else if (gZelda3dLinkForceFrame >= 0.0f && gZelda3dLinkForceCsab[0] != '\0') {
            // REPL `linkframe`: hold the forced clip at one playhead (deterministic pose/face capture).
            Zelda3D_UpdateAnim(modelId, csab, gZelda3dLinkForceFrame);
            Zelda3D_RecordLastAuto(modelId, csab, gZelda3dLinkForceFrame);
        } else {
            // Idle / one-shot anims: curFrame is valid here, so keep the N64-progress phase-lock.
            Zelda3D_UpdateAnimAuto(modelId, csab, gZelda3dAnimRate, player->skelAnime.curFrame,
                                   player->skelAnime.animLength, player->skelAnime.morphWeight);
        }
    }
    // Pose-scan QA: sample the per-frame discontinuity now that lastSkin is set (once per drawn frame).
    P().poseScan.record(modelId, csab, player->skelAnime.curFrame);
    // #201d FACE: bind this frame's eye/mouth from the clip's `.faceb` track. Must run AFTER the anim
    // update — it samples the playhead that update resolved (zelda3d_link_face.cpp). Only on the 3DS
    // own-CSAB path: the N64-retarget debug mode (`linksrc n64`) resolves no CSAB, so there is no clip
    // to sample and the face would latch whatever was last bound.
    if (Zelda3D_LinkAnimSrc() != 1 || gZelda3dLinkForceCsab[0] != '\0') {
        Zelda3D_LinkFaceUpdate(modelId);
    }
    // Select Link's live equipment / hand-pose variant subset (the childlink_v2 mesh bakes them
    // all on distinct mesh_ids). Must be set BEFORE EmitPose so it pairs with this draw item.
    unsigned long long midMask = P().midmask.compute(player);
    if (gZelda3dAnimDebug) {
        // Log on CHANGE, for the same reason as the clip line above: the whole point of this line is
        // to catch the mesh mask changing, and a 1-in-30 frame sample can step straight over the
        // transition that matters. Now it prints every distinct mask exactly once, which is both
        // quieter AND strictly more informative.
        static unsigned long long sLastMask = ~0ull;
        if (midMask != sLastMask) {
            sLastMask = midMask;
            fprintf(stderr, "SOH3D LINK mids: LH=%d RH=%d sheath=%d shield=%d -> mask=0x%llx\n", player->leftHandType,
                    player->rightHandType, player->sheathType, player->currentShield, midMask);
            fflush(stderr);
        }
    }
    Zelda3D_GL_SetMidMask(modelId, midMask);

    // GAUNTLET TINT. OoT3D writes a per-upgrade colour before the gauntlet visibility calls
    // (Player_DrawImpl, table at 0x0053ca1c indexed strengthUpgrade*0x10 read at -0x20 — see
    // oot3d-decomp/docs/player_draw_impl_located.md): silver is (1,1,1,1), i.e. the IDENTITY, and
    // gold is (0.996, 0.812, 0.059, 1). That is why silver already looked right with no tint at all
    // and gave no sign a colour path was missing — a missing multiply is invisible where the factor
    // is 1. Only gold is wrong today.
    //
    // All four adult gauntlet groups share material 14, whose TEV declares combUsesConst=1
    // constIdx=5. NOTE this is a HYPOTHESIS about the seam: OoT3D's own call targets slot 0xe on
    // player+0x254, which is a different numbering from the CMB material's constIdx, so the two are
    // not known to be the same field. Verified by result, not by assumption — if gold does not
    // appear, the slot is wrong rather than the colour.
    if (LINK_AGE_IN_YEARS != YEARS_CHILD && CUR_UPG_VALUE(UPG_STRENGTH) >= 2) {
        const bool gold = (CUR_UPG_VALUE(UPG_STRENGTH) >= 3);
        Zelda3D_GL_SetMatConstOverride(modelId, 14, 5, gold ? 0.996f : 1.0f, gold ? 0.812f : 1.0f, gold ? 0.059f : 1.0f,
                                       1.0f);
    }
    // FALSIFIED MECHANISM (2026-07-23, was "#29b feet-grounding"): this path used to measure the
    // posed model's lowest visible vertex EVERY frame (Zelda3D_PosedGroundOffset) and shove the
    // whole body so that vertex touched actor.world.pos.y. That per-frame min-vertex anchor was a
    // stand-in for the real mechanism and was itself two of the user-reported bugs:
    //   (a) WALK VIBRATION — during a walk cycle the lowest vertex alternates feet, so the anchor
    //       carried per-frame noise (measured: groundOff std 16.8 local / p2p 81 local = 0.9 world
    //       units of vertical shake at logic rate, scratch/logs/ground_walk40.txt);
    //   (c) CLIMB WARP-UP — climb poses tuck the feet, so the anchor swung thousands of units
    //       (climb_upL->upR jump = 1521 local = 15 world units in ONE frame, measured live).
    // The REAL mechanism (RE'd 2026-07-23): every Link CSAB — including the child/anim clips —
    // authors the hip (bone 1) translation in the BOY rig's space (hip rest 3538; live child
    // jointTable reads the raw boy values, oracle), and the engine scales the ANIM root translation
    // by the AGE factor at draw time: N64 Player_OverrideLimbDrawGameplayDefault (z_player_lib.c)
    // child pos *= 0.64f; Grezzo kept the 0.64 literal on 3DS (FUN_002bc768 DAT_002bc8b8, the
    // root-motion twin). With that scale applied (Zelda3D_SetAnimTransScale below) the posed feet
    // land at the floor from the ANIMATION DATA itself — no per-frame grounding, no climb freeze.
    // (The scale itself is set before the anim update, right after the model is resolved above.)
    s32 mountedPose = (player->stateFlags1 & PLAYER_STATE1_ON_HORSE) != 0;
    // #152 seat diagnostics (`log rider 1`): posed origins of the first rig bones while mounted —
    // identifies which bone carries the riding clip's root translation (the term the 3DS subtracts
    // from the attach: player.pos = anchor - rootJoint*scale, FUN_002b7fd0 / en_horse_rider_pos.md).
    if (mountedPose && Zelda3D_LogEnabled(Z3D_LOG_RIDER)) {
        float b0[3] = { 0 }, b1[3] = { 0 }, b2[3] = { 0 };
        Zelda3D_PosedBoneWorldPos(modelId, 0, b0);
        Zelda3D_PosedBoneWorldPos(modelId, 1, b1);
        Zelda3D_PosedBoneWorldPos(modelId, 2, b2);
        Z3D_LOG(RIDER,
                "LINKROOT b0=(%.0f,%.0f,%.0f) b1=(%.0f,%.0f,%.0f) b2=(%.0f,%.0f,%.0f) "
                "scale=%.5f csab=%s\n",
                b0[0], b0[1], b0[2], b1[0], b1[1], b1[2], b2[0], b2[1], b2[2], gZelda3dLinkScale,
                csab ? csab : "(n64)");
    }
    // Mounted seat anchor (#152, RE'd: oot3d-decomp/docs/en_horse_rider_pos.md FUN_002b7fd0):
    // OoT3D sets player.pos = seatAnchor - rootJoint*scale and draws the pose WITH its root, so
    // the pelvis lands exactly on the seat anchor. Our z_player port subtracts the N64's folded
    // constant 27 instead of the live root, and the 3DS riding clips carry a much larger root
    // (uma_anim_*: bone-1 y=3538 * scale) — net: Link drew ~12 world units above the
    // saddle. Reproduce the 3DS algebra at draw time: cancel the pose's live root translation and
    // add back the 27 world units z_player already removed, so pelvis == horse.pos + riderPos.
    float mountRootFix[3] = { 0.0f, 0.0f, 0.0f };
    if (mountedPose && gZelda3dLinkScale > 1e-6f) {
        float rootL[3];
        if (Zelda3D_PosedBoneWorldPos(modelId, 1, rootL)) {
            mountRootFix[0] = -rootL[0];
            mountRootFix[1] = -rootL[1] + 27.0f / gZelda3dLinkScale;
            mountRootFix[2] = -rootL[2];
        }
    }
    // Per-draw world-anchor trace (jitter/slide/climb measurement): every term that enters the draw
    // translate, one line per logic frame.
    Z3D_LOG(LINK, "GROUND gf=%d pos=(%.3f,%.3f,%.3f) yOff=%.3f drawY=%.3f spd=%.3f csab=%s%s\n",
            (int)play->gameplayFrames, actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, actor->shape.yOffset,
            actor->world.pos.y + actor->shape.yOffset * actor->scale.y, player->actor.speedXZ, csab ? csab : "(n64)",
            mountedPose ? " [mounted]" : "");
    // Faithful 3DS actor base transform: T(pos.x, pos.y + shape.yOffset*scale.y, pos.z) · R · S —
    // RE'd from FUN_00408828 (oot3d-decomp/docs/en_horse_rider_pos.md), same as N64 Actor_Draw.
    // The yOffset term is LOAD-BEARING for the ledge climb (#79): z_player.c hides the one-frame
    // `world.pos.y += yDistToLedge` jump behind `shape.yOffset -= yDist*100` (z_player.c:5002-5005,
    // decayed by Math_StepToF(&yOffset,0,150) at :10639). Omitting it here made the ledge mount a
    // visible TELEPORT — the user-reported "climb warp-up".
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y + actor->shape.yOffset * actor->scale.y, actor->world.pos.z,
                     MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(actor->shape.rot.y), MTXMODE_APPLY);
    Matrix_Scale(gZelda3dLinkScale, gZelda3dLinkScale, gZelda3dLinkScale, MTXMODE_APPLY);
    if (gZelda3dLinkRotX != 0.0f)
        Matrix_RotateX(gZelda3dLinkRotX * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gZelda3dLinkRotY != 0.0f)
        Matrix_RotateY(gZelda3dLinkRotY * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gZelda3dLinkRotZ != 0.0f)
        Matrix_RotateZ(gZelda3dLinkRotZ * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (mountRootFix[0] != 0.0f || mountRootFix[1] != 0.0f || mountRootFix[2] != 0.0f) {
        Matrix_Translate(mountRootFix[0], mountRootFix[1], mountRootFix[2], MTXMODE_APPLY);
    }
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    // #6: attach a carried actor (e.g. held cucco) to the 3DS Link's hands. The held actor's
    // world.pos is normally set by Player_PostLimbDrawGameplay (midpoint of the hands), the post-limb
    // hook of Link's N64 SkelAnime draw inside Player_DrawGameplay — which z_player.c SKIPS when this
    // replacement draws, so without this the cucco stays at its pickup spot. Reproduce that anchor on
    // the posed 3DS rig: childlink_v2 left hand = bone 16, right hand = bone 20 (kLinkChildBoneCorr
    // limb map: limb 15/18 = L/R_HAND). The bone positions are model-local; the matrix stack top is
    // the player world transform just loaded, so Matrix_MultVec3f lifts the midpoint to world space.
    // Gated on carrying & not holding the hookshot / an item-in-hand. Works in both anim sources (both
    // cache skin via cacheSkinForGround). gZelda3dHeldAttach is the A/B toggle (REPL linkheldfix).
    if (gZelda3dHeldAttach && player->heldActor != NULL && !Player_HoldsHookshot(player) &&
        (player->stateFlags1 & PLAYER_STATE1_ITEM_IN_HAND) == 0) {
        float lh[3], rh[3];
        if (Zelda3D_PosedBoneWorldPos(modelId, 16, lh) && Zelda3D_PosedBoneWorldPos(modelId, 20, rh)) {
            Vec3f midLocal = { (lh[0] + rh[0]) * 0.5f, (lh[1] + rh[1]) * 0.5f, (lh[2] + rh[2]) * 0.5f };
            Vec3f midWorld;
            Matrix_MultVec3f(&midLocal, &midWorld);
            Math_Vec3f_Copy(&player->heldActor->world.pos, &midWorld);
        }
        // #85b: also make the carried actor FACE with Link. N64 sets this in Player_PostLimbDrawGameplay
        // (z_player_lib.c ~1845, the PLAYER_STATE1_CARRYING_ACTOR branch) — which z_player.c SKIPS when
        // this replacement draws, so without it the cucco keeps its pickup-time facing and never rotates
        // as Link turns/walks. Reproduce N64 exactly: world.rot.y = shape.rot.y = Link's facing yaw plus
        // unk_3BC.y, the held-vs-Link yaw offset captured at pickup (z_player.c ~10459). The actor's own
        // Draw uses shape.rot.y, so set both. The X-rot-influence variant (heavy/tilted carries) keeps
        // the actor's own X and is left untouched. Position behavior above is unchanged.
        if (player->stateFlags1 & PLAYER_STATE1_CARRYING_ACTOR &&
            (player->heldActor->flags & ACTOR_FLAG_CARRY_X_ROT_INFLUENCE) == 0) {
            s16 faceYaw = (s16)(player->actor.shape.rot.y + player->unk_3BC.y);
            player->heldActor->world.rot.y = faceYaw;
            player->heldActor->shape.rot.y = faceYaw;
        }
    }
    // #16(b): keep Link's actor.focus.pos (the head world position) live. N64 sets it in
    // Player_PostLimbDrawGameplay at the HEAD limb (z_player_lib.c) inside Player_DrawGameplay,
    // which z_player.c SKIPS when this replacement draws — so without this focus.pos goes STALE
    // and the first-person (C-up) camera, which reads Actor_GetFocus()->pos, snaps to where the
    // head USED to be (#16 reopen: "3DS Link first-person camera moves to the wrong place"). Same
    // mechanism/pattern as the #6 held-actor hand anchor above: take the posed head bone origin
    // (childlink_v2 OoT3D bone 11 — the HEAD; eyes/mouth bind there, Y~=3226. b10 is the extra
    // CHEST bone at Y~=2665, NOT the head — see zelda3d_link_bonecorr.inc spine-shift fix) in
    // model-local space and lift it to world via the matrix stack top (player world transform
    // just loaded). Faithful: position only, every frame.
    {
        float hd[3];
        if (gZelda3dFocusFix && Zelda3D_PosedBoneWorldPos(modelId, 11, hd)) {
            Vec3f hdLocal = { hd[0], hd[1], hd[2] };
            Vec3f hdWorld;
            Matrix_MultVec3f(&hdLocal, &hdWorld);
            Math_Vec3f_Copy(&actor->focus.pos, &hdWorld);
        }
        if (gZelda3dAnimDebug) {
            float b9[3] = { 0 }, b10[3] = { 0 }, b11[3] = { 0 };
            Zelda3D_PosedBoneWorldPos(modelId, 9, b9);
            Zelda3D_PosedBoneWorldPos(modelId, 10, b10);
            Zelda3D_PosedBoneWorldPos(modelId, 11, b11);
            Vec3f l9 = { b9[0], b9[1], b9[2] }, w9;
            Matrix_MultVec3f(&l9, &w9);
            Vec3f l10 = { b10[0], b10[1], b10[2] }, w10;
            Matrix_MultVec3f(&l10, &w10);
            Vec3f l11 = { b11[0], b11[1], b11[2] }, w11;
            Matrix_MultVec3f(&l11, &w11);
            static int fdbg = 0;
            if ((fdbg++ % 30) == 0) {
                fprintf(stderr,
                        "SOH3D FOCUS dbg b9=(%.0f,%.0f,%.0f) b10=(%.0f,%.0f,%.0f) b11=(%.0f,%.0f,%.0f) "
                        "world=(%.0f,%.0f,%.0f)\n",
                        w9.x, w9.y, w9.z, w10.x, w10.y, w10.z, w11.x, w11.y, w11.z, actor->world.pos.x,
                        actor->world.pos.y, actor->world.pos.z);
                fflush(stderr); // these lines go to stderr; flushing stdout never flushed them
            }
        }
    }
    Zelda3D_GL_EmitPose(modelId); // capture the CSAB-posed skin matrices
    gSPZelda3DDraw(POLY_OPA_DISP++, modelId | (int)0x80000000, tint[0], tint[1], tint[2]);
    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}

// #79 diagnostic: report the feet-grounding offset Zelda3D_PosedGroundOffset computes for Link's
// CURRENT cached pose (the most recent draw — so it reflects whatever CSAB is live, including a
// `linkanim`-forced climb clip), plus the resolved CSAB name. groundOff = -(lowest visible posed
// vertex Y); the draw lands that lowest vertex on actor.world.pos.y. If a climb pose's lowest point
// is NOT the feet (knees/tucked foot), groundOff differs from idle and the WHOLE body is shoved
// up/down for the same actor.y — the suspected "teleports upward while climbing" mechanism. Uses the
// real per-frame mid-mask so it matches the live draw. Returns groundOff; writes the CSAB to outCsab.
float Zelda3D::PlayerBehavior::groundDiag(PlayState* play, const char** outCsab) {
    Player* player = GET_PLAYER(play);
    const char* zar =
        (LINK_AGE_IN_YEARS == YEARS_CHILD) ? "/actor/zelda_link_child_new.zar" : "/actor/zelda_link_boy_new.zar";
    int modelId = Zelda3D_AutoModelId(zar);
    if (modelId < 0) {
        if (outCsab)
            *outCsab = "(no model)";
        return 0.0f;
    }
    if (outCsab) {
        if (gZelda3dLinkForceCsab[0] != '\0') {
            *outCsab = gZelda3dLinkForceCsab;
        } else {
            const char* c = Zelda3D_ResolvePlayerCsab((const char*)player->skelAnime.animation);
            c = Zelda3D_LinkWalkRunGate(c, player->actor.speedXZ); // report the gated (drawn) CSAB
            *outCsab = c ? c : ZELDA3D_LINK_IDLE_CSAB " (fallback)";
        }
    }
    return Zelda3D_PosedGroundOffset(modelId, P().midmask.compute(player));
}
