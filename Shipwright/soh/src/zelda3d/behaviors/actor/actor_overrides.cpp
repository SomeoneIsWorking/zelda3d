// Zelda3D actor draw-override glue — see actor_overrides.h for why these live grouped here
// instead of split per-actor. Ported out of core/zelda3d.c and render/zelda3d_render.cpp during
// the phase-3 codebase reorg (2026-07).
#include "actor_overrides.h"
#include "z64.h"
#include "../../zelda3d.h" // gZelda3dAnimDebug
#include "overlays/actors/ovl_En_Ge1/z_en_ge1.h" // EnGe1 (read live SkelAnime state)
#include "overlays/actors/ovl_En_Ko/z_en_ko.h"   // EnKo ENKO_TYPE_* (shared-CMB head-variant select)
#include "objects/object_ge1/object_ge1.h"       // dgGerudoWhite*Anim OTR-path strings

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// Forward-declared here rather than pulled in via a shared header, matching the existing pattern
// used by ported actor behaviors (e.g. behaviors/actor/door.cpp, kokiri_kid.cpp) for these small
// cross-module C-linkage bridges defined in anim/zelda3d_anim.cpp / model/zelda3d_model.cpp.
extern "C" {
void Zelda3D_SetBoneRotDelta(int modelId, int boneId, float rx, float ry, float rz);
void Zelda3D_ClearBoneRotDeltas(int modelId);
const char* Zelda3D_AutoModelZar(int modelId);
}

// ===========================================================================
// En_Ge1 (white Gerudo) N64-animation port
// ===========================================================================

// En_Ge1 (white Gerudo): map her N64 animation -> the OoT3D CSAB, and phase-sync to her
// SkelAnime clock. The N64 actor stores the current anim as an OTR-path string in
// this->animation (SoH ALIGN_ASSET pattern), so identify it by strcmp. Mapping (by use
// site in z_en_ge1.c): Idle->ge1_s_wait, Clap(open-gate)->ge1_mon_akeru, Dismissive
// (post-talk reaction)->ge1_hanasi. ge1_matsu is unused by this actor's 3 N64 anims.
const char* Zelda3D_ResolveAnim_EnGe1(Actor* actor) {
    EnGe1* ge = (EnGe1*)actor;
    const char* n64 = (const char*)ge->animation;
    const char* csab = "ge1_s_wait"; // idle / unknown
    if (n64 != NULL) {
        if (strcmp(n64, dgGerudoWhiteClapAnim) == 0) {
            csab = "ge1_mon_akeru";
        } else if (strcmp(n64, dgGerudoWhiteDismissiveAnim) == 0) {
            csab = "ge1_hanasi";
        }
    }
    if (gZelda3dAnimDebug) {
        static int dbg = 0;
        if ((dbg++ % 20) == 0) {
            fprintf(stderr, "SOH3D anim: csab=%s curFrame=%.2f animLength=%.2f n64=%s\n",
                   csab, ge->skelAnime.curFrame, ge->skelAnime.animLength, n64 ? n64 : "(null)");
            fflush(stdout);
        }
    }
    return csab;
}

// En_Ge1 joints for the N64-animation port: hand back &jointTable[1] (per-limb binang
// rotations; skip jointTable[0] root translation) + limbCount. The OoT3D geldwoman skeleton
// is the SAME rig as N64 En_Ge1 (15 limbs, same order: WAIST, L/R legs ×3, TORSO, L/R arms
// ×3, HEAD), so OoT3D bone i == N64 limb (i+1) and Zelda3D_UpdateAnimN64 maps bone i <- rots[i].
int Zelda3D_Joints_EnGe1(Actor* actor, const s16** outJointRots, int* outLimbCount) {
    EnGe1* ge = (EnGe1*)actor;
    if (ge->skelAnime.jointTable == NULL || ge->skelAnime.limbCount <= 0) {
        return 0;
    }
    *outJointRots = (const s16*)&ge->skelAnime.jointTable[1]; // [0] = root translation, skip it
    *outLimbCount = ge->skelAnime.limbCount;
    return 1;
}

// ===========================================================================
// En_Ko (Kokiri kid) per-type CSAB override
// ===========================================================================

