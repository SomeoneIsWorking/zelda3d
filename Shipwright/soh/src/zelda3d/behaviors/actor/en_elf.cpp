// Zelda3D behavior: En_Elf (Navi + generic fairies) — native billboard REPLACEMENT.
//
// Ground truth (oot3d-decomp/docs/en_elf_navi.md): OoT3D's Navi is NOT a CMB actor.
// EnElf_Draw @ 0x001d6ec4 dispatches TWO sprite handles allocated in Init (actor+0x918
// outer glow, actor+0x91c inner core), setting per-sprite rot/scale/color from live
// actor state and calling FUN_00371eac(handle, 1) per frame. No skeleton, no CMB, no
// CSAB — a pure two-sprite additive effect.
//
// The port mirrors that shape: two camera-facing additive-glow sprites emitted at the
// actor's world.pos + a small head-height lift, using the REAL OoT3D fairy sprites
// (acto_navi_light1/2, below), coloured from `elf->outerColor` / `elf->innerColor`
// with the N64 draw's own animated alphas, gated by the N64 draw's own visibility
// conditions (both verified against the live oracle sprite handles, 2026-07-22).
//
// Remaining follow-up: the WINGS — /actor/zelda_keep.zar also carries
// elf/model/elf_fly_mdl_info.cmb + elf/Anim/elf_fly_soft_anim_tbl_info.csab (the flapping
// wing model the oracle renders on top of the glow). Not yet ported.
#include "z64.h"
#include "src/overlays/actors/ovl_En_Elf/z_en_elf.h" // EnElf (timer, innerColor, outerColor, disappearTimer, fairyFlags)
#include "en_elf.h"

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
int Zelda3D_EmitActorBillboard(PlayState* play, int modelId, Actor* actor,
                               float xOff, float yOff, float zOff, float scale,
                               u8 r, u8 g, u8 b, u8 a);
}

// The REAL OoT3D fairy sprites, found 2026-07-22 in /actor/zelda_keep.zar under elf/:
// acto_navi_light1.ctxb (outer glow) + acto_navi_light2.ctxb (inner core) — matching the
// two sprite handles EnElf_Draw submits (en_elf_navi.md). The earlier fine_sun stand-in
// (the SUN's lens glare) is what made pulse-peak fairies read as giant gate orbs.
// (elf/model/elf_fly_mdl_info.cmb + elf_fly_soft_anim_tbl_info.csab — the wings — live in
// the same archive; wings port is a follow-up, see report.)
// ~MIRROR: both ctxbs are QUADRANT-authored (bright corner; verified by offline decode
// 2026-07-22) exactly like the moon's fine_moon1/2 — the loader mirror-expands them.
#define ZELDA3D_NAVI_TEX_OUTER "BILLBOARDADD:/actor/zelda_keep.zar|elf/tex/acto_navi_light1.ctxb~MIRROR"
#define ZELDA3D_NAVI_TEX_INNER "BILLBOARDADD:/actor/zelda_keep.zar|elf/tex/acto_navi_light2.ctxb~MIRROR"

static constexpr float kNaviYLift = 12.0f;      // head-height lift over actor.world.pos
// Sizes: the live oracle sprite handles at Kokiri read outer scale 15.0, inner 7.5 (2:1),
// and the 3DS sprite quad is 1x1 world unit: the live scales match the measured on-screen
// orb (Kokiri: ~15px at depth ~242 with the 52-deg projection = 7.4 world units = the
// 7.5-scaled inner). Our billboard quad is 63u, so scale = size/63.
static constexpr float kNaviOuterScale = 15.0f / 63.0f;
static constexpr float kNaviInnerScale = 7.5f / 63.0f;
static constexpr int kNaviFairyFlagBig = (1 << 9); // FAIRY_FLAG_BIG (z_en_elf.c-local)

namespace Zelda3D {

s16 EnElfBehavior::actorId() const {
    return ACTOR_EN_ELF;
}

bool EnElfBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    EnElf* elf = reinterpret_cast<EnElf*>(actor);

    static int sOuterId = 0, sInnerId = 0; // 0 = unresolved (retry), <0 = no CTXB (fall through)
    if (sOuterId == 0) {
        sOuterId = Zelda3D_AutoModelId(ZELDA3D_NAVI_TEX_OUTER);
    }
    if (sInnerId == 0) {
        sInnerId = Zelda3D_AutoModelId(ZELDA3D_NAVI_TEX_INNER);
    }
    if (sOuterId <= 0 || sInnerId <= 0) {
        return sOuterId < 0 || sInnerId < 0 ? false : true; // pending: draw nothing this frame
    }

    // N64 EnElf_Draw's own visibility gates, replicated exactly (this hook REPLACES the draw,
    // so the gates inside it must come along): state 8 / FAIRY_FLAG_8 = hidden (Navi inside
    // Link, cutscene holds); first-person = hidden unless projected in front of kREG(90).
    if (elf->unk_2A8 == 8 || (elf->fairyFlags & 8)) {
        return true; // hidden: draw nothing, and don't fall through to the N64 sprite
    }
    {
        Player* player = GET_PLAYER(play);
        if ((player->stateFlags1 & PLAYER_STATE1_FIRST_PERSON) && !(kREG(90) < actor->projectedPos.z)) {
            return true;
        }
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

    // ALPHAS are the N64 draw's own, verified against the live oracle sprite handles
    // (Kokiri gameplay, 2026-07-22): the 3DS fairy sprites carry float RGBA whose alpha
    // samples (0.010, 0.490, 1.0) trace the SAME animation N64 computes — outer = the
    // (timer*50)&0x1FF folded triangle PULSE x alphaScale, inner = innerColor.a x
    // alphaScale. The previous hardcoded 255/255 is what blew the fairies out into the
    // giant gate orbs (a full-brightness additive sun-glow quad).
    s32 envAlpha = (elf->timer * 50) & 0x1FF;
    if (envAlpha > 255) {
        envAlpha = 511 - envAlpha;
    }

    // Live actor colours drive the tint — matches OoT3D's Navi-following-Link colour flip
    // (targetCtx.naviInner/naviOuter). alphaScale fades pickups.
    u8 outA = (u8)((f32)envAlpha * alphaScale);
    u8 inA = (u8)(elf->innerColor.a * alphaScale);
    Zelda3D_EmitActorBillboard(play, sOuterId, actor, 0.0f, kNaviYLift, 0.0f, outerWorld,
                               (u8)elf->outerColor.r, (u8)elf->outerColor.g, (u8)elf->outerColor.b, outA);
    Zelda3D_EmitActorBillboard(play, sInnerId, actor, 0.0f, kNaviYLift, 0.0f, innerWorld,
                               (u8)elf->innerColor.r, (u8)elf->innerColor.g, (u8)elf->innerColor.b, inA);
    return true;
}

} // namespace Zelda3D
