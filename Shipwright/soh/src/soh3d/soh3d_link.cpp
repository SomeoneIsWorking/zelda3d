// SoH3D — OoT3D Link (player) replacement POLICY (split out of soh3d.c; pure refactor, no behavior
// change). Owns the player draw, the equipment mesh-id mask, the per-bone retarget correction table
// + hand-weave state, the pose-freeze, linkpin, the cucco-grab repro driver, and all the `link*`
// REPL commands. The LOW-LEVEL CMB skeleton forward-kinematics primitives stay in soh3d_model.cpp
// (SoH3D_UpdateAnimN64*, SoH3D_PosedGroundOffset, ...); this file CALLS them.
#include "soh3d.h"
#include "soh3d_link.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// childlink_v2 per-bone retarget correction (kLinkChildBoneCorr). Legs/neck stay on pure replace
// (no regression); the divergent spine/upper-arms are HAND-WOVEN (#7/#31) — a constant fitted C
// provably can't reconcile the re-authored OoT3D arm motion, so the arm corrections are tuned by
// hand against the OoT3D CSAB ground truth (linksrc 3ds), NOT auto-fit.
#include "soh3d_link_bonecorr.inc"

// HAND-WEAVE live-tune scaffold (#7 long arm). A mutable runtime copy of the table that the player
// path actually passes, so each upper-body bone's correction can be adjusted LIVE over the REPL
// (`linkcorr`) and seen in-game against the 3ds ground truth, instead of rebuild-per-guess. The
// final hand-tuned values get baked back into soh3d_link_bonecorr.inc (`linkcorr bake <path>`).
static SoH3dBoneCorr gLinkBoneCorr[25];
static int gLinkBoneCorrInit = 0;
// Pose-freeze for hand-weaving: latch the live player jointTable so the idle pose holds still and the
// only variable while tuning `linkcorr` is the correction itself (REPL `linkfreeze 1|0`).
static Vec3s gLinkFrozenJoints[40];
static int gLinkFrozenCount = 0; // >0 = pose frozen at this limbCount
static int gLinkFreezeReq = 0;   // 1 = latch the live pose on the next player draw
static void SoH3D_LinkBoneCorrEnsure(void) {
    if (gLinkBoneCorrInit) return;
    memcpy(gLinkBoneCorr, kLinkChildBoneCorr, sizeof(gLinkBoneCorr));
    gLinkBoneCorrInit = 1;
}
// Build a row-major 3x3 rotation C = Rz(cz)·Ry(cy)·Rx(cx) (the live/csab ZYX convention) from
// Euler degrees, written into out[9]. Identity when all zero.
static void SoH3D_EulerToMat3(float cxDeg, float cyDeg, float czDeg, float* out) {
    float rx = cxDeg * (3.14159265358979f / 180.0f);
    float ry = cyDeg * (3.14159265358979f / 180.0f);
    float rz = czDeg * (3.14159265358979f / 180.0f);
    float cx = cosf(rx), sx = sinf(rx), cy = cosf(ry), sy = sinf(ry), cz = cosf(rz), sz = sinf(rz);
    // Rz*Ry*Rx, row-major.
    out[0] = cz * cy;                 out[1] = cz * sy * sx - sz * cx;  out[2] = cz * sy * cx + sz * sx;
    out[3] = sz * cy;                 out[4] = sz * sy * sx + cz * cx;  out[5] = sz * sy * cx - cz * sx;
    out[6] = -sy;                     out[7] = cy * sx;                 out[8] = cy * cx;
}
// Inverse: recover (cx,cy,cz) degrees from a row-major 3x3 such that M = Rz·Ry·Rx. For display.
static void SoH3D_Mat3ToEuler(const float* m, float* outDeg) {
    float sy = -m[6];
    if (sy > 1.0f) sy = 1.0f; else if (sy < -1.0f) sy = -1.0f;
    float ry = asinf(sy), rx, rz;
    if (fabsf(m[6]) < 0.99999f) { rx = atan2f(m[7], m[8]); rz = atan2f(m[3], m[0]); }
    else { rx = atan2f(-m[5], m[4]); rz = 0.0f; }
    outDeg[0] = rx * (180.0f / 3.14159265358979f);
    outDeg[1] = ry * (180.0f / 3.14159265358979f);
    outDeg[2] = rz * (180.0f / 3.14159265358979f);
}

// --- OoT3D Link (player) replacement — proof-of-hook stage (see SoH3D_TryDrawPlayer) ---
int gSoH3dLinkOn = -1;  // sub-toggle (env SOH3D_LINK, default OFF — WIP) / REPL `link`
float gSoH3dLinkScale = 0.011f; // world scale OoT3D-link-local -> N64 player units (REPL `linkscale`, calibrate live)
float gSoH3dLinkRotX = 0.0f;   // rest->upright orientation correction (deg) (REPL `linkrot`)
float gSoH3dLinkRotY = 0.0f;
float gSoH3dLinkRotZ = 0.0f;
char gSoH3dLinkForceCsab[64] = ""; // REPL `linkanim <csab>` pins a CSAB on Link (verify idle/walk/run
                                   // deterministically without real movement input); empty = live-resolve
int gSoH3dClimbGroundFix = 0; // #79: freeze feet-grounding during climb poses (REPL `climbgroundfix`).
                              // DEFAULT OFF: root cause confirmed (grounding swings wildly on climb
                              // poses) but the freeze DIRECTION is not yet live-verified on a real
                              // sustained climb (repro blocked — see #79). Off = current behavior.
int gSoH3dHeldAttach = 1;          // #6 attach a carried actor (held cucco) to 3DS Link's posed hands
int gSoH3dFocusFix = 1;            // #16(b) keep actor.focus.pos at 3DS Link's posed head (first-person cam)
                                   // (A/B toggle for before/after evidence; REPL `linkheldfix <0|1>`)
// #7: CSAB frames advanced per draw per unit of Link's ground speed, when speed-driving the
// locomotion cycle (run/walk) — Link's run/walk advances its pose through a player-internal
// accumulator, NOT skelAnime.curFrame (pinned at 0 the whole run), so curFrame can't phase-lock the
// CSAB (it froze at frame 0 -> the motionless slide). A footstep cadence is a function of movement
// speed, so we free-run the CSAB at speedXZ * this gain. Calibrated live vs N64 (REPL `linkloco`).
float gSoH3dLinkLocoGain = 0.30f;
// Player animation SOURCE (REPL `linksrc`, env SOH3D_LINK_SRC). Two independent, both-working modes:
//   0 = 3DS own-CSAB: play the OoT3D link rig's own CSAB matching the named player anim (kPlayerAnimMap).
//       Faithful for discrete states (idle fidgets, jumps, item use) but BLIND to walk/run — OoT blends
//       locomotion into jointTable without naming an anim, so Link slides without a walk cycle.
//   1 = N64 retarget: drive the OoT3D rig from Link's LIVE skelAnime.jointTable every frame (the final
//       blended pose). Captures walk/run/everything exactly, in lockstep with the N64 body.
// User-selectable per [[soh3d-link-player-path]]; default N64 so locomotion works out of the box.
int gSoH3dLinkAnimSrc = -1;

// Per-frame mesh_id visibility mask for Link's body CMB. childlink_v2 bakes every hand-pose and
// held-equipment variant onto distinct mesh_ids; the game shows a state-dependent subset. We pick
// the visible set each frame (SoH3D_LinkComputeMidMask) and push it via SoH3D_GL_SetMidMask.
// gSoH3dLinkMidOverride: REPL `linkmid` debug override (identification sweep). ~0 sentinel = no
// override (use the computed policy); any other value forces that exact mask.
static unsigned long long gSoH3dLinkMidOverride = ~0ull; // ~0 = "no override" sentinel
static int gSoH3dLinkMidOverrideSet = 0;