// #87: per-ENKO_TYPE CSAB override for Kokiri kids, grounded in OoT3D GROUND TRUTH (the running 3DS
// game is authoritative; its per-type pose selection DIVERGES from the N64 sOsAnimeLookup table, so
// the live N64 animation alone cannot pick the right OoT3D pose for some types). Returns a CSAB base
// to force for this actor's ENKO_TYPE, or NULL to leave the normal N64-anim mapping in place. The
// resolver only applies it if the CSAB actually exists in this kid's zar.
//
// ALL 8 Kokiri Forest types are overridden here, keyed to the oracle animLength discriminator:
//   oracle = python3 oot3d-decomp/tools/enko_anim.py  (Kokiri Forest, scene 85)
//
// Two failure modes this addresses:
//   - COLLAPSE: types 1, 5, 6 all play gKokiriStandUpAnim in N64 (a 4-frame stub) -> animmap
//     collapses all to km1_ukiuki_wait (34f). OoT3D plays distinct 25f, 21f, 25f poses.
//   - DIVERGENCE: the N64-mapped CSAB duration simply does not match what OoT3D plays. E.g.
//     type 3 (blocking): N64 full gKokiriBlockingAnim->km1_udekumi_pose (25f), OoT3D animLen=14.
//     type 2 (punch): N64 gKokiriPunchingAnim->km1_ukiuki_wait (34f), OoT3D animLen=20.
//     type 12 (Fado): N64 gKokiriIdleAnim->km1_ukiuki_wait (34f), OoT3D animLen=40.
//
// CSAB pick methodology: animLength is the stable discriminator (oracle_export.py --json).
//   When multiple CSABs share the same duration, name semantics + model (km1=boy, kw1=girl) disambiguate.
//   Each entry below is annotated with [oracle animLen] and the reasoning.
//   Types 0 and 4 ARE correct via the N64 mapping alone, but are overridden here for explicitness.
//
// Verification: run oot3d-decomp/tools/enko_anim.py to re-read oracle; compare CSAB durations.
const char* Zelda3D_EnKoCsabOverride(int modelId, Actor* actor) {
    (void)modelId;
    if (actor == NULL || actor->id != ACTOR_EN_KO) {
        return NULL;
    }
    switch (actor->params & 0xFF) {
        // BOY types (km1 skeleton):
        case ENKO_TYPE_CHILD_0:
            // oracle animLen=19; km1_ishi_wait(19) = "stone/lift-wait", matches N64 LIFTING_ROCK.
            return "km1_ishi_wait";
        case ENKO_TYPE_CHILD_2:
            // oracle animLen=20; km1_ijiiji(20) = fidget/impatient, fits energetic punching-type boy.
            // Other 20f: asekaki_wait(wipe forehead), osiete_wait(explain), utsumuki_pose(look down).
            return "km1_ijiiji";
        case ENKO_TYPE_CHILD_3:
            // oracle animLen=14; km1_out_in_pose3(14) is the ONLY 14f CSAB -> unique match.
            return "km1_out_in_pose3";
        case ENKO_TYPE_CHILD_4:
            // oracle animLen=19; km1_kusakari(19) = "grass cutting", matches N64 CUTTING_GRASS.
            // Also 19f in kw1: km1_ishi_wait -- but km1_kusakari is semantically correct.
            return "km1_kusakari";
        // GIRL types (kw1 skeleton):
        case ENKO_TYPE_CHILD_1:
            // oracle animLen=25; kw1 25f CSABs: kw1_out_in_pose1, kw1_out_in_pose3, kw1_out_in_pose4,
            // km1_udekumi_pose, km1_out_in_pose2. N64: STANDUP_1/STANDING_HAND_ON_CHEST.
            // kw1_out_in_pose1 = first girl-specific standby pose (named kw1 = girl body).
            return "kw1_out_in_pose1";
        case ENKO_TYPE_CHILD_5:
            // oracle animLen=21; kw1_out_in_pose2(21) is the ONLY 21f CSAB -> unique match.
            // (Prior override km1_suwari_pose=24f was WRONG -- duration mismatch vs oracle=21.)
            return "kw1_out_in_pose2";
        case ENKO_TYPE_CHILD_6:
            // oracle animLen=25; same 25f pool as type 1. N64: STANDUP_3/STANDING_RIGHT_ARM_UP.
            // kw1_out_in_pose3 = third girl standby, distinct from pose1 (used for type 1).
            return "kw1_out_in_pose3";
        // FADO (kw1 skeleton):
        case ENKO_TYPE_CHILD_FADO:
            // oracle animLen=40; fad_n_wait(40) = "Fado normal wait" = idle standby.
            // Also 40f: fad_kusukusu(laugh) -- IDLE_NOMORPH -> wait, not laugh.
            return "fad_n_wait";
        default:
            return NULL;
    }
}

