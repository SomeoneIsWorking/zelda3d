// Zelda3D behavior: En_Elf (Navi + generic fairies) — native billboard REPLACEMENT.
//
// Ground truth (oot3d-decomp/docs/en_elf_navi.md): OoT3D's Navi is NOT a CMB actor.
// EnElf_Draw @ 0x001d6ec4 dispatches TWO sprite handles allocated in Init (actor+0x918
// outer glow, actor+0x91c inner core), setting per-sprite rot/scale/color from live
// actor state and calling FUN_00371eac(handle, 1) per frame. No skeleton, no CMB, no
// CSAB — a pure two-sprite additive effect.
//
// The port mirrors that shape: two camera-facing additive-glow sprites emitted at the
// actor's world.pos EXACTLY (== the 3DS sprite position source, see the sprite-position
// note below), using the REAL OoT3D fairy sprites
// (acto_navi_light1/2, below), coloured from `elf->outerColor` / `elf->innerColor`
// with the N64 draw's own animated alphas, gated by the N64 draw's own visibility
// conditions (both verified against the live oracle sprite handles, 2026-07-22).
//
// WINGS: the 3DS draw callback is sprite-only, but EnElf_Init (FUN_0018a8e0, decomp lines
// 41-47) also binds a skeleton-model instance — FUN_00358ea8(handle, play, actor+0x1a4
// /*shape*/, modelRes(10), actor+0x178, 0, ...) — which the engine's model manager draws at
// the ACTOR's transform+scale: that is elf/model/elf_fly_mdl_info.cmb with its flap clip
// elf/Anim/elf_fly_soft_anim_tbl_info.csab (both in /actor/zelda_keep.zar). Ported below via
// the standard behavior-module seam (AutoModelId zar|cmb + UpdateAnimAuto + DrawActorModel),
// at the actor's LIVE scale (z_en_elf.c animates it: 0.008 base, shrink on pickup/despawn),
// phase-locked to the N64 skelAnime flap like En_Butte. See oot3d-decomp/docs/en_elf_navi.md.
#include "z64.h"
#include "functions/math.h"
#include "src/overlays/actors/ovl_En_Elf/z_en_elf.h" // EnElf (timer, innerColor, outerColor, disappearTimer, fairyFlags)
#include "en_elf.h"
#include "zelda3d/anim/automatic_playback.h"
#include "zelda3d/render/model_draw.h"
#include "zelda3d/render/model_queries.h"

// The REAL OoT3D fairy sprites, found 2026-07-22 in /actor/zelda_keep.zar under elf/:
// acto_navi_light1.ctxb (outer glow) + acto_navi_light2.ctxb (inner core) — matching the
// two sprite handles EnElf_Draw submits (en_elf_navi.md). The earlier fine_sun stand-in
// (the SUN's lens glare) is what made pulse-peak fairies read as giant gate orbs.
// (elf/model/elf_fly_mdl_info.cmb + elf_fly_soft_anim_tbl_info.csab — the wings — live in
// the same archive and are ported below.)
// ~MIRROR: both ctxbs are QUADRANT-authored (bright corner; verified by offline decode
// 2026-07-22) exactly like the moon's fine_moon1/2 — the loader mirror-expands them.
#define ZELDA3D_NAVI_TEX_OUTER "BILLBOARDADD:/actor/zelda_keep.zar|elf/tex/acto_navi_light1.ctxb~MIRROR"
#define ZELDA3D_NAVI_TEX_INNER "BILLBOARDADD:/actor/zelda_keep.zar|elf/tex/acto_navi_light2.ctxb~MIRROR"
#define ZELDA3D_NAVI_WING_CMB "/actor/zelda_keep.zar|elf/model/elf_fly_mdl_info.cmb"
#define ZELDA3D_NAVI_WING_CSAB "elf_fly_soft_anim_tbl_info"

