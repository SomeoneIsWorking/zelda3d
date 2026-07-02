// Z3DRepl.c — see Z3DRepl.h. The minimal per-game REPL for native MM: PlayState-only queries over
// a private FIFO. Input is NOT handled here (that is the shared libultraship ScriptedInput seam);
// this exists purely for state that needs MM's decomp types. Modeled on OoT zelda3d's Zelda3D_ReplPoll
// FIFO transport (per-frame non-blocking read, reply to "<fifo>.out"), kept deliberately small.
#include "Z3DRepl.h"

#include "global.h" // gPlayState, GET_PLAYER, PlayState, Player, Actor, ACTORCAT_MAX
#include "2s2h/zelda3d/mm3d_model.h" // Zelda3D_SetObjectScale (per-object calibration)

// `cam` framing state (below). Non-static because Z3D_Repl_Tick needs to
// apply it every frame (post-update) to override the game camera drift.
extern int gZ3dRepl_CamActive;
extern float gZ3dRepl_CamYawDeg;
extern float gZ3dRepl_CamDist;
extern float gZ3dRepl_CamHeight;
void Z3DRepl_ApplyCam(PlayState* play);

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // mkfifo
#include <sys/types.h>
#include <unistd.h>

int gZ3dRepl_CamActive = 0;
float gZ3dRepl_CamYawDeg = 0.0f;
float gZ3dRepl_CamDist = 180.0f;
float gZ3dRepl_CamHeight = 60.0f;

// Snap the active MM camera to a side-profile of the focal actor (usually Link),
// at (yaw, dist, height) around them. Called every REPL tick when cam is active,
// so the game's per-frame camera update can't drift the framing back. This is
// intentionally a HARD snap — no interpolation — because pose validation wants
// deterministic framing, not natural camera motion.
void Z3DRepl_ApplyCam(PlayState* play) {
    if (!gZ3dRepl_CamActive || play == NULL) return;
    Camera* cam = GET_ACTIVE_CAM(play);
    if (cam == NULL) return;
    Vec3f focus;
    if (cam->focalActor != NULL) {
        focus = cam->focalActor->world.pos;
    } else {
        Player* p = GET_PLAYER(play);
        if (p == NULL) return;
        focus = p->actor.world.pos;
    }
    float yawRad = gZ3dRepl_CamYawDeg * (3.14159265358979f / 180.0f);
    Vec3f at;
    at.x = focus.x;
    at.y = focus.y + gZ3dRepl_CamHeight;
    at.z = focus.z;
    Vec3f eye;
    eye.x = at.x + sinf(yawRad) * gZ3dRepl_CamDist;
    eye.y = at.y;
    eye.z = at.z + cosf(yawRad) * gZ3dRepl_CamDist;
    cam->at = at;
    cam->eye = eye;
    cam->eyeNext = eye;
    cam->up.x = 0.0f;
    cam->up.y = 1.0f;
    cam->up.z = 0.0f;
}

static int sFd = -2; // -2 = not yet opened, -1 = disabled/failed
static char sOutPath[512];
static char sBuf[1024];
static int sBufLen = 0;

static void Z3D_Repl_Reply(const char* line);

// Line sink adapter for Zelda3D_ListModels — matches the (line, user) shape.
static void Z3D_Repl_ListLine(const char* line, void* user) {
    (void)user;
    Z3D_Repl_Reply(line);
}

static void Z3D_Repl_Reply(const char* line) {
    if (sOutPath[0] == '\0') {
        return;
    }
    FILE* f = fopen(sOutPath, "a");
    if (f != NULL) {
        fputs(line, f);
        fputc('\n', f);
        fclose(f);
    }
}