// ===========================================================================
// En_Hy (adult townsfolk) per-type idle CSAB override
// ===========================================================================

// En_Hy adult townsfolk: per-ENHY_TYPE idle CSAB override, grounded in OoT3D oracle animLength.
// Only types whose idle CSAB differs from the body's Zelda3D_AutoModelDefaultAnim selection are
// listed.  Others fall through to *_matsu (or the first idle-matching CSAB) which is correct.
//
// Oracle ground truth (Market Day, scene 32; tools/oracle_export.py animLength column):
//   type 3  (BOJ_3,  ENHY_ANIM_15): oracle 16f → Boj2_9_2 (16f, only remaining 16f BOJ CSAB)
//   type 4  (AHG_4,  ENHY_ANIM_11): oracle 20f → Ahg2_8   (20f); auto picks Ahg_matsu (wrong)
//   type 5  (BOJ_5,  ENHY_ANIM_16): oracle 11f → Boj2_9   (11f, UNIQUE)
//   type 6  (BBA,    ENHY_ANIM_10): oracle 40f → Bba_n_wait(40f); auto picks Bba_matsu (wrong)
//   type 8  (CNE_8,  ENHY_ANIM_9 ): oracle 40f → Cne_n_wait (40f, UNIQUE); matsu=20f wrong
//   type 9  (BOJ_9,  ENHY_ANIM_13): oracle 30f → Boj_13   (30f); suffix=anim_idx naming
//   type 10 (BOJ_10, ENHY_ANIM_14): oracle 23f → Boj_14   (23f, UNIQUE)
//   type 11 (CNE_11, ENHY_ANIM_20): oracle 12f → Cne2_15  (12f, UNIQUE)
//   type 12 (BOJ_12, ENHY_ANIM_18): oracle 30f → Boj2_17  (30f); auto picks Boj_matsu (wrong)
//   type 13 (AHG_13, ENHY_ANIM_12): static 20f → Ahg2_18  (20f); auto picks Ahg_matsu (wrong)
//   type 14 (BOJ_14, ENHY_ANIM_19): static 30f → Boj2_19 (30f); Boj_matsu=30f same dur wrong
//   type 15 (BJI_15, ENHY_ANIM_21): static 40f → Bji2_20  (40f); auto picks Bji_matsu (wrong)
//   type 16 (BOJ_16, ENHY_ANIM_5 ): static 30f → Boj_matsu(30f) — auto picks Boj_matsu, CORRECT
//   type 17 (AHG_17, ENHY_ANIM_11): static 20f → Ahg2_8   (20f, same as type 4)
//   type 19 (BJI_19, ENHY_ANIM_21): static 40f → Bji2_20  (40f, same as type 15)
//   type 20 (AHG_20, ENHY_ANIM_12): static 20f → Ahg2_18  (20f, same as type 13)
// zelda_aob.zar: Aob_mastu(20), Aob_tataku_roop(15), Aob_te_wait(15), Aob_n_wait(15).
// zelda_ahg.zar: Ahg_matsu(20), Ahg2_18(20), Ahg2_8(20), sth_oya_matsu(28).
// zelda_bba.zar: Bba_matsu(40), Bba_n_wait(40).
// zelda_bji.zar: Bji_matsu(40), Bji_aruku(60), Bji2_20(40).
// zelda_boj.zar: Boj_13(30), Boj_14(23), Boj_matsu(30), Boj2_5(16), Boj2_9(11),
//   Boj2_9_2(16), Boj2_17(30), Boj2_19(30). zelda_cne.zar: Cne_matsu(20), Cne_n_wait(40), Cne2_15(12).
const char* Zelda3D_EnHyCsabOverride(int modelId, Actor* actor) {
    (void)modelId;
    if (actor == NULL || actor->id != ACTOR_EN_HY) {
        return NULL;
    }
    switch (actor->params & 0x7F) { // ENHY_TYPE
        // AHG body skeleton (zelda_ahg.zar) — auto picks Ahg_matsu first; oracle/static says otherwise:
        case 4:   // ENHY_TYPE_AHG_4: ENHY_ANIM_11 → oracle 20f → Ahg2_8 (pool 11, csab_idx 2)
        case 17:  // ENHY_TYPE_AHG_17: same pool as type 4 → Ahg2_8 (20f)
            return "Ahg2_8";
        case 13:  // ENHY_TYPE_AHG_13: ENHY_ANIM_12 → Ahg2_18 (pool 12, csab_idx 1, 20f)
        case 20:  // ENHY_TYPE_AHG_20: same pool as type 13 → Ahg2_18 (20f)
            return "Ahg2_18";
        // BBA body skeleton (zelda_bba.zar) — auto picks Bba_matsu first; oracle says Bba_n_wait:
        case 6:   // ENHY_TYPE_BBA: ENHY_ANIM_10 → oracle 40f → Bba_n_wait (pool 10, csab_idx 1)
                  // Both are 40f; Bba_matsu is the wrong one (auto picks by first "matsu" hit).
            return "Bba_n_wait";
        // BJI body skeleton (zelda_bji.zar) — auto picks Bji_matsu; static says Bji2_20 for types 15/19:
        case 15:  // ENHY_TYPE_BJI_15: ENHY_ANIM_21 → Bji2_20 (pool 21, csab_idx 2, 40f)
        case 19:  // ENHY_TYPE_BJI_19: same pool as type 15 → Bji2_20 (40f)
            return "Bji2_20";
        // BOJ body skeleton (zelda_boj.zar) — types where OoT3D idle ≠ Boj_matsu:
        case 3:   // ENHY_TYPE_BOJ_3: ENHY_ANIM_15 → oracle 16f → Boj2_5 (pool 15, csab_idx=1)
                  // csab_zar_idx=1 in zelda_boj.zar is Boj2_5 (16f); prior code returned Boj2_9_2
                  // by mistake (assumed Boj2_5 was "taken by type16", but type16 uses Boj_matsu idx0).
            return "Boj2_5";
        case 5:   // ENHY_TYPE_BOJ_5: ENHY_ANIM_16 → oracle 11f → Boj2_9 (11f, UNIQUE in BOJ ZAR)
            return "Boj2_9";
        case 9:   // ENHY_TYPE_BOJ_9: ENHY_ANIM_13 → oracle 30f → Boj_13 (30f, naming suffix=13)
            return "Boj_13";
        case 10:  // ENHY_TYPE_BOJ_10: ENHY_ANIM_14 → oracle 23f → Boj_14 (23f, UNIQUE in BOJ ZAR)
            return "Boj_14";
        case 12:  // ENHY_TYPE_BOJ_12: ENHY_ANIM_18 → oracle 30f → Boj2_17 (pool 18, csab_idx 4)
                  // Auto picks Boj_matsu (30f, also "matsu" first); oracle confirms Boj2_17 is correct.
            return "Boj2_17";
        case 14:  // ENHY_TYPE_BOJ_14: ENHY_ANIM_19 → Boj2_19 (30f, naming suffix=19)
                  // Same duration as Boj_matsu but explicit to avoid drift if default changes.
            return "Boj2_19";
        case 16:  // ENHY_TYPE_BOJ_16: ENHY_ANIM_5  → Boj_matsu (30f) — auto already correct; explicit
                  // for documentation (Boj_matsu IS the right CSAB here, index 0 via pool 5).
            return NULL;
        // CNE body skeleton (zelda_cne.zar):
        case 8:   // ENHY_TYPE_CNE_8: ENHY_ANIM_9  → oracle 40f → Cne_n_wait (40f, UNIQUE)
                  // Zelda3D_AutoModelDefaultAnim picks Cne_matsu (20f) via "matsu" keyword first;
                  // oracle says 40f → must override to Cne_n_wait.
            return "Cne_n_wait";
        case 11:  // ENHY_TYPE_CNE_11: ENHY_ANIM_20 → oracle 12f → Cne2_15 (12f, UNIQUE)
            return "Cne2_15";
        default:
            return NULL;
    }
}