// `linkjointdump` capture state: dump the live player jointTable over N consecutive frames to a CSV,
// for the QUANTITATIVE per-bone retarget-correction derivation (tools/soh3d_link_retarget_derive.py).
// Each row: cap,curFrame,animLength,anim,limb,x,y,z — cap=frame index, curFrame/animLength=skelAnime
// phase, anim=base name, limb=jointTable rotation index (1..limbCount; bonemap value m drives
// jointTable[1+m]), x/y/z=binang.
static FILE* gLinkJointDumpFile = NULL;
static int gLinkJointDumpRemaining = 0;
static int gLinkJointDumpCap = 0;

// `linkgrab` REPL: deterministically drive a REAL cucco pickup headless (for #6/#9). A plain A tap
// can't reliably grab — the cucco wanders out of range and the A rising edge has to coincide with
// the frame Link's interactRangeActor detection is true. This driver (run in SoH3D_WalkInject, just
// before Play_Update) holds the asel-selected actor pinned directly in front of Link's facing every
// frame AND re-issues a fresh A rising edge every few frames, so detection and edge always line up;
// it stops the instant player->heldActor is set (the engine's real grab action then takes over).
static int gSoH3dLinkGrabFrames = 0;

// Pin the PLAYER's world transform (pos + facing yaw) while hand-weaving the Link retarget, so the
// view is byte-deterministic across `linkcorr` tweaks. linkfreeze pins the LIMB pose; this pins the
// actor transform the limbs hang off (Link otherwise idle-turns toward the camera/Navi and desyncs
// the side-profile comparison). REPL `linkpin <0|1>`. asel excludes the player, so this is separate.
int gSoH3dLinkPin = 0;
static Vec3f sSoH3dLinkPinPos;
static s16 sSoH3dLinkPinYaw;

extern "C" int SoH3D_LinkEnabled(void) {
    if (gSoH3dLinkOn < 0) {
        const char* v = getenv("SOH3D_LINK");
        gSoH3dLinkOn = (v != NULL && v[0] == '1') ? 1 : 0; // default OFF (WIP)
    }
    return gSoH3dLinkOn;
}

// Player animation source: 1 = N64 jointTable retarget (default), 0 = 3DS own-CSAB. env SOH3D_LINK_SRC
// ("n64"/"3ds" or 1/0); REPL `linksrc`.
extern "C" int SoH3D_LinkAnimSrc(void) {
    if (gSoH3dLinkAnimSrc < 0) {
        const char* v = getenv("SOH3D_LINK_SRC");
        // default N64 retarget (the complete path: legs/head + the divergent spine/upper-arm bones
        // via kLinkChildBoneCorr, AND captures walk/run from the live blended jointTable, which the
        // named-CSAB path is blind to). env "3"/"0" -> 3DS own-CSAB. REPL `linksrc` overrides live.
        gSoH3dLinkAnimSrc = (v != NULL && (v[0] == '3' || v[0] == '0')) ? 0 : 1;
    }
    return gSoH3dLinkAnimSrc;
}

// childlink_v2 mesh_id idle fallback. See SoH3D_TryDrawPlayer.
#define SOH3D_LINK_IDLE_CSAB "nml_wait_typeA_20f"

// Compute which childlink_v2 mesh_ids are visible this frame from Link's live state. The CMB bakes
// EVERY hand-pose + held-equipment variant on its own mesh_id; the game (N64 and OoT3D) shows a
// state-dependent subset. N64 already resolves the selection per limb each frame into
// player->leftHandType / rightHandType / sheathType (PlayerModelType) + currentShield; we translate
// those to the matching childlink_v2 mesh_ids. The N64 state is self-consistent (sword drawn =>
// empty sheath on back + sword in left hand + shield on right arm; stowed => open hands + shield +
// sword on back), so just composing per-limb avoids double shields/swords. mesh_id map: see
// link_mesh_id_map.md (next to this file) (texture + posed-geometry + render-sweep identification).
#define LINK_MID(n) (1ull << (n))

// ADULT/boy link_v2.cmb mesh_id map. Same 25-bone rig as child (LH=bones15/16, RH=19/20,
// back/sheath=21, waist=23/24) but a DIFFERENT mesh_id layout — identified the same way as child
// (texture dump + posed-geometry + in-game `linkmid only <n>` render sweep, see link_mesh_id_map.md
// "## ADULT"). Boy's shield set is Hylian (default) / Mirror, vs child's Deku / Hylian.
static unsigned long long SoH3D_LinkBoyMidMask(Player* player) {
    unsigned long long m = LINK_MID(45) | LINK_MID(46); // full body + head/face always
    int hylian = (player->currentShield == PLAYER_SHIELD_HYLIAN);
    int mirror = (player->currentShield == PLAYER_SHIELD_MIRROR);
    int haveShield = (hylian || mirror);

    // LEFT hand (sword hand), bones 15/16.
    switch (player->leftHandType) {
        case PLAYER_MODELTYPE_LH_SWORD:
        case PLAYER_MODELTYPE_LH_SWORD_2: m |= LINK_MID(16); break; // Master sword in hand
        case PLAYER_MODELTYPE_LH_BGS:     m |= LINK_MID(37); break; // Biggoron/giant knife (long blade)
        case PLAYER_MODELTYPE_LH_CLOSED:
        case PLAYER_MODELTYPE_LH_BOTTLE:
        case PLAYER_MODELTYPE_LH_HAMMER:  m |= LINK_MID(14); break; // closed fist (hammer drawn elsewhere)
        case PLAYER_MODELTYPE_LH_OPEN:
        default:                          m |= LINK_MID(13); break; // open empty hand (idle)
    }

    // RIGHT hand (shield/bow hand), bones 19/20.
    switch (player->rightHandType) {
        case PLAYER_MODELTYPE_RH_SHIELD:
            m |= haveShield ? LINK_MID(23) : LINK_MID(20); // Hylian/Mirror shield on the forearm
            break;
        case PLAYER_MODELTYPE_RH_BOW_SLINGSHOT:
        case PLAYER_MODELTYPE_RH_BOW_SLINGSHOT_2: m |= LINK_MID(30); break; // bow drawn
        case PLAYER_MODELTYPE_RH_CLOSED:   m |= LINK_MID(21); break; // closed fist
        case PLAYER_MODELTYPE_RH_OPEN:
        case PLAYER_MODELTYPE_RH_HOOKSHOT: // adult hookshot model drawn separately -> empty hand
        case PLAYER_MODELTYPE_RH_OCARINA:
        default:                           m |= LINK_MID(20); break; // open empty hand
    }

    // BACK (shield panel + sheath), bone 21. Combine sheathType with currentShield.
    switch (player->sheathType) {
        case PLAYER_MODELTYPE_SHEATH_18: // shield on back AND sword sheathed
            m |= hylian ? LINK_MID(0) : (mirror ? LINK_MID(2) : LINK_MID(31));
            break;
        case PLAYER_MODELTYPE_SHEATH_19: // shield on back, empty sheath (sword drawn)
            m |= hylian ? LINK_MID(1) : (mirror ? LINK_MID(3) : 0ull);
            break;
        case PLAYER_MODELTYPE_SHEATH_16: // sword on back, no shield
            m |= LINK_MID(31);
            break;
        case PLAYER_MODELTYPE_SHEATH_17: // empty sheath, no shield (sword drawn, no shield)
        default:
            break;
    }
    return m;
}