// Sprite position: NO offset. RE'd + live-verified 2026-07-22 (en_elf_navi.md): the 3DS sprite
// handles' world pos (+0x3c) is copied from the actor's engine-computed world MATRIX translation
// (row-major 3x4 @actor+0x148; translation column +0x154/+0x164/+0x174 == world.pos exactly),
// the SAME matrix the wing skeleton-model instance is drawn at. Glow and wings share one origin.
// FALSIFIED: an earlier kNaviYLift=12 "visual centering" offset — no such offset exists on the
// 3DS; do not reintroduce a lift here.
// Glow sizes — the EXACT 3DS formula (FUN_001d6ec4 with its pool constants resolved from
// code.bin, en_elf_navi.md):
//   outer = actor.scale.x * pulse(1 + sin*0.1) * 0.012 * 125 * 1000 * 1.3   [* 2 if BIG]
//   inner = outer * 0.5
// At the standard fairy scale 0.008 this is 15.6*pulse — matching the LIVE oracle handle
// scale 15.003 byte-for-byte (the 3DS sprite quad is 1x1 world unit). Driving from
// actor.scale.x (not a constant) carries the 3DS behavior for big/heal fairies and the
// despawn shrink for free. Our billboard quad is 63u, hence the /63.
static constexpr float kNaviGlowPerScale = 0.012f * 125.0f * 1000.0f * 1.3f; // = 1950 world units per scale unit
static constexpr float kNaviQuadUnits = 63.0f;
static constexpr int kNaviFairyFlagBig = (1 << 9); // 0x200 — same bit the 3DS draw tests at actor+0x9c4

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

    // "Breathing" pulse — sin(angle)*0.1 + 1, the same shape on both engines (3DS drives the
    // angle from a global frame counter, N64 from the fairy timer; the N64-timer drive is the
    // N64-faithful equivalent and keeps per-fairy phase).
    float wobble = Math_SinS(elf->timer * 4096) * 0.1f + 1.0f;
    float sizeMul = (elf->fairyFlags & kNaviFairyFlagBig) ? 2.0f : 1.0f;
    float outerWorld = actor->scale.x * kNaviGlowPerScale * wobble * sizeMul / kNaviQuadUnits;
    float innerWorld = outerWorld * 0.5f;

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
    // (targetCtx.naviInner/naviOuter). alphaScale fades pickups. zelda3dGlowFade is the 3DS
    // glow master fade (+0x924): the 3DS draw computes outer.a = fade * envAlpha * fadeOut and
    // inner.a = innerColor.a * fade * fadeOut (FUN_001d6ec4 lines 114-130) — distant Kokiri
    // fairies show BARE WINGS with no glow, fading in within 900 units of the player.
    float glowFade = elf->zelda3dGlowFade;
    u8 outA = (u8)((f32)envAlpha * alphaScale * glowFade);
    u8 inA = (u8)(elf->innerColor.a * alphaScale * glowFade);
    // WINGS first (the additive glows draw over them, matching the 3DS layering: model
    // instance in the normal pass, sprite effects after). Drawn at the actor's LIVE scale —
    // the 3DS model instance is bound to actor+0x1a4 (shape: pos/rot/scale), and z_en_elf.c
    // animates scale (0.008 base; pickup shrink; despawn shrink) which this follows for free.
    // Flap phase-locked to the live N64 skelAnime (falls back to free-run if unlockable).
    static int sWingId = 0;
    if (sWingId == 0) {
        sWingId = Zelda3D_AutoModelId(ZELDA3D_NAVI_WING_CMB);
    }
    if (sWingId > 0) {
        Zelda3D_UpdateAnimAuto(sWingId, ZELDA3D_NAVI_WING_CSAB, 1.0f, elf->skelAnime.curFrame,
                               elf->skelAnime.animLength, elf->skelAnime.morphWeight);
        Zelda3D_DrawActorModel(play, sWingId, actor, actor->scale.x);
    }

    Zelda3D_EmitActorBillboard(play, sOuterId, actor, 0.0f, 0.0f, 0.0f, outerWorld,
                               (u8)elf->outerColor.r, (u8)elf->outerColor.g, (u8)elf->outerColor.b, outA);
    Zelda3D_EmitActorBillboard(play, sInnerId, actor, 0.0f, 0.0f, 0.0f, innerWorld,
                               (u8)elf->innerColor.r, (u8)elf->innerColor.g, (u8)elf->innerColor.b, inA);
    return true;
}

} // namespace Zelda3D