// ===========================================================================
// Cucco (En_Niw) procedural wing-flap replay
// ===========================================================================

// --- Procedural OverrideLimbDraw replay (#23 cucco wing-flap) -------------------------------------
// Some N64 actors animate a few limbs PROCEDURALLY in their SkelAnime overrideLimbDraw callback
// (rot->axis += value) rather than in any animation — the cucco wing-flap (EnNiw_OverrideLimbDraw,
// limbs 7 & 11, local Z) is the canonical case. The OoT3D auto-replace path plays the actor's CSAB
// but drops that callback, so the flap is missing. We capture the override callback the actor
// passed to SkelAnime_Draw*, PROBE it per limb to recover the additive rotation delta, map the N64
// limb -> OoT3D bone, and feed the delta to the OoT3D bone's local rotation (Zelda3D_SetBoneRotDelta).
// The 6-arg Opa and 7-arg Draw override types share their first 6 args' ABI; `kind` distinguishes
// them so the probe passes the right argument count. Generalises to any procedural-override actor.
static void* gZelda3dPendingOverride = NULL;
static void* gZelda3dPendingOverrideArg = NULL;
static int gZelda3dPendingOverrideKind = 0; // 0 = OverrideLimbDrawOpa (6 args), 1 = OverrideLimbDraw (7)