// yaw as the raw s16 angle (0..0xFFFF); callers can convert. Kept raw so it round-trips exactly.
static void Z3D_Repl_PosInfo(PlayState* play) {
    char out[256];
    if (play == NULL) {
        Z3D_Repl_Reply("posinfo scene=-1 (no PlayState)");
        return;
    }
    Player* player = GET_PLAYER(play);
    if (player == NULL) {
        snprintf(out, sizeof(out), "posinfo scene=%d room=%d (no player)", play->sceneId,
                 play->roomCtx.curRoom.num);
        Z3D_Repl_Reply(out);
        return;
    }
    Vec3f* p = &player->actor.world.pos;
    snprintf(out, sizeof(out), "posinfo scene=%d room=%d pos=(%.1f, %.1f, %.1f) yaw=%d", play->sceneId,
             play->roomCtx.curRoom.num, p->x, p->y, p->z, player->actor.world.rot.y);
    Z3D_Repl_Reply(out);
}

static void Z3D_Repl_Warp(PlayState* play, const char* arg) {
    if (play == NULL) {
        Z3D_Repl_Reply("warp err (no PlayState)");
        return;
    }
    s32 entrance = (s32)strtol(arg, NULL, 0);
    // Same live scene-transition path MM uses for a void-out (z_play.c func_80169EFC).
    play->nextEntrance = (u16)entrance;
    play->transitionTrigger = TRANS_TRIGGER_START;
    play->transitionType = TRANS_TYPE_FADE_BLACK;
    char out[128];
    snprintf(out, sizeof(out), "ok warp entrance=%d", entrance);
    Z3D_Repl_Reply(out);
}

static void Z3D_Repl_Actors(PlayState* play, const char* arg) {
    if (play == NULL) {
        Z3D_Repl_Reply("actors err (no PlayState)");
        return;
    }
    ActorContext* ac = &play->actorCtx;
    s32 want = (s32)strtol(arg, NULL, 10); // 0 = just category counts

    char out[512];
    s32 total = 0;
    s32 off = 0;
    off += snprintf(out + off, sizeof(out) - off, "actors counts:");
    for (s32 cat = 0; cat < ACTORCAT_MAX; cat++) {
        s32 len = ac->actorLists[cat].length;
        total += len;
        if (len > 0) {
            off += snprintf(out + off, sizeof(out) - off, " c%d=%d", cat, len);
        }
    }
    snprintf(out + off, sizeof(out) - off, " total=%d", total);
    Z3D_Repl_Reply(out);

    if (want <= 0) {
        return;
    }
    Player* player = GET_PLAYER(play);
    if (player == NULL) {
        return;
    }
    Vec3f lp = player->actor.world.pos;

    // Collect every non-player actor with its distance to Link, then partial-selection-sort the
    // nearest `want`. Headless scenes hold well under this cap; anything past it is dropped (and
    // reported), never silently truncated.
    enum { Z3D_ACTOR_CAP = 256 };
    Actor* actors[Z3D_ACTOR_CAP];
    f32 dists[Z3D_ACTOR_CAP];
    s32 count = 0;
    s32 dropped = 0;
    for (s32 cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* a = ac->actorLists[cat].first; a != NULL; a = a->next) {
            if (a == &player->actor) {
                continue;
            }
            if (count >= Z3D_ACTOR_CAP) {
                dropped++;
                continue;
            }
            f32 dx = a->world.pos.x - lp.x;
            f32 dy = a->world.pos.y - lp.y;
            f32 dz = a->world.pos.z - lp.z;
            actors[count] = a;
            dists[count] = sqrtf(dx * dx + dy * dy + dz * dz);
            count++;
        }
    }
    if (dropped > 0) {
        char note[96];
        snprintf(note, sizeof(note), "  (note: %d actors past the %d cap were not ranked)", dropped,
                 Z3D_ACTOR_CAP);
        Z3D_Repl_Reply(note);
    }

    s32 report = (want < count) ? want : count;
    for (s32 rank = 0; rank < report; rank++) {
        s32 min = rank;
        for (s32 i = rank + 1; i < count; i++) {
            if (dists[i] < dists[min]) {
                min = i;
            }
        }
        Actor* ta = actors[min];
        f32 td = dists[min];
        actors[min] = actors[rank];
        actors[rank] = ta;
        dists[min] = dists[rank];
        dists[rank] = td;

        s32 objId = (ta->objectSlot >= 0) ? play->objectCtx.slots[ta->objectSlot].id : -1;
        char line[192];
        snprintf(line, sizeof(line),
                 "  #%d id=0x%03X obj=0x%03X cat=%d params=0x%04X dist=%.1f pos=(%.1f, %.1f, %.1f)",
                 rank, ta->id, objId, ta->category, (u16)ta->params, td, ta->world.pos.x, ta->world.pos.y,
                 ta->world.pos.z);
        Z3D_Repl_Reply(line);
    }
}

