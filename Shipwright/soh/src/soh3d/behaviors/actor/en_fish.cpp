// SoH3D behavior: En_Fish (ambient swimming fish) — animated model REPLACEMENT.
//
// Ground truth: N64 EnFish_Draw draws a 7-limb SkelAnime fish (gFishSkel + gFishInWaterAnim /
// gFishOutOfWaterAnim, OBJECT_GAMEPLAY_KEEP) via SkelAnime. OoT3D keeps the same fish in the keep
// zar, `/actor/zelda_keep.zar` under `fish/`:
//   model  fish/model/fishsmall.cmb  — 8 bones (a spine chain for the swim wiggle)
//   anim   fish/Anim/fs2_swim.csab   — looping swim (mirrors N64 gFishInWaterAnim)
//   anim   fish/Anim/fs2_jump.csab   — out-of-water flop (mirrors N64 gFishOutOfWaterAnim)
//
// All four scenes where the fish shows up in the render audit (Zora's Domain, Zora's Fountain,
// Gerudo Valley, Grottos) render AMBIENT SWIMMING fish; the out-of-water flop only happens to a
// fish Link has caught and dropped (a held/edge-case state, not in the ambient audit). So this first
// increment always plays fs2_swim. (Reliable swim-vs-flop detection would need to identify the live
// N64 anim — gFishInWaterAnim vs gFishOutOfWaterAnim — which isn't a stable pointer compare across
// the OTR load; left as a follow-up since the flop isn't a visible world gap. fs2_jump is present in
// the zar and ready when that's wired.)
//
// The fish's swim motion (wander, dart, flee Link) is shared N64 code that runs unchanged and just
// moves actor.world.pos + shape.rot; only the body-wiggle DRAW is replaced.
#include "z64.h"
#include "src/overlays/actors/ovl_En_Fish/z_en_fish.h" // EnFish (skelAnime, 64-bit-safe)
#include "en_fish.h"

// Keep zar + the fish CMB. The CSAB (fs2_swim) resolves from the same zar by basename.
#define SOH3D_FISH_ZAR "/actor/zelda_keep.zar"
#define SOH3D_FISH_CMB "fish/model/fishsmall.cmb"
#define SOH3D_FISH_CSAB "fs2_swim"

// World scale: N64 draws the swimming fish at actor scale 0.01 (ICHAIN_VEC3F_DIV1000(scale,10)); the
// OoT3D CMB is ~1930 units long (Z), so 0.008 is the calibration starting point, matched live to the
// small N64 fish footprint. Live-retunable via REPL `gscale 24`.
static constexpr float kFishWorldScale = 0.008f;
static constexpr int kFishGScaleSlot = 24;
static constexpr float kFishAnimRate = 1.0f;

extern "C" {
int SoH3D_AutoModelId(const char* zarPath);
int SoH3D_DrawActorModel(PlayState* play, int modelId, Actor* actor, float worldScale);
float SoH3D_GScale(int slot, float def);
void SoH3D_UpdateAnimAuto(int modelId, const char* animName, float rate, float n64CurFrame,
                          float n64AnimLength, float morphWeight);
}

namespace SoH3D {

s16 EnFishBehavior::actorId() const {
    return ACTOR_EN_FISH;
}

bool EnFishBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    static int sModelId = 0; // 0 = unresolved, <0 = no CMB (fall through to N64)
    if (sModelId == 0) {
        sModelId = SoH3D_AutoModelId(SOH3D_FISH_ZAR "|" SOH3D_FISH_CMB);
    }
    if (sModelId < 0) {
        return false; // no OoT3D fish CMB -> let the N64 fish draw
    }
    EnFish* f = reinterpret_cast<EnFish*>(actor);
    // Wiggle the spine via fs2_swim.csab, phase-locked to the live N64 SkelAnime so the tempo matches
    // the actor's speed-driven playSpeed; the per-emit pose snapshot pairs each fish with its own pose.
    SoH3D_UpdateAnimAuto(sModelId, SOH3D_FISH_CSAB, kFishAnimRate, f->skelAnime.curFrame,
                         f->skelAnime.animLength, f->skelAnime.morphWeight);
    SoH3D_DrawActorModel(play, sModelId, actor, SoH3D_GScale(kFishGScaleSlot, kFishWorldScale));
    return true;
}

} // namespace SoH3D
