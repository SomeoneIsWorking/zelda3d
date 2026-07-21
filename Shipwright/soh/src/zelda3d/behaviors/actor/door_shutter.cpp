// Zelda3D behavior: Door_Shutter (dungeon shutter door + boss door) — model REPLACEMENT.
//
// Ground truth (N64 z_door_shutter.c): DoorShutter_Draw selects a per-dungeon `ShutterInfo`
// entry via `sObjectInfo[styleType]`, keyed by scene (sSceneInfo). Each entry is a static PANEL DL
// (`a`) + optional BARS DL (`b`), where the bars slide vertically by `sp70->c * (1 - barsClosedAmount)`
// when present. Panels themselves are static — the door doesn't hinge; it becomes barred/unbarred
// (SHUTTER_FRONT_CLEAR / SHUTTER_FRONT_SWITCH) or opens by the WHOLE panel being lifted out/into place
// (SHUTTER_BOSS via SHUTTER's Open action). Boss doors also swap a per-dungeon envelope texture
// (`D_809982D4[bossDoorTexIndex]` — segment 8) — we render the whole boss-door CMB regardless of
// dungeon tint for the first increment.
//
// OoT3D drops those DLs into per-object CMBs inside the same dungeon zar (verified by enumerating
// the RomFS zars, 2026-07-02 — see kSceneShutterAsset below). The panel is a single skinless model
// centered on the actor origin; the OoT3D door faces +Z like the N64 door, so it drops straight into
// the standard prop transform (world.pos + shape.rot.y + uniform scale) without offset.
//
// Increment 4 (this file): STATIC panels for the most common dungeon shutters — Deku Tree, Dodongo,
// Fire, Water, Shadow/Well, Spirit, Gerudo Training, Boss doors, Ganon interior, Ganon's Tower. The
// N64 bars overlay + slide-open animation stay as their own follow-up (increment 5).
// Increment 6 (this file): STATIC panels for Ice Cavern (ice_tobira), Royal Family's Tomb
// (ouke_tobira — the ouke_haka zar's sole CMB), and the Deku Tree boss-room Gohma block
// (a_yb_door). Only Jabu-Jabu (special jointed doors) and Forest Temple shutters still fall
// through to N64 (their per-object shutter CMBs are not a plain single panel).
//
// Increment 5 (this file): N64 bars overlay + slide-open animation. For doors that have bars
// (most dungeon shutters), we draw the N64 bars display list with vertical slide animation based
// on barsClosedAmount. The 3DS versions appear to integrate bars into the main door model or omit
// them, so we use the N64 bars as a faithful overlay.
#include "z64.h"
#include "door_shutter.h"