static unsigned long long SoH3D_LinkComputeMidMask(Player* player) {
    unsigned long long m;
    int deku, hylian;
    if (gSoH3dLinkMidOverrideSet) {
        return gSoH3dLinkMidOverride; // REPL `linkmid` debug override (identification sweep)
    }
    if (LINK_AGE_IN_YEARS != YEARS_CHILD) {
        return SoH3D_LinkBoyMidMask(player); // adult uses link_v2.cmb's own mesh_id layout
    }
    m = LINK_MID(24) | LINK_MID(26); // body + head/face always (25 = far-LOD, never)
    deku = (player->currentShield == PLAYER_SHIELD_DEKU);
    hylian = (player->currentShield == PLAYER_SHIELD_HYLIAN || player->currentShield == PLAYER_SHIELD_MIRROR);

    // LEFT hand (the sword hand). childlink left-hand variants on bones 15/16.
    switch (player->leftHandType) {
        case PLAYER_MODELTYPE_LH_SWORD:
        case PLAYER_MODELTYPE_LH_SWORD_2:
        case PLAYER_MODELTYPE_LH_BGS:      m |= LINK_MID(16); break; // sword in hand
        case PLAYER_MODELTYPE_LH_BOOMERANG: m |= LINK_MID(8); break; // boomerang
        case PLAYER_MODELTYPE_LH_CLOSED:
        case PLAYER_MODELTYPE_LH_BOTTLE:   m |= LINK_MID(1); break;  // closed (bottle drawn separately)
        case PLAYER_MODELTYPE_LH_OPEN:
        case PLAYER_MODELTYPE_LH_HAMMER:   /* child hammer = empty */
        default:                           m |= LINK_MID(0); break;  // open empty hand
    }

    // RIGHT hand (the shield hand). childlink right-hand variants on bones 19/20.
    switch (player->rightHandType) {
        case PLAYER_MODELTYPE_RH_SHIELD:
            m |= (deku || hylian) ? LINK_MID(5) : LINK_MID(3); // shield on arm only if one is equipped
            break;
        case PLAYER_MODELTYPE_RH_BOW_SLINGSHOT:
        case PLAYER_MODELTYPE_RH_BOW_SLINGSHOT_2: m |= LINK_MID(18); break; // slingshot
        case PLAYER_MODELTYPE_RH_CLOSED:   m |= LINK_MID(4); break;  // closed
        case PLAYER_MODELTYPE_RH_OPEN:
        case PLAYER_MODELTYPE_RH_HOOKSHOT: /* child hookshot = empty */
        case PLAYER_MODELTYPE_RH_OCARINA:
        default:                           m |= LINK_MID(3); break;  // open empty hand
    }

    // BACK (sheath + back shield), bone 21. Combine sheathType with currentShield. The sword-on-back
    // hilt (mid 11/14) and shield panel are baked together per combination, so this is one choice.
    switch (player->sheathType) {
        case PLAYER_MODELTYPE_SHEATH_18: // sword sheathed AND shield on back
            m |= deku ? LINK_MID(11) : (hylian ? LINK_MID(9) : LINK_MID(14));
            break;
        case PLAYER_MODELTYPE_SHEATH_19: // shield on back, empty sheath (sword drawn)
            m |= deku ? LINK_MID(13) : (hylian ? LINK_MID(10) : 0ull);
            break;
        case PLAYER_MODELTYPE_SHEATH_16: // sword on back, no shield
            m |= LINK_MID(14);
            break;
        case PLAYER_MODELTYPE_SHEATH_17: // empty sheath, no shield (sword drawn, no shield)
        default:
            break; // nothing on the back
    }
    return m;
}
#undef LINK_MID

static int sLinkModelId = -1; // last modelId the player body drew with (for the REPL pose scanner)
extern "C" int SoH3D_LinkModelId(void) { return sLinkModelId; }

// Pose-scan LOGGER. The pose (lastSkin) is recomputed in the DRAW path (here), not in Play_Update, so
// the per-frame discontinuity must be sampled here — once per rendered frame — to be meaningful (the
// frame-step `step` advances logic without drawing). When active, each drawn player frame records the
// max per-bone rotation jump + the bone + the resolved csab + its frame. The REPL reads the log back.
struct PoseScanRec { float deg; int bone; float frame; char csab[28]; };
static PoseScanRec sPoseLog[512];
static int sPoseLogN = 0;
static int sPoseScanActive = 0;
extern "C" void SoH3D_PoseScanSetActive(int on) {
    sPoseScanActive = on ? 1 : 0;
    if (on) {
        sPoseLogN = 0; // clear the log only when STARTING a scan; `off` keeps it for `dump`
        if (sLinkModelId >= 0) SoH3D_PoseScanReset(sLinkModelId); // baseline = next frame
    }
}
extern "C" int SoH3D_PoseScanCount(void) { return sPoseLogN; }
extern "C" float SoH3D_PoseScanGet(int i, int* bone, float* frame, const char** csab) {
    if (i < 0 || i >= sPoseLogN) { if (bone) *bone = -1; if (frame) *frame = 0; if (csab) *csab = ""; return 0; }
    if (bone) *bone = sPoseLog[i].bone;
    if (frame) *frame = sPoseLog[i].frame;
    if (csab) *csab = sPoseLog[i].csab;
    return sPoseLog[i].deg;
}
// Called from the draw path right after the pose is set, when the scan is active.
static void poseScanRecord(int modelId, const char* csab, float frame) {
    if (!sPoseScanActive || sPoseLogN >= (int)(sizeof(sPoseLog) / sizeof(sPoseLog[0]))) return;
    int bone = -1;
    float deg = SoH3D_PoseDiscontinuity(modelId, &bone);
    PoseScanRec& r = sPoseLog[sPoseLogN++];
    r.deg = deg; r.bone = bone; r.frame = frame;
    const char* c = csab ? csab : "(n64-retarget)";
    int k = 0; for (; c[k] && k < 27; k++) r.csab[k] = c[k];
    r.csab[k] = '\0';
}

