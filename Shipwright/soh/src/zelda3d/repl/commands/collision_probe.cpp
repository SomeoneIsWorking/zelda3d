#include "collision_probe.h"
#include "functions/collision.h"

#include "../../render/room_geometry_queries.h"
#include "../../scene/scene_draw.h"
#include "../zelda3d_repl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bool Zelda3D_CollisionProbeReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    float f1;
    float f2;
    if (strcmp(command, "floorat") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        // Authoritative N64-collision floor height at world (x,z): raycast straight down
        // through SoH's BgCheck from high above. This is exactly the surface Link stands
        // on, so it is the ground truth the OoT3D render mesh must be warped to match.
        Vec3f pos = { f1, 10000.0f, f2 };
        CollisionPoly* poly = NULL;
        f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
        if (poly != NULL) {
            Zelda3D_ReplReply(outPath, "floorat (%.0f,%.0f) y=%.2f ny=%.4f", f1, f2, y,
                              COLPOLY_GET_NORMAL(poly->normal.y));
        } else {
            Zelda3D_ReplReply(outPath, "floorat (%.0f,%.0f) NO FLOOR", f1, f2);
        }
    } else if (strcmp(command, "floorcol") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) >= 2) {
        // #25 climb-drop diagnostic: enumerate EVERY floor poly stacked in the column at world (x,z),
        // top to bottom (floorat only returns the topmost). The climb-out / ledge logic raycasts a
        // floor just behind the wall each frame (z_player.c:11397); a SPURIOUS OoT3D floor poly
        // partway up a climbable face makes yDistToLedge collapse → Link "reaches a ledge" and
        // detaches HALFWAY. Run at the back-of-wall XZ under `collision 1` (OoT3D) and `collision 0`
        // (N64) and diff: an extra mid-height floor in the OoT3D set is the dismount poly.
        // Optional 3rd arg = start Y (default 10000).
        f32 ystart = 10000.0f;
        sscanf(line, "%*s %*f %*f %f", &ystart);
        {
            int n = 0;
            f32 yc = ystart;
            while (n < 32 && yc > -3000.0f) {
                Vec3f pos = { f1, yc, f2 };
                CollisionPoly* poly = NULL;
                f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
                if (poly == NULL || y <= BGCHECK_Y_MIN) {
                    break;
                }
                Zelda3D_ReplReply(outPath, "floorcol[%d] (%.0f,%.0f) y=%.2f ny=%.4f type=%d", n, f1, f2, y,
                                  COLPOLY_GET_NORMAL(poly->normal.y), poly->type);
                yc = y - 1.0f; // step just below this floor to find the next one down
                n++;
            }
            if (n == 0) {
                Zelda3D_ReplReply(outPath, "floorcol (%.0f,%.0f) NO FLOOR", f1, f2);
            }
        }
    } else if (strcmp(command, "exitat") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        // Report the floor poly's SurfaceType gameplay data at (x,z): scene exit index, camera
        // index, and floor type. Verifies the OoT3D surfaceType list is wired (exits/cameras).
        Vec3f pos = { f1, 10000.0f, f2 };
        CollisionPoly* poly = NULL;
        f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
        if (poly != NULL) {
            u32 exitIdx = SurfaceType_GetSceneExitIndex(&play->colCtx, poly, BGCHECK_SCENE);
            u32 camIdx = SurfaceType_GetCamDataIndex(&play->colCtx, poly, BGCHECK_SCENE);
            Zelda3D_ReplReply(outPath, "exitat (%.0f,%.0f) y=%.1f type=%d exit=%d cam=%d", f1, f2, y, poly->type,
                              exitIdx, camIdx);
        } else {
            Zelda3D_ReplReply(outPath, "exitat (%.0f,%.0f) NO FLOOR", f1, f2);
        }
    } else if (strcmp(command, "exitgrid") == 0) {
        // Like floorgrid, but dumps the per-floor SurfaceType exit/cam/type at each XZ cell in one
        // FIFO round-trip (CSV: x,z,y,type,exit,cam; floorless cells get nan). Used to verify the
        // #13 per-poly N64 exit/cam re-sourcing matches N64 collision across a whole scene
        // (run under `collision 1` and `collision 0`, diff the two CSVs).
        float x0, z0, x1, z1, step;
        char gpath[1024];
        if (sscanf(line, "%*s %f %f %f %f %f %1023s", &x0, &z0, &x1, &z1, &step, gpath) == 6 && step > 0.0f) {
            FILE* gf = fopen(gpath, "w");
            if (gf == NULL) {
                Zelda3D_ReplReply(outPath, "exitgrid: cannot open %s", gpath);
            } else {
                int hits = 0;
                float x, z;
                fprintf(gf, "x,z,y,type,exit,cam\n");
                for (z = z0; z <= z1; z += step) {
                    for (x = x0; x <= x1; x += step) {
                        Vec3f pos = { x, 10000.0f, z };
                        CollisionPoly* poly = NULL;
                        f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
                        if (poly != NULL) {
                            u32 e = SurfaceType_GetSceneExitIndex(&play->colCtx, poly, BGCHECK_SCENE);
                            u32 c = SurfaceType_GetCamDataIndex(&play->colCtx, poly, BGCHECK_SCENE);
                            fprintf(gf, "%.1f,%.1f,%.2f,%d,%u,%u\n", x, z, y, poly->type, e, c);
                            hits++;
                        } else {
                            fprintf(gf, "%.1f,%.1f,nan,nan,nan,nan\n", x, z);
                        }
                    }
                }
                fclose(gf);
                Zelda3D_ReplReply(outPath, "exitgrid -> %s (%d floor hits)", gpath, hits);
            }
        } else {
            Zelda3D_ReplReply(outPath, "exitgrid needs: x0 z0 x1 z1 step path");
        }
    } else if (strcmp(command, "floorgrid") == 0) {
        // Batch raycast a regular XZ grid into a CSV (looped in C -> one FIFO round-trip,
        // not thousands). Used offline to build the dense N64 floor field for terrain warp.
        float x0, z0, x1, z1, step;
        char gpath[1024];
        if (sscanf(line, "%*s %f %f %f %f %f %1023s", &x0, &z0, &x1, &z1, &step, gpath) == 6 && step > 0.0f) {
            FILE* gf = fopen(gpath, "w");
            if (gf == NULL) {
                Zelda3D_ReplReply(outPath, "floorgrid: cannot open %s", gpath);
            } else {
                int hits = 0;
                float x, z;
                fprintf(gf, "x,z,y,ny\n");
                for (z = z0; z <= z1; z += step) {
                    for (x = x0; x <= x1; x += step) {
                        Vec3f pos = { x, 10000.0f, z };
                        CollisionPoly* poly = NULL;
                        f32 y = BgCheck_EntityRaycastFloor1(&play->colCtx, &poly, &pos);
                        if (poly != NULL) {
                            fprintf(gf, "%.1f,%.1f,%.2f,%.4f\n", x, z, y, COLPOLY_GET_NORMAL(poly->normal.y));
                            hits++;
                        } else {
                            fprintf(gf, "%.1f,%.1f,nan,nan\n", x, z);
                        }
                    }
                }
                fclose(gf);
                Zelda3D_ReplReply(outPath, "floorgrid -> %s (%d floor hits)", gpath, hits);
            }
        } else {
            Zelda3D_ReplReply(outPath, "floorgrid needs: x0 z0 x1 z1 step path");
        }
    } else if (strcmp(command, "wallscan") == 0) {
        // #14 climb drop-off probe: dump EVERY wall poly of the scene's STATIC collision (the
        // installed colHeader — OoT3D when `collision 1`, N64 when `collision 0`) to a CSV, with
        // its vertical extent and its wall-climb classification. A climbable surface is decided by
        // the SurfaceType "wall property" (data[0] bits 21..25 -> D_80119D90 -> flags): flag bit 0
        // = ledge-grab/vine, bit 3 (=8) = ladder climb-up. So to find why Link drops off a
        // climbable HALFWAY, run this under `collision 1` and `collision 0` and diff the climbable
        // walls (flags & 9): a shorter ymax / missing poly / lost flag in the OoT3D set is the bug.
        // CSV: idx,cx,cy,cz,nx,ny,nz,ymin,ymax,wallProp,flags,data0(hex),data1(hex)
        char gpath[1024];
        if (sscanf(line, "%*s %1023s", gpath) == 1) {
            CollisionHeader* ch = play->colCtx.colHeader;
            FILE* gf = fopen(gpath, "w");
            if (gf == NULL) {
                Zelda3D_ReplReply(outPath, "wallscan: cannot open %s", gpath);
            } else if (ch == NULL || ch->polyList == NULL || ch->vtxList == NULL || ch->surfaceTypeList == NULL) {
                fclose(gf);
                Zelda3D_ReplReply(outPath, "wallscan: no static colHeader");
            } else {
                int i, walls = 0, climb = 0;
                fprintf(gf, "idx,cx,cy,cz,nx,ny,nz,ymin,ymax,wallProp,flags,data0,data1\n");
                for (i = 0; i < ch->numPolygons; i++) {
                    CollisionPoly* p = &ch->polyList[i];
                    float ny = COLPOLY_GET_NORMAL(p->normal.y);
                    Vec3s *a, *b, *c;
                    s16 ymin, ymax;
                    s32 flags;
                    u32 wallProp;
                    if (ny > 0.5f || ny < -0.5f) {
                        continue; // floors/ceilings out; keep wall-ish polys
                    }
                    a = &ch->vtxList[p->flags_vIA & 0x1FFF];
                    b = &ch->vtxList[p->flags_vIB & 0x1FFF];
                    c = &ch->vtxList[p->vIC & 0x1FFF];
                    ymin = a->y;
                    if (b->y < ymin)
                        ymin = b->y;
                    if (c->y < ymin)
                        ymin = c->y;
                    ymax = a->y;
                    if (b->y > ymax)
                        ymax = b->y;
                    if (c->y > ymax)
                        ymax = c->y;
                    wallProp = func_80041D94(&play->colCtx, p, BGCHECK_SCENE);
                    flags = func_80041DB8(&play->colCtx, p, BGCHECK_SCENE);
                    fprintf(gf, "%d,%.1f,%.1f,%.1f,%.4f,%.4f,%.4f,%d,%d,%u,%d,0x%08x,0x%08x\n", i,
                            (a->x + b->x + c->x) / 3.0f, (a->y + b->y + c->y) / 3.0f, (a->z + b->z + c->z) / 3.0f,
                            COLPOLY_GET_NORMAL(p->normal.x), ny, COLPOLY_GET_NORMAL(p->normal.z), ymin, ymax, wallProp,
                            flags, ch->surfaceTypeList[p->type].data[0], ch->surfaceTypeList[p->type].data[1]);
                    walls++;
                    if (flags & 9)
                        climb++;
                }
                fclose(gf);
                Zelda3D_ReplReply(outPath, "wallscan -> %s (%d wall polys, %d climbable)", gpath, walls, climb);
            }
        } else {
            Zelda3D_ReplReply(outPath, "wallscan needs: path");
        }
    } else if (strcmp(command, "meshfloor") == 0 && sscanf(line, "%*s %f %f", &f1, &f2) == 2) {
        // Height of the OoT3D render mesh's floor at (x,z) for the room Link is in. After
        // the terrain warp this should match `floorat` (N64) on walkable ground.
        const char* sn = Zelda3D_SceneName(play);
        int mid = (sn != NULL) ? Zelda3D_RoomModelId(sn, play->roomCtx.curRoom.num) : -1;
        float my;
        if (mid >= 0 && Zelda3D_RoomMeshFloorAt(mid, f1, f2, &my)) {
            Zelda3D_ReplReply(outPath, "meshfloor (%.0f,%.0f) y=%.2f (room model %d)", f1, f2, my, mid);
        } else {
            Zelda3D_ReplReply(outPath, "meshfloor (%.0f,%.0f) no hit (model %d)", f1, f2, mid);
        }
    } else {
        return false;
    }
    return true;
}
