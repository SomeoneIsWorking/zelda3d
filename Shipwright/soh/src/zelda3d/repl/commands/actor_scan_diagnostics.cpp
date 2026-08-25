#include "actor_scan_diagnostics.h"

#include "../../diagnostics/zelda3d_diagnostics.h"
#include "../../render/actor_motion_observation.h"
#include "../../render/model_queries.h"
#include "../../render/terrain_alignment_render.h"
#include "../zelda3d_repl.h"
#include <fast/zelda3d_instrumentation.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

bool Zelda3D_ActorScanDiagnosticsReplCommand(PlayState* play, const char* command, const char* line,
                                             const char* outPath) {
    if (strcmp(command, "floaters") == 0) {
        // Find mid-air / half-buried actors (the per-actor-Y bug family, e.g. an NPC walking
        // above a roof or a boulder sunk underground). For every live actor, raycast the N64
        // floor at its XZ and report those whose world.pos.y sits more than <thr> (default 100)
        // ABOVE that floor — i.e. visibly off the ground. dy>0 = airborne/floating; sorted-ish
        // by category. Tooling-first: replaces blind scene-wandering to locate the offender.
        // dy   = world.pos.y - N64 floor (actor's ACTUAL position off the ground)
        // rofs = Zelda3D_ActorRenderYOffset (the lift we ADD to the render onto the OoT3D mesh);
        //        a large +rofs draws the actor in mid-air (e.g. RoomOoT3DFloorAt picking a roof),
        //        a large -rofs buries it. Either |signal| > thr is flagged.
        float thr = 100.0f;
        (void)sscanf(line, "%*s %f", &thr);
        Player* pl = GET_PLAYER(play);
        s32 cat, n = 0;
        sWarpPlay = play; // Zelda3D_N64FloorCb needs the PlayState/colCtx
        Zelda3D_ReplReply(outPath, "floaters thr=%.0f (dy=Y-above-floor, rofs=render lift):", thr);
        for (cat = 0; cat < ACTORCAT_MAX; cat++) {
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL && n < 60; a = a->next) {
                float floor, dy, rofs, dx, dz, dist;
                if (a->id == ACTOR_PLAYER)
                    continue;
                rofs = Zelda3D_ActorRenderYOffset(play, a);
                sWarpPlay = play; // ActorRenderYOffset reset it; restore for our raycast
                floor = Zelda3D_N64FloorCb(a->world.pos.x, a->world.pos.z);
                dy = (floor <= -31000.0f) ? 0.0f : a->world.pos.y - floor;
                if (dy <= thr && fabsf(rofs) <= thr)
                    continue;
                dx = a->world.pos.x - pl->actor.world.pos.x;
                dz = a->world.pos.z - pl->actor.world.pos.z;
                dist = sqrtf(dx * dx + dz * dz);
                Zelda3D_ReplReply(
                    outPath,
                    "  id=0x%-4X p=0x%04X cat=%d pos=(%.0f,%.0f,%.0f) floor=%.0f dy=%.0f rofs=%.0f dist=%.0f drawn=%d",
                    a->id, (u16)a->params, cat, a->world.pos.x, a->world.pos.y, a->world.pos.z, floor, dy, rofs, dist,
                    a->isDrawn);
                n++;
            }
        }
        Zelda3D_ReplReply(outPath, "floaters: %d flagged (dy or rofs >%.0f)", n, thr);
    } else if (strcmp(command, "bscan") == 0) {
        // BEHAVIORAL anomaly scan (whole-game sweep primitive): dump every live actor's pos +
        // speedXZ + velocity.y, and FLAG the signatures of the known actor-bug families so an
        // automated sweep can surface them without per-actor inspection:
        //   ORIGIN  pos within 1u of (0,0,0) and not the player -> collider/transform stuck at origin
        //           (the #107 stalchild root: collision sphere pinned at origin -> phantom collision).
        //   NAN     pos/vel is NaN -> blown-up transform.
        //   ZIP     speedXZ > <thr> (default 60) -> flung/zipping (the #107 visible symptom).
        //   FALL    velocity.y < -50 sustained -> falling through the world.
        // Prints one line per FLAGGED actor + a summary count; `bscan all` lists every actor.
        float thr = 60.0f;
        char sub[16] = "";
        int listAll = (sscanf(line, "%*s %15s", sub) == 1 && strcmp(sub, "all") == 0);
        if (!listAll)
            (void)sscanf(line, "%*s %f", &thr);
        Player* pl = GET_PLAYER(play);
        s32 cat, n = 0, nflag = 0;
        Zelda3D_ReplReply(outPath, "bscan thr=%.0f (ORIGIN/NAN/ZIP/FALL):", thr);
        for (cat = 0; cat < ACTORCAT_MAX; cat++) {
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL && n < 120; a = a->next, n++) {
                float x = a->world.pos.x, y = a->world.pos.y, z = a->world.pos.z;
                int isPlayer = (cat == ACTORCAT_PLAYER);
                int nan = (x != x) || (y != y) || (z != z) || (a->speedXZ != a->speedXZ);
                int origin = !isPlayer && (x > -1.0f && x < 1.0f) && (z > -1.0f && z < 1.0f) && (y > -1.0f && y < 1.0f);
                int zip = (a->speedXZ > thr) || (a->speedXZ < -thr);
                int fall = (a->velocity.y < -50.0f);
                const char* flag = nan ? "NAN" : origin ? "ORIGIN" : zip ? "ZIP" : fall ? "FALL" : NULL;
                if (flag != NULL)
                    nflag++;
                if (flag != NULL || listAll)
                    Zelda3D_ReplReply(outPath,
                                      "  %sid=0x%-4X cat=%d p=0x%04X pos=(%.0f,%.0f,%.0f) "
                                      "speedXZ=%.1f vy=%.1f",
                                      flag ? flag : "  ", a->id, cat, (u16)a->params, x, y, z, a->speedXZ,
                                      a->velocity.y);
            }
        }
        (void)pl;
        Zelda3D_ReplReply(outPath, "bscan: %d actors, %d flagged", n, nflag);
    } else if (strcmp(command, "geomscan") == 0) {
        // GEOMETRY-VALUE sweep: read every Zelda3D model draw's WORLD-space AABB straight out of the
        // renderer (zelda3d_vk capture) — NOT pixels — and flag MISRENDERED objects by VALUE: a world
        // extent far larger than any real OoT3D model (default > 1500u) or NaN = a mis-scaled/blown-up
        // draw (e.g. a push block rendering as a giant dark blob). This is what a parity sweep needs to
        // catch render glitches automatically. `geomscan all` lists every draw; `geomscan <thr>` sets
        // the extent threshold. Maps each draw's modelId -> its OoT3D ZAR so the offender is named.
        std::vector<int> ids(2048);
        std::vector<float> mins(2048 * 3);
        std::vector<float> maxs(2048 * 3);
        float thr = 1500.0f;
        char sub[16] = "";
        int listAll = (sscanf(line, "%*s %15s", sub) == 1 && strcmp(sub, "all") == 0);
        if (!listAll) {
            (void)sscanf(line, "%*s %f", &thr);
        }
        int gn = Zelda3D_GeomScanDump(ids.data(), mins.data(), maxs.data(), 2048);
        int gflag = 0;
        Zelda3D_ReplReply(outPath, "geomscan thr=%.0f (%d Zelda3D draws this frame):", thr, gn);
        for (int i = 0; i < gn; i++) {
            float ex = maxs[i * 3 + 0] - mins[i * 3 + 0];
            float ey = maxs[i * 3 + 1] - mins[i * 3 + 1];
            float ez = maxs[i * 3 + 2] - mins[i * 3 + 2];
            float mx = ex > ey ? (ex > ez ? ex : ez) : (ey > ez ? ey : ez);
            int isnan = (mx != mx);
            int huge = (mx > thr);
            const char* zar = Zelda3D_AutoModelZar(ids[i]);
            if (isnan || huge) {
                gflag++;
            }
            if (isnan || huge || listAll) {
                Zelda3D_ReplReply(outPath, "  %smodel=%d ext=(%.0f,%.0f,%.0f) maxext=%.0f wmin=(%.0f,%.0f,%.0f) %s",
                                  isnan  ? "NAN "
                                  : huge ? "HUGE "
                                         : "  ",
                                  ids[i], ex, ey, ez, mx, mins[i * 3 + 0], mins[i * 3 + 1], mins[i * 3 + 2],
                                  zar ? zar : "?");
            }
        }
        Zelda3D_ReplReply(outPath, "geomscan: %d draws, %d flagged (huge/nan)", gn, gflag);
    } else if (strcmp(command, "asample") == 0) {
        // BEHAVIORAL motion-parity sampler: `asample <n> [path]` streams the selected actor's
        // pos/rot/vel for the next n game frames to a CSV (default scratch/motion/zelda3d.csv), then
        // closes. Pair with the oracle side (tools/oracle_motion_sample.py) + tools/motion_parity.py.
        // Do NOT afreeze the actor if you want to observe its real motion.
        int n = 0;
        char path[256] = "scratch/motion/zelda3d.csv";
        int got = sscanf(line, "%*s %d %255s", &n, path);
        if (gZelda3dSelActor == NULL) {
            Zelda3D_ReplReply(outPath, "asample: no selection (asel first)");
        } else if (got < 1 || n <= 0) {
            Zelda3D_ReplReply(outPath, "asample needs <n> [path] (n frames to log)");
        } else {
            if (sZelda3dMotionFile != NULL) {
                fclose(sZelda3dMotionFile);
                sZelda3dMotionFile = NULL;
            }
            sZelda3dMotionFile = fopen(path, "w");
            if (sZelda3dMotionFile == NULL) {
                Zelda3D_ReplReply(outPath, "asample: cannot open '%s' (mkdir scratch/motion?)", path);
            } else {
                fprintf(sZelda3dMotionFile, "frame,gframe,id,posx,posy,posz,rotx,roty,rotz,velx,vely,velz,speedXZ\n");
                fflush(sZelda3dMotionFile);
                sZelda3dMotionActor = gZelda3dSelActor;
                sZelda3dMotionRemaining = n;
                sZelda3dMotionFrame = 0;
                Zelda3D_ReplReply(outPath, "asample: logging id=0x%X for %d frames -> %s", gZelda3dSelActor->id, n,
                                  path);
            }
        }
    } else {
        return false;
    }
    return true;
}
