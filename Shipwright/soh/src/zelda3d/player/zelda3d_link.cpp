// Zelda3D — OoT3D Link (player) replacement POLICY (split out of zelda3d.c; pure refactor, no behavior
// change). Owns the player draw, the equipment mesh-id mask, the per-bone retarget correction table
// + hand-weave state, the pose-freeze, linkpin, the cucco-grab repro driver, and all the `link*`
// REPL commands. The LOW-LEVEL CMB skeleton forward-kinematics primitives stay in zelda3d_model.cpp
// (Zelda3D_UpdateAnimN64*, Zelda3D_PosedGroundOffset, ...); this file CALLS them.
#include "../zelda3d.h"
#include "zelda3d_link.h"
#include "../core/zelda3d_log.h"
#include "overlays/actors/ovl_En_Horse/z_en_horse.h" // EnHorse animationIdx/curFrame (LINKTRACE mounted extras)
#include "../behaviors/actor/player.h" // PlayerBehavior — Link as a structured ActorBehavior class
#include "player/link_midmask.h" // Stage 2a shared adult mesh-mask policy (Shipwright/zelda3d_shared/)
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// childlink_v2 per-bone retarget correction (kLinkChildBoneCorr). Legs/neck stay on pure replace
// (no regression); the divergent spine/upper-arms are HAND-WOVEN (#7/#31) — a constant fitted C
// provably can't reconcile the re-authored OoT3D arm motion, so the arm corrections are tuned by
// hand against the OoT3D CSAB ground truth (linksrc 3ds), NOT auto-fit.
#include "../tables/zelda3d_link_bonecorr.inc"

// HAND-WEAVE live-tune scaffold (#7 long arm). A mutable runtime copy of the table that the player
// path actually passes, so each upper-body bone's correction can be adjusted LIVE over the REPL
// (`linkcorr`) and seen in-game against the 3ds ground truth, instead of rebuild-per-guess. The
// final hand-tuned values get baked back into zelda3d_link_bonecorr.inc (`linkcorr bake <path>`).
// Shorthand for the single PlayerBehavior instance (its composed subsystems hold the former
// file-static Link state). Used by the extern "C" draw body + the per-frame hooks below.
static inline Zelda3D::PlayerBehavior& P() { return Zelda3D::PlayerBehavior::instance(); }

// LinkRetarget: lazy-init the runtime correction table from the baked child table (kLinkChildBoneCorr).
void Zelda3D::LinkRetarget::ensure() {
    if (inited) return;
    memcpy(table, kLinkChildBoneCorr, sizeof(table));
    inited = true;
}

