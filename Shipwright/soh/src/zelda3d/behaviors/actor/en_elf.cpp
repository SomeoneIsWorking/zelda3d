// Zelda3D behavior: En_Elf (Navi + generic fairies) — native billboard REPLACEMENT.
//
// Ground truth (oot3d-decomp/docs/en_elf_navi.md): OoT3D's Navi is NOT a CMB actor.
// EnElf_Draw @ 0x001d6ec4 dispatches TWO sprite handles allocated in Init (actor+0x918
// outer glow, actor+0x91c inner core), setting per-sprite rot/scale/color from live
// actor state and calling FUN_00371eac(handle, 1) per frame. No skeleton, no CMB, no
// CSAB — a pure two-sprite additive effect.
//
// The port mirrors that shape: two camera-facing additive-glow sprites emitted at the
// actor's world.pos + a small head-height lift, coloured from `elf->outerColor` /
// `elf->innerColor`. The OoT3D Navi orb sprite ships in a non-`/actor/*.zar` archive
// we haven't extracted; the sun's `fine_sun.ctxb` from `/kankyo/BlueSky.zar` is a
// proven-available and visually-appropriate stand-in (soft round glow, additive blend,
// camera-facing). Swapping to the real Navi orb sprite is a texture-swap follow-up —
// the emit shape does not change.
//
// Sizing: `fine_sun.ctxb` is a 63-unit quad. Scale 0.5 makes the outer glow ~32 world
// units across (a Navi-scale orb next to Link), scale 0.2 makes the inner bright core
// ~13 units — calibrated live at Lon Lon Ranch.
//
// Verified via tools/navi_close_test.py (checks the runtime probe in Zelda3D_Sg_DrawModel
// for a non-sky DrawModel of the sun-billboard model id) and a visual capture at
// entrance 0x157 dayTime 0x8001 showing a blue-white glow orb where the N64 fairy sprite
// used to be.
//
// Not-yet-ported: the N64 EnElf_Draw's `unk_2A8 == 8` / `fairyFlags & 8` vanish states
// (Navi should be invisible during first-person / Z-target hover / hidden-in-Link modes).
// Skipping the gate keeps the sprite always-visible during bringup so a live sweep
// doesn't false-negative when the state machine has moved her to 8. Adding the gate is a
// follow-up once we confirm the state timing across scene transitions.
#include "z64.h"
#include "src/overlays/actors/ovl_En_Elf/z_en_elf.h" // EnElf (timer, innerColor, outerColor, disappearTimer, fairyFlags)
#include "en_elf.h"

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
int Zelda3D_EmitActorBillboard(PlayState* play, int modelId, Actor* actor,
                               float xOff, float yOff, float zOff, float scale,
                               u8 r, u8 g, u8 b, u8 a);
}

#define ZELDA3D_NAVI_TEX "BILLBOARDADD:/kankyo/BlueSky.zar|tex/fine_sun.ctxb"

static constexpr float kNaviYLift = 12.0f;      // head-height lift over actor.world.pos
static constexpr float kNaviOuterScale = 0.50f; // fine_sun 63u quad -> ~32 world units
static constexpr float kNaviInnerScale = 0.20f; // ~13 world units bright core
static constexpr int kNaviFairyFlagBig = (1 << 9); // FAIRY_FLAG_BIG (z_en_elf.c-local)

namespace Zelda3D {

s16 EnElfBehavior::actorId() const {
    return ACTOR_EN_ELF;
}

bool EnElfBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    EnElf* elf = reinterpret_cast<EnElf*>(actor);

    static int sModelId = 0; // 0 = unresolved, <0 = no CTXB (fall through to N64)
    if (sModelId == 0) {
        sModelId = Zelda3D_AutoModelId(ZELDA3D_NAVI_TEX);
    }
    if (sModelId < 0) {
        return false;
    }

    // N64 EnElf_Draw's fade-out curve for one-shot pickups (heal/big fairies).
    float alphaScale = 1.0f;
    if (elf->disappearTimer < 0) {
        alphaScale = elf->disappearTimer * (7.0f / 6000.0f) + 1.0f;
        if (alphaScale < 0.0f) alphaScale = 0.0f;
    }

    // "Breathing" pulse — same curve N64 EnElf_OverrideLimbDraw applies to the body limb.
    float wobble = Math_SinS(elf->timer * 4096) * 0.1f + 1.0f;
    float sizeMul = (elf->fairyFlags & kNaviFairyFlagBig) ? 2.0f : 1.0f;
    float outerWorld = wobble * sizeMul * kNaviOuterScale;
    float innerWorld = wobble * sizeMul * kNaviInnerScale;

    // Live actor colours drive the tint — matches OoT3D's Navi-following-Link colour flip
    // (targetCtx.naviInner/naviOuter). alphaScale fades pickups; the tint alpha byte is
    // rendered as (a/255) into the additive blend.
    u8 outA = (u8)(255.0f * alphaScale);
    u8 inA = (u8)(255.0f * alphaScale);
    Zelda3D_EmitActorBillboard(play, sModelId, actor, 0.0f, kNaviYLift, 0.0f, outerWorld,
                               (u8)elf->outerColor.r, (u8)elf->outerColor.g, (u8)elf->outerColor.b, outA);
    Zelda3D_EmitActorBillboard(play, sModelId, actor, 0.0f, kNaviYLift, 0.0f, innerWorld,
                               (u8)elf->innerColor.r, (u8)elf->innerColor.g, (u8)elf->innerColor.b, inA);
    return true;
}

} // namespace Zelda3D
