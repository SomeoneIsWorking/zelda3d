#include "actor_diagnostics.h"
#include "functions/math.h"

#include "../../behaviors/actor_behavior_bridge.h"
#include "../../diagnostics/zelda3d_diagnostics.h"
#include "../../render/model_queries.h"
#include "../../render/replacement_calibration.h"
#include "../../render/replacement_catalog.h"
#include "../../tables/zelda3d_object_zars.inc"
#include "../zelda3d_repl.h"
#include "overlays/actors/ovl_En_Door/z_en_door.h"
#include "overlays/actors/ovl_En_Ex_Ruppy/z_en_ex_ruppy.h"
#include "overlays/actors/ovl_En_Horse/z_en_horse.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

bool Zelda3D_ActorDiagnosticsReplCommand(PlayState* play, const char* command, const char* line, const char* outPath) {
    if (strcmp(command, "actors") == 0) {
        // List actors (id + object id + world pos + distance from Link), so an NPC can be
        // located and framed (cam/tp) without hunting. Default: NPC category only; "actors all"
        // lists every category. Used to drive character-replacement verification.
        Player* p = GET_PLAYER(play);
        int wantAll = (strstr(line, "all") != NULL);
        s32 cat, shown = 0;
        for (cat = 0; cat < ACTORCAT_MAX; cat++) {
            if (!wantAll && cat != ACTORCAT_NPC && cat != ACTORCAT_ENEMY && cat != ACTORCAT_BOSS) {
                continue;
            }
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL && shown < 40; a = a->next) {
                float dx = a->world.pos.x - p->actor.world.pos.x;
                float dz = a->world.pos.z - p->actor.world.pos.z;
                int objId = -1;
                if (a->objBankIndex >= 0 && a->objBankIndex < play->objectCtx.num) {
                    objId = play->objectCtx.status[a->objBankIndex].id;
                }
                Zelda3D_ReplReply(outPath, "actor id=0x%x cat=%d obj=0x%x pos=(%.0f,%.0f,%.0f) dist=%.0f", a->id, cat,
                                  objId, a->world.pos.x, a->world.pos.y, a->world.pos.z, sqrtf(dx * dx + dz * dz));
                shown++;
            }
        }
        if (!shown) {
            Zelda3D_ReplReply(outPath, "actors: none in the requested categories");
        }
    } else if (strcmp(command, "actorscan") == 0) {
        int actorId = 0;
        if (sscanf(line, "%*s %i", &actorId) != 1) {
            return false;
        }
        // List world positions of every live actor with id `iv` (decimal or 0xHEX), plus
        // distance from Link — for framing multi-instance actors (e.g. En_Hata flags, id
        // 0x26) to verify per-item pose. Tooling-first: replaces blind scene-wandering.
        Player* pl = GET_PLAYER(play);
        s32 cat, n = 0;
        Zelda3D_ReplReply(outPath, "actorscan id=0x%X:", actorId);
        for (cat = 0; cat < ACTORCAT_MAX; cat++) {
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL; a = a->next) {
                if (a->id == actorId) {
                    float dx = a->world.pos.x - pl->actor.world.pos.x;
                    float dy = a->world.pos.y - pl->actor.world.pos.y;
                    float dz = a->world.pos.z - pl->actor.world.pos.z;
                    Zelda3D_ReplReply(outPath, "  [%d] pos=(%.0f,%.0f,%.0f) dist=%.0f cat=%d drawn=%d", n,
                                      a->world.pos.x, a->world.pos.y, a->world.pos.z,
                                      sqrtf(dx * dx + dy * dy + dz * dz), cat, a->isDrawn);
                    n++;
                }
            }
        }
        Zelda3D_ReplReply(outPath, "actorscan: %d found", n);
    } else if (strcmp(command, "actorsnear") == 0) {
        // Coverage AUDIT: list every live actor within <radius> (default 700) of Link with its
        // OoT3D-replacement status, so "what still renders as N64" is visible at a glance. Per
        // actor: id, category, distance, and coverage = TABLE (hand sModelTable entry) / AUTO:<zar>
        // (object has an OoT3D /actor model; (skin) = skinned, only drawn with ZELDA3D_N64ANIM) /
        // --N64-- (no object->ZAR mapping -> always N64). Tooling-first for the 100%-3DS pass.
        float radius = 700.0f;
        (void)sscanf(line, "%*s %f", &radius);
        Player* pl = GET_PLAYER(play);
        s32 cat, n = 0, nN64 = 0;
        Zelda3D_ReplReply(outPath, "actorsnear r=%.0f:", radius);
        for (cat = 0; cat < ACTORCAT_MAX; cat++) {
            Actor* a = play->actorCtx.actorLists[cat].head;
            for (; a != NULL && n < 60; a = a->next) {
                float dx = a->world.pos.x - pl->actor.world.pos.x;
                float dy = a->world.pos.y - pl->actor.world.pos.y;
                float dz = a->world.pos.z - pl->actor.world.pos.z;
                float d = sqrtf(dx * dx + dy * dy + dz * dz);
                if (d > radius)
                    continue;
                const char* cov = "--N64--";
                char buf[96];
                int inTable = 0;
                for (s32 ti = 0; ti < Zelda3D_ExplicitReplacementCount(); ti++) {
                    const Zelda3D_ModelEntry* entry = Zelda3D_ExplicitReplacementAt(ti);
                    if (entry != NULL && entry->actorId == a->id) {
                        inTable = 1;
                        break;
                    }
                }
                if (a->id == ACTOR_OBJ_HANA) {
                    int v = a->params & 3;
                    cov = (v == 2) ? "HANA-bush(3DS)" : (v == 0) ? "HANA-flower(3DS)" : "HANA-debris(3DS)";
                } else if (a->id == ACTOR_EN_ISHI) {
                    cov = "ISHI-rock(3DS)";
                } else if (a->id == ACTOR_EN_KUSA && (a->params & 3) == 0) {
                    cov = "KUSA-field-grass(3DS)";
                } else if (inTable) {
                    cov = "TABLE";
                } else {
                    int objId = Zelda3D_ActorObjectId(play, a);
                    const char* zar =
                        (objId >= 0 && objId < (int)ARRAY_COUNT(kZelda3dObjectZars)) ? kZelda3dObjectZars[objId] : NULL;
                    if (zar != NULL) {
                        int skin = Zelda3D_AutoModelSkinned(Zelda3D_AutoModelId(zar));
                        snprintf(buf, sizeof(buf), "AUTO:%s%s", zar, skin ? " (skin)" : "");
                        cov = buf;
                    } else if (Zelda3D_ActorHasBehaviorModule(a->id)) {
                        // No object->ZAR mapping, but a behaviors/actor/<x>.cpp module REPLACES the
                        // model (draws a distinct OoT3D CMB, suppressing the N64 draw) — e.g. En_Door,
                        // En_Fish. NOT an N64 gap; the legacy table/auto path just doesn't see it.
                        cov = "MODULE(3DS)";
                    } else {
                        nN64++;
                    }
                }
                Zelda3D_ReplReply(outPath, "  id=0x%-4X p=0x%04X cat=%d d=%4.0f %s", a->id, (u16)a->params, cat, d,
                                  cov);
                n++;
            }
        }
        Zelda3D_ReplReply(outPath, "actorsnear: %d listed, %d with no object->ZAR (always N64)", n, nN64);
    } else if (strcmp(command, "ainfo") == 0) {
        // GENERIC: dump the selected actor's live state.
        if (gZelda3dSelActor == NULL) {
            Zelda3D_ReplReply(outPath, "ainfo: no selection (asel first)");
        } else {
            Actor* a = gZelda3dSelActor;
            Zelda3D_ReplReply(outPath,
                              "ainfo id=0x%X params=%d pos=(%.0f,%.0f,%.0f) rot=(%d,%d,%d) "
                              "vel=(%.1f,%.1f,%.1f) speedXZ=%.1f disp=(%.1f,%.1f,%.1f) "
                              "bgFlags=0x%X floorY=%.0f freeze=%d",
                              a->id, a->params, a->world.pos.x, a->world.pos.y, a->world.pos.z, a->world.rot.x,
                              a->world.rot.y, a->world.rot.z, a->velocity.x, a->velocity.y, a->velocity.z, a->speedXZ,
                              a->colChkInfo.displacement.x, a->colChkInfo.displacement.y, a->colChkInfo.displacement.z,
                              a->bgCheckFlags, a->floorHeight, gZelda3dActorFreeze);
            // Colored-rupee debug aid: surface En_Ex_Ruppy's live colorIdx so the OoT3D
            // mesh-select port (behaviors/actor/ruppy.cpp: mesh_id == colorIdx) can be verified
            // against the on-screen color. Read through the C struct, not a raw offset.
            if (a->id == ACTOR_EN_EX_RUPPY) {
                EnExRuppy* r = (EnExRuppy*)a;
                Zelda3D_ReplReply(outPath, "ainfo ruppy colorIdx=%d type=%d invisible=%d scale=%.3f", r->colorIdx,
                                  r->type, r->invisible, a->scale.x);
            }
            if (a->id == ACTOR_EN_DOOR) {
                // #115 door-swing trace: read the live swing state through the EnDoor C struct (never
                // a raw offset — 64-bit build). N64 EnDoor_OverrideLimbDraw swings panel limb 4 by
                // rot->z += world.rot.y (steps 0 -> -0x1800 on open) on TOP of the open SkelAnime
                // (gDoorOpeningLeft/Right). jointTable holds the per-limb animated rotations.
                EnDoor* d = (EnDoor*)a;
                Zelda3D_ReplReply(outPath,
                                  "ainfo door worldRotY=%d shapeRotY=%d animStyle=%d opening=%d "
                                  "animFrame=%.1f playSpeed=%.2f dList=%d",
                                  d->actor.world.rot.y, d->actor.shape.rot.y, d->animStyle, d->playerIsOpening,
                                  d->skelAnime.curFrame, d->skelAnime.playSpeed, d->dListIndex);
                Zelda3D_ReplReply(outPath, "ainfo door joint[0..4].z = %d %d %d %d %d  joint[4]=(%d,%d,%d)",
                                  d->jointTable[0].z, d->jointTable[1].z, d->jointTable[2].z, d->jointTable[3].z,
                                  d->jointTable[4].z, d->jointTable[4].x, d->jointTable[4].y, d->jointTable[4].z);
            }
            if (a->id == ACTOR_EN_HORSE) {
                // Title-rider rearing-anim verify (2026-07-15): read live EnHorse anim-select
                // state through the proper C struct (never a raw N64-offset poke — SoH is 64-bit,
                // see CLAUDE.md). animationIdx/curFrame/action/cutsceneAction are exactly the
                // fields EnHorse_CsWarpRearingInit/-CsWarpRearing (ported into title_rider.cpp)
                // write; ENHORSE_ANIM_REARING == 3, ENHORSE_ANIM_IDLE == 0.
                EnHorse* h = (EnHorse*)a;
                Zelda3D_ReplReply(outPath,
                                  "ainfo horse action=%d animationIdx=%d curFrame=%.1f "
                                  "cutsceneAction=%d speedXZ=%.2f",
                                  h->action, h->animationIdx, h->curFrame, h->cutsceneAction, a->speedXZ);
                Zelda3D_ReplReply(outPath,
                                  "ainfo horse skel startFrame=%.1f endFrame=%.1f animLength=%.1f "
                                  "playSpeed=%.2f morphWeight=%.2f mode=%d",
                                  h->skin.skelAnime.startFrame, h->skin.skelAnime.endFrame,
                                  h->skin.skelAnime.animLength, h->skin.skelAnime.playSpeed,
                                  h->skin.skelAnime.morphWeight, h->skin.skelAnime.mode);
            }
            if (a->id == ACTOR_EN_ITEM00) {
                EnItem00* it = (EnItem00*)a;
                Zelda3D_ReplReply(outPath,
                                  "ainfo item00 params=%d scale=%.4f shapeRot=(%d,%d,%d) "
                                  "unk_156=0x%X unk_158=0x%X blinkHidden=%d",
                                  a->params, a->scale.x, a->shape.rot.x, a->shape.rot.y, a->shape.rot.z,
                                  (u16)it->unk_156, (u16)it->unk_158, (it->unk_156 & it->unk_158) ? 1 : 0);
            }
        }
    } else if (strcmp(command, "apeek") == 0) {
        // GENERIC actor-memory peek: dump <count> s16s at byte offset <off> from the selected
        // actor, PLUS the actor's facing (shape.rot.y) and the yaw it would need to face Link
        // (the head-track expectation). For En_Ko Kokiri kids headRot Vec3s is at +0x1F0
        // (interactOff 0x1E8 + 0x08): `asel 0x163` then `apeek 0x1F0` reads (pitch,yaw,roll);
        // headRot.y should track `rel` (yawToLink - actorYaw) within the head-turn clamp as Link
        // moves. Used to debug #115b weird Kokiri-kid head orientation by VALUES, not pixels.
        int off = 0, cnt = 3;
        (void)sscanf(line, "%*s %i %i", &off, &cnt);
        if (gZelda3dSelActor == NULL) {
            Zelda3D_ReplReply(outPath, "apeek: no selection (asel first)");
        } else if (cnt < 1 || cnt > 16 || off < 0 || off > 0x2000) {
            Zelda3D_ReplReply(outPath, "apeek <byteoff> [count<=16] (off in [0,0x2000])");
        } else {
            Actor* a = gZelda3dSelActor;
            Player* pl = GET_PLAYER(play);
            s16* p = (s16*)((u8*)a + off);
            char buf[256];
            int k = 0;
            k += snprintf(buf + k, sizeof(buf) - k, "apeek +0x%X:", off);
            for (int i = 0; i < cnt && k < (int)sizeof(buf) - 8; i++)
                k += snprintf(buf + k, sizeof(buf) - k, " %d", p[i]);
            s16 yawToLink = Math_Vec3f_Yaw(&a->world.pos, &pl->actor.world.pos);
            Zelda3D_ReplReply(outPath, "%s | actorYaw=%d yawToLink=%d rel=%d", buf, a->shape.rot.y, yawToLink,
                              (s16)(yawToLink - a->shape.rot.y));
        }
    } else {
        return false;
    }
    return true;
}