void Zelda3D_SetLimbOverride(void* overrideFn, void* arg, int kind) {
    gZelda3dPendingOverride = overrideFn;
    gZelda3dPendingOverrideArg = arg;
    gZelda3dPendingOverrideKind = kind;
}

typedef s32 (*Zelda3dOverrideOpaFn)(PlayState*, s32, Gfx**, Vec3f*, Vec3s*, void*);
typedef s32 (*Zelda3dOverride7Fn)(PlayState*, s32, Gfx**, Vec3f*, Vec3s*, void*, Gfx**);

// One row per (N64 limb -> OoT3D bone) procedural-rotation correspondence, keyed by actor ZAR.
// The N64 limb-local rotation delta and the OoT3D bone-local frame differ by a constant rest-frame
// rotation, which (for these rigs) is a signed AXIS PERMUTATION. For each OoT3D bone axis o in
// {x,y,z}, srcAxis[o] picks which N64 limb axis feeds it (0=x,1=y,2=z; -1 = leave 0) and srcSign[o]
// is its multiplier. This generalises the old same-axis-only sign[] so a flap whose N64 input is on
// one axis can drive a DIFFERENT OoT3D bone axis.
//
// Cucco (En_Niw) wing bones 3 & 5: derived as the unique PROPER rotation (det=+1) consistent with
// (a) idle flap N64-z -> OoT3D-z, +1 (pre-#23, verified visually) and (b) the agitated wing-LIFT,
// which N64 drives on its limb-local y (unk_26C[5]/[7]=25000) and which on the OoT3D bone is the
// local -x (probed: -x lifts the wing up; +x folds it down). That forces y->x(-1), x->y(+1),
// z->z(+1): a 90deg roll about the shared wing-Z axis. Verified live A/B (cuccopose) vs the N64
// model: idle Z-flap unchanged, agitated wing now lifts+fans like N64 instead of spreading flat.
typedef struct {
    const char* zar;
    s8 n64Limb;
    s8 oot3dBone;
    s8 srcAxis[3];  // OoT3D bone axis [x,y,z] <- this N64 limb axis (0=x,1=y,2=z; -1 = none)
    f32 srcSign[3]; // multiplier applied to that N64 axis' binang delta
} Zelda3dProcOverrideRow;
static const Zelda3dProcOverrideRow kZelda3dProcOverride[] = {
    // cucco: N64 wing limbs 7 & 11 -> OoT3D WING bones 4 & 6. (Bones 3 & 5 are the FEET — low,
    // trans.y=-640, 38 verts; the wings are bones 4 & 6 — high on the sides, meanPos y~901, 65 verts.
    // #5: the flap was previously mis-mapped onto the feet, so the rendered wing never moved despite
    // a correct time-varying delta. Verified by `bonestats`/`bonerot` sweep — bone 4/6 fold the wing
    // fan, 3/5 only twitch a foot.) Axis permutation re-derived for the wing bones' local frame.
    { "/actor/zelda_nw.zar", 7, 4, { 1, 0, 2 }, { -1.0f, 1.0f, 1.0f } },
    { "/actor/zelda_nw.zar", 11, 6, { 1, 0, 2 }, { -1.0f, 1.0f, 1.0f } },
};