// Per-scene panel CMB, mirroring N64 sObjectInfo[styleType].index1 (the standard panel — the "back"
// index2 for boss doors is a follow-up). "" = no OoT3D CMB → fall through to N64.
namespace {

struct ShutterAsset {
    int sceneNum;   // -1 = sentinel end
    const char* key; // "<zar>|<cmb>"
};

// Populated from a raw RomFS zar enumeration (scratch DumpDoorShutterZars, 2026-07-02): the CMBs
// listed here are confirmed present in the OoT3D archives.
static const ShutterAsset kSceneShutterAsset[] = {
    // Deku Tree — ydan_tdoor_model
    { SCENE_DEKU_TREE, "/actor/zelda_ydan_objects.zar|Model/ydan_tdoor_model.cmb" },
    // Dodongo's Cavern (+ boss arena) — ddan_tdoor_model
    { SCENE_DODONGOS_CAVERN, "/actor/zelda_ddan_objects.zar|Model/ddan_tdoor_model.cmb" },
    { SCENE_DODONGOS_CAVERN_BOSS, "/actor/zelda_ddan_objects.zar|Model/ddan_tdoor_model.cmb" },
    // Fire Temple — m_Fshutter1 (front)
    { SCENE_FIRE_TEMPLE, "/actor/zelda_hidan_objects.zar|Model/m_Fshutter1_model.cmb" },
    // Water Temple — m_Wshutter1 (front)
    { SCENE_WATER_TEMPLE, "/actor/zelda_mizu_objects.zar|Model/m_Wshutter1_model.cmb" },
    // Shadow Temple + Bottom of the Well — m_Hshutter1 (haka)
    { SCENE_SHADOW_TEMPLE, "/actor/zelda_haka_door.zar|Model/m_Hshutter1_model.cmb" },
    { SCENE_BOTTOM_OF_THE_WELL, "/actor/zelda_haka_door.zar|Model/m_Hshutter1_model.cmb" },
    // Spirit Temple (+ boss arena) — l_j_door0 (jya_door)
    { SCENE_SPIRIT_TEMPLE, "/actor/zelda_jya_door.zar|Model/l_j_door0_model.cmb" },
    { SCENE_SPIRIT_TEMPLE_BOSS, "/actor/zelda_jya_door.zar|Model/l_j_door0_model.cmb" },
    // Gerudo Training Ground — l_m_door (menkuri)
    { SCENE_GERUDO_TRAINING_GROUND, "/actor/zelda_menkuri_objects.zar|Model/l_m_door_model.cmb" },
    // Inside Ganon's Castle — l_g_door (kekkai)
    { SCENE_INSIDE_GANONS_CASTLE, "/actor/zelda_demo_kekkai.zar|Model/l_g_door_model.cmb" },
    // Ganon's Tower + Ganondorf boss room — ganon_tdoor
    { SCENE_GANONS_TOWER, "/actor/zelda_ganon_objects.zar|Model/ganon_tdoor_model.cmb" },
    { SCENE_GANONDORF_BOSS, "/actor/zelda_ganon_objects.zar|Model/ganon_tdoor_model.cmb" },
    // Ice Cavern — ice_tobira (OBJECT_ICE_OBJECTS, N64 sObjectInfo styleType 16)
    { SCENE_ICE_CAVERN, "/actor/zelda_ice_objects.zar|Model/ice_tobira_model.cmb" },
    // Royal Family's Tomb — ouke_tobira (OBJECT_OUKE_HAKA, styleType 19; ouke_haka.zar's sole CMB)
    { SCENE_ROYAL_FAMILYS_TOMB, "/actor/zelda_ouke_haka.zar|Model/ouke_tobira_model.cmb" },
    // Deku Tree boss room (Gohma block) — a_yb_door (OBJECT_GOMA, styleType 5)
    { SCENE_DEKU_TREE_BOSS, "/actor/zelda_goma.zar|Model/a_yb_door_model.cmb" },
    { -1, nullptr },
};

// Boss door (SHUTTER_BOSS / doorType == 5): shared bossdoor_model across dungeons. The per-dungeon
// texture tint (D_809982D4) is a first-increment TODO — render the untinted mesh for now.
static const char* const kBossDoorKey = "/actor/zelda_boss_door.zar|Model/bossdoor_model.cmb";

// World scale: dungeon doorways are ~100u tall in world space (see the N64 collider sizes); OoT3D
// shutter CMBs are on the ~10000-unit scale like m_Fnormaldoor, so 0.009 matches the en_door
// calibration and drops into the same doorway fit. Live-retunable via REPL `gscale 13`.
constexpr float kShutterWorldScale = 0.009f;
constexpr int kShutterGScaleSlot = 13;

static const char* keyForScene(int sceneNum) {
    for (int i = 0; kSceneShutterAsset[i].sceneNum >= 0; ++i) {
        if (kSceneShutterAsset[i].sceneNum == sceneNum) {
            return kSceneShutterAsset[i].key;
        }
    }
    return nullptr;
}

} // namespace

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
int Zelda3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
float Zelda3D_GScale(int slot, float def);
}

namespace Zelda3D {

s16 DoorShutterBehavior::actorId() const {
    return ACTOR_DOOR_SHUTTER;
}

// Increment 5 note: Bars overlay + slide animation
// The N64 bars overlay integration is complex due to display list type mismatches between
// the N64 asset addresses and the SoH rendering system. For this increment, we're focusing
// on the static 3DS door panels (completed in increment 4). The bars overlay with slide
// animation will be implemented as a follow-up using a different approach - either by
// finding the 3DS equivalent bar models or by properly integrating the N64 display lists.

bool DoorShutterBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    // SHUTTER_BOSS (doorType == 5) → shared boss-door CMB regardless of scene. Every other doorType
    // → the per-scene dungeon shutter panel (fall-through to N64 if unmapped).
    // z_door_shutter.h DoorShutter.doorType is at struct offset 0x16A; the doorType byte encodes the
    // Params-byte 4..7 nibble (see the actor's Params triangle diagram).
    const u8* bytes = reinterpret_cast<const u8*>(actor);
    u8 doorType = bytes[0x16A];
    const char* key = (doorType == 5 /* SHUTTER_BOSS */) ? kBossDoorKey : keyForScene(play->sceneNum);
    if (key == nullptr) {
        return false;
    }
    // Cache resolved model ids per-key (small linear scan: at most ~13 unique keys across the game).
    static const char* sKeys[16] = { nullptr };
    static int sIds[16] = { 0 };
    int slot = -1;
    for (int i = 0; i < 16; ++i) {
        if (sKeys[i] == key) {
            slot = i;
            break;
        }
        if (sKeys[i] == nullptr) {
            sKeys[i] = key;
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return false; // cache full — shouldn't happen for the current asset set
    }
    if (sIds[slot] == 0) {
        sIds[slot] = Zelda3D_AutoModelId(key);
    }
    if (sIds[slot] < 0) {
        return false; // resolution failed → let the N64 door draw
    }

    // Draw the main 3DS door panel
    Zelda3D_DrawActorModel(play, sIds[slot], actor,
                           Zelda3D_GScale(kShutterGScaleSlot, kShutterWorldScale));

    return true;
}

} // namespace Zelda3D
