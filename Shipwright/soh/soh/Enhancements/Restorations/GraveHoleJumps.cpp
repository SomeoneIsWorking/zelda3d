#include <libultraship/bridge/consolevariablebridge.h>
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "init/ShipInit.hpp"
#include "soh/Enhancements/enhancementTypes.h"
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/scenecommand/SceneCommand.h"
#include "soh/resource/type/scenecommand/SetCollisionHeader.h"

#define CVAR_GRAVE_HOLE_NAME CVAR_ENHANCEMENT("GraveHoles")
#define GRAVE_HOLES_DEFAULT 0
#define CVAR_GRAVE_HOLE_VALUE CVarGetInteger(CVAR_GRAVE_HOLE_NAME, GRAVE_HOLES_DEFAULT)
#define GRAVEYARD_SCENE_FILEPATH "scenes/shared/spot02_scene/spot02_scene"
#define CUSTOM_SURFACE_TYPE 32

const static std::array<std::pair<std::pair<u16, u16>, std::pair<u16, u16>>, 6> graveyardGeometryPatches = { {
    // { { startPolygon, endPolygon }, { originalSurfaceType, patchedSurfaceType } }
    { { 487, 509 }, { 20, CUSTOM_SURFACE_TYPE } }, // Floor around graves
    { { 651, 658 }, { 20, CUSTOM_SURFACE_TYPE } }, // Floor around Royal Family Tomb
    { { 613, 620 }, { 0, 15 } },                   // Grave ledges (Hylian Shield)
    { { 623, 630 }, { 0, 15 } },                   // Grave ledges (Redead)
    { { 633, 640 }, { 0, 15 } },                   // Grave ledges (Dampe)
    { { 643, 650 }, { 0, 15 } },                   // Grave ledges (Royal Family)
} };

CollisionHeader* getGraveyardCollisionHeader() {
    /*
     * Load the graveyard collision header manually. Since its position varies between versions, we cannot directly use
     * dspot02_sceneCollisionHeader_003C54. We have to scroll through the scene cmds to get the header the same way the
     * game does.
     */
    SOH::Scene* scene = (SOH::Scene*)Ship::Context::GetRawInstance()
                            ->GetResourceManager()
                            ->LoadResource(GRAVEYARD_SCENE_FILEPATH)
                            .get();
    SOH::SetCollisionHeader* sceneCmd = nullptr;
    for (size_t i = 0; i < scene->commands.size(); i++) {
        auto cmd = scene->commands[i];
        if (cmd->cmdId == SOH::SceneCommandID::SetCollisionHeader) {
            sceneCmd = static_cast<SOH::SetCollisionHeader*>(cmd.get());
            break;
        }
    }
    CollisionHeader* graveyardColHeader = (CollisionHeader*)sceneCmd->GetRawPointer();
    uint32_t surfaceTypesCount = sceneCmd->collisionHeader->surfaceTypesCount;

    /*
     * Copy the surface type list and give ourselves some extra space to create another surface type for Link to fall
     * into graves. NTSC 1.0's graveyard has 31 surface types, while later versions have 32. The contents of the lists
     * are shifted somewhat between versions, so to be safe we just create an extra slot that is not in any version.
     */
    static SurfaceType newSurfaceTypes[33];
    memcpy(newSurfaceTypes, graveyardColHeader->surfaceTypeList, sizeof(SurfaceType) * surfaceTypesCount);
    newSurfaceTypes[CUSTOM_SURFACE_TYPE].data[0] = 0x24000004;
    newSurfaceTypes[CUSTOM_SURFACE_TYPE].data[1] = 0xFC8;
    graveyardColHeader->surfaceTypeList = newSurfaceTypes;

    return graveyardColHeader;
}

void ApplyGraveyardGeometryPatches() {
    // NOT `static`. It was, and the initialiser then ran once per dlopen of this core rather than
    // once per run -- caching a pointer into a scene resource owned by the ResourceManager of
    // whichever run happened to be first. Under the launcher a core is run more than once, and
    // starting a different game in between destroys that ResourceManager, so the third run
    // dereferenced freed memory here (OoT -> MM -> OoT SIGSEGV'd in this function, inside InitOTR).
    //
    // A same-game restart hid it, which is worth knowing before trusting a green oot,oot run: that
    // path reuses the existing GameSession, so the resource -- and the stale pointer -- stayed
    // valid by accident.
    //
    // Re-resolving costs nothing worth caching: LoadResource hits the ResourceManager's own cache.
    // The rule this follows is the general one -- anything whose validity ends with the run must not
    // be held anywhere that outlives the run (see zelda3d/core/zelda3d_core_lifecycle.c).
    CollisionHeader* graveyardColHeader = getGraveyardCollisionHeader();
    for (auto& mappingPatch : graveyardGeometryPatches) {
        for (int i = mappingPatch.first.first; i <= mappingPatch.first.second; i++) {
            CollisionPoly* poly = &graveyardColHeader->polyList[i];
            poly->type = CVAR_GRAVE_HOLE_VALUE ? mappingPatch.second.first : mappingPatch.second.second;
        }
    }
}

void RegisterGraveHoleJumps() {
    ApplyGraveyardGeometryPatches();
}

static RegisterShipInitFunc initFunc(RegisterGraveHoleJumps, { CVAR_GRAVE_HOLE_NAME });