// Verification gate (env ZELDA3D_PROCOVERRIDE, default ON; REPL `wingflap <0|1>`): when 0 the
// procedural-override replay is skipped (the OoT3D actor plays only its CSAB) so the flap can be
// A/B'd in the same scene. gZelda3dWingForce >= 0 forces a fixed binang on the mapped Z axis (REPL
// `wingflap force <binang>`) to confirm the flap DIRECTION/amplitude visually.
int gZelda3dProcOverride = -1;
int gZelda3dWingForce = -1;
int gZelda3dForceCuccoAgitate = 0; // #5 diagnostic: hold cuccos in the agitated wing-spread pose
int gZelda3dCuccoState = -1;        // #5 force func_80AB5BF8 arg (-1 = live AI); see zelda3d.h
int gZelda3dCuccoDbgPhase = -1;     // #5 last cucco's flap phase (unk_29C)
short gZelda3dCuccoDbgWing[6] = { 0, 0, 0, 0, 0, 0 }; // #5 limb7 xyz, limb11 xyz applied this frame
int gZelda3dCuccoHeld = 0;          // #5 force the held-by-Link carried state (func_80AB6BF8)

// #5 derivation probe: when active, force a fixed rotation (binang) DIRECTLY on the OoT3D wing
// bones' local x/y/z, bypassing the N64->bone sign map — to discover which OoT3D bone axis is the
// "lift"/"fan" so the multi-axis agitated mapping can be derived. REPL `wingprobe <x> <y> <z>`.
int gZelda3dWingProbeActive = 0;
int gZelda3dWingProbe[3] = { 0, 0, 0 };
// #5 wing-bone identification: persistently rotate ONE arbitrary CMB bone of the drawn auto model
// (binang), surviving the per-frame ClearBoneRotDeltas, so each bone can be swept to find which one
// actually moves the wing geometry. REPL `bonerot <id> <rx> <ry> <rz>` (id<0 = off).
int gZelda3dDbgBone = -1;
int gZelda3dDbgBoneRot[3] = { 0, 0, 0 };
// #5 LIVE proc-override axis-map override (REPL `wingmap`), so the N64-limb->OoT3D-bone signed
// permutation can be searched headless without a rebuild. src[0]<0 = inactive (use table rows).
// src[o] = which N64 axis (0=x,1=y,2=z, -1=none) feeds OoT3D bone axis o; sign[o] = its multiplier.
int gZelda3dWingMapSrc[3] = { -1, -1, -1 };
int gZelda3dWingMapSign[3] = { 1, 1, 1 };
// #5 HAND-WOVEN cucco flap: the N64 procedural wing rotation can't be replayed onto the 3DS rig
// (its wing rest pose is already spread, so the deltas don't compose). Instead author the flap
// directly on the 3DS wing bones (4 & 6): oscillate them on their local Y axis (bonerot showed y-
// = wing up, y+ = down for BOTH wings) between a center and an amplitude, driven by the N64 flap
// INTENSITY (so idle/agitated/still scale naturally). REPL `chickflap`. Default-on once tuned.
int gZelda3dChickFlap = 1;       // 1 = hand-woven flap replaces the replay for the cucco
int gZelda3dChickAxis = 1;       // OoT3D bone-local axis to flap on (1 = Y)
int gZelda3dChickCenter = -4000; // baseline offset (binang): slight raise from the spread rest
int gZelda3dChickAmp = 14000;    // peak flap amplitude (binang) at full agitation
float gZelda3dChickFreq = 0.9f;  // oscillation phase advance per draw (rad); frantic flap
int gZelda3dChickBone2Sign = -1; // #5: the 3DS rig's wing bones 4 & 6 have MIRRORED local frames, so
                               // the same signed local-Y angle rotates them in the SAME world sense
                               // (parallel, not mirrored) -> asymmetric flap. Negate bone 6 so its
                               // local rotation is the world-space mirror of bone 4. (The old +1
                               // "both y- = up" assumption was never L/R-verified in a held run;
                               // playtest 2026-06-20 showed asymmetry.)