// Build a row-major 3x3 rotation C = Rz(cz)·Ry(cy)·Rx(cx) (the live/csab ZYX convention) from
// Euler degrees, written into out[9]. Identity when all zero.
static void Zelda3D_EulerToMat3(float cxDeg, float cyDeg, float czDeg, float* out) {
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
static void Zelda3D_Mat3ToEuler(const float* m, float* outDeg) {
    float sy = -m[6];
    if (sy > 1.0f) sy = 1.0f; else if (sy < -1.0f) sy = -1.0f;
    float ry = asinf(sy), rx, rz;
    if (fabsf(m[6]) < 0.99999f) { rx = atan2f(m[7], m[8]); rz = atan2f(m[3], m[0]); }
    else { rx = atan2f(-m[5], m[4]); rz = 0.0f; }
    outDeg[0] = rx * (180.0f / 3.14159265358979f);
    outDeg[1] = ry * (180.0f / 3.14159265358979f);
    outDeg[2] = rz * (180.0f / 3.14159265358979f);
}

// --- OoT3D Link (player) replacement — proof-of-hook stage (see Zelda3D_TryDrawPlayer) ---
int gZelda3dLinkOn = 1;  // OoT3D player body: ALWAYS ON. Dev A/B only via REPL `link`.
float gZelda3dLinkScale = 0.011f; // world scale OoT3D-link-local -> N64 player units (REPL `linkscale`, calibrate live)
float gZelda3dLinkRotX = 0.0f;   // rest->upright orientation correction (deg) (REPL `linkrot`)
float gZelda3dLinkRotY = 0.0f;
float gZelda3dLinkRotZ = 0.0f;
char gZelda3dLinkForceCsab[64] = ""; // REPL `linkanim <csab>` pins a CSAB on Link (verify idle/walk/run
                                   // deterministically without real movement input); empty = live-resolve
// Per-draw Link trace (`log link 1` / REPL `linktrace 1`): heldActor/carryWalk/lower+upper anim/
// csab + mounted extras — routed through the diagnostic-logger registry (core/zelda3d_log.h).
// REPL `linktwo <lower> <upper>`: force the #85 two-source per-limb blend (lower loco + upper carry)
// with explicit CSAB bases, so the carry-walk pose can be captured/verified WITHOUT a live grab
// (skindump + the bone partition). Lower free-runs at gZelda3dAnimRate (legs cycle); upper free-runs
// (carry hold). Empty = off. Also reusable for the future per-state Link parity sweep.
char gZelda3dLinkForceTwoLower[64] = "";
char gZelda3dLinkForceTwoUpper[64] = "";
int gZelda3dClimbGroundFix = 0; // #79: freeze feet-grounding during climb poses (REPL `climbgroundfix`).
                              // DEFAULT OFF: root cause confirmed (grounding swings wildly on climb
                              // poses) but the freeze DIRECTION is not yet live-verified on a real
                              // sustained climb (repro blocked — see #79). Off = current behavior.
int gZelda3dHeldAttach = 1;          // #6 attach a carried actor (held cucco) to 3DS Link's posed hands
int gZelda3dFocusFix = 1;            // #16(b) keep actor.focus.pos at 3DS Link's posed head (first-person cam)
                                   // (A/B toggle for before/after evidence; REPL `linkheldfix <0|1>`)
// #7: CSAB frames advanced per draw per unit of Link's ground speed, when speed-driving the
// locomotion cycle (run/walk) — Link's run/walk advances its pose through a player-internal
// accumulator, NOT skelAnime.curFrame (pinned at 0 the whole run), so curFrame can't phase-lock the
// CSAB (it froze at frame 0 -> the motionless slide). A footstep cadence is a function of movement
// speed, so we free-run the CSAB at speedXZ * this gain. Calibrated live vs N64 (REPL `linkloco`).
float gZelda3dLinkLocoGain = 0.30f;
extern int gZelda3dLogicFrame; // zelda3d_anim.cpp: logic-frame clock for the walk-stop synthetic morph
// Player animation SOURCE (REPL `linksrc`, env ZELDA3D_LINK_SRC). Two independent, both-working modes:
//   0 = 3DS own-CSAB: play the OoT3D link rig's own CSAB matching the named player anim (kPlayerAnimMap).
//       Faithful for discrete states (idle fidgets, jumps, item use) but BLIND to walk/run — OoT blends
//       locomotion into jointTable without naming an anim, so Link slides without a walk cycle.
//   1 = N64 retarget: drive the OoT3D rig from Link's LIVE skelAnime.jointTable every frame (the final
//       blended pose). Captures walk/run/everything exactly, in lockstep with the N64 body.
// User-selectable per [[zelda3d-link-player-path]]; default N64 so locomotion works out of the box.
int gZelda3dLinkAnimSrc = -1;

// Per-frame mesh_id visibility mask for Link's body CMB. childlink_v2 bakes every hand-pose and
// held-equipment variant onto distinct mesh_ids; the game shows a state-dependent subset. We pick
// the visible set each frame (PlayerBehavior::midmask.compute) and push it via Zelda3D_GL_SetMidMask.
// The mesh-id override (REPL `linkmid`), jointTable-dump capture (`linkjointdump`), cucco-grab driver
// (`linkgrab`), and transform pin (`linkpin`) state moved into PlayerBehavior's composed subsystems
// (midmask / jointDump / grab / pin) — see player.h.

// The OoT3D player body is the DEFAULT and only shipped path — no env gate.
//
// It used to be off unless ZELDA3D_LINK=1, left over from when the hook was a
// proof-of-concept bind pose. That stopped being true long ago (the on-foot port
// sweeps at 22/25 states matching and mounted Link was verified at title), but the
// gate stayed, so every normal run silently rendered N64 Link while every other
// actor rendered from OoT3D — which is exactly what the Kokiri A/B caught.
extern "C" int Zelda3D_LinkEnabled(void) {
    return gZelda3dLinkOn;
}

// Player animation source: 0 = 3DS own-CSAB (DEFAULT), 1 = N64 jointTable retarget. env ZELDA3D_LINK_SRC
// ("3ds"/0 or "n64"/1); REPL `linksrc`.
extern "C" int Zelda3D_LinkAnimSrc(void) {
    if (gZelda3dLinkAnimSrc < 0) {
        const char* v = getenv("ZELDA3D_LINK_SRC");
        // DEFAULT = 3DS own-CSAB: Zelda3D is a graphical port of OoT3D, so Link plays the OoT3D rig's
        // OWN CSABs (1-1 parity). The former blocker — the named-CSAB path being "blind to walk/run" —
        // is fixed (#117 Zelda3D_LinkWalkRunGate selects nml_walk_free/nml_run_free by speedXZ). The N64
        // jointTable-retarget path is retained as a fallback/debug mode only (env "n64"/"1", REPL
        // `linksrc n64`). env "n64"/"1" -> N64 retarget; anything else -> 3DS own-CSAB.
        gZelda3dLinkAnimSrc = (v != NULL && (v[0] == 'n' || v[0] == '1')) ? 1 : 0;
    }
    return gZelda3dLinkAnimSrc;
}

// childlink_v2 mesh_id idle fallback. See Zelda3D_TryDrawPlayer.
#define ZELDA3D_LINK_IDLE_CSAB "nml_wait_typeA_20f"

// #88 "weird yawn" / wrong idle fidget — PREMISE FALSIFIED, DO NOT RE-CHASE (measured 2026-07-23,
// live oracle vs live game at Kokiri 0xEE; oot3d-decomp/docs/player_port.md "#88", journal
// debug_journal/2026-07-23-88-idle-fidget-premise-falsified.md).
//
// There IS no yawn animation. The decomp note that started this ("default idle table @0x53a5f8
// {0x50=yawn,...}") mis-read a raw table index: animId 0x50 is `nml_wait_free`, the neutral standing
// idle, and a scan of all 582 player anim names finds no yawn/akubi/stretch clip at all. OoT3D's
// Player_GetIdleAnim table {0x50,0x58,0x58,0x119} = {nml_wait_free, nml_wait, nml_wait, ft_wait_long}
// is byte-identical to N64's D_80853914[PLAYER_ANIMGROUP_wait]. So the idle path here needs no change:
// we run the vendored N64 Player_ChooseNextIdleAnim and map its anim resource through kPlayerAnimMap,
// and OoT3D's inlined twin (004ba538) is faithful to that N64 code for every reachable plain idle —
// including the -6.0f LinkAnimation_Change morph, which N64 already does.
//
// Measured at matched state -- neither side had a weapon IN HAND at idle (the gate tests
// rightHandType == RH_SHIELD, set only with the sword drawn; at a plain idle it is RH_OPEN on both
// sides regardless of inventory, so commonType 0/3 are rejected either way): default idle
// nml_wait_free both sides; fidget set + distribution match the faithful N64 roll (ours n=26:
// look-around 69% / tunic 15% / tap-feet 15%; oracle n=6: 67% / 33% / 0%; predicts 60/20/20); default<->fidget
// alternation and the 2:1 fidget:default hold ratio match.
//
// SAMPLING TRAP (cost this session an hour): idle re-picks only fire on animDone, ~130-280 frames
// apart. A short capture (n<=3 picks) shows ONLY the look-around fidget and reads as a hard divergence
// ("OoT3D suppresses the tunic/tap-feet fidgets") — it is small-n noise. Any idle-distribution claim
// needs >=20 fidget picks per side.
//
// Two genuine 3DS-only deltas exist in 004ba538 but are INERT at a reachable idle and are NOT this
// symptom, so nothing is stubbed or faked for them here:
//   (1) HOT-room bit `if (play[0x4c37]) fidgetType = FIDGET_HOT(3)` — authored per-room via
//       SCENE_CMD_ROOM_BEHAVIOR; a faithful port needs that bit read from the ROM room header, and
//       FIDGET_HOT shares its anims with FIDGET_WARM anyway.
//   (2) `if ((focusActor==0) && (play[0x2130] != 0))` bypasses the common-fidget roll. play+0x2130 is
//       the 3DS-only auto-aim head-track TARGET actor (pinned via 002b7fd0.c:556 ->
//       func_0x002bf814(...)); porting this gate is BLOCKED on porting auto-aim 0x2bf814 itself.
//       Measured 0 at Kokiri (the common fidgets do fire on the oracle). Do NOT approximate it with a
//       "nearest actor" guess — that would fake an un-RE'd subsystem's output.

// #117 walk/run SELECTION threshold (speedXZ). N64 OoT uses ONE run anim
// (gPlayerAnim_link_normal_run_free) with speed-scaled playback for ALL ground movement, so Zelda3D's
// N64-anim->CSAB map always yields nml_run_free. OoT3D/Grezzo instead SELECTS distinct walk/run CSABs
// by speed (RE'd: FUN_002be660 walk/run picker, in the FUN_004ba378 run action family; oot3d-decomp
// player_port.md). Measured live off the oracle (tools/parity_speed_sweep.py): nml_walk_free sustains
// to speedXZ ~3.17, nml_run_free starts at ~4.04. 3.6 sits in that gap and also separates Zelda3D's own
// speed curve (walk 0.98/1.49, run 5.50) cleanly. Below this -> play the walk cycle, not a slow run.
#define ZELDA3D_LINK_WALKRUN_SPEED 3.6f

// Apply the #117 walk/run SELECTION gate to a resolved player locomotion CSAB. The N64 anim is the
// single run cycle (-> nml_run_free at every ground speed); OoT3D plays nml_walk_free in the walk band.
// Shared by the draw path AND the `linkanimstate` reporter (so the parity sweep reads what is actually
// drawn, not the raw N64-anim mapping). Plain run only (damage_run/heavy_run/side_walk are own states).
extern "C" const char* Zelda3D_LinkWalkRunGate(const char* csab, float speedXZ) {
    if (csab != NULL && strcmp(csab, "nml_run_free") == 0 &&
        speedXZ > 0.5f && speedXZ <= ZELDA3D_LINK_WALKRUN_SPEED) {
        return "nml_walk_free";
    }
    return csab;
}

// #85 carry-WALK upper-body bone mask for the two-source per-limb blend (Zelda3D_UpdateAnimTwoSource).
// The OoT3D analogue of N64 sUpperBodyLimbCopyMap (z_player.c:397): the N64 marks limbs UPPER..TORSO
// (PLAYER_LIMB 10-21) as upper-body. Mapping those to the shared 25-bone link rig via the verified
// bone<->limb correspondence (zelda3d_link_bonecorr.inc / oot3d-decomp/docs/link_bone_map.md): N64
// UPPER->b9, the extra OoT3D chest/collar bones b10/b13/b17 (TORSO/COLLAR region, no own N64 limb),
// HEAD->b11, HAT->b12, L arm->b14/15/16, R arm->b18/19/20, SHEATH->b21. So bones 9..21 are upper;
// 0..8 (root/waist/lower-pivot/legs) and 22..24 (aux root2) stay on the lower locomotion clip. Same
// partition for child (childlink_v2) and adult (link_v2) — they share the rig.
static const unsigned char kLinkUpperBodyMask[25] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, // b0..b8  lower: root, waist, lower-pivot, R/L legs
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, // b9..b21 upper: spine, chest, head, hat, both arms, sheath
    0, 0, 0 // b22..b24 aux root2 -> lower
};

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
//
// Stage 2a of the MM/OoT unification (scratch/plans/mm_oot_link_unify.md): the actual mesh_id policy
// now lives in Shipwright/zelda3d_shared/player/link_midmask.cpp and consumes a game-agnostic
// LinkGear POD. This function is the OoT-side ADAPTER — translates the live OoT Player fields into
// LinkGear, hands off to the shared computation, then returns. Zero behavior change (the shared
// code is a verbatim port); MM will add its own translator in Stage 2b when MmPlayerBehavior lands.
unsigned long long Zelda3D::LinkMidMask::boyMidMask(Player* player) const {
    Zelda3D::LinkGear g;

    switch (player->leftHandType) {
        case PLAYER_MODELTYPE_LH_SWORD:
        case PLAYER_MODELTYPE_LH_SWORD_2: g.leftHand = LinkHandLeft::SwordOneHand; break;
        case PLAYER_MODELTYPE_LH_BGS:     g.leftHand = LinkHandLeft::SwordTwoHand; break;
        case PLAYER_MODELTYPE_LH_BOTTLE:  g.leftHand = LinkHandLeft::Bottle; break;
        case PLAYER_MODELTYPE_LH_HAMMER:  g.leftHand = LinkHandLeft::Hammer; break;
        case PLAYER_MODELTYPE_LH_CLOSED:  g.leftHand = LinkHandLeft::Closed; break;
        case PLAYER_MODELTYPE_LH_OPEN:
        default:                          g.leftHand = LinkHandLeft::Open; break;
    }
    switch (player->rightHandType) {
        case PLAYER_MODELTYPE_RH_SHIELD:  g.rightHand = LinkHandRight::Shield; break;
        case PLAYER_MODELTYPE_RH_BOW_SLINGSHOT:
        case PLAYER_MODELTYPE_RH_BOW_SLINGSHOT_2: g.rightHand = LinkHandRight::Bow; break;
        case PLAYER_MODELTYPE_RH_CLOSED:  g.rightHand = LinkHandRight::Closed; break;
        case PLAYER_MODELTYPE_RH_HOOKSHOT: g.rightHand = LinkHandRight::Hookshot; break;
        case PLAYER_MODELTYPE_RH_OCARINA:  g.rightHand = LinkHandRight::Ocarina; break;
        case PLAYER_MODELTYPE_RH_OPEN:
        default:                          g.rightHand = LinkHandRight::Open; break;
    }
    switch (player->sheathType) {
        case PLAYER_MODELTYPE_SHEATH_18: g.sheath = LinkSheath::ShieldOnBackSwordSheathed; break;
        case PLAYER_MODELTYPE_SHEATH_19: g.sheath = LinkSheath::ShieldOnBackSwordDrawn; break;
        case PLAYER_MODELTYPE_SHEATH_16: g.sheath = LinkSheath::SwordOnBackNoShield; break;
        case PLAYER_MODELTYPE_SHEATH_17:
        default:                         g.sheath = LinkSheath::EmptySheathNoShield; break;
    }
    switch (player->currentShield) {
        case PLAYER_SHIELD_HYLIAN: g.shield = LinkShield::Hylian; break;
        case PLAYER_SHIELD_MIRROR: g.shield = LinkShield::Mirror; break;
        default:                   g.shield = LinkShield::None; break;
    }
    return Zelda3D::linkAdultMidMask(g);
}

