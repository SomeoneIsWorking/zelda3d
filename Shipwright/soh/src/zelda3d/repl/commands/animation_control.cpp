#include "animation_control.h"

#include "../../anim/automatic_playback.h"
#include "../../anim/pose_inspection.h"
#include "../../anim/skeleton_draw_bridge.h"
#include "../../anim/zelda3d_anim_override.h"
#include "../../control/zelda3d_control_bridge.h"
#include "../../core/zelda3d_runtime.h"
#include "../../diagnostics/zelda3d_diagnostics.h"
#include "../../diagnostics/model_tuning.h"
#include "../../render/model_queries.h"
#include "../../render/en_sw_draw_transform.h"
#include "../zelda3d_repl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool Zelda3D_AnimationControlReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    const char* cmd = command;
    char arg[64];
    char path[1024];
    float f1, f2, f3;
    int iv, iv2;
    if (strcmp(cmd, "n64anim") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dN64Anim = (int)f1;
        Zelda3D_ReplReply(outPath, "n64anim=%d (1=N64 SkelAnime joints on OoT3D skeleton, 0=CSAB)", gZelda3dN64Anim);
    } else if (strcmp(cmd, "animlist") == 0) {
        // LIVE anim-compare: print the CSABs of the last replaced model so they can be `animforce`d.
        char buf[3072];
        buf[0] = '\0';
        if (gZelda3dLastAutoModel >= 0) {
            Zelda3D_AutoModelCsabList(gZelda3dLastAutoModel, buf, (int)sizeof(buf));
        }
        Zelda3D_ReplReply(outPath, "animlist model=%d: %s", gZelda3dLastAutoModel, buf[0] ? buf : "(none seen yet)");
    } else if (strcmp(cmd, "animforce") == 0) {
        // `animforce <csab-base>` pins that CSAB on EVERY replaced actor (eyeball it vs the N64 anim,
        // toggle `auto 0/1`); `animforce off` / no-arg returns to the auto resolver.
        char name[64] = "";
        if (sscanf(line, "%*s %63s", name) == 1 && strcmp(name, "off") != 0) {
            strncpy(gZelda3dForceCsab, name, sizeof(gZelda3dForceCsab) - 1);
            gZelda3dForceCsab[sizeof(gZelda3dForceCsab) - 1] = '\0';
            Zelda3D_ReplReply(outPath, "animforce='%s' (forced on all replaced actors; `animforce off` to release)",
                              gZelda3dForceCsab);
        } else {
            gZelda3dForceCsab[0] = '\0';
            Zelda3D_ReplReply(outPath, "animforce OFF (auto-resolve restored)");
        }
    } else if (strcmp(cmd, "swtilt") == 0 && sscanf(line, "%*s %i", &iv) == 1) {
        // #75 A/B: toggle the En_Sw wall/tree draw-tilt replication. `swtilt 0` reproduces the bug
        // (Gold Skulltula renders upright/splayed); default 1 leans it onto the surface.
        gZelda3dSwTilt = (iv != 0);
        Zelda3D_ReplReply(outPath, "swtilt=%d (replicate En_Sw wall/tree draw tilt)", gZelda3dSwTilt);
    } else if (strcmp(cmd, "rotx") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dRotX = f1;
        Zelda3D_ReplReply(outPath, "rot=(%.0f,%.0f,%.0f)", gZelda3dRotX, gZelda3dRotY, gZelda3dRotZ);
    } else if (strcmp(cmd, "roty") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dRotY = f1;
        Zelda3D_ReplReply(outPath, "rot=(%.0f,%.0f,%.0f)", gZelda3dRotX, gZelda3dRotY, gZelda3dRotZ);
    } else if (strcmp(cmd, "rotz") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dRotZ = f1;
        Zelda3D_ReplReply(outPath, "rot=(%.0f,%.0f,%.0f)", gZelda3dRotX, gZelda3dRotY, gZelda3dRotZ);
    } else if (strcmp(cmd, "morph") == 0) {
        // Keystone fix #2 (#8/#86) — anim-transition cross-fade in the CSAB auto/own-anim path.
        // `morph <0|1>` toggles it (A/B the transition pop vs the smooth blend); alone reports state.
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gZelda3dMorph = (iv != 0);
        }
        Zelda3D_ReplReply(outPath, "morph=%d", gZelda3dMorph);
    } else if (strcmp(cmd, "track") == 0) {
        // Keystone fix #1 (#93) — OoT3D head/torso tracking port (zelda3d_anim_override). `track <0|1>`
        // toggles it (A/B head-tracking vs straight-ahead); alone reports state.
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gZelda3dTrack = (iv != 0);
        }
        Zelda3D_ReplReply(outPath, "track=%d", gZelda3dTrack);
    } else if (strcmp(cmd, "facial") == 0) {
        // Keystone #3 — OoT3D eye/mouth material-anim port (zelda3d_anim_override). `facial <0|1>`
        // toggles the per-material frame swap (A/B blink/mouth vs frozen base); alone reports state.
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gZelda3dFacial = (iv != 0);
        }
        Zelda3D_ReplReply(outPath, "facial=%d", gZelda3dFacial);
    } else if (strcmp(cmd, "faceframe") == 0) {
        // Keystone #3 verification: force every facial actor's eye+mouth to a fixed frame index
        // (bypassing the live N64 index, which the headless throttle stalls). `faceframe -1` = live.
        int iv;
        if (sscanf(line, "%*s %d", &iv) == 1) {
            gZelda3dFaceForce = iv;
        }
        Zelda3D_ReplReply(outPath, "faceframe=%d", gZelda3dFaceForce);
    } else if (strcmp(cmd, "animrate") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dAnimRate = f1;
        Zelda3D_ReplReply(outPath, "animrate=%.3f frame=%.1f", gZelda3dAnimRate, gZelda3dAnimFrame);
    } else if (strcmp(cmd, "animframe") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dAnimFrame = f1;
        Zelda3D_ReplReply(outPath, "animframe=%.1f (rate=%.3f)", gZelda3dAnimFrame, gZelda3dAnimRate);
    } else if (strcmp(cmd, "animlive") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dAnimLive = (int)f1;
        Zelda3D_ReplReply(outPath, "animlive=%d (1=actor SkelAnime, 0=scrub animframe)", gZelda3dAnimLive);
    } else if (strcmp(cmd, "animdbg") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        gZelda3dAnimDebug = (int)f1;
        Zelda3D_ReplReply(outPath, "animdbg=%d", gZelda3dAnimDebug);
    } else if (strcmp(cmd, "boneinfo") == 0) {
        // `boneinfo <modelId> [animBase] [frame]` — dump the AUTO model's per-bone animated LOCAL
        // rotation (Csab::localTransforms) to stderr for a bone-for-bone diff vs the oracle's
        // `titleactors a`. With anim/frame omitted, uses the live-resolved clip+frame. Title Epona
        // is model 2010 (auto /actor/zelda_horse.zar). Output goes to the run log, not the REPL fifo.
        int mid = -1;
        char animBase[64] = { 0 };
        float bframe = -1.0f;
        int nread = sscanf(line, "%*s %d %63s %f", &mid, animBase, &bframe);
        if (nread >= 1) {
            Zelda3D_DumpAnimBonesLocal(mid, animBase[0] ? animBase : NULL, bframe);
            Zelda3D_ReplReply(outPath, "boneinfo model=%d anim=%s frame=%.3f -> stderr", mid,
                              animBase[0] ? animBase : "(live)", bframe);
        } else {
            Zelda3D_ReplReply(outPath, "usage: boneinfo <modelId> [animBase] [frame]");
        }
    } else {
        return false;
    }
    return true;
}
