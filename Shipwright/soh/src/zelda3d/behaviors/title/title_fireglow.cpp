// Zelda3D title fire-glow overlay — port of OoT3D's g_title.cmb + Misc/g_title_fire.cmab
// (additive gold flame-wash composited over the title wordmark). Phase 3 of
// oot3d-decomp/docs/title_2d_overlay_logo.md §5 (item 1.c); CMAB byte format + curve data fully
// decoded in oot3d-decomp/docs/title_logo_fireglow_cmab.md.
//
// GROUND TRUTH (title_logo_fireglow_cmab.md §2/§3/§3.1): g_title.cmb is a 1-material, 1-mesh,
// 21760B model with TWO live texture bindings — g_title_efc 128x128 (the flame/glow gradient
// sprite, binding 0) and g_title_mable_t 64x64 (a detail/brightening mask, binding 1, sampled
// through coordinator 1's baked scale(3,2)/trans(0,0.9433) transform) — and BAKED material state:
// blend_enable=true, src=SRC_ALPHA dst=ONE (standard "additive glow": brightens whatever was
// drawn before it, never occludes), depth_write=false. Its 3-stage TEV combiner is
//     stage0.rgb = (efc + mableT) * efc         (ADD_MULT dual-texture, x1)
//     stage1.rgb = 2.0 * (stage0 * constColor0)  (MODULATE at scaleRGB=x2 — the CMAB-driven stage)
//     stage2     = passthrough
// Both the dual-texture stage 0 and the x2 stage-1 scale are first-class renderer features since
// 2026-07-10 (cmb.cpp comb0_dual_addmult / comb_const_scale_rgb -> zelda3d_sdl3gpu.cpp
// uSheen.y/uTex1Xf/uMatConst.a), so THIS module only supplies the CMAB-animated inputs per frame.
// Misc/g_title_fire.cmab (1296B, same ZAR) drives it: duration=300, loopMode=Once (plays once
// during the fade-in flourish, then holds the frame-300 value — NOT a perpetual loop), 4 mmad
// entries, of which entries 0-1 (mat=0 chan=1 Translation V-track "flame drift", mat=0 chan=0
// ConstColor R/G/B "warm gold flicker") are the confirmed pair driving THIS mesh; entries 2-3
// were once hypothesized to target the SEPARATE ura.ctxb billboard strip, but that reading is
// SUPERSEDED (title_ura_ctxb_identified.md §3: ura.ctxb is a file-select UI atlas with no fire
// imagery and no material list — both cmabs almost certainly target g_title.cmb's own material;
// entries 2-3 are duplicates in the non-ura file). Historical context — CONFIRMED (title_logo_actor.md §6.1/§6.4, full
// decompile of the draw fn FUN_001da4f4 and every callee) NOT drawn by the logo actor (0x171) at
// all: the draw fn has exactly THREE draw blocks (backdrop/wordmark/copyright, all ported here),
// no fourth handle, no `_ura` reference anywhere in the function or its callees. Whatever draws
// ura.ctxb is a separate, still-unidentified subsystem — out of scope for this actor's port; not
// guessed at here (flagged back to the decomp stream, see this change's journal entry).
//
// DRAW MECHANISM: reuses the exact seams the sky cloud-band scroll (zelda3d.c #28b) and the EnHy
// townsfolk body-color override already exercise — no new renderer plumbing needed.
//   - ConstColor R/G/B -> the draw's flat TINT (gSPZelda3DDrawUV's tintR/G/B), which the shared
//     Zelda3D fragment shader multiplies unconditionally into the final color
//     (`rgb = t.rgb * vColor.rgb * shade`, shade = uTintSkin.xyz — see
//     zelda3d_sdl3gpu.cpp's kSgFrag). Mathematically identical to writing the CONSTANT0 register
//     the CMAB actually targets: the material's own baked constColor0 is (255,255,255,255)
//     (verified byte-level, fireglow doc §3.1), so the renderer's const-modulate contributes
//     only the x2 stage scale and the tint carries the animated color — one multiply either way.
//     (The townsfolk Zelda3D_GL_SetMatConstOverride channel can't serve here anyway: it rides
//     the EmitPose/ItemPose pairing, and this raw gSPZelda3DDrawUV overlay never emits a pose —
//     same reason the wordmark's light-dir override needed a direct-read path.)
//   - Translation V-track (materialIndex 0, channelIndex 1) -> the draw's UV SCROLL offset
//     (gSPZelda3DDrawUV's uvV arg). The renderer routes a dual-texture group's per-draw scroll
//     to COORDINATOR 1 (the mableT mask's UV transform — exactly the coordinator a channel-1
//     Translation track drives, fireglow doc §3.2 fix 3), not to the efc/coordinator-0 UV the
//     single-texture sky cloud band uses.
//   - Placement: same 2D ortho overlay pass as the wordmark (title_logo.cpp,
//     zelda3d_overlay2d.h), at the SAME screen-fraction card position
//     (Zelda3D_TitleWordmarkPlacementFracs) — g_title.cmb is authored to wash over the wordmark,
//     not sit elsewhere (title_logo_fireglow_cmab.md §3; confirmed by the decompiled draw fn too,
//     title_logo_actor.md §6.4's local-offset table: backdrop's own local translate is
//     (0,0,-33.99), i.e. no X/Y offset from the wordmark's own placement at all).
//   - Alpha: this element's OWN decompiled channel, actor instance +0x1D0 (title_logo_actor.md
//     §5.2/§5.3/§6.2 — const-color-5.a, multiplicative into the texture's own alpha) via
//     Zelda3D_TitleLogoPhaseAlpha3's backdrop output — staged to fade in AFTER the wordmark ramp
//     completes (cf fadeIn+40+81), not simultaneously with it, then falls together with the other
//     two elements on fade-out. CORRECTION (title_logo_actor.md §6.3, 2026-07-10): the sibling
//     field +0x1DC ("sheen") does NOT ride with this alpha — full decompile of the draw fn shows
//     it is a light-DIRECTION parameter feeding a fragment-light term on the WORDMARK's own
//     material (title_logo_us.cmb), not an alpha channel at all, and not on g_title.cmb. See
//     title_logo.cpp's header for the still-unported follow-up.
//   - cmab frame cursor: anchored at the fade-in TRIGGER frame (same anchor the wordmark's csab
//     playhead uses) — the flicker starts counting from the trigger, independent of this
//     element's own later-starting alpha ramp, and (being loopMode=Once) settles at its
//     frame-300 value for the rest of the display phase.
#include "global.h"
#include "title_fireglow.h"
#include "title_logo.h"
#include "../../zelda3d_cmab.h"
#include "../../zelda3d_overlay2d.h"

