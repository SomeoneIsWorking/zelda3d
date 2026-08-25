// Zelda3D En_Niw procedural wing override. This owner captures the limb callback used by the N64
// cucco draw and applies the derived rotation to the OoT3D wing bones.
#include "cucco_wing_override.h"

#include "../../anim/skeleton_draw_bridge.h"
#include "../../anim/zelda3d_anim_override.h"
#include "../../render/model_queries.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
// them so the probe passes the right argument count.
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

// One row per En_Niw (N64 limb -> OoT3D bone) procedural-rotation correspondence.
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
    s8 n64Limb;
    s8 oot3dBone;
    s8 srcAxis[3];  // OoT3D bone axis [x,y,z] <- this N64 limb axis (0=x,1=y,2=z; -1 = none)
    f32 srcSign[3]; // multiplier applied to that N64 axis' binang delta
} CuccoWingMap;

static const char* kCuccoZar = "/actor/zelda_nw.zar";
static const CuccoWingMap kCuccoWingMap[] = {
    // cucco: N64 wing limbs 7 & 11 -> OoT3D WING bones 4 & 6. (Bones 3 & 5 are the FEET — low,
    // trans.y=-640, 38 verts; the wings are bones 4 & 6 — high on the sides, meanPos y~901, 65 verts.
    // #5: the flap was previously mis-mapped onto the feet, so the rendered wing never moved despite
    // a correct time-varying delta. Verified by `bonestats`/`bonerot` sweep — bone 4/6 fold the wing
    // fan, 3/5 only twitch a foot.) Axis permutation re-derived for the wing bones' local frame.
    { 7, 4, { 1, 0, 2 }, { -1.0f, 1.0f, 1.0f } },
    { 11, 6, { 1, 0, 2 }, { -1.0f, 1.0f, 1.0f } },
};

// Verification gate (env ZELDA3D_PROCOVERRIDE, default ON; REPL `wingflap <0|1>`): when 0 the
// procedural-override replay is skipped (the OoT3D actor plays only its CSAB) so the flap can be
// A/B'd in the same scene. gZelda3dWingForce >= 0 forces a fixed binang on the mapped Z axis (REPL
// `wingflap force <binang>`) to confirm the flap DIRECTION/amplitude visually.
int gZelda3dProcOverride = -1;
int gZelda3dWingForce = -1;
int gZelda3dForceCuccoAgitate = 0;                    // #5 diagnostic: hold cuccos in the agitated wing-spread pose
int gZelda3dCuccoState = -1;                          // #5 force func_80AB5BF8 arg (-1 = live AI); see zelda3d.h
int gZelda3dCuccoDbgPhase = -1;                       // #5 last cucco's flap phase (unk_29C)
short gZelda3dCuccoDbgWing[6] = { 0, 0, 0, 0, 0, 0 }; // #5 limb7 xyz, limb11 xyz applied this frame
int gZelda3dCuccoHeld = 0;                            // #5 force the held-by-Link carried state (func_80AB6BF8)

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

extern "C" void Zelda3D_CuccoAdvanceFrame(void) {
    gZelda3dFrameCtr++;
}

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
    if (zar == NULL || strcmp(zar, kCuccoZar) != 0) {
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
    double chickPhase = (double)gZelda3dFrameCtr * gZelda3dChickFreq;
    for (s32 i = 0; i < (s32)ARRAY_COUNT(kCuccoWingMap); i++) {
        const CuccoWingMap* row = &kCuccoWingMap[i];
        if (row->n64Limb < 0 || row->n64Limb >= limbCount) {
            if (sampleThisCall) {
                fprintf(stderr, "[WINGFLAP-SKIP] zar=%s n64limb=%d->bone=%d SKIPPED (limbCount=%d)\n", zar,
                        row->n64Limb, row->oot3dBone, limbCount);
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
        if (gZelda3dChickFlap) {
            // HAND-WOVEN flap: oscillate this wing bone on chickAxis, amplitude scaled by the N64
            // flap INTENSITY (so idle/agitated/still differ), around chickCenter. Ignores the
            // (ill-composed) replay output dx/dy/dz entirely. bone 6 mirrors bone 4 via Bone2Sign.
            s16 a0 = dd[0] < 0 ? (s16)-dd[0] : dd[0];
            s16 a1 = dd[1] < 0 ? (s16)-dd[1] : dd[1];
            s16 a2 = dd[2] < 0 ? (s16)-dd[2] : dd[2];
            s16 mag = a0;
            if (a1 > mag)
                mag = a1;
            if (a2 > mag)
                mag = a2;
            f32 instInten = (f32)mag / 25000.0f;
            if (instInten > 1.0f)
                instInten = 1.0f;
            // PEAK-HOLD the intensity: the N64 wing delta oscillates fast (8000<->25000), so using
            // it directly pulses the flap amplitude (every other beat goes weak -> looks sluggish).
            // Hold the peak and decay slowly so an agitated cucco flaps at full, steady amplitude
            // while a calming one fades out. Per oot3dBone so both wings track together.
            static f32 sHeld[8] = { 0 };
            int bi = row->oot3dBone & 7;
            if (instInten > sHeld[bi])
                sHeld[bi] = instInten;
            else
                sHeld[bi] *= 0.97f;
            f32 inten = sHeld[bi];
            f32 ang = ((f32)gZelda3dChickCenter + (f32)gZelda3dChickAmp * inten * sinf((f32)chickPhase)) * kBinangToRad;
            if (row->oot3dBone == 6)
                ang *= (f32)gZelda3dChickBone2Sign;
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
                                (f32)gZelda3dDbgBoneRot[1] * kBinangToRad, (f32)gZelda3dDbgBoneRot[2] * kBinangToRad);
    }
}
