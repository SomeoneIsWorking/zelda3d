// Z3DRepl.c — see Z3DRepl.h. The minimal per-game REPL for native MM: PlayState-only queries over
// a private FIFO. Input is NOT handled here (that is the shared libultraship ScriptedInput seam);
// this exists purely for state that needs MM's decomp types. Modeled on OoT soh3d's SoH3D_ReplPoll
// FIFO transport (per-frame non-blocking read, reply to "<fifo>.out"), kept deliberately small.
#include "Z3DRepl.h"

#include "global.h" // gPlayState, GET_PLAYER, PlayState, Player, Actor, ACTORCAT_MAX

#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h> // mkfifo
#include <sys/types.h>
#include <unistd.h>

static int sFd = -2; // -2 = not yet opened, -1 = disabled/failed
static char sOutPath[512];
static char sBuf[1024];
static int sBufLen = 0;

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

        char line[160];
        snprintf(line, sizeof(line), "  #%d id=0x%03X cat=%d params=0x%04X dist=%.1f pos=(%.1f, %.1f, %.1f)",
                 rank, ta->id, ta->category, (u16)ta->params, td, ta->world.pos.x, ta->world.pos.y,
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
}
