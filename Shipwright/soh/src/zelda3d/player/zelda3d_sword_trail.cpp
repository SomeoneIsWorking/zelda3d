// zelda3d_sword_trail — see zelda3d_sword_trail.h.
//
// Ground truth: OoT3D EffectBlure_AddVertex caller at VA 0x002b9a88, factor table @ VA 0x004dc27c.
// 3DS reads the factor as `tbl[this->itemAction]` with tbl[3] = 0.85f and tbl[4] = 0.65f, guarded by
// `itemAction == 4 || itemAction == 3` — i.e. only the two swords are trimmed. The explicit two-way
// branch below is behaviorally identical to that guarded table read.
//
// DELIBERATELY NOT PORTED HERE (see debug_journal / the port spec): OoT3D also selects one of 11
// weapon-specific trail MATERIALS from a 5-entry table at VA 0x004dc3c4. Those indices are 3DS
// resource slots in the blure's own GAR, NOT SoH `TrailType` values, so assigning them into
// EffectBlure.trailType would be meaningless and would regress SoH's existing trail enhancement.
// That half needs the 3DS trail materials wired up first.
#include "zelda3d_sword_trail.h"

extern "C" void Zelda3D_SwordTrail_TrimTip(Player* player, Vec3f* tip, Vec3f* base, Vec3f* out) {
    f32 factor;

    if (player->itemAction == PLAYER_IA_SWORD_MASTER) {
        factor = 0.85f;
    } else if (player->itemAction == PLAYER_IA_SWORD_KOKIRI) {
        factor = 0.65f;
    } else {
        // No 3DS trim factor for this weapon — the trail uses the full blade, as on N64.
        *out = *tip;
        return;
    }

    out->x = ((tip->x - base->x) * factor) + base->x;
    out->y = ((tip->y - base->y) * factor) + base->y;
    out->z = ((tip->z - base->z) * factor) + base->z;
}
