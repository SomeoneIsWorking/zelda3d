// Zelda3D behavior: Obj_Oshihiki (push/pull block) — model REPLACEMENT.
//
// Ground truth: N64 ObjOshihiki_Draw does `gSPDisplayList(POLY_OPA_DISP++, gPushBlockDL)` from
// OBJECT_GAMEPLAY_DANGEON_KEEP — ONE unit-cube mesh — scaled by the actor's `Actor_SetScale(...,
// sScales[params & 0xF])` and tinted per-scene with `gDPSetEnvColor(this->color)` (z_obj_oshihiki.c).
// OoT3D instead ships a per-dungeon *themed* brick CMB in the same keep zar,
// `/actor/zelda_dangeon_keep.zar` under `Model/brick_15_<theme>_<Size>_model.cmb`, so the brick
// texture matches the dungeon — there is no runtime env tint, the theme is baked into the CMB.
//
// Faithful scale derivation (NOT a guess): the N64 collision face vertices are multiplied by
// `actor.scale * 10.0f` (z_obj_oshihiki.c:328 `sColCheckPoints[i].x * (scale.x * 10.0f)`), and the
// bottom-face check points sit at +/-29.99. So at actor scale 1.0 the cube half-extent is
// 29.99 * 10 ~= 300 -> the raw gPushBlockDL is a **600-unit cube**, base at Y=0. Every OoT3D
// brick CMB is ALSO a 600-unit cube (measured: `_sa` and `_La` are both 600u; the filename size
// letter selects texture detail, not geometry). The in-world block size is therefore
// (600 * sScales[type]); to make the 600u CMB match it the world scale is simply sScales[type].
//
// sScales[params & 0xF] (z_obj_oshihiki.c): {S 1/10, M 1/6, L 1/5, H 1/3} for indices 0-3
// (START_ON) and again 4-7 (START_OFF) — so size = (params & 0xF) & 3.
//
// Only the DRAW differs; the block BEHAVIOR (push/pull/fall/move-under) is shared N64 code that runs
// unchanged and just moves actor.world.pos, which this draw follows.
#include <cstdio>
#include "z64.h"
#include "push_block.h"
#include "overlays/actors/ovl_Obj_Oshihiki/z_obj_oshihiki.h"

#define ZELDA3D_PUSHBLOCK_ZAR "/actor/zelda_dangeon_keep.zar"

// Live-retunable base scale slot; per-size scale = sScales[type] applied below.
static constexpr int kPushBlockGScaleSlot = 20;

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
float Zelda3D_GScale(int slot, float def);
}

namespace Zelda3D {

namespace {

// The OoT3D dungeon-keep brick CMBs (only the sizes that actually occur in each dungeon are shipped).
// Index order is the resolve-cache order.
enum Brick {
    BRICK_DEKU_S, // Deku Tree, small
    BRICK_DOD_S,  // Dodongo's Cavern, small
    BRICK_FRS_M,  // Forest Temple, medium
    BRICK_FIRE_S, // Fire Temple, small
    BRICK_WAT_M,  // Water Temple, medium
    BRICK_SOUL_S, // Spirit Temple, small
    BRICK_SOUL_L, // Spirit Temple, large
    BRICK_DARK_M, // Shadow Temple, medium
    BRICK_GERD_L, // Gerudo Training Ground, large
    BRICK_COUNT,
};

const char* const kBrickCmb[BRICK_COUNT] = {
    "Model/brick_15_deku_Sa_model.cmb",
    "Model/brick_15_dod_Sa_model.cmb",
    "Model/brick_15_frs_Ma_model.cmb",
    "Model/brick_15_fire_Sa_model.cmb",
    "Model/brick_15_wat_Ma_model.cmb",
    "Model/brick_15_soul_sa_model.cmb",
    "Model/brick_15_soul_La_model.cmb",
    "Model/brick_15_dark_Ma_model.cmb",
    "Model/brick_15_gerd_La_model.cmb",
};

// N64 actor sizes by sScales index (& 3): S/M/L/H.
enum Size { SZ_S, SZ_M, SZ_L, SZ_H };

float sizeScale(Size sz) {
    switch (sz) {
        case SZ_S: return 1.0f / 10.0f;
        case SZ_M: return 1.0f / 6.0f;
        case SZ_L: return 1.0f / 5.0f;
        case SZ_H: return 1.0f / 3.0f;
    }
    return 1.0f / 10.0f;
}

// Map the live scene + requested size to the dungeon's brick CMB. Returns -1 if the scene has no
// OoT3D brick (e.g. Ganon's Tower, which the N64 draws with a runtime white tint) -> fall through.
int pickBrick(s32 sceneNum, Size sz) {
    switch (sceneNum) {
        case SCENE_DEKU_TREE:              return BRICK_DEKU_S;
        case SCENE_DODONGOS_CAVERN:        return BRICK_DOD_S;
        case SCENE_FOREST_TEMPLE:          return BRICK_FRS_M;
        case SCENE_FIRE_TEMPLE:            return BRICK_FIRE_S;
        case SCENE_WATER_TEMPLE:           return BRICK_WAT_M;
        case SCENE_SPIRIT_TEMPLE:          return (sz == SZ_L) ? BRICK_SOUL_L : BRICK_SOUL_S;
        case SCENE_SHADOW_TEMPLE:          return BRICK_DARK_M;
        case SCENE_GERUDO_TRAINING_GROUND: return BRICK_GERD_L;
        default:                           return -1;
    }
}

int resolveBrick(int brick) {
    static int sIds[BRICK_COUNT] = {0}; // 0 = unresolved, <0 = no CMB
    if (sIds[brick] == 0) {
        char path[160];
        snprintf(path, sizeof(path), "%s|%s", ZELDA3D_PUSHBLOCK_ZAR, kBrickCmb[brick]);
        sIds[brick] = Zelda3D_AutoModelId(path);
    }
    return sIds[brick];
}

} // namespace

s16 ObjOshihikiBehavior::actorId() const {
    return ACTOR_OBJ_OSHIHIKI;
}

bool ObjOshihikiBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    Size sz = (Size)((actor->params & 0xF) & 3);
    int brick = pickBrick(play->sceneNum, sz);
    if (brick < 0) {
        return false; // no OoT3D brick for this scene -> let the N64 block draw
    }
    int modelId = resolveBrick(brick);
    if (modelId < 0) {
        return false; // brick CMB missing -> N64 fallback
    }
    // world scale = sScales[type] (raw 600u cube == 600u CMB). gscale slot scales the whole family.
    float scale = sizeScale(sz) * Zelda3D_GScale(kPushBlockGScaleSlot, 1.0f);
    Zelda3D_DrawActorModel(play, modelId, actor, scale);
    return true;
}

} // namespace Zelda3D