unsigned long long Zelda3D::LinkMidMask::compute(Player* player) const {
    unsigned long long m;
    int deku, hylian;
    if (overrideSet) {
        return overrideMask; // REPL `linkmid` debug override (identification sweep)
    }
    if (LINK_AGE_IN_YEARS != YEARS_CHILD) {
        return boyMidMask(player); // adult uses link_v2.cmb's own mesh_id layout
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

// sLinkModelId moved to PlayerBehavior::modelId (the player's current draw model id).
extern "C" int Zelda3D_LinkModelId(void) { return P().modelId; }

// LinkPoseScan LOGGER. The pose (lastSkin) is recomputed in the DRAW path, not in Play_Update, so the
// per-frame discontinuity must be sampled there — once per rendered frame — to be meaningful (the
// frame-step `step` advances logic without drawing). When active, each drawn player frame records the
// max per-bone rotation jump + the bone + the resolved csab + its frame. The REPL reads the log back.
void Zelda3D::LinkPoseScan::setActive(int on) {
    mActive = on ? 1 : 0;
    if (on) {
        mCount = 0; // clear the log only when STARTING a scan; `off` keeps it for `dump`
        int mid = Zelda3D::PlayerBehavior::instance().modelId;
        if (mid >= 0) Zelda3D_PoseScanReset(mid); // baseline = next frame
    }
}
float Zelda3D::LinkPoseScan::get(int i, int* bone, float* frame, const char** csab) {
    if (i < 0 || i >= mCount) { if (bone) *bone = -1; if (frame) *frame = 0; if (csab) *csab = ""; return 0; }
    if (bone) *bone = mLog[i].bone;
    if (frame) *frame = mLog[i].frame;
    if (csab) *csab = mLog[i].csab;
    return mLog[i].deg;
}
// Called from the draw path right after the pose is set, when the scan is active.
void Zelda3D::LinkPoseScan::record(int modelId, const char* csab, float frame) {
    if (!mActive || mCount >= (int)(sizeof(mLog) / sizeof(mLog[0]))) return;
    int bone = -1;
    float deg = Zelda3D_PoseDiscontinuity(modelId, &bone);
    Rec& r = mLog[mCount++];
    r.deg = deg; r.bone = bone; r.frame = frame;
    const char* c = csab ? csab : "(n64-retarget)";
    int k = 0; for (; c[k] && k < 27; k++) r.csab[k] = c[k];
    r.csab[k] = '\0';
}
extern "C" void Zelda3D_PoseScanSetActive(int on) { P().poseScan.setActive(on); }
extern "C" int Zelda3D_PoseScanCount(void) { return P().poseScan.count(); }
extern "C" float Zelda3D_PoseScanGet(int i, int* bone, float* frame, const char** csab) {
    return P().poseScan.get(i, bone, frame, csab);
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
    zar = (LINK_AGE_IN_YEARS == YEARS_CHILD) ? "/actor/zelda_link_child_new.zar"
                                             : "/actor/zelda_link_boy_new.zar";
    Zelda3D_EnsureModelProvider();
    modelId = Zelda3D_AutoModelId(zar); // auto-picks the largest single CMB = the body (link.cmb)
    if (modelId < 0) {
        return 0; // model unavailable -> fall back to the N64 body
    }
    P().modelId = modelId; // expose to the REPL pose-discontinuity scanner (Zelda3D_LinkModelId)
    // Player is Actor-first so the cast is valid (see z64player.h).
    player = (Player*)actor;
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
    // Measure the posed feet this frame so we can ground them: the OoT3D Link CSABs lift the
    // skeleton off the floor (boy-rig hip translation; #29b). The world matrix is built AFTER the
    // pose + visible-mesh mask are known (below), so the ground offset can use the live pose.
    Zelda3D_SetTrackPosedMinY(modelId, 1);
    // linkjointdump capture: append this frame's live jointTable + phase, for offline retarget fitting.
    if (P().jointDump.file != NULL && P().jointDump.remaining > 0 &&
        player->skelAnime.jointTable != NULL && player->skelAnime.limbCount > 0) {
        const char* an = (const char*)player->skelAnime.animation;
        const char* base = an ? strrchr(an, '/') : NULL;
        base = base ? base + 1 : (an ? an : "(null)");
        int li;
        for (li = 1; li <= player->skelAnime.limbCount; li++) {
            Vec3s* j = &player->skelAnime.jointTable[li];
            fprintf(P().jointDump.file, "%d,%.3f,%.1f,%s,%d,%d,%d,%d\n", P().jointDump.cap,
                    player->skelAnime.curFrame, player->skelAnime.animLength, base, li, j->x, j->y, j->z);
        }
        P().jointDump.cap++;
        if (--P().jointDump.remaining == 0) {
            fclose(P().jointDump.file);
            P().jointDump.file = NULL;
        }
    }
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
    if (Zelda3D_LinkAnimSrc() == 1 && gZelda3dLinkForceCsab[0] == '\0' &&
        player->skelAnime.jointTable != NULL && player->skelAnime.limbCount > 0) {
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
                fflush(stdout);
            }
        }
        P().retarget.ensure();
        // Hand-weave pose-freeze: latch the live idle jointTable on request, then feed the frozen copy
        // so tuning `linkcorr` is the only variable (the idle fidget otherwise moves the pose).
        const s16* jrots = (const s16*)&player->skelAnime.jointTable[1];
        int jcount = player->skelAnime.limbCount;
        if (P().retarget.freezeReq && jcount > 0 && jcount < 39) {
            for (int i = 0; i <= jcount; i++) P().retarget.frozenJoints[i] = player->skelAnime.jointTable[i];
            P().retarget.frozenCount = jcount;
            P().retarget.freezeReq = 0;
        }
        if (P().retarget.frozenCount > 0) { jrots = (const s16*)&P().retarget.frozenJoints[1]; jcount = P().retarget.frozenCount; }
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
            const char* lob = lo ? strrchr(lo, '/') : NULL; lob = lob ? lob + 1 : (lo ? lo : "(null)");
            const char* up = (const char*)player->upperSkelAnime.animation;
            const char* upb = up ? strrchr(up, '/') : NULL; upb = upb ? upb + 1 : (up ? up : "(null)");
            // Mounted extras: the horse anim index the Player's D_80854944 selector keys on, the
            // Player's latched copy (av2.actionVar2), and the horse frame Link syncs to (z_player.c
            // ~14048/14081) — the title-demo riding-pose diagnosis needs exactly these three.
            EnHorse* rh = (EnHorse*)player->rideActor;
            Z3D_LOG(LINK, "held=%d carryWalk=%d lin=%.2f spd=%.2f lower=%s(cf=%.1f/%.1f mw=%.3f mr=%.3f) upper=%s(cf=%.1f)"
                    " unk868=%.2f horseAnimIdx=%d av2=%d horseCurFrame=%.1f -> csab=%s\n",
                    player->heldActor != NULL, carryWalk, player->linearVelocity, player->actor.speedXZ,
                    lob, player->skelAnime.curFrame, player->skelAnime.animLength,
                    player->skelAnime.morphWeight, player->skelAnime.morphRate,
                    upb, player->upperSkelAnime.curFrame, player->unk_868,
                    rh ? (int)rh->animationIdx : -1, (int)player->av2.actionVar2,
                    rh ? rh->curFrame : -1.0f, csab);
        }
        if (gZelda3dAnimDebug) {
            static int dbg = 0;
            if ((dbg++ % 30) == 0) {
                const char* otr = (const char*)player->skelAnime.animation;
                fprintf(stderr, "SOH3D LINK: src=3DS n64=%s -> csab=%s frame=%.1f/%.1f speedXZ=%.2f\n",
                       otr ? otr : "(none)", csab, player->skelAnime.curFrame,
                       player->skelAnime.animLength, player->actor.speedXZ);
                fflush(stdout);
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
            Zelda3D_UpdateAnimTwoSource(modelId, gZelda3dLinkForceTwoLower, gZelda3dAnimRate,
                                      gZelda3dLinkForceTwoUpper, 0.0f, 0.0f, kLinkUpperBodyMask, 25);
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
                Zelda3D_UpdateAnimAuto(modelId, csab, player->actor.speedXZ * gZelda3dLinkLocoGain,
                                     0.0f, 0.0f, 0.0f);
            } else {
                Zelda3D_UpdateAnimAuto(modelId, csab, 0.0f, player->unk_868, 29.0f, 0.0f);
            }
        } else {
            // Idle / one-shot anims: curFrame is valid here, so keep the N64-progress phase-lock.
            Zelda3D_UpdateAnimAuto(modelId, csab, gZelda3dAnimRate, player->skelAnime.curFrame,
                                 player->skelAnime.animLength, player->skelAnime.morphWeight);
        }
    }
    // Pose-scan QA: sample the per-frame discontinuity now that lastSkin is set (once per drawn frame).
    P().poseScan.record(modelId, csab, player->skelAnime.curFrame);
    // Select Link's live equipment / hand-pose variant subset (the childlink_v2 mesh bakes them
    // all on distinct mesh_ids). Must be set BEFORE EmitPose so it pairs with this draw item.
    unsigned long long midMask = P().midmask.compute(player);
    if (gZelda3dAnimDebug) {
        static int dbg = 0;
        if ((dbg++ % 30) == 0) {
            fprintf(stderr, "SOH3D LINK mids: LH=%d RH=%d sheath=%d shield=%d -> mask=0x%llx\n",
                   player->leftHandType, player->rightHandType, player->sheathType,
                   player->currentShield, midMask);
            fflush(stdout);
        }
    }
    Zelda3D_GL_SetMidMask(modelId, midMask);
    // Build the world matrix now that the pose + visible-mesh mask are known. Do NOT inherit the
    // actor matrix (it carries the N64 0.01 actor scale through which the OoT3D dlist renders
    // nothing). groundOff (model-local, applied innermost/pre-scale like the auto path's
    // groundOffset) lands the posed feet on the actor's world pos.y — fixes the #29b float.
    float groundOff = Zelda3D_PosedGroundOffset(modelId, midMask);
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
    s32 climbPose = gZelda3dClimbGroundFix && csab != NULL &&
                    (strstr(csab, "climb") != NULL || strstr(csab, "hang") != NULL);
    // Mounted (horseback): actor.world.pos.y is ALREADY the correct seat height, set every frame
    // by the native mount code (z_player.c Player_Action_8084CC98: world.pos.y = rideActor->
    // actor.world.pos.y + rideActor->riderPos.y - 27.0f — Epona's back position, not the ground).
    // The feet-grounding heuristic below assumes the pose's lowest visible vertex is a planted
    // foot on the floor; for the riding pose that lowest vertex is a bent knee/stirrup nowhere
    // near actor.world.pos.y, so applying it here shoves the whole body up/down by that offset —
    // this IS the reported "Link floats detached above/behind Epona" bug. No grounding is correct
    // (same as N64's own mounted draw, which never grounds either) — zero it outright rather than
    // freeze-last (unlike climb, the seat height genuinely changes every frame as Epona moves over
    // terrain, so a frozen offset would itself drift stale).
    s32 mountedPose = (player->stateFlags1 & PLAYER_STATE1_ON_HORSE) != 0;
    // #152 seat diagnostics (`log rider 1`): posed origins of the first rig bones while mounted —
    // identifies which bone carries the riding clip's root translation (the term the 3DS subtracts
    // from the attach: player.pos = anchor - rootJoint*scale, FUN_002b7fd0 / en_horse_rider_pos.md).
    if (mountedPose && Zelda3D_LogEnabled(Z3D_LOG_RIDER)) {
        float b0[3] = {0}, b1[3] = {0}, b2[3] = {0};
        Zelda3D_PosedBoneWorldPos(modelId, 0, b0);
        Zelda3D_PosedBoneWorldPos(modelId, 1, b1);
        Zelda3D_PosedBoneWorldPos(modelId, 2, b2);
        Z3D_LOG(RIDER, "LINKROOT b0=(%.0f,%.0f,%.0f) b1=(%.0f,%.0f,%.0f) b2=(%.0f,%.0f,%.0f) "
                "scale=%.5f csab=%s\n", b0[0], b0[1], b0[2], b1[0], b1[1], b1[2],
                b2[0], b2[1], b2[2], gZelda3dLinkScale, csab ? csab : "(n64)");
    }
    // Mounted seat anchor (#152, RE'd: oot3d-decomp/docs/en_horse_rider_pos.md FUN_002b7fd0):
    // OoT3D sets player.pos = seatAnchor - rootJoint*scale and draws the pose WITH its root, so
    // the pelvis lands exactly on the seat anchor. Our z_player port subtracts the N64's folded
    // constant 27 instead of the live root, and the 3DS riding clips carry a much larger root
    // (uma_anim_*: bone-1 y=3538 * 0.011 = 38.9) — net: Link drew ~12 world units above the
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
    if (mountedPose) {
        groundOff = 0.0f;
    } else if (climbPose) {
        groundOff = sLinkLastGroundedOff;
    } else {
        sLinkLastGroundedOff = groundOff;
    }
    if (gZelda3dAnimDebug) {
        static int dbg = 0;
        if ((dbg++ % 30) == 0) {
            fprintf(stderr, "SOH3D LINK groundOff=%.1f (model-local)%s\n", groundOff,
                   mountedPose ? " [mounted:zeroed]" : (climbPose ? " [climb:frozen]" : ""));
            fflush(stdout);
        }
    }
    Matrix_Translate(actor->world.pos.x, actor->world.pos.y, actor->world.pos.z, MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(actor->shape.rot.y), MTXMODE_APPLY);
    Matrix_Scale(gZelda3dLinkScale, gZelda3dLinkScale, gZelda3dLinkScale, MTXMODE_APPLY);
    if (gZelda3dLinkRotX != 0.0f) Matrix_RotateX(gZelda3dLinkRotX * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gZelda3dLinkRotY != 0.0f) Matrix_RotateY(gZelda3dLinkRotY * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (gZelda3dLinkRotZ != 0.0f) Matrix_RotateZ(gZelda3dLinkRotZ * (3.14159265f / 180.0f), MTXMODE_APPLY);
    if (groundOff != 0.0f) Matrix_Translate(0.0f, groundOff, 0.0f, MTXMODE_APPLY);
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
            float b9[3] = {0}, b10[3] = {0}, b11[3] = {0};
            Zelda3D_PosedBoneWorldPos(modelId, 9, b9);
            Zelda3D_PosedBoneWorldPos(modelId, 10, b10);
            Zelda3D_PosedBoneWorldPos(modelId, 11, b11);
            Vec3f l9 = {b9[0],b9[1],b9[2]}, w9; Matrix_MultVec3f(&l9, &w9);
            Vec3f l10 = {b10[0],b10[1],b10[2]}, w10; Matrix_MultVec3f(&l10, &w10);
            Vec3f l11 = {b11[0],b11[1],b11[2]}, w11; Matrix_MultVec3f(&l11, &w11);
            static int fdbg = 0;
            if ((fdbg++ % 30) == 0) {
                fprintf(stderr, "SOH3D FOCUS dbg b9=(%.0f,%.0f,%.0f) b10=(%.0f,%.0f,%.0f) b11=(%.0f,%.0f,%.0f) world=(%.0f,%.0f,%.0f)\n",
                       w9.x,w9.y,w9.z, w10.x,w10.y,w10.z, w11.x,w11.y,w11.z,
                       actor->world.pos.x, actor->world.pos.y, actor->world.pos.z);
                fflush(stdout);
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
    const char* zar = (LINK_AGE_IN_YEARS == YEARS_CHILD) ? "/actor/zelda_link_child_new.zar"
                                                         : "/actor/zelda_link_boy_new.zar";
    int modelId = Zelda3D_AutoModelId(zar);
    if (modelId < 0) {
        if (outCsab) *outCsab = "(no model)";
        return 0.0f;
    }
    if (outCsab) {
        if (gZelda3dLinkForceCsab[0] != '\0') {
            *outCsab = gZelda3dLinkForceCsab;
        } else {
            const char* c = Zelda3D_ResolvePlayerCsab((const char*)player->skelAnime.animation);
            c = Zelda3D_LinkWalkRunGate(c, player->actor.speedXZ);  // report the gated (drawn) CSAB
            *outCsab = c ? c : ZELDA3D_LINK_IDLE_CSAB " (fallback)";
        }
    }
    return Zelda3D_PosedGroundOffset(modelId, P().midmask.compute(player));
}

// #8 hand-weave: pin Link's world pos + facing yaw each frame, so the side-profile view is identical
// across `linkcorr` tweaks. Extracted from Zelda3D_ActorPostUpdate; called from that same call site.
void Zelda3D::LinkPin::apply(PlayState* play, Actor* actor) {
    if (actor != NULL && on && play != NULL && actor == &GET_PLAYER(play)->actor) {
        actor->velocity.x = actor->velocity.y = actor->velocity.z = 0.0f;
        actor->speedXZ = 0.0f;
        actor->world.pos = pos;
        actor->shape.rot.y = actor->world.rot.y = yaw;
    }
}

// #6/#9 linkgrab: hold the selected actor in front of Link + inject fresh A edges until grabbed.
// Extracted from Zelda3D_WalkInject; called from that same call site.
void Zelda3D::LinkGrabDriver::walkInject(PlayState* play) {
    if (frames > 0) {
        Player* pl = GET_PLAYER(play);
        if (pl == NULL || gZelda3dSelActor == NULL) {
            frames = 0;
        } else if (pl->heldActor != NULL) {
            frames = 0; // grabbed — let the real carry action take over
        } else {
            float yawRad = pl->actor.shape.rot.y * (3.14159265f / 32768.0f);
            float fx = sinf(yawRad), fz = cosf(yawRad);
            const float d = 26.0f; // grab range is small; keep the cucco just in front
            gZelda3dSelActor->world.pos.x = pl->actor.world.pos.x + fx * d;
            gZelda3dSelActor->world.pos.y = pl->actor.world.pos.y + 4.0f;
            gZelda3dSelActor->world.pos.z = pl->actor.world.pos.z + fz * d;
            gZelda3dSelActor->speedXZ = 0.0f;
            gZelda3dSelActor->velocity.y = 0.0f;
            // fresh A rising edge every 4th frame so an edge always lands once detection is true.
            play->state.input[0].cur.button |= BTN_A;
            if ((frames & 3) == 0) play->state.input[0].press.button |= BTN_A;
            frames--;
        }
    }
}

// Write one N64 limb to a file in tools/zelda3d_skel_match.py's load_n64() format (REPL linkskeldump).
static void Zelda3D_DumpLimbFileCb(int limbIndex, StandardLimb* lb, void* ud) {
    FILE* f = (FILE*)ud;
    fprintf(f, "N64 limb=%d jointPos=(%d,%d,%d) child=%d sibling=%d\n", limbIndex,
            lb->jointPos.x, lb->jointPos.y, lb->jointPos.z, lb->child, lb->sibling);
}

// Handle a `link*` REPL command. Returns 1 if handled (cmd was a link command), 0 otherwise.
int Zelda3D::PlayerBehavior::repl(PlayState* play, const char* cmd, const char* line, const char* outPath) {
    char path[1024];
    float f1, f2, f3;
    int iv;
    if (strcmp(cmd, "linkgrab") == 0) {
        // #6/#9 — drive a REAL cucco pickup: `asel 0x19` first, then `linkgrab [frames]` (default
        // 60). Holds the selected actor in front of Link + injects A edges until heldActor is set,
        // so the genuine grab/lift action fires (B then shows Throw). afreeze should be 0.
        int frames = 60;
        sscanf(line, "%*s %d", &frames);
        if (gZelda3dSelActor == NULL) {
            Zelda3D_ReplReply(outPath, "linkgrab: no actor selected (asel 0x19 first)");
        } else {
            gZelda3dActorFreeze = 0; // the driver positions it every frame; freeze would fight that
            P().grab.frames = frames;
            Zelda3D_ReplReply(outPath, "linkgrab: driving pickup of id=0x%X for %d frames",
                            gZelda3dSelActor->id, frames);
        }
    } else if (strcmp(cmd, "linkskeldump") == 0 && sscanf(line, "%*s %1023s", path) == 1) {
        // Dump the live PLAYER N64 skeleton (limb tree + jointPos) to a file in the
        // zelda3d_skel_match.py load_n64() format, to DERIVE the OoT3D-bone -> N64-limb bonemap for
        // the N64-retarget Link mode (linksrc n64). One-shot, on demand.
        Player* pl = GET_PLAYER(play);
        if (pl == NULL || pl->skelAnime.skeleton == NULL || pl->skelAnime.limbCount <= 0) {
            Zelda3D_ReplReply(outPath, "linkskeldump: no player skeleton");
        } else {
            FILE* sf = fopen(path, "w");
            if (sf == NULL) {
                Zelda3D_ReplReply(outPath, "linkskeldump: cannot open %s", path);
            } else {
                fprintf(sf, "# player N64 skeleton; limbCount=%d age=%d\n", pl->skelAnime.limbCount,
                        LINK_AGE_IN_YEARS);
                Zelda3D_WalkN64Skeleton((void**)pl->skelAnime.skeleton, pl->skelAnime.limbCount,
                                      Zelda3D_DumpLimbFileCb, sf);
                fclose(sf);
                Zelda3D_ReplReply(outPath, "linkskeldump -> %s (limbCount=%d age=%d)", path,
                                pl->skelAnime.limbCount, LINK_AGE_IN_YEARS);
            }
        }
    } else if (strcmp(cmd, "linktrace") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // Routed through the diagnostic-logger registry (equivalent to `log link <0|1>`).
        Zelda3D_LogSet("link", iv ? 1 : 0);
        Zelda3D_ReplReply(outPath, "linktrace=%d (per-draw held/carryWalk/lower+upper anim/csab -> run.log)",
                        iv ? 1 : 0);
    } else if (strcmp(cmd, "link") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        gZelda3dLinkOn = iv ? 1 : 0;
        Zelda3D_ReplReply(outPath, "link=%d (OoT3D player body replacement; 1 = the shipped default)",
                        gZelda3dLinkOn);
    } else if (strcmp(cmd, "linkscale") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dLinkScale = f1;
        Zelda3D_ReplReply(outPath, "linkscale=%.5f (OoT3D-link-local -> N64 player world units)",
                        gZelda3dLinkScale);
    } else if (strcmp(cmd, "linkloco") == 0) {
        // #7 calibrate the speed->CSAB-cadence gain for Link's locomotion cycle. `linkloco <gain>`
        // sets it; no-arg reports it + Link's live speedXZ (so the cadence can be tuned vs N64).
        if (sscanf(line, "%*s %f", &f1) == 1) {
            gZelda3dLinkLocoGain = f1;
        }
        Zelda3D_ReplReply(outPath, "linkloco gain=%.3f (CSAB frames/draw per speed unit); link speedXZ=%.2f",
                        gZelda3dLinkLocoGain, GET_PLAYER(play)->actor.speedXZ);
    } else if (strcmp(cmd, "linkfreeze") == 0) {
        // #7 hand-weave: freeze Link's live idle pose so `linkcorr` tweaks are the only variable.
        if (sscanf(line, "%*s %i", &iv) == 1) {
            if (iv) { P().retarget.freezeReq = 1; P().retarget.frozenCount = 0; }
            else { P().retarget.frozenCount = 0; P().retarget.freezeReq = 0; }
        }
        Zelda3D_ReplReply(outPath, "linkfreeze=%d (frozenLimbs=%d, req=%d)",
                        P().retarget.frozenCount > 0 ? 1 : 0, P().retarget.frozenCount, P().retarget.freezeReq);
    } else if (strcmp(cmd, "linkpin") == 0) {
        // #8 hand-weave: pin Link's world pos + facing yaw so the side-profile view is identical
        // across `linkcorr` tweaks (linkfreeze alone leaves the actor free to idle-turn -> desync).
        if (sscanf(line, "%*s %i", &iv) == 1) {
            Player* pl = GET_PLAYER(play);
            if (iv) {
                P().pin.pos = pl->actor.world.pos;
                P().pin.yaw = pl->actor.shape.rot.y;
                P().pin.on = 1;
            } else {
                P().pin.on = 0;
            }
        }
        Zelda3D_ReplReply(outPath, "linkpin=%d (pins player world pos+yaw each frame)", P().pin.on);
    } else if (strcmp(cmd, "linkcorr") == 0) {
        // #7 HAND-WEAVE the per-bone arm/upper-body retarget correction LIVE (linksrc n64).
        //   linkcorr                         -> show upper-body bones (b9..b20): mode + C euler (zyx deg)
        //   linkcorr set <bid> <mode> <cx> <cy> <cz> [<c2x> <c2y> <c2z>]
        //                                    -> set bone bid (mode 1=replace 2=C·R 3=R·C 4=C·R·C2 5=C·R·C⁻¹ conj)
        //   linkcorr reset                   -> restore the generated table
        //   linkcorr bake <path>             -> write the live table as a 25-row .inc for committing
        char sub[64] = "";
        P().retarget.ensure();
        int n = sscanf(line, "%*s %63s", sub);
        if (n == 1 && strcmp(sub, "reset") == 0) {
            memcpy(P().retarget.table, kLinkChildBoneCorr, sizeof(P().retarget.table));
            Zelda3D_ReplReply(outPath, "linkcorr reset to generated table");
        } else if (n == 1 && strcmp(sub, "set") == 0) {
            int bid = -1, mode = 0; float c[3] = { 0, 0, 0 }, c2[3] = { 0, 0, 0 };
            int got = sscanf(line, "%*s %*s %i %i %f %f %f %f %f %f", &bid, &mode, &c[0], &c[1], &c[2],
                             &c2[0], &c2[1], &c2[2]);
            if (got >= 5 && bid >= 0 && bid < 25) {
                P().retarget.table[bid].mode = (unsigned char)mode;
                Zelda3D_EulerToMat3(c[0], c[1], c[2], P().retarget.table[bid].C);
                Zelda3D_EulerToMat3(c2[0], c2[1], c2[2], P().retarget.table[bid].C2);
                Zelda3D_ReplReply(outPath, "linkcorr b%d limb=%d mode=%d C=(%.1f,%.1f,%.1f) C2=(%.1f,%.1f,%.1f)",
                                bid, P().retarget.table[bid].limb, mode, c[0], c[1], c[2], c2[0], c2[1], c2[2]);
            } else {
                Zelda3D_ReplReply(outPath, "usage: linkcorr set <bid> <mode> <cx> <cy> <cz> [c2x c2y c2z]");
            }
        } else if (n == 1 && strcmp(sub, "limb") == 0) {
            // #8 remap test: change which N64 jointTable limb drives an OoT3D bone, live (the
            // .inc's static limb field is otherwise the only authority). Lets us test the
            // structural-mismatch fix (OoT3D head b11 <- N64 head limb 10, chest b10 <- torso
            // limb 9) without a rebuild-per-guess. -1 = rest (no live limb).
            int bid = -1, limb = -2;
            if (sscanf(line, "%*s %*s %i %i", &bid, &limb) == 2 && bid >= 0 && bid < 25) {
                P().retarget.table[bid].limb = (signed char)limb;
                Zelda3D_ReplReply(outPath, "linkcorr b%d limb=%d mode=%d", bid, P().retarget.table[bid].limb,
                                P().retarget.table[bid].mode);
            } else {
                Zelda3D_ReplReply(outPath, "usage: linkcorr limb <bid> <n64limb|-1>");
            }
        } else if (n == 1 && strcmp(sub, "bake") == 0) {
            char path[1024] = "";
            if (sscanf(line, "%*s %*s %1023s", path) == 1) {
                FILE* f = fopen(path, "w");
                if (!f) { Zelda3D_ReplReply(outPath, "linkcorr bake: cannot open %s", path); }
                else {
                    fprintf(f, "// HAND-WOVEN by REPL `linkcorr` (#7 long-arm) — arms/upper body tuned by\n");
                    fprintf(f, "// hand vs the OoT3D CSAB ground truth; legs/neck on pure replace. NOT auto-fit.\n");
                    fprintf(f, "// Per OoT3D childlink_v2 bone: { n64Limb, mode, C[9], C2[9] }. mode 0=rest,\n");
                    fprintf(f, "// 1=replace, 2=left C·R, 3=right R·C, 4=two-sided C·R·C2.\n");
                    fprintf(f, "static const Zelda3dBoneCorr kLinkChildBoneCorr[25] = {\n");
                    for (int b = 0; b < 25; b++) {
                        const Zelda3dBoneCorr* bc = &P().retarget.table[b];
                        fprintf(f, "    { %3d, %d, {", bc->limb, bc->mode);
                        for (int k = 0; k < 9; k++) fprintf(f, "%s%.6ff", k ? "," : "", bc->C[k]);
                        fprintf(f, " }, {");
                        for (int k = 0; k < 9; k++) fprintf(f, "%s%.6ff", k ? "," : "", bc->C2[k]);
                        fprintf(f, " } }, // b%d\n", b);
                    }
                    fprintf(f, "};\n");
                    fclose(f);
                    Zelda3D_ReplReply(outPath, "linkcorr baked -> %s", path);
                }
            } else {
                Zelda3D_ReplReply(outPath, "usage: linkcorr bake <path>");
            }
        } else {
            // show: dump upper-body bones with their C (and C2) decomposed to zyx euler degrees.
            char buf[1024]; int off = 0;
            off += snprintf(buf + off, sizeof(buf) - off, "linkcorr (upper body):");
            for (int b = 9; b <= 20 && off < (int)sizeof(buf) - 80; b++) {
                float e[3], e2[3];
                Zelda3D_Mat3ToEuler(P().retarget.table[b].C, e);
                Zelda3D_Mat3ToEuler(P().retarget.table[b].C2, e2);
                off += snprintf(buf + off, sizeof(buf) - off, " b%d:l%d m%d C(%.0f,%.0f,%.0f)", b,
                                P().retarget.table[b].limb, P().retarget.table[b].mode, e[0], e[1], e[2]);
                if (P().retarget.table[b].mode == 4)
                    off += snprintf(buf + off, sizeof(buf) - off, "/C2(%.0f,%.0f,%.0f)", e2[0], e2[1], e2[2]);
            }
            Zelda3D_ReplReply(outPath, "%s", buf);
        }
    } else if (strcmp(cmd, "linkrot") == 0 && sscanf(line, "%*s %f %f %f", &f1, &f2, &f3) == 3) {
        gZelda3dLinkRotX = f1;
        gZelda3dLinkRotY = f2;
        gZelda3dLinkRotZ = f3;
        Zelda3D_ReplReply(outPath, "linkrot=(%.0f,%.0f,%.0f)", gZelda3dLinkRotX, gZelda3dLinkRotY, gZelda3dLinkRotZ);
    } else if (strcmp(cmd, "linkanim") == 0) {
        // `linkanim <csab-base>` pins that CSAB on Link (verify idle/walk/run without real input);
        // `linkanim off` / no-arg returns to live anim resolution.
        char name[64] = "";
        if (sscanf(line, "%*s %63s", name) == 1 && strcmp(name, "off") != 0) {
            strncpy(gZelda3dLinkForceCsab, name, sizeof(gZelda3dLinkForceCsab) - 1);
            gZelda3dLinkForceCsab[sizeof(gZelda3dLinkForceCsab) - 1] = '\0';
            Zelda3D_ReplReply(outPath, "linkanim='%s' (forced on Link; `linkanim off` to release)", gZelda3dLinkForceCsab);
        } else {
            gZelda3dLinkForceCsab[0] = '\0';
            Zelda3D_ReplReply(outPath, "linkanim OFF (live anim resolution restored)");
        }
    } else if (strcmp(cmd, "linktwo") == 0) {
        // `linktwo <lowerCsab> <upperCsab>` — force the #85 carry-WALK two-source per-limb blend with
        // explicit CSABs (lower drives legs, upper drives arms via kLinkUpperBodyMask), so the blend
        // can be skindumped/verified without a live grab. `linktwo off` releases.
        char lo[64] = "", up[64] = "";
        int got = sscanf(line, "%*s %63s %63s", lo, up);
        if (got >= 1 && strcmp(lo, "off") == 0) {
            gZelda3dLinkForceTwoLower[0] = '\0'; gZelda3dLinkForceTwoUpper[0] = '\0';
            Zelda3D_ReplReply(outPath, "linktwo OFF");
        } else if (got == 2) {
            strncpy(gZelda3dLinkForceTwoLower, lo, sizeof(gZelda3dLinkForceTwoLower) - 1);
            strncpy(gZelda3dLinkForceTwoUpper, up, sizeof(gZelda3dLinkForceTwoUpper) - 1);
            Zelda3D_ReplReply(outPath, "linktwo lower='%s' upper='%s' (forced two-source; `linktwo off`)",
                            gZelda3dLinkForceTwoLower, gZelda3dLinkForceTwoUpper);
        } else {
            Zelda3D_ReplReply(outPath, "usage: linktwo <lowerCsab> <upperCsab> | off");
        }
    } else if (strcmp(cmd, "climbgroundfix") == 0) {
        // #79 A/B toggle: freeze feet-grounding during climb poses (default ON). `climbgroundfix 0`
        // reverts to per-pose lowest-vertex grounding so the climb teleport reproduces for before/after.
        if (sscanf(line, "%*s %i", &iv) == 1) gZelda3dClimbGroundFix = (iv != 0);
        Zelda3D_ReplReply(outPath, "climbgroundfix=%d (freeze groundOff during climb poses)", gZelda3dClimbGroundFix);
    } else if (strcmp(cmd, "linkheldfix") == 0) {
        // #6 A/B toggle: attach the carried actor (held cucco) to 3DS Link's posed hands. Default on;
        // `linkheldfix 0` reverts to the engine's stale pickup-spot pos for before/after evidence.
        if (sscanf(line, "%*s %i", &iv) == 1) gZelda3dHeldAttach = (iv != 0);
        Zelda3D_ReplReply(outPath, "linkheldfix=%d (attach carried actor to posed hands)", gZelda3dHeldAttach);
    } else if (strcmp(cmd, "linkfocusfix") == 0) {
        // #16(b) A/B toggle: keep actor.focus.pos at the 3DS Link posed head so the first-person
        // (C-up) camera frames Link's eye. Default on; `linkfocusfix 0` reverts to the stale focus
        // (Player_DrawGameplay skipped) for before/after evidence.
        if (sscanf(line, "%*s %i", &iv) == 1) gZelda3dFocusFix = (iv != 0);
        Zelda3D_ReplReply(outPath, "linkfocusfix=%d (keep focus.pos at posed head)", gZelda3dFocusFix);
    } else if (strcmp(cmd, "linkjointdump") == 0) {
        // `linkjointdump <path> [nframes]` — capture the live player jointTable over nframes (default 60)
        // consecutive draws to a CSV, for the per-bone retarget-correction derivation. Pin Link to a
        // named anim first (`linkanim` only forces the 3DS CSAB display; the N64 jointTable still reflects
        // whatever named anim Link's SkelAnime is actually playing — keep him idle/standing for nml_wait).
        int n = 60;
        if (sscanf(line, "%*s %1023s %d", path, &n) >= 1) {
            if (P().jointDump.file != NULL) { fclose(P().jointDump.file); P().jointDump.file = NULL; }
            P().jointDump.file = fopen(path, "w");
            if (P().jointDump.file == NULL) {
                Zelda3D_ReplReply(outPath, "linkjointdump: cannot open %s", path);
            } else {
                if (n < 1) n = 1;
                fprintf(P().jointDump.file, "# player jointTable capture; bonemap value m -> limb (m+1)\n");
                fprintf(P().jointDump.file, "cap,curFrame,animLength,anim,limb,x,y,z\n");
                P().jointDump.remaining = n;
                P().jointDump.cap = 0;
                Zelda3D_ReplReply(outPath, "linkjointdump -> %s (%d frames)", path, n);
            }
        } else {
            Zelda3D_ReplReply(outPath, "usage: linkjointdump <path> [nframes]");
        }
    } else if (strcmp(cmd, "skindump") == 0) {
        // `skindump <path> [nframes]` — capture the ACTUAL resolved per-bone CSAB pose (skin matrices)
        // the renderer draws for Link, over nframes (default 60) consecutive DRAWS, tagged with the
        // resolved CSAB name + the REAL playhead frame (free-run accumulator). This is the Zelda3D side of
        // the #117 direct-vs-oracle per-frame diff: it proves whether the pose actually cycles (slide
        // bug) and exposes the geometry for the bone-rotation diff. Unlike linkjointdump (the N64 input
        // jointTable) this is the OUTPUT geometry after CSAB resolution.
        int n = 60;
        if (sscanf(line, "%*s %1023s %d", path, &n) >= 1) {
            if (P().modelId < 0) {
                Zelda3D_ReplReply(outPath, "skindump: Link model not drawn yet (run `link 1` and let it draw)");
            } else {
                if (n < 1) n = 1;
                Zelda3D_SkinDumpArm(P().modelId, path, n);
                Zelda3D_ReplReply(outPath, "skindump -> %s (%d draws, model %d)", path, n, P().modelId);
            }
        } else {
            Zelda3D_ReplReply(outPath, "usage: skindump <path> [nframes]");
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
                P().midmask.overrideSet = 0;
            } else if (strcmp(arg, "all") == 0) {
                P().midmask.overrideMask = ~0ull; P().midmask.overrideSet = 1;
            } else if (strcmp(arg, "only") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                P().midmask.overrideMask = (n >= 0 && n < 64) ? (1ull << n) : 0ull; P().midmask.overrideSet = 1;
            } else if (strcmp(arg, "add") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                if (n >= 0 && n < 64) P().midmask.overrideMask |= (1ull << n); P().midmask.overrideSet = 1;
            } else if (strcmp(arg, "del") == 0 && sscanf(line, "%*s %*s %d", &n) == 1) {
                if (n >= 0 && n < 64) P().midmask.overrideMask &= ~(1ull << n); P().midmask.overrideSet = 1;
            } else {
                P().midmask.overrideMask = strtoull(arg, NULL, 0); P().midmask.overrideSet = 1;
            }
        }
        Zelda3D_ReplReply(outPath, "linkmid override=%s mask=0x%llx",
                        P().midmask.overrideSet ? "ON" : "OFF(auto)", P().midmask.overrideMask);
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
        Zelda3D_ReplReply(outPath, "linkgear sword=%d shield=%d (equipped)", sw, sh);
    } else if (strcmp(cmd, "linksrc") == 0) {
        // `linksrc n64|3ds` — choose Link's animation source. n64 = retarget the live blended jointTable
        // (walk/run + everything), 3ds = the OoT3D rig's own named CSABs.
        char name[16] = "";
        if (sscanf(line, "%*s %15s", name) == 1) {
            gZelda3dLinkAnimSrc = (name[0] == '0' || name[0] == '3') ? 0 : 1;
        }
        Zelda3D_ReplReply(outPath, "linksrc=%s", gZelda3dLinkAnimSrc == 1 ? "n64 (live jointTable retarget)"
                                                                      : "3ds (own CSAB)");
    } else {
        return 0; // not a link command
    }
    return 1;
}