static void Z3D_Repl_Exec(PlayState* play, char* line) {
    while (*line == ' ' || *line == '\t') {
        line++;
    }
    if (*line == '\0') {
        return;
    }
    if (strncmp(line, "posinfo", 7) == 0) {
        Z3D_Repl_PosInfo(play);
    } else if (strncmp(line, "warp", 4) == 0) {
        Z3D_Repl_Warp(play, line + 4);
    } else if (strncmp(line, "actors", 6) == 0) {
        Z3D_Repl_Actors(play, line + 6);
    } else if (strncmp(line, "mscale", 6) == 0) {
        // `mscale <objId> <scale>` — per-object world-scale calibration. objId may be
        // hex (0x1AD) or decimal. scale <= 0 clears the override back to the default.
        char* end = NULL;
        s32 objectId = (s32)strtol(line + 6, &end, 0);
        float s = (end != NULL) ? (float)strtod(end, NULL) : 0.0f;
        Zelda3D_SetObjectScale(objectId, s);
        char out[96];
        snprintf(out, sizeof(out), "mscale obj=0x%03X scale=%.4f", objectId, s);
        Z3D_Repl_Reply(out);
    } else if (strncmp(line, "mlist", 5) == 0) {
        Zelda3D_ListModels(Z3D_Repl_ListLine, NULL);
    } else if (strncmp(line, "tp", 2) == 0 && (line[2] == ' ' || line[2] == '\t')) {
        // `tp <x> <y> <z>` — teleport Link. Mirrors OoT's REPL tp; needed for
        // framing skinned actors during MM3D pose validation.
        Player* player = (play != NULL) ? GET_PLAYER(play) : NULL;
        float x, y, z;
        if (player != NULL && sscanf(line + 2, "%f %f %f", &x, &y, &z) == 3) {
            player->actor.world.pos.x = x;
            player->actor.world.pos.y = y;
            player->actor.world.pos.z = z;
            player->actor.prevPos = player->actor.world.pos;
            char out[128];
            snprintf(out, sizeof(out), "tp -> (%.0f,%.0f,%.0f)", x, y, z);
            Z3D_Repl_Reply(out);
        } else {
            Z3D_Repl_Reply("usage: tp <x> <y> <z>");
        }
    } else if (strncmp(line, "turn", 4) == 0) {
        // `turn <deg>` — snap Link's yaw. Pair with `tp` + `cam` to frame a rig.
        Player* player = (play != NULL) ? GET_PLAYER(play) : NULL;
        float deg;
        if (player != NULL && sscanf(line + 4, "%f", &deg) == 1) {
            s16 yaw = (s16)(deg * 182.0444f); // deg -> binang
            player->actor.shape.rot.y = yaw;
            player->actor.world.rot.y = yaw;
            char out[64];
            snprintf(out, sizeof(out), "turn -> %.0f deg (yaw=%d)", deg, yaw);
            Z3D_Repl_Reply(out);
        } else {
            Z3D_Repl_Reply("usage: turn <deg>");
        }
    } else if (strncmp(line, "roomwarp", 8) == 0) {
        // `roomwarp <n>` — force-load room n so its actors spawn, WITHOUT moving Link.
        // Pair with `tp` to reach an unloaded-room actor for A/B framing. Mirrors OoT
        // REPL roomwarp; uses MM's Room_RequestNewRoom.
        s32 n;
        if (play != NULL && sscanf(line + 8, "%d", &n) == 1) {
            s32 cnt = (s32)play->roomList.count;
            if (n >= 0 && n < cnt) {
                s32 r = Room_RequestNewRoom(play, &play->roomCtx, n);
                char out[128];
                snprintf(out, sizeof(out), "roomwarp %d -> req=%d (rooms=%d)", n, r, cnt);
                Z3D_Repl_Reply(out);
            } else {
                char out[64];
                snprintf(out, sizeof(out), "roomwarp: bad room %d (rooms=%d)", n, cnt);
                Z3D_Repl_Reply(out);
            }
        } else {
            Z3D_Repl_Reply("usage: roomwarp <n>");
        }
    } else if (strncmp(line, "cam", 3) == 0 && (line[3] == ' ' || line[3] == '\t' || line[3] == '\0')) {
        // `cam <yawDeg> [dist] [height]` — side-frame the active camera on Link (or on
        // the current focal actor). Persistent — reasserted every REPL tick via
        // Z3DRepl_ApplyCam so the game's cam-update doesn't override it. `cam off`
        // releases and lets the game camera drive again.
        char sub[16] = { 0 };
        if (sscanf(line + 3, "%15s", sub) == 1 &&
            (strcmp(sub, "off") == 0 || strcmp(sub, "release") == 0)) {
            gZ3dRepl_CamActive = 0;
            Z3D_Repl_Reply("cam released (game camera resumed)");
        } else {
            float yaw = 0.0f, dist = 180.0f, height = 60.0f;
            int n = sscanf(line + 3, "%f %f %f", &yaw, &dist, &height);
            if (n >= 1) {
                gZ3dRepl_CamYawDeg = yaw;
                if (n >= 2) gZ3dRepl_CamDist = dist;
                if (n >= 3) gZ3dRepl_CamHeight = height;
                gZ3dRepl_CamActive = 1;
                char out[128];
                snprintf(out, sizeof(out), "cam yaw=%.1f dist=%.1f h=%.1f (persistent)", gZ3dRepl_CamYawDeg,
                         gZ3dRepl_CamDist, gZ3dRepl_CamHeight);
                Z3D_Repl_Reply(out);
            } else {
                Z3D_Repl_Reply("usage: cam <yawDeg> [dist] [height] | cam off");
            }
        }
    } else if (strncmp(line, "ping", 4) == 0) {
        Z3D_Repl_Reply("pong");
    } else {
        Z3D_Repl_Reply("err unknown-command");
    }
}