extern "C" int SoH3D_TryDrawPlayer(PlayState* play, Actor* actor) {
    const char* zar;
    const char* csab = NULL; // set only in the own-CSAB (linksrc 3ds) branch; NULL in N64-retarget
    Player* player;
    int modelId;
    u8 tint[3];
    if (!SoH3D_Enabled() || !SoH3D_LinkEnabled()) {
        return 0;
    }
    // Use the *_new (link_v2/childlink_v2) body: a single CMB with FULL embedded textures
    // (the body skin atlas, 128x128 ETC1) and a 25-bone rig — renders correctly textured. The
    // *_ultra rigs store the body skin in external CTXB files bound at runtime (the embedded
    // CMB texture is just a 32x32 'cube_01' placeholder -> renders untextured/red), and carry
    // the held-equipment CMBs; wiring up ultra's external textures + equipment is the next stage.
    zar = (LINK_AGE_IN_YEARS == YEARS_CHILD) ? "/actor/zelda_link_child_new.zar"
                                             : "/actor/zelda_link_boy_new.zar";
    SoH3D_EnsureModelProvider();
    modelId = SoH3D_AutoModelId(zar); // auto-picks the largest single CMB = the body (link.cmb)
    if (modelId < 0) {
        return 0; // model unavailable -> fall back to the N64 body
    }
    sLinkModelId = modelId; // expose to the REPL pose-discontinuity scanner (SoH3D_LinkModelId)
    // Player is Actor-first so the cast is valid (see z64player.h).
    player = (Player*)actor;
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
    SoH3D_SceneTint(play, tint);
    // Measure the posed feet this frame so we can ground them: the OoT3D Link CSABs lift the
    // skeleton off the floor (boy-rig hip translation; #29b). The world matrix is built AFTER the
    // pose + visible-mesh mask are known (below), so the ground offset can use the live pose.
    SoH3D_SetTrackPosedMinY(modelId, 1);
    // linkjointdump capture: append this frame's live jointTable + phase, for offline retarget fitting.
    if (gLinkJointDumpFile != NULL && gLinkJointDumpRemaining > 0 &&
        player->skelAnime.jointTable != NULL && player->skelAnime.limbCount > 0) {
        const char* an = (const char*)player->skelAnime.animation;
        const char* base = an ? strrchr(an, '/') : NULL;
        base = base ? base + 1 : (an ? an : "(null)");
        int li;
        for (li = 1; li <= player->skelAnime.limbCount; li++) {
            Vec3s* j = &player->skelAnime.jointTable[li];
            fprintf(gLinkJointDumpFile, "%d,%.3f,%.1f,%s,%d,%d,%d,%d\n", gLinkJointDumpCap,
                    player->skelAnime.curFrame, player->skelAnime.animLength, base, li, j->x, j->y, j->z);
        }
        gLinkJointDumpCap++;
        if (--gLinkJointDumpRemaining == 0) {
            fclose(gLinkJointDumpFile);
            gLinkJointDumpFile = NULL;
        }
    }
    // #85 carry-walk: in N64, walking-while-carrying merges TWO anims per-limb — the BASE skelAnime's
    // locomotion anim drives the LOWER body (legs cycle) while the upper carry anim (carryB_wait, arms
    // raised) is copied onto only the upper-body limbs (z_player.c ~3611, SetCopyTrue +
    // sUpperBodyLimbCopyMap; "moving" = linearVelocity != 0). The 3ds own-CSAB path plays ONE CSAB on
    // the whole rig and, when carrying, picks the upper carry CSAB (below) — which has STATIC legs, so
    // Link slides with frozen legs (#85a). There is no per-limb two-CSAB blend in the model layer. The
    // N64-retarget path, however, reads the ALREADY-MERGED jointTable, so it gets the correct
    // legs+arms carry-walk pose for free. So for carry-WALK only, route the body pose through the
    // retarget path even in 3ds mode (it is the faithful N64 pose). Carry-IDLE (linearVelocity == 0)
    // still uses the 3ds carry CSAB below — N64 copies the carry anim onto ALL limbs there
    // (SetCopyAll), so the standing carry pose is a single whole-rig anim with no leg cycle anyway.
    int carryWalk = (player->heldActor != NULL && player->linearVelocity != 0.0f &&
                     player->skelAnime.jointTable != NULL && player->skelAnime.limbCount > 0);
    // Two user-selectable animation sources (REPL `linksrc`), both kept working:
    if ((SoH3D_LinkAnimSrc() == 1 || carryWalk) && gSoH3dLinkForceCsab[0] == '\0' &&
        player->skelAnime.jointTable != NULL && player->skelAnime.limbCount > 0) {
        // N64 RETARGET: drive the OoT3D rig from Link's LIVE blended jointTable (captures walk/run and
        // every blended state, which the named-CSAB path is blind to). jointTable[0] = root translation,
        // skip it. The per-bone kLinkChildBoneCorr table maps each OoT3D bone to its N64 limb and how to
        // apply it: legs/head use pure "replace" (their rest frame matches the N64 rig), the divergent
        // spine/upper-arm bones get a constant rest-frame correction C (Grezzo re-rigged Link's torso).
        if (gSoH3dAnimDebug) {
            static int dbg = 0;
            if ((dbg++ % 30) == 0) {
                printf("SOH3D LINK: src=N64 jointTable limbCount=%d (live blended pose, per-bone corr)\n",
                       player->skelAnime.limbCount);
                fflush(stdout);
            }
        }
        SoH3D_LinkBoneCorrEnsure();
        // Hand-weave pose-freeze: latch the live idle jointTable on request, then feed the frozen copy
        // so tuning `linkcorr` is the only variable (the idle fidget otherwise moves the pose).
        const s16* jrots = (const s16*)&player->skelAnime.jointTable[1];
        int jcount = player->skelAnime.limbCount;
        if (gLinkFreezeReq && jcount > 0 && jcount < 39) {
            for (int i = 0; i <= jcount; i++) gLinkFrozenJoints[i] = player->skelAnime.jointTable[i];
            gLinkFrozenCount = jcount;
            gLinkFreezeReq = 0;
        }
        if (gLinkFrozenCount > 0) { jrots = (const s16*)&gLinkFrozenJoints[1]; jcount = gLinkFrozenCount; }
        SoH3D_UpdateAnimN64Corr(modelId, jrots, jcount, gLinkBoneCorr, (int)ARRAY_COUNT(gLinkBoneCorr));
    } else {
        // 3DS OWN-CSAB: pick the link CSAB matching Link's named anim, phase-locked to curFrame/animLength.
        // Unmapped -> idle so it never freezes in bind pose. NOTE (#29b): the documented "slide" (idle
        // played while translating because OoT blends locomotion into jointTable without naming an anim)
        // does NOT reproduce here — during sustained movement player->skelAnime.animation resolves to
        // gPlayerAnim_link_normal_run_free, so this maps to nml_run_free and Link animates while moving
        // (verified live, Kakariko + Kokiri). speedXZ shown in the debug for future locomotion work.
        csab = SoH3D_ResolvePlayerCsab((const char*)player->skelAnime.animation);
        // #6/#9: while carrying, OoT layers a held-item animation (e.g. carryB_wait, arms raised
        // overhead) on the UPPER body via player->upperSkelAnime, leaving the base skelAnime on
        // wait/locomotion. The N64-retarget path reads the already-merged jointTable so it gets the
        // carry pose for free; the named-CSAB path here sees only the base name (wait_free -> arms
        // DOWN), dropping the carry. When carrying, resolve the CSAB from the upper-body anim instead
        // so the 3DS rig faithfully plays OoT3D's own carry CSAB. (Standing carry is exact; a true
        // per-limb upper/lower BLEND for walking-while-carrying is future locomotion work, #9.)
        if (player->heldActor != NULL && player->upperSkelAnime.animation != NULL) {
            const char* upperCsab = SoH3D_ResolvePlayerCsab((const char*)player->upperSkelAnime.animation);
            if (upperCsab != NULL) csab = upperCsab;
        }
        if (csab == NULL) {
            csab = SOH3D_LINK_IDLE_CSAB;
        }
        if (gSoH3dLinkForceCsab[0] != '\0') {
            csab = gSoH3dLinkForceCsab; // REPL `linkanim` override (verification)
        }
        if (gSoH3dAnimDebug) {
            static int dbg = 0;
            if ((dbg++ % 30) == 0) {
                const char* otr = (const char*)player->skelAnime.animation;
                printf("SOH3D LINK: src=3DS n64=%s -> csab=%s frame=%.1f/%.1f speedXZ=%.2f\n",
                       otr ? otr : "(none)", csab, player->skelAnime.curFrame,
                       player->skelAnime.animLength, player->actor.speedXZ);
                fflush(stdout);
            }
        }
        int isLoco = (strstr(csab, "run") != NULL) || (strstr(csab, "walk") != NULL);
        if (strcmp(csab, "rest") == 0) {
            SoH3D_UpdateAnim(modelId, NULL, 0); // diagnostic: force bind pose (linkanim rest)
        } else if (isLoco && gSoH3dLinkForceCsab[0] == '\0' && player->actor.speedXZ > 0.5f) {
            // #7 SLIDE FIX: Link's run/walk advances its pose via a player-internal accumulator, not
            // skelAnime.curFrame (verified pinned at 0 across an entire run). Phase-locking to that
            // dead clock froze the run CSAB at frame 0 -> static pose sliding over the ground. Drive
            // the leg cycle by ground speed instead (free-run; the CSAB wraps the frame internally).
            SoH3D_UpdateAnimAuto(modelId, csab, player->actor.speedXZ * gSoH3dLinkLocoGain, 0.0f, 0.0f,
                                 player->skelAnime.morphWeight);
        } else {
            // Idle / one-shot anims: curFrame is valid here, so keep the N64-progress phase-lock.
            SoH3D_UpdateAnimAuto(modelId, csab, gSoH3dAnimRate, player->skelAnime.curFrame,
                                 player->skelAnime.animLength, player->skelAnime.morphWeight);
        }
    }
    // Pose-scan QA: sample the per-frame discontinuity now that lastSkin is set (once per drawn frame).
    poseScanRecord(modelId, csab, player->skelAnime.curFrame);
    // Select Link's live equipment / hand-pose variant subset (the childlink_v2 mesh bakes them
    // all on distinct mesh_ids). Must be set BEFORE EmitPose so it pairs with this draw item.
    unsigned long long midMask = SoH3D_LinkComputeMidMask(player);
    if (gSoH3dAnimDebug) {
        static int dbg = 0;
        if ((dbg++ % 30) == 0) {
            printf("SOH3D LINK mids: LH=%d RH=%d sheath=%d shield=%d -> mask=0x%llx\n",
                   player->leftHandType, player->rightHandType, player->sheathType,
                   player->currentShield, midMask);
            fflush(stdout);
        }
    }
    SoH3D_GL_SetMidMask(modelId, midMask);
    // Build the world matrix now that the pose + visible-mesh mask are known. Do NOT inherit the
    // actor matrix (it carries the N64 0.01 actor scale through which the OoT3D dlist renders
    // nothing). groundOff (model-local, applied innermost/pre-scale like the auto path's
    // groundOffset) lands the posed feet on the actor's world pos.y — fixes the #29b float.
    float groundOff = SoH3D_PosedGroundOffset(modelId, midMask);
    // #79 fix: feet-grounding measures the LOWEST visible vertex, which is a planted foot only while
    // grounded. The CLIMB poses (ladder/vine/forced-climb) raise a foot/knee/whole-body, so the
    // lowest vertex (hence groundOff) swings by thousands of model-local units between frames
    // (idle -1263 vs climb_up/upL/upR -1694/-2644/-4193, climb_startB -8443) and the whole body
    // teleports vertically while climbing. N64 draws the climb pose at actor.world.pos.y with no
    // grounding; reproduce that continuity by FREEZING groundOff to the last value measured on a
    // non-climb pose (self-calibrating, no jump at climb entry). Gate on the climb CSAB name (the
    // actual cause is the pose): "climb" also matches "Fclimb"; "hang" covers the ledge-hang holds.
    // The N64-retarget path (linksrc 1, csab==NULL) is untouched (that's #8, a separate issue).
    static float sLinkLastGroundedOff = 0.0f;
    s32 climbPose = gSoH3dClimbGroundFix && csab != NULL &&
                    (strstr(csab, "climb") != NULL || strstr(csab, "hang") != NULL);
    if (climbPose) {
        groundOff = sLinkLastGroundedOff;
    } else {
        sLinkLastGroundedOff = groundOff;
    }
    if (gSoH3dAnimDebug) {
        static int dbg = 0;
        if ((dbg++ % 30) == 0) {
            printf("SOH3D LINK groundOff=%.1f (model-local)%s\n", groundOff, climbPose ? " [climb:frozen]" : "");
            fflush(stdout);
        }
    }
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(actor->shape.rot.y), MTXMODE_APPLY);
    Matrix_Scale(gSoH3dLinkScale, gSoH3dLinkScale, gSoH3dLinkScale, MTXMODE_APPLY);
    if (gSoH3dLinkRotX != 0.0f) Matrix_RotateX(gSoH3dLinkRotX * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gSoH3dLinkRotY != 0.0f) Matrix_RotateY(gSoH3dLinkRotY * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gSoH3dLinkRotZ != 0.0f) Matrix_RotateZ(gSoH3dLinkRotZ * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (groundOff != 0.0f) Matrix_Translate(0.0f, groundOff, 0.0f, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
    // #6: attach a carried actor (e.g. held cucco) to the 3DS Link's hands. The held actor's
    // world.pos is normally set by Player_PostLimbDrawGameplay (midpoint of the hands), the post-limb
    // hook of Link's N64 SkelAnime draw inside Player_DrawGameplay — which z_player.c SKIPS when this
    // replacement draws, so without this the cucco stays at its pickup spot. Reproduce that anchor on
    // the posed 3DS rig: childlink_v2 left hand = bone 16, right hand = bone 20 (kLinkChildBoneCorr
    // limb map: limb 15/18 = L/R_HAND). The bone positions are model-local; the matrix stack top is
    // the player world transform just loaded, so Matrix_MultVec3f lifts the midpoint to world space.
    // Gated on carrying & not holding the hookshot / an item-in-hand. Works in both anim sources (both
    // cache skin via cacheSkinForGround). gSoH3dHeldAttach is the A/B toggle (REPL linkheldfix).
    if (gSoH3dHeldAttach && player->heldActor != NULL && !Player_HoldsHookshot(player) &&
        (player->stateFlags1 & PLAYER_STATE1_ITEM_IN_HAND) == 0) {
        float lh[3], rh[3];
        if (SoH3D_PosedBoneWorldPos(modelId, 16, lh) && SoH3D_PosedBoneWorldPos(modelId, 20, rh)) {
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
    // CHEST bone at Y~=2665, NOT the head — see soh3d_link_bonecorr.inc spine-shift fix) in
    // model-local space and lift it to world via the matrix stack top (player world transform
    // just loaded). Faithful: position only, every frame.
    {
        float hd[3];
        if (gSoH3dFocusFix && SoH3D_PosedBoneWorldPos(modelId, 11, hd)) {
            Vec3f hdLocal = { hd[0], hd[1], hd[2] };
            Vec3f hdWorld;
            Matrix_MultVec3f(&hdLocal, &hdWorld);
            Math_Vec3f_Copy(&actor->focus.pos, &hdWorld);
        }
        if (gSoH3dAnimDebug) {
            float b9[3] = {0}, b10[3] = {0}, b11[3] = {0};
            SoH3D_PosedBoneWorldPos(modelId, 9, b9);
            SoH3D_PosedBoneWorldPos(modelId, 10, b10);
            SoH3D_PosedBoneWorldPos(modelId, 11, b11);
            Vec3f l9 = {b9[0],b9[1],b9[2]}, w9; Matrix_MultVec3f(&l9, &w9);
            Vec3f l10 = {b10[0],b10[1],b10[2]}, w10; Matrix_MultVec3f(&l10, &w10);
            Vec3f l11 = {b11[0],b11[1],b11[2]}, w11; Matrix_MultVec3f(&l11, &w11);
            static int fdbg = 0;
            if ((fdbg++ % 30) == 0) {
                printf("SOH3D FOCUS dbg b9=(%.0f,%.0f,%.0f) b10=(%.0f,%.0f,%.0f) b11=(%.0f,%.0f,%.0f) world=(%.0f,%.0f,%.0f)\n",
                       w9.x,w9.y,w9.z, w10.x,w10.y,w10.z, w11.x,w11.y,w11.z,
                       actor->world.pos.x, actor->world.pos.y, actor->world.pos.z);
                fflush(stdout);
            }
        }
    }
    SoH3D_GL_EmitPose(modelId); // capture the CSAB-posed skin matrices
    gSPSoH3DDraw(POLY_OPA_DISP++, modelId | (int)0x80000000, tint[0], tint[1], tint[2]);
    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}

// #79 diagnostic: report the feet-grounding offset SoH3D_PosedGroundOffset computes for Link's
// CURRENT cached pose (the most recent draw — so it reflects whatever CSAB is live, including a
// `linkanim`-forced climb clip), plus the resolved CSAB name. groundOff = -(lowest visible posed
// vertex Y); the draw lands that lowest vertex on actor.world.pos.y. If a climb pose's lowest point
// is NOT the feet (knees/tucked foot), groundOff differs from idle and the WHOLE body is shoved
// up/down for the same actor.y — the suspected "teleports upward while climbing" mechanism. Uses the
// real per-frame mid-mask so it matches the live draw. Returns groundOff; writes the CSAB to outCsab.
extern "C" float SoH3D_LinkGroundDiag(PlayState* play, const char** outCsab) {
    Player* player = GET_PLAYER(play);
    const char* zar = (LINK_AGE_IN_YEARS == YEARS_CHILD) ? "/actor/zelda_link_child_new.zar"
                                                         : "/actor/zelda_link_boy_new.zar";
    int modelId = SoH3D_AutoModelId(zar);
    if (modelId < 0) {
        if (outCsab) *outCsab = "(no model)";
        return 0.0f;
    }
    if (outCsab) {
        if (gSoH3dLinkForceCsab[0] != '\0') {
            *outCsab = gSoH3dLinkForceCsab;
        } else {
            const char* c = SoH3D_ResolvePlayerCsab((const char*)player->skelAnime.animation);
            *outCsab = c ? c : SOH3D_LINK_IDLE_CSAB " (fallback)";
        }
    }
    return SoH3D_PosedGroundOffset(modelId, SoH3D_LinkComputeMidMask(player));
}

// #8 hand-weave: pin Link's world pos + facing yaw each frame, so the side-profile view is identical
// across `linkcorr` tweaks. Extracted from SoH3D_ActorPostUpdate; called from that same call site.
extern "C" void SoH3D_LinkApplyPin(PlayState* play, Actor* actor) {
    if (actor != NULL && gSoH3dLinkPin && play != NULL && actor == &GET_PLAYER(play)->actor) {
        actor->velocity.x = actor->velocity.y = actor->velocity.z = 0.0f;
        actor->speedXZ = 0.0f;
        actor->world.pos = sSoH3dLinkPinPos;
        actor->shape.rot.y = actor->world.rot.y = sSoH3dLinkPinYaw;
    }
}

// #6/#9 linkgrab: hold the selected actor in front of Link + inject fresh A edges until grabbed.
// Extracted from SoH3D_WalkInject; called from that same call site.
extern "C" void SoH3D_LinkWalkInject(PlayState* play) {
    if (gSoH3dLinkGrabFrames > 0) {
        Player* pl = GET_PLAYER(play);
        if (pl == NULL || gSoH3dSelActor == NULL) {
            gSoH3dLinkGrabFrames = 0;
        } else if (pl->heldActor != NULL) {
            gSoH3dLinkGrabFrames = 0; // grabbed — let the real carry action take over
        } else {
            float yawRad = pl->actor.shape.rot.y * (3.14159265f / 32768.0f);
            float fx = sinf(yawRad), fz = cosf(yawRad);
            const float d = 26.0f; // grab range is small; keep the cucco just in front
            gSoH3dSelActor->world.pos.x = pl->actor.world.pos.x + fx * d;
            gSoH3dSelActor->world.pos.y = pl->actor.world.pos.y + 4.0f;
            gSoH3dSelActor->world.pos.z = pl->actor.world.pos.z + fz * d;
            gSoH3dSelActor->speedXZ = 0.0f;
            gSoH3dSelActor->velocity.y = 0.0f;
            // fresh A rising edge every 4th frame so an edge always lands once detection is true.
            play->state.input[0].cur.button |= BTN_A;
            if ((gSoH3dLinkGrabFrames & 3) == 0) play->state.input[0].press.button |= BTN_A;
            gSoH3dLinkGrabFrames--;
        }
    }
}

// Write one N64 limb to a file in tools/soh3d_skel_match.py's load_n64() format (REPL linkskeldump).
static void SoH3D_DumpLimbFileCb(int limbIndex, StandardLimb* lb, void* ud) {
    FILE* f = (FILE*)ud;
    fprintf(f, "N64 limb=%d jointPos=(%d,%d,%d) child=%d sibling=%d\n", limbIndex,
            lb->jointPos.x, lb->jointPos.y, lb->jointPos.z, lb->child, lb->sibling);
}

// Handle a `link*` REPL command. Returns 1 if handled (cmd was a link command), 0 otherwise.
extern "C" int SoH3D_LinkRepl(PlayState* play, const char* cmd, const char* line, const char* outPath) {
    char path[1024];
    float f1, f2, f3;
    int iv;
    if (strcmp(cmd, "linkgrab") == 0) {
        // #6/#9 — drive a REAL cucco pickup: `asel 0x19` first, then `linkgrab [frames]` (default
        // 60). Holds the selected actor in front of Link + injects A edges until heldActor is set,
        // so the genuine grab/lift action fires (B then shows Throw). afreeze should be 0.
        int frames = 60;
        sscanf(line, "%*s %d", &frames);
        if (gSoH3dSelActor == NULL) {
            SoH3D_ReplReply(outPath, "linkgrab: no actor selected (asel 0x19 first)");
        } else {
            gSoH3dActorFreeze = 0; // the driver positions it every frame; freeze would fight that
            gSoH3dLinkGrabFrames = frames;
            SoH3D_ReplReply(outPath, "linkgrab: driving pickup of id=0x%X for %d frames",
                            gSoH3dSelActor->id, frames);
        }
    } else if (strcmp(cmd, "linkskeldump") == 0 && sscanf(line, "%*s %1023s", path) == 1) {
        // Dump the live PLAYER N64 skeleton (limb tree + jointPos) to a file in the
        // soh3d_skel_match.py load_n64() format, to DERIVE the OoT3D-bone -> N64-limb bonemap for
        // the N64-retarget Link mode (linksrc n64). One-shot, on demand.
        Player* pl = GET_PLAYER(play);
        if (pl == NULL || pl->skelAnime.skeleton == NULL || pl->skelAnime.limbCount <= 0) {
            SoH3D_ReplReply(outPath, "linkskeldump: no player skeleton");
        } else {
            FILE* sf = fopen(path, "w");
            if (sf == NULL) {
                SoH3D_ReplReply(outPath, "linkskeldump: cannot open %s", path);
            } else {
                fprintf(sf, "# player N64 skeleton; limbCount=%d age=%d\n", pl->skelAnime.limbCount,
                        LINK_AGE_IN_YEARS);
                SoH3D_WalkN64Skeleton((void**)pl->skelAnime.skeleton, pl->skelAnime.limbCount,
                                      SoH3D_DumpLimbFileCb, sf);
                fclose(sf);
                SoH3D_ReplReply(outPath, "linkskeldump -> %s (limbCount=%d age=%d)", path,
                                pl->skelAnime.limbCount, LINK_AGE_IN_YEARS);
            }
        }
    } else if (strcmp(cmd, "link") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        gSoH3dLinkOn = iv ? 1 : 0;
        SoH3D_ReplReply(outPath, "link=%d (OoT3D player body replacement, proof-of-hook bind pose)",
                        gSoH3dLinkOn);
    } else if (strcmp(cmd, "linkscale") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gSoH3dLinkScale = f1;
        SoH3D_ReplReply(outPath, "linkscale=%.5f (OoT3D-link-local -> N64 player world units)",
                        gSoH3dLinkScale);
    } else if (strcmp(cmd, "linkloco") == 0) {
        // #7 calibrate the speed->CSAB-cadence gain for Link's locomotion cycle. `linkloco <gain>`
        // sets it; no-arg reports it + Link's live speedXZ (so the cadence can be tuned vs N64).
        if (sscanf(line, "%*s %f", &f1) == 1) {
            gSoH3dLinkLocoGain = f1;
        }
        SoH3D_ReplReply(outPath, "linkloco gain=%.3f (CSAB frames/draw per speed unit); link speedXZ=%.2f",
                        gSoH3dLinkLocoGain, GET_PLAYER(play)->actor.speedXZ);
    } else if (strcmp(cmd, "linkfreeze") == 0) {
        // #7 hand-weave: freeze Link's live idle pose so `linkcorr` tweaks are the only variable.
        if (sscanf(line, "%*s %i", &iv) == 1) {
            if (iv) { gLinkFreezeReq = 1; gLinkFrozenCount = 0; }
            else { gLinkFrozenCount = 0; gLinkFreezeReq = 0; }
        }
        SoH3D_ReplReply(outPath, "linkfreeze=%d (frozenLimbs=%d, req=%d)",
                        gLinkFrozenCount > 0 ? 1 : 0, gLinkFrozenCount, gLinkFreezeReq);
    } else if (strcmp(cmd, "linkpin") == 0) {
        // #8 hand-weave: pin Link's world pos + facing yaw so the side-profile view is identical
        // across `linkcorr` tweaks (linkfreeze alone leaves the actor free to idle-turn -> desync).
        if (sscanf(line, "%*s %i", &iv) == 1) {
            Player* pl = GET_PLAYER(play);
            if (iv) {
                sSoH3dLinkPinPos = pl->actor.world.pos;
                sSoH3dLinkPinYaw = pl->actor.shape.rot.y;
                gSoH3dLinkPin = 1;
            } else {
                gSoH3dLinkPin = 0;
            }
        }
        SoH3D_ReplReply(outPath, "linkpin=%d (pins player world pos+yaw each frame)", gSoH3dLinkPin);
    } else if (strcmp(cmd, "linkcorr") == 0) {
        // #7 HAND-WEAVE the per-bone arm/upper-body retarget correction LIVE (linksrc n64).
        //   linkcorr                         -> show upper-body bones (b9..b20): mode + C euler (zyx deg)
        //   linkcorr set <bid> <mode> <cx> <cy> <cz> [<c2x> <c2y> <c2z>]
        //                                    -> set bone bid (mode 1=replace 2=C·R 3=R·C 4=C·R·C2 5=C·R·C⁻¹ conj)
        //   linkcorr reset                   -> restore the generated table
        //   linkcorr bake <path>             -> write the live table as a 25-row .inc for committing
        char sub[64] = "";
        SoH3D_LinkBoneCorrEnsure();
        int n = sscanf(line, "%*s %63s", sub);
        if (n == 1 && strcmp(sub, "reset") == 0) {
            memcpy(gLinkBoneCorr, kLinkChildBoneCorr, sizeof(gLinkBoneCorr));
            SoH3D_ReplReply(outPath, "linkcorr reset to generated table");
        } else if (n == 1 && strcmp(sub, "set") == 0) {
            int bid = -1, mode = 0; float c[3] = { 0, 0, 0 }, c2[3] = { 0, 0, 0 };
            int got = sscanf(line, "%*s %*s %i %i %f %f %f %f %f %f", &bid, &mode, &c[0], &c[1], &c[2],
                             &c2[0], &c2[1], &c2[2]);
            if (got >= 5 && bid >= 0 && bid < 25) {
                gLinkBoneCorr[bid].mode = (unsigned char)mode;
                SoH3D_EulerToMat3(c[0], c[1], c[2], gLinkBoneCorr[bid].C);
                SoH3D_EulerToMat3(c2[0], c2[1], c2[2], gLinkBoneCorr[bid].C2);
                SoH3D_ReplReply(outPath, "linkcorr b%d limb=%d mode=%d C=(%.1f,%.1f,%.1f) C2=(%.1f,%.1f,%.1f)",
                                bid, gLinkBoneCorr[bid].limb, mode, c[0], c[1], c[2], c2[0], c2[1], c2[2]);
            } else {
                SoH3D_ReplReply(outPath, "usage: linkcorr set <bid> <mode> <cx> <cy> <cz> [c2x c2y c2z]");
            }
        } else if (n == 1 && strcmp(sub, "limb") == 0) {
            // #8 remap test: change which N64 jointTable limb drives an OoT3D bone, live (the
            // .inc's static limb field is otherwise the only authority). Lets us test the
            // structural-mismatch fix (OoT3D head b11 <- N64 head limb 10, chest b10 <- torso
            // limb 9) without a rebuild-per-guess. -1 = rest (no live limb).
            int bid = -1, limb = -2;
            if (sscanf(line, "%*s %*s %i %i", &bid, &limb) == 2 && bid >= 0 && bid < 25) {
                gLinkBoneCorr[bid].limb = (signed char)limb;
                SoH3D_ReplReply(outPath, "linkcorr b%d limb=%d mode=%d", bid, gLinkBoneCorr[bid].limb,
                                gLinkBoneCorr[bid].mode);
            } else {
                SoH3D_ReplReply(outPath, "usage: linkcorr limb <bid> <n64limb|-1>");
            }
        } else if (n == 1 && strcmp(sub, "bake") == 0) {
            char path[1024] = "";
            if (sscanf(line, "%*s %*s %1023s", path) == 1) {
                FILE* f = fopen(path, "w");
                if (!f) { SoH3D_ReplReply(outPath, "linkcorr bake: cannot open %s", path); }
                else {
                    fprintf(f, "// HAND-WOVEN by REPL `linkcorr` (#7 long-arm) — arms/upper body tuned by\n");
                    fprintf(f, "// hand vs the OoT3D CSAB ground truth; legs/neck on pure replace. NOT auto-fit.\n");
                    fprintf(f, "// Per OoT3D childlink_v2 bone: { n64Limb, mode, C[9], C2[9] }. mode 0=rest,\n");
                    fprintf(f, "// 1=replace, 2=left C·R, 3=right R·C, 4=two-sided C·R·C2.\n");
                    fprintf(f, "static const SoH3dBoneCorr kLinkChildBoneCorr[25] = {\n");
                    for (int b = 0; b < 25; b++) {
                        const SoH3dBoneCorr* bc = &gLinkBoneCorr[b];
                        fprintf(f, "    { %3d, %d, {", bc->limb, bc->mode);
                        for (int k = 0; k < 9; k++) fprintf(f, "%s%.6ff", k ? "," : "", bc->C[k]);
                        fprintf(f, " }, {");
                        for (int k = 0; k < 9; k++) fprintf(f, "%s%.6ff", k ? "," : "", bc->C2[k]);
                        fprintf(f, " } }, // b%d\n", b);
                    }
                    fprintf(f, "};\n");
                    fclose(f);
                    SoH3D_ReplReply(outPath, "linkcorr baked -> %s", path);
                }
            } else {
                SoH3D_ReplReply(outPath, "usage: linkcorr bake <path>");
            }
        } else {
            // show: dump upper-body bones with their C (and C2) decomposed to zyx euler degrees.
            char buf[1024]; int off = 0;
            off += snprintf(buf + off, sizeof(buf) - off, "linkcorr (upper body):");
            for (int b = 9; b <= 20 && off < (int)sizeof(buf) - 80; b++) {
                float e[3], e2[3];
                SoH3D_Mat3ToEuler(gLinkBoneCorr[b].C, e);
                SoH3D_Mat3ToEuler(gLinkBoneCorr[b].C2, e2);
                off += snprintf(buf + off, sizeof(buf) - off, " b%d:l%d m%d C(%.0f,%.0f,%.0f)", b,
                                gLinkBoneCorr[b].limb, gLinkBoneCorr[b].mode, e[0], e[1], e[2]);
                if (gLinkBoneCorr[b].mode == 4)
                    off += snprintf(buf + off, sizeof(buf) - off, "/C2(%.0f,%.0f,%.0f)", e2[0], e2[1], e2[2]);
            }
            SoH3D_ReplReply(outPath, "%s", buf);
        }
    } else if (strcmp(cmd, "linkrot") == 0 && sscanf(line, "%*s %f %f %f", &f1, &f2, &f3) == 3) {
        gSoH3dLinkRotX = f1;
        gSoH3dLinkRotY = f2;
        gSoH3dLinkRotZ = f3;
        SoH3D_ReplReply(outPath, "linkrot=(%.0f,%.0f,%.0f)", gSoH3dLinkRotX, gSoH3dLinkRotY, gSoH3dLinkRotZ);
    } else if (strcmp(cmd, "linkanim") == 0) {
        // `linkanim <csab-base>` pins that CSAB on Link (verify idle/walk/run without real input);
        // `linkanim off` / no-arg returns to live anim resolution.
        char name[64] = "";
        if (sscanf(line, "%*s %63s", name) == 1 && strcmp(name, "off") != 0) {
            strncpy(gSoH3dLinkForceCsab, name, sizeof(gSoH3dLinkForceCsab) - 1);
            gSoH3dLinkForceCsab[sizeof(gSoH3dLinkForceCsab) - 1] = '\0';
            SoH3D_ReplReply(outPath, "linkanim='%s' (forced on Link; `linkanim off` to release)", gSoH3dLinkForceCsab);
        } else {
            gSoH3dLinkForceCsab[0] = '\0';
            SoH3D_ReplReply(outPath, "linkanim OFF (live anim resolution restored)");
        }
    } else if (strcmp(cmd, "climbgroundfix") == 0) {
        // #79 A/B toggle: freeze feet-grounding during climb poses (default ON). `climbgroundfix 0`
        // reverts to per-pose lowest-vertex grounding so the climb teleport reproduces for before/after.
        if (sscanf(line, "%*s %i", &iv) == 1) gSoH3dClimbGroundFix = (iv != 0);
        SoH3D_ReplReply(outPath, "climbgroundfix=%d (freeze groundOff during climb poses)", gSoH3dClimbGroundFix);
    } else if (strcmp(cmd, "linkheldfix") == 0) {
        // #6 A/B toggle: attach the carried actor (held cucco) to 3DS Link's posed hands. Default on;
        // `linkheldfix 0` reverts to the engine's stale pickup-spot pos for before/after evidence.
        if (sscanf(line, "%*s %i", &iv) == 1) gSoH3dHeldAttach = (iv != 0);
        SoH3D_ReplReply(outPath, "linkheldfix=%d (attach carried actor to posed hands)", gSoH3dHeldAttach);
    } else if (strcmp(cmd, "linkfocusfix") == 0) {
        // #16(b) A/B toggle: keep actor.focus.pos at the 3DS Link posed head so the first-person
        // (C-up) camera frames Link's eye. Default on; `linkfocusfix 0` reverts to the stale focus
        // (Player_DrawGameplay skipped) for before/after evidence.
        if (sscanf(line, "%*s %i", &iv) == 1) gSoH3dFocusFix = (iv != 0);
        SoH3D_ReplReply(outPath, "linkfocusfix=%d (keep focus.pos at posed head)", gSoH3dFocusFix);
    } else if (strcmp(cmd, "linkjointdump") == 0) {
        // `linkjointdump <path> [nframes]` — capture the live player jointTable over nframes (default 60)
        // consecutive draws to a CSV, for the per-bone retarget-correction derivation. Pin Link to a
        // named anim first (`linkanim` only forces the 3DS CSAB display; the N64 jointTable still reflects
        // whatever named anim Link's SkelAnime is actually playing — keep him idle/standing for nml_wait).
        int n = 60;
        if (sscanf(line, "%*s %1023s %d", path, &n) >= 1) {
            if (gLinkJointDumpFile != NULL) { fclose(gLinkJointDumpFile); gLinkJointDumpFile = NULL; }
            gLinkJointDumpFile = fopen(path, "w");
            if (gLinkJointDumpFile == NULL) {
                SoH3D_ReplReply(outPath, "linkjointdump: cannot open %s", path);
            } else {
                if (n < 1) n = 1;
                fprintf(gLinkJointDumpFile, "# player jointTable capture; bonemap value m -> limb (m+1)\n");
                fprintf(gLinkJointDumpFile, "cap,curFrame,animLength,anim,limb,x,y,z\n");
                gLinkJointDumpRemaining = n;
                gLinkJointDumpCap = 0;
                SoH3D_ReplReply(outPath, "linkjointdump -> %s (%d frames)", path, n);
            }
        } else {
            SoH3D_ReplReply(outPath, "usage: linkjointdump <path> [nframes]");
        }
    } else if (strcmp(cmd, "linkmid") == 0) {
        // `linkmid <arg>` — debug override of Link's mesh_id visibility mask (childlink_v2 bakes all
        // hand/equipment variants on distinct mesh_ids). Used to identify each mesh_id by rendering it
        // alone. `only <n>` shows just mesh_id n; `add <n>`/`del <n>` toggle one; `0xHEX` sets the raw
        // mask; `all` shows everything; `auto` releases the override (back to the computed policy).
        char arg[32] = "";
        int n = 0;
        if (sscanf(line, "%*s %31s", arg) == 1) {
            if (strcmp(arg, "auto") == 0) {
                gSoH3dLinkMidOverrideSet = 0;
            } else if (strcmp(arg, "all") == 0) {
                gSoH3dLinkMidOverride = ~0ull; gSoH3dLinkMidOverrideSet = 1;
            } else if (strcmp(arg, "only") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                gSoH3dLinkMidOverride = (n >= 0 && n < 64) ? (1ull << n) : 0ull; gSoH3dLinkMidOverrideSet = 1;
            } else if (strcmp(arg, "add") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                if (n >= 0 && n < 64) gSoH3dLinkMidOverride |= (1ull << n); gSoH3dLinkMidOverrideSet = 1;
            } else if (strcmp(arg, "del") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                if (n >= 0 && n < 64) gSoH3dLinkMidOverride &= ~(1ull << n); gSoH3dLinkMidOverrideSet = 1;
            } else {
                gSoH3dLinkMidOverride = strtoull(arg, NULL, 0); gSoH3dLinkMidOverrideSet = 1;
            }
        }
        SoH3D_ReplReply(outPath, "linkmid override=%s mask=0x%llx",
                        gSoH3dLinkMidOverrideSet ? "ON" : "OFF(auto)", gSoH3dLinkMidOverride);
    } else if (strcmp(cmd, "linkgear") == 0) {
        // `linkgear <sword 0-3> <shield 0-3>` — equip a sword (0=none,1=kokiri,2=master,3=biggoron)
        // + shield (0=none,1=deku,2=hylian,3=mirror) on Link so the boy/adult equipment mids can be
        // verified (the debug save spawns child with deku+kokiri; adult has no Hylian shield/master
        // sword by default). Marks them owned, equips them, sets B to the sword; the player recomputes
        // currentShield from CUR_EQUIP_VALUE next frame (z_player_lib.c ~679).
        int sw = 2, sh = 2;
        sscanf(line, "%*s %i %i", &sw, &sh);
        gSaveContext.inventory.equipment |= OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_KOKIRI) |
                                            OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_MASTER) |
                                            OWNED_EQUIP_FLAG(EQUIP_TYPE_SWORD, EQUIP_INV_SWORD_BIGGORON) |
                                            OWNED_EQUIP_FLAG(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_DEKU) |
                                            OWNED_EQUIP_FLAG(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_HYLIAN) |
                                            OWNED_EQUIP_FLAG(EQUIP_TYPE_SHIELD, EQUIP_INV_SHIELD_MIRROR);
        Inventory_ChangeEquipment(EQUIP_TYPE_SWORD, sw);
        Inventory_ChangeEquipment(EQUIP_TYPE_SHIELD, sh);
        {
            static const s16 swItem[] = { ITEM_NONE, ITEM_SWORD_KOKIRI, ITEM_SWORD_MASTER, ITEM_SWORD_BGS };
            if (sw >= 0 && sw < 4) gSaveContext.equips.buttonItems[0] = swItem[sw];
        }
        // currentShield/currentSword are cached on the Player and only refreshed on equip events;
        // force the refresh now so the change takes effect this frame.
        Player_SetEquipmentData(play, GET_PLAYER(play));
        SoH3D_ReplReply(outPath, "linkgear sword=%d shield=%d (equipped)", sw, sh);
    } else if (strcmp(cmd, "linksrc") == 0) {
        // `linksrc n64|3ds` — choose Link's animation source. n64 = retarget the live blended jointTable
        // (walk/run + everything), 3ds = the OoT3D rig's own named CSABs.
        char name[16] = "";
        if (sscanf(line, "%*s %15s", name) == 1) {
            gSoH3dLinkAnimSrc = (name[0] == '0' || name[0] == '3') ? 0 : 1;
        }
        SoH3D_ReplReply(outPath, "linksrc=%s", gSoH3dLinkAnimSrc == 1 ? "n64 (live jointTable retarget)"
                                                                      : "3ds (own CSAB)");
    } else {
        return 0; // not a link command
    }
    return 1;
}