// --- PlayerBehavior class boilerplate + C-ABI shims --------------------------------------------
// Link's behavior is reached through the DEDICATED player hooks (z_player.c's Zelda3D_TryDrawPlayer and
// zelda3d.c's per-frame inject/pin/repl call sites), NOT the generic findActorBehavior registry — the
// player draw path is special. These shims keep the existing C entry points stable while routing them
// to the single PlayerBehavior instance, so the rest of the engine is unchanged.
Zelda3D::PlayerBehavior& Zelda3D::PlayerBehavior::instance() {
    static Zelda3D::PlayerBehavior sPlayer;
    return sPlayer;
}
s16 Zelda3D::PlayerBehavior::actorId() const {
    return ACTOR_PLAYER;
}
bool Zelda3D::PlayerBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    return Zelda3D_PlayerDrawImpl(play, actor) != 0;
}

extern "C" int Zelda3D_TryDrawPlayer(PlayState* play, Actor* actor) {
    return Zelda3D::PlayerBehavior::instance().tryDrawModel(play, actor) ? 1 : 0;
}
extern "C" float Zelda3D_LinkGroundDiag(PlayState* play, const char** outCsab) {
    return Zelda3D::PlayerBehavior::instance().groundDiag(play, outCsab);
}
extern "C" void Zelda3D_LinkApplyPin(PlayState* play, Actor* actor) {
    Zelda3D::PlayerBehavior::instance().applyPin(play, actor);
}
extern "C" void Zelda3D_LinkWalkInject(PlayState* play) {
    Zelda3D::PlayerBehavior::instance().walkInject(play);
}
extern "C" int Zelda3D_LinkRepl(PlayState* play, const char* cmd, const char* line, const char* outPath) {
    return Zelda3D::PlayerBehavior::instance().repl(play, cmd, line, outPath);
}