#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <algorithm>

extern "C" {
int Zelda3D_AutoModelId(const char* zarPath);
float Zelda3D_AutoModelHeight(int modelId);
void Zelda3D_EnsureModelProvider(void);
int Zelda3D_Title_IsActive(void);
int Zelda3D_TitleCsFrame(void);
uint8_t* Zelda3D_AutoModelReadZarFile(int modelId, const char* suffix, size_t* outSize);
}

namespace {

int gFireGlowModelId = -1;
void* gFireGlowCmab = nullptr;   // parsed once, lives for the process (small, ~1.3KB source)
bool gFireGlowCmabTried = false; // avoid retrying a failed parse every frame

int fireGlowModelId() {
    if (gFireGlowModelId < 0) {
        gFireGlowModelId = Zelda3D_AutoModelId("/actor/zelda_mag.zar|g_title");
    }
    return gFireGlowModelId;
}

// Lazily read + parse g_title_fire.cmab from the same ZAR the model loaded from (Zelda3D_
// AutoModelReadZarFile finds it as a sibling file once the model itself has loaded, so this must
// be called AFTER a Zelda3D_AutoModelHeight() probe has forced the model load).
void* fireGlowCmab(int modelId) {
    if (gFireGlowCmab || gFireGlowCmabTried) {
        return gFireGlowCmab;
    }
    gFireGlowCmabTried = true;
    size_t sz = 0;
    uint8_t* bytes = Zelda3D_AutoModelReadZarFile(modelId, "g_title_fire.cmab", &sz);
    if (!bytes) {
        return nullptr;
    }
    gFireGlowCmab = Zelda3D_CmabParse(bytes, sz);
    free(bytes);
    return gFireGlowCmab;
}

} // namespace

