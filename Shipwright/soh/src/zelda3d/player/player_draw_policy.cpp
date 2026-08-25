#include "player_draw_policy.h"

#include <cstdlib>
#include <cstring>

// --- OoT3D Link (player) replacement — proof-of-hook stage (see Zelda3D_TryDrawPlayer) ---
int gZelda3dLinkOn = 1; // OoT3D player body: ALWAYS ON. Dev A/B only via REPL `link`.
// World scale OoT3D-link-local -> world units (REPL `linkscale` can still override live). 0.01 is
// the 3DS player actor scale read LIVE from the oracle (player+0x54 vec3 = (0.01,0.01,0.01), child
// save, Kokiri 2026-07-23) — same as N64's Player actor scale. The old hand-tuned 0.011 compensated
// for the (now removed) min-vertex grounding sink; with the faithful age root-translation scale the
// authentic value renders at oracle size.
float gZelda3dLinkScale = 0.01f;
float gZelda3dLinkRotX = 0.0f; // rest->upright orientation correction (deg) (REPL `linkrot`)
float gZelda3dLinkRotY = 0.0f;
float gZelda3dLinkRotZ = 0.0f;
// REPL `linkframe <f>`: pin the forced clip's playhead (verification only; -1 = live phase-lock).
float gZelda3dLinkForceFrame = -1.0f;
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
int gZelda3dHeldAttach = 1; // #6 attach a carried actor (held cucco) to 3DS Link's posed hands
int gZelda3dFocusFix = 1;   // #16(b) keep actor.focus.pos at 3DS Link's posed head (first-person cam)
                            // (A/B toggle for before/after evidence; REPL `linkheldfix <0|1>`)
// #7: CSAB frames advanced per draw per unit of Link's ground speed, when speed-driving the
// locomotion cycle (run/walk) — Link's run/walk advances its pose through a player-internal
// accumulator, NOT skelAnime.curFrame (pinned at 0 the whole run), so curFrame can't phase-lock the
// CSAB (it froze at frame 0 -> the motionless slide). A footstep cadence is a function of movement
// speed, so we free-run the CSAB at speedXZ * this gain. Calibrated live vs N64 (REPL `linkloco`).
float gZelda3dLinkLocoGain = 0.30f;
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
// The DEFAULT IDLE is not a yawn. The decomp note that started this ("default idle table @0x53a5f8
// {0x50=yawn,...}") mis-read a raw table index: animId 0x50 is `nml_wait_free`, the neutral standing
// idle, and no clip is NAMED yawn/akubi/stretch. OoT3D's
// Player_GetIdleAnim table {0x50,0x58,0x58,0x119} = {nml_wait_free, nml_wait, nml_wait, ft_wait_long}
// is byte-identical to N64's D_80853914[PLAYER_ANIMGROUP_wait]. So the idle path here needs no change:
// we run the vendored N64 Player_ChooseNextIdleAnim and map its anim resource through kPlayerAnimMap,
// and OoT3D's inlined twin (004ba538) is faithful to that N64 code for every reachable plain idle —
// including the -6.0f LinkAnimation_Change morph, which N64 already does.
//
// CORRECTION (2026-07-23, #201d): the older wording here — "a scan of all 582 player anim names finds
// no yawn/akubi/stretch clip AT ALL" — was wrong and sent the next session hunting a nonexistent bug.
// The yawn/stretch fidget DOES exist: it is `wait_typeD_20f` / `waitF_typeD_20f`, N64
// sFidgetAnimations FIDGET_STRETCH_1..3 — Grezzo simply did not put "yawn" in the filename, and its
// yawning FACE lives in the sibling `wait_typeD_20f.faceb` (eye 7 = squeezed shut over frames 19..38,
// mouth 3 = wide open over 36..78; see zelda3d_link_face.cpp). It is unreachable in Kokiri Forest
// because the STRETCH fidget is only picked when `curRoom.behaviorType2 >= 4`, and a scan of all 724
// OoT3D room .zsi (cmd 0x08, cmd2 & 0xFF) gives Kokiri Forest 0 (= FIDGET_LOOK_AROUND). Only 45 rooms
// in the game (e.g. Market Entrance day = 4, Link's House = 5, Market Alley = 6) can produce it —
// which is also why an idle-fidget capture there only ever sees look-around / tunic / tap-feet.
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
// Apply the #117 walk/run SELECTION gate to a resolved player locomotion CSAB. The N64 anim is the
// single run cycle (-> nml_run_free at every ground speed); OoT3D plays nml_walk_free in the walk band.
// Shared by the draw path AND the `linkanimstate` reporter (so the parity sweep reads what is actually
// drawn, not the raw N64-anim mapping). Plain run only (damage_run/heavy_run/side_walk are own states).
extern "C" const char* Zelda3D_LinkWalkRunGate(const char* csab, float speedXZ) {
    if (csab != NULL && strcmp(csab, "nml_run_free") == 0 && speedXZ > 0.5f && speedXZ <= ZELDA3D_LINK_WALKRUN_SPEED) {
        return "nml_walk_free";
    }
    return csab;
}
