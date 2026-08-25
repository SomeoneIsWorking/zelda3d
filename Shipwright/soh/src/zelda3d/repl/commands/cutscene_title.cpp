#include "cutscene_title.h"
#include "functions/camera_cutscene.h"
#include "functions/player.h"

#include "../../control/zelda3d_control_bridge.h"
#include "../../behaviors/title/title_activity.h"
#include "../../behaviors/title/title_camera.h"
#include "../../cutscene/zelda3d_cutscene.h"
#include "../../diagnostics/zelda3d_diagnostics.h"
#include "../repl_camera_state.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <string.h>

bool Zelda3D_CutsceneTitleReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    float f1;
    int iv;
    int iv2;
    if (strcmp(command, "skip") == 0 && sscanf(line, "%*s %f", &f1) == 1) {
        // #2 — toggle press-to-skip for onepoint cutscene cameras (Start/Space force-ends them).
        gZelda3dSkip = (f1 != 0.0f) ? 1 : 0;
        Zelda3D_ReplReply(outPath, "skip=%d", gZelda3dSkip);
    } else if (strcmp(command, "cscams") == 0) {
        // #2 verify — list the active subcameras (idx/csId/timer). A non-zero csId == an active
        // onepoint cutscene camera holding the view.
        char rep[512];
        int off = 0;
        off += snprintf(rep + off, sizeof(rep) - off, "active=%d", play->activeCamera);
        for (s32 i = SUBCAM_FIRST; i < NUM_CAMS; i++) {
            Camera* cam = play->cameraPtrs[i];
            if (cam != NULL) {
                off += snprintf(rep + off, sizeof(rep) - off, " | cam%d csId=%d timer=%d", i, cam->csId, cam->timer);
            }
        }
        Zelda3D_ReplReply(outPath, "%s", rep);
    } else if (strcmp(command, "csinfo") == 0) {
        // #15 diagnostic — which system is holding control away from the player? Dumps the
        // scripted-cutscene state (csCtx.state/frames + gSaveContext.cutsceneIndex), the Player
        // cutscene lock (InCsMode + csAction + the IN_CUTSCENE state flag), and the active
        // onepoint subcams. Use to identify a "scene-intro pan": if csCtx.state != 0 it's the
        // scripted Cutscene system; if only the Player lock / a subcam is set it's a different one.
        Player* p = GET_PLAYER(play);
        int subcams = 0;
        for (s32 i = SUBCAM_FIRST; i < NUM_CAMS; i++) {
            Camera* c = play->cameraPtrs[i];
            if (c != NULL && c->csId != 0) {
                subcams++;
            }
        }
        Zelda3D_ReplReply(outPath,
                          "csState=%d csFrames=%d csIndex=0x%x | inCsMode=%d csAction=%d inCsFlag=%d "
                          "stateFlags1=0x%x | activeCam=%d onepointSubcams=%d",
                          play->csCtx.state, play->csCtx.frames, gSaveContext.cutsceneIndex, Player_InCsMode(play),
                          p->csAction, (p->stateFlags1 & PLAYER_STATE1_IN_CUTSCENE) ? 1 : 0, p->stateFlags1,
                          play->activeCamera, subcams);
    } else if (strcmp(command, "skiptest") == 0 && sscanf(line, "%*s %i %i", &iv, &iv2) == 2) {
        // #2 verify — start a onepoint cutscene camera (csId=iv, timer=iv2 frames) anchored on
        // Link, to confirm press-to-skip ends it. Returns the created subcam index.
        Player* p = GET_PLAYER(play);
        s16 idx = OnePointCutscene_Init(play, (s16)iv, (s16)iv2, &p->actor, MAIN_CAM);
        Zelda3D_ReplReply(outPath, "skiptest csId=%d timer=%d -> subcam %d", iv, iv2, idx);
    } else if (strcmp(command, "titlecam") == 0) {
        // #92 toggle/inspect the title-screen camera override. `titlecam 0|1` sets it; `titlecam`
        // alone reports current state + the live view eye so you can verify framing.
        // Set `titlecam 0` then `cam x y z x y z` for A/B against OoT3D reference.
        if (sscanf(line, "%*s %i", &iv) == 1) {
            gZelda3dTitleCam = iv ? 1 : 0;
        }
        Zelda3D_ReplReply(outPath,
                          "titlecam=%d scene=%d csState=%d autoWarp=%d "
                          "view.eye=(%.0f,%.0f,%.0f) target.eye=(%.0f,%.0f,%.0f)",
                          gZelda3dTitleCam, play->sceneNum, play->csCtx.state, Zelda3D_AutoWarpEnabled(),
                          play->view.eye.x, play->view.eye.y, play->view.eye.z, kZelda3dTitleEye[0],
                          kZelda3dTitleEye[1], kZelda3dTitleEye[2]);
    } else if (strcmp(command, "titlecs") == 0) {
        // Read/pin the ported title-cs cursor (Zelda3D_TitleCsFrame — the clock the
        // TitlePresentation module and logo phase gating run off). `titlecs` alone reads;
        // `titlecs <n>` pins the cursor to n (it keeps advancing from there). NOTE: pinning
        // moves ALL cs-derived state (dayTime/lighting/logo phase) to that instant — fine for
        // phase-boundary verification, wrong for oracle A/B calibration (use tools/title_ab.py).
        if (sscanf(line, "%*s %i", &iv) == 1) {
            Zelda3D_TitleCsSetFrame(iv);
        }
        int fadeStart = -1, fadeEnd = -1;
        Zelda3D_TitleCsScreenFade(&fadeStart, &fadeEnd);
        Zelda3D_ReplReply(outPath, "titlecs frame=%d end=%d fadeIn=%d fadeOut=%d screenFade=[%d,%d) loop=%d",
                          Zelda3D_TitleCsFrame(), Zelda3D_TitleCsEndFrame(), Zelda3D_TitleCsMiscTriggerFrame(0x1e),
                          Zelda3D_TitleCsMiscTriggerFrame(0x1f), fadeStart, fadeEnd, Zelda3D_TitleCsLoopFrame());
    } else if (strcmp(command, "titlecue") == 0) {
        // Debug: dump the active rider cue's action/window at a given (or current) cs frame, so
        // agents can find real 0x41/0x26 rearing-cue bounds instead of guessing them (title rider
        // rearing-anim verify, 2026-07-15).
        int qFrame = Zelda3D_TitleCsFrame();
        sscanf(line, "%*s %i", &qFrame);
        int cueIndex = -1, startF = -1, endF = -1;
        float p0[3], p1[3];
        int16_t yaw = 0;
        uint16_t action = 0;
        int ok = Zelda3D_TitleCsRiderCue(qFrame, &cueIndex, p0, p1, &startF, &endF, &yaw, &action);
        if (ok) {
            Zelda3D_ReplReply(outPath, "titlecue frame=%d cueIndex=%d action=0x%x window=(%d,%d] yaw=%d", qFrame,
                              cueIndex, action, startF, endF, yaw);
        } else {
            Zelda3D_ReplReply(outPath, "titlecue frame=%d: no cue", qFrame);
        }
    } else {
        return false;
    }
    return true;
}