extern "C" int Zelda3D_TryDrawTitleFireGlow(PlayState* play) {
    if (!Zelda3D_Title_IsActive() || play == nullptr) {
        return 0;
    }
    // g_title.cmb is the "backdrop/sheen" element in the decompiled actor (+0x1D0/+0x1DC,
    // title_logo_actor.md §5.2/§5.3) — its own alpha channel, staged to start AFTER the wordmark
    // ramp completes (not the wordmark's own alpha).
    float alpha = 0.0f;
    int fadeInFrame = -1;
    if (!Zelda3D_TitleLogoPhaseAlpha3(nullptr, &alpha, nullptr, &fadeInFrame)) {
        return 0; // Hidden phase — no glow without a visible wordmark to wash over.
    }
    Zelda3D_EnsureModelProvider();
    int modelId = fireGlowModelId();
    if (modelId < 0) {
        return 0;
    }
    float localHeight = Zelda3D_AutoModelHeight(modelId);
    if (localHeight <= 0.0f) {
        return 0; // model failed to load this frame; try again next frame
    }
    void* cmab = fireGlowCmab(modelId);
    if (!cmab) {
        return 0; // cmab missing/malformed — draw nothing rather than a static, unflickered glow
    }

    // cmab frame cursor: anchored at the fade-in trigger, same as the wordmark's csab playhead.
    // Zelda3D_CmabSampleTranslationV/ConstColorRGB clamp internally per the cmab's own loopMode
    // (Once: hold frame-300's value past duration; see file header).
    const int csFrame = Zelda3D_TitleCsFrame();
    float cmabFrame = (fadeInFrame >= 0) ? (float)(csFrame - fadeInFrame) : 0.0f;
    if (cmabFrame < 0.0f) {
        cmabFrame = 0.0f;
    }

    float uvV = 0.0f;
    Zelda3D_CmabSampleTranslationV(cmab, /*materialIndex=*/0, /*channelIndex=*/1, cmabFrame, &uvV);
    float rgb[3] = { 1.0f, 1.0f, 1.0f };
    Zelda3D_CmabSampleConstColorRGB(cmab, /*materialIndex=*/0, /*channelIndex=*/0, cmabFrame, rgb);

    // Verification aid (ZELDA3D_DBG_FIREGLOW=1): print the sampled curve values so the CMAB
    // player can be confirmed live-varying quantitatively, independent of screen-capture timing
    // (camera pan/attract-mode cuts make screenshot diffing an unreliable isolation of the
    // material-anim's own contribution — see debug_journal/2026-07-10-title-fireglow-copyright.md).
    {
        static int sDbg = -1;
        if (sDbg < 0) {
            const char* v = std::getenv("ZELDA3D_DBG_FIREGLOW");
            sDbg = (v != nullptr && v[0] != '\0') ? 1 : 0;
        }
        if (sDbg) {
            fprintf(stderr, "[FIREGLOW] csFrame=%d cmabFrame=%.1f rgb=(%.4f,%.4f,%.4f) uvV=%.4f alpha=%.1f\n",
                    csFrame, cmabFrame, rgb[0], rgb[1], rgb[2], uvV, alpha);
        }
    }

    float centerXFrac, centerYFrac, heightFrac, refW, refH;
    Zelda3D_TitleWordmarkPlacementFracs(&centerXFrac, &centerYFrac, &heightFrac);
    Zelda3D_TitleOverlayRefWH(&refW, &refH);

    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    Zelda3D_Overlay2D_PlaceModel(play, centerXFrac * refW, centerYFrac * refH, heightFrac * refH,
                                 localHeight);

    // Wrap the sampled V offset into [0,1) and pack as 16-bit fixed, same convention as the sky
    // cloud-band scroll (zelda3d.c #28b).
    float vWrap = uvV - std::floor(uvV);
    int vFx = (int)(vWrap * 65536.0f) & 0xFFFF;
    uint8_t r8 = (uint8_t)std::clamp(rgb[0] * 255.0f + 0.5f, 0.0f, 255.0f);
    uint8_t g8 = (uint8_t)std::clamp(rgb[1] * 255.0f + 0.5f, 0.0f, 255.0f);
    uint8_t b8 = (uint8_t)std::clamp(rgb[2] * 255.0f + 0.5f, 0.0f, 255.0f);
    uint8_t a8 = (uint8_t)(alpha + 0.5f); // this element's own backdrop/sheen alpha channel

    // FORCE_UNLIT: g_title.cmb is a self-illuminated additive overlay, same reasoning as the
    // wordmark (title_logo.cpp) — don't let the scene's ambient darken the glow.
    gSPZelda3DDrawUV(POLY_OPA_DISP++, modelId | (int)ZELDA3D_HANDLE_FORCE_UNLIT, a8, 0, vFx, r8, g8,
                     b8);
    CLOSE_DISPS(play->state.gfxCtx);
    return 1;
}