int gZelda3dFrameCtr = 0;        // ++ once per rendered frame (Zelda3D_EmitRenderPass); flap phase clock

// Probe the captured override callback for each mapped limb of the current auto actor and push the
// resulting per-bone local-rotation delta (binang -> radians) onto the OoT3D model. No-op when no
// override was captured or this ZAR has no procedural-override rows.
void Zelda3D_ApplyProcOverride(PlayState* play, int modelId, Vec3s* jointTable, int limbCount) {
    Zelda3D_ClearBoneRotDeltas(modelId); // stale-delta guard (model may be drawn via a path w/o probe)
    if (gZelda3dProcOverride < 0) {
        const char* v = getenv("ZELDA3D_PROCOVERRIDE");
        gZelda3dProcOverride = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    if (!gZelda3dProcOverride || gZelda3dPendingOverride == NULL || jointTable == NULL) {
        return;
    }
    const char* zar = Zelda3D_AutoModelZar(modelId);
    if (zar == NULL) {
        return;
    }
    const float kBinangToRad = 3.14159265358979f / 32768.0f;
    // Sample ONCE PER CALL (not per row) so the per-row prints below don't alias with the row order
    // (2 rows/frame in fixed order + a shared %N counter would only ever show row 0). When sampled,
    // every row in this call prints — so both wing bones are visible each sampled frame.
    int sampleThisCall = 0;
    if (gZelda3dAnimDebug) {
        static int callCtr = 0;
        sampleThisCall = ((callCtr++ % 20) == 0);
    }
    // #5 hand-woven cucco flap: phase clocked off the per-FRAME counter (not per draw call) so the
    // beat rate is independent of how many cuccos are on screen (each one calls this per frame).
    int isCucco = (strcmp(zar, "/actor/zelda_nw.zar") == 0);
    double chickPhase = (double)gZelda3dFrameCtr * gZelda3dChickFreq;
    for (s32 i = 0; i < (s32)ARRAY_COUNT(kZelda3dProcOverride); i++) {
        const Zelda3dProcOverrideRow* row = &kZelda3dProcOverride[i];
        if (strcmp(row->zar, zar) != 0) {
            continue;
        }
        if (row->n64Limb < 0 || row->n64Limb >= limbCount) {
            if (sampleThisCall) {
                fprintf(stderr, "[WINGFLAP-SKIP] zar=%s n64limb=%d->bone=%d SKIPPED (limbCount=%d)\n",
                        zar, row->n64Limb, row->oot3dBone, limbCount);
                fflush(stderr);
            }
            continue;
        }
        // BACKLOG-specified delta = (override-applied rot) - jointTable rot. jointTable[0] is the
        // root translation, so limb i's rotation is jointTable[i+1] (matches the N64 draw walk).
        Vec3s rot = jointTable[row->n64Limb + 1];
        Vec3s before = rot;
        Gfx* dummyDl = NULL;
        Gfx* dummyGfx = NULL;
        Vec3f pos = { 0.0f, 0.0f, 0.0f };
        if (gZelda3dPendingOverrideKind == 0) {
            ((Zelda3dOverrideOpaFn)gZelda3dPendingOverride)(play, row->n64Limb, &dummyDl, &pos, &rot,
                                                        gZelda3dPendingOverrideArg);
        } else {
            ((Zelda3dOverride7Fn)gZelda3dPendingOverride)(play, row->n64Limb, &dummyDl, &pos, &rot,
                                                      gZelda3dPendingOverrideArg, &dummyGfx);
        }
        s16 dd[3] = { (s16)(rot.x - before.x), (s16)(rot.y - before.y), (s16)(rot.z - before.z) };
        if (gZelda3dWingForce >= 0) {
            dd[0] = dd[1] = 0;
            dd[2] = (s16)gZelda3dWingForce; // direction/amplitude probe (applied on the mapped Z axis)
        }
        // Route each N64 limb axis to its OoT3D bone axis via the signed permutation (rest-frame diff).
        // A LIVE override (REPL `wingmap`) replaces the table's srcAxis/srcSign for fast headless
        // derivation without a rebuild; -1 (default) = use the table row.
        f32 out[3] = { 0.0f, 0.0f, 0.0f };
        for (s32 o = 0; o < 3; o++) {
            int src = (gZelda3dWingMapSrc[0] >= 0) ? gZelda3dWingMapSrc[o] : row->srcAxis[o];
            f32 sign = (gZelda3dWingMapSrc[0] >= 0) ? (f32)gZelda3dWingMapSign[o] : row->srcSign[o];
            if (src >= 0) {
                out[o] = (f32)dd[src] * kBinangToRad * sign;
            }
        }
        f32 dx = out[0], dy = out[1], dz = out[2];
        if (gZelda3dWingProbeActive) {
            // direct OoT3D-bone-local probe (derivation only): same delta on both wing bones
            dx = (f32)gZelda3dWingProbe[0] * kBinangToRad;
            dy = (f32)gZelda3dWingProbe[1] * kBinangToRad;
            dz = (f32)gZelda3dWingProbe[2] * kBinangToRad;
        }
        if (sampleThisCall) {
            fprintf(stderr, "[WINGFLAP] zar=%s n64limb=%d->bone=%d n64binang=(%d,%d,%d) -> oot rad=(%.3f,%.3f,%.3f)\n",
                    zar, row->n64Limb, row->oot3dBone, dd[0], dd[1], dd[2], dx, dy, dz);
            fflush(stderr);
        }
        if (gZelda3dChickFlap && isCucco) {
            // HAND-WOVEN flap: oscillate this wing bone on chickAxis, amplitude scaled by the N64
            // flap INTENSITY (so idle/agitated/still differ), around chickCenter. Ignores the
            // (ill-composed) replay output dx/dy/dz entirely. bone 6 mirrors bone 4 via Bone2Sign.
            s16 a0 = dd[0] < 0 ? (s16)-dd[0] : dd[0];
            s16 a1 = dd[1] < 0 ? (s16)-dd[1] : dd[1];
            s16 a2 = dd[2] < 0 ? (s16)-dd[2] : dd[2];
            s16 mag = a0; if (a1 > mag) mag = a1; if (a2 > mag) mag = a2;
            f32 instInten = (f32)mag / 25000.0f;
            if (instInten > 1.0f) instInten = 1.0f;
            // PEAK-HOLD the intensity: the N64 wing delta oscillates fast (8000<->25000), so using
            // it directly pulses the flap amplitude (every other beat goes weak -> looks sluggish).
            // Hold the peak and decay slowly so an agitated cucco flaps at full, steady amplitude
            // while a calming one fades out. Per oot3dBone so both wings track together.
            static f32 sHeld[8] = { 0 };
            int bi = row->oot3dBone & 7;
            if (instInten > sHeld[bi]) sHeld[bi] = instInten;
            else sHeld[bi] *= 0.97f;
            f32 inten = sHeld[bi];
            f32 ang = ((f32)gZelda3dChickCenter + (f32)gZelda3dChickAmp * inten * sinf((f32)chickPhase)) *
                      kBinangToRad;
            if (row->oot3dBone == 6) ang *= (f32)gZelda3dChickBone2Sign;
            f32 hf[3] = { 0.0f, 0.0f, 0.0f };
            int ax = (gZelda3dChickAxis >= 0 && gZelda3dChickAxis < 3) ? gZelda3dChickAxis : 1;
            hf[ax] = ang;
            Zelda3D_SetBoneRotDelta(modelId, row->oot3dBone, hf[0], hf[1], hf[2]);
        } else {
            Zelda3D_SetBoneRotDelta(modelId, row->oot3dBone, dx, dy, dz);
        }
    }
    // #5 wing-bone sweep: persistently rotate one arbitrary bone (survives the clear above) to find
    // which CMB bone actually drives the wing geometry.
    if (gZelda3dDbgBone >= 0) {
        Zelda3D_SetBoneRotDelta(modelId, gZelda3dDbgBone, (f32)gZelda3dDbgBoneRot[0] * kBinangToRad,
                              (f32)gZelda3dDbgBoneRot[1] * kBinangToRad,
                              (f32)gZelda3dDbgBoneRot[2] * kBinangToRad);
    }
}