void Z3D_Repl_Tick(void) {
    if (sFd == -2) {
        const char* p = getenv("ZELDA3D_MM_REPL");
        if (p == NULL || p[0] == '\0') {
            sFd = -1;
            return;
        }
        mkfifo(p, 0666); // ignore EEXIST
        sFd = open(p, O_RDWR | O_NONBLOCK); // O_RDWR keeps a writer so reads never EOF
        snprintf(sOutPath, sizeof(sOutPath), "%s.out", p);
        if (sFd >= 0) {
            FILE* f = fopen(sOutPath, "w");
            if (f != NULL) {
                fprintf(f, "Z3D MM REPL ready (fifo=%s)\n", p);
                fclose(f);
            }
        }
    }
    if (sFd < 0) {
        return;
    }

    for (;;) {
        if (sBufLen >= (int)sizeof(sBuf) - 1) {
            sBufLen = 0; // overflow guard
        }
        ssize_t n = read(sFd, sBuf + sBufLen, sizeof(sBuf) - 1 - sBufLen);
        if (n <= 0) {
            break;
        }
        sBufLen += (int)n;
    }
    sBuf[sBufLen] = '\0';
    char* start = sBuf;
    char* nl;
    while ((nl = strchr(start, '\n')) != NULL) {
        *nl = '\0';
        Z3D_Repl_Exec(gPlayState, start);
        start = nl + 1;
    }
    sBufLen = (int)strlen(start);
    memmove(sBuf, start, sBufLen + 1);

    // Persistent `cam` framing — snap the active camera to the requested side view
    // AFTER command processing so a same-tick `cam` change takes effect immediately.
    Z3DRepl_ApplyCam(gPlayState);
}
