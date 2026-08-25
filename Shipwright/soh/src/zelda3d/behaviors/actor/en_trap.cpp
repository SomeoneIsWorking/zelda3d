// Zelda3D behavior: En_Trap (sliding-blade floor trap) — model REPLACEMENT.
//
// Ground truth: N64 EnTrap_Draw is one line — Gfx_DrawDListOpa(play, gSlidingBladeTrapDL) from
// OBJECT_TRAP (z_en_trap.c). OoT3D drops the same asset into /actor/zelda_yukabyun.zar under
// Model/trap_yukabyun_model.cmb (confirmed by enumerating the RomFS zars, 2026-07-02 — "yukabyun"
// = the floor-trap object family). One CMB everywhere, so it drops straight into the standard
// prop transform (world.pos + shape.rot.y + uniform scale) — no per-scene or per-type branch.
//
// OBJECT_TRAP is unmapped in kZelda3dObjectZars (NULL entry) because the OoT3D object id renumbering
// doesn't line up with N64's — we force-load the CMB by path here, mirroring how door_ana.cpp
// force-loads the field-keep grotto CMB regardless of the actor's declared OBJECT_GAMEPLAY_FIELD_KEEP.
#include "z64.h"
#include "en_trap.h"
#include "zelda3d/render/model_draw.h"
#include "zelda3d/render/model_queries.h"
#include "zelda3d/diagnostics/model_tuning_query.h"

#define ZELDA3D_EN_TRAP_ZAR "/actor/zelda_yukabyun.zar"
#define ZELDA3D_EN_TRAP_CMB "Model/trap_yukabyun_model.cmb"

// World scale: N64 gSlidingBladeTrapDL is authored at ~1x, the OoT3D CMB is around 100x larger
// (measured 2026-07-02). Start at 0.01 like other prop CMBs; live-retunable via REPL `gscale 14`.
static constexpr float kEnTrapWorldScale = 0.01f;
static constexpr int kEnTrapGScaleSlot = 14;

namespace Zelda3D {

s16 EnTrapBehavior::actorId() const {
    return ACTOR_EN_TRAP;
}

bool EnTrapBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    static int sModelId = 0; // 0 = unresolved, <0 = no CMB (fall through to N64)
    if (sModelId == 0) {
        sModelId = Zelda3D_AutoModelId(ZELDA3D_EN_TRAP_ZAR "|" ZELDA3D_EN_TRAP_CMB);
    }
    if (sModelId < 0) {
        return false;
    }
    Zelda3D_DrawActorModel(play, sModelId, actor, Zelda3D_ModelScaleOrDefault(kEnTrapGScaleSlot, kEnTrapWorldScale));
    return true;
}

} // namespace Zelda3D
