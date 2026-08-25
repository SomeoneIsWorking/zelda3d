// Zelda3D behavior: En_Item00 (dropped/placed collectible) — model REPLACEMENT for its RUPEE types.
//
// Ground truth: N64 EnItem00_Draw dispatches on `actor.params` (the Item00Type). For the rupee types
// it calls EnItem00_DrawRupee (z_en_item00.c), which draws ONE display list `gRupeeDL` and swaps
// texture segment 0x08 to one of five rupee textures indexed by:
//   texIndex = (params <= ITEM00_RUPEE_RED) ? params : params - 0x10
// i.e. GREEN(0x00)->0  BLUE(0x01)->1  RED(0x02)->2  ORANGE(0x13)->3  PURPLE(0x14)->4. The SoH 3D path
// maps those same indices to GID_RUPEE_{GREEN,BLUE,RED,GOLD,PURPLE} (the N64 orange/pink texture
// names are an authentic bug), so texIndex is the OoT3D color order — identical to En_Ex_Ruppy.
//
// The OoT3D rupee CMB (zelda_gi_rupy.cmb) packs all five colors as distinct mesh_ids 0..4 in that
// order, so we draw it masked to mesh_id == texIndex via the shared drawRupeeColorMesh helper. Every
// NON-rupee item type (heart, magic, seeds, nuts, bombs, arrows, sticks, heart piece/container, …)
// returns false so the existing N64 draw renders it — this module is the rupee increment only.
//
// Honors the N64 blink-visibility gate: EnItem00_Draw only draws when `!(unk_156 & unk_158)` (the item
// flashes before it despawns), so while that gate hides the item we draw nothing but still suppress
// the N64 rupee.
#include "z64.h"
#include "en_item00.h"
#include "rupee_draw.h"
#include "zelda3d/diagnostics/model_tuning_query.h"

// World scale: N64 draws the rupee at (this->scale * mtxScale) with mtxScale 25.0 (17.5 gold/purple),
// the same as En_Ex_Ruppy. We drive worldScale off the actor's own live scale and share the rupee
// scale knob (REPL `gscale 17`) with En_Ex_Ruppy since it's the same CMB. The OoT3D CMB bakes the
// bigger gold gem into its mesh, so no per-color multiplier is needed.
static constexpr float kRupeeScaleMul = 25.0f;
static constexpr int kRupeeGScaleSlot = 17;

namespace Zelda3D {

s16 EnItem00Behavior::actorId() const {
    return ACTOR_EN_ITEM00;
}

bool EnItem00Behavior::tryDrawModel(PlayState* play, Actor* actor) {
    s16 params = actor->params;

    // Rupee types only; map params -> color mesh index (mesh_id). Anything else is not a rupee.
    int colorIdx;
    if (params == ITEM00_RUPEE_GREEN || params == ITEM00_RUPEE_BLUE || params == ITEM00_RUPEE_RED) {
        colorIdx = params;
    } else if (params == ITEM00_RUPEE_ORANGE || params == ITEM00_RUPEE_PURPLE) {
        colorIdx = params - 0x10;
    } else {
        return false; // hearts / magic / seeds / nuts / bombs / arrows / sticks -> N64 draw
    }

    EnItem00* item = (EnItem00*)actor;
    if (item->unk_156 & item->unk_158) {
        return true; // blink gate hides the item this frame: draw nothing, keep N64 rupee suppressed
    }

    float worldScale = actor->scale.x * Zelda3D_ModelScaleOrDefault(kRupeeGScaleSlot, kRupeeScaleMul);
    return drawRupeeColorMesh(play, actor, colorIdx, worldScale);
}

} // namespace Zelda3D
