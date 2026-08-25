#include "../core/zelda3d_runtime.h"
#include "functions/math.h"
#include "functions/rendering.h"
#include "../behaviors/title/title_activity.h"
#include "../scene/scene_draw.h"
#include "../scene/sky_control.h"
#include "celestial_render.h"
#include "model_queries.h"

#include "soh/frame_interpolation.h"

#include <cmath>

// #28e — the OoT3D sun/moon discs. N64 Environment_DrawSunAndMoon billboards two I-format sprites
// (gSun1Tex / gMoonTex). OoT3D ships them as standalone CTXB sprites (tex/fine_sun.ctxb additive
// glow, tex/fine_moon0.ctxb alpha-masked disc) that its engine billboards itself. We draw those
// CTXBs as synthetic billboard quads (loadBillboard) at the SAME world positions, sizes and
// camera-facing transform the N64 path uses — so the placement is byte-identical to N64, only the
// texture is the OoT3D asset. The quad verts already match the N64 sprite (-31..32), so here we
// just reproduce N64's translate * billboardMtxF * scale per sprite and pin both to the far plane
// (handle bit 30) like the dome. Tint is left white (the CTXB carries its own colour, unlike the
// N64 I-format sprites that need the prim/env tint); only the moon's day/night alpha fade is kept.
static int Zelda3D_SunModelId(void) {
    return Zelda3D_AutoModelId("BILLBOARDADD:/kankyo/BlueSky.zar|tex/fine_sun.ctxb");
}
static int Zelda3D_MoonModelId(void) {
    return Zelda3D_AutoModelId("BILLBOARD:/kankyo/BlueSky.zar|tex/fine_moon0.ctxb");
}
// OoT3D's moon is a THREE-LAYER composite, RE'd via a draw-log probe
// instrumenting Azahar's SW rasterizer at settled title. The captured
// frame shows three quads in the moon area, in this exact order:
//
//   1. fine_moon1 (64×64) ADDITIVE (srcAlpha, One)   — inner glow
//                                                      screen 188×133
//   2. fine_moon0 (128×128) ALPHA (srcAlpha, 1-srcA) — crescent disc
//                                                      screen 103×91
//   3. fine_moon2 (64×64) ADDITIVE (srcAlpha, One)   — outer glow
//                                                      screen 200×139
//
// Screen-size ratios of the additive halos to the disc:
//   fine_moon1: ~1.72× wider (188/103), 1.46× taller (133/91)
//   fine_moon2: ~1.94× wider, 1.53× taller
// Averaged uniformly: 1.65× and 1.85×.
//
// See oot3d-decomp docs/title_moon_composition.md for the RE trail.
// fine_moon1/fine_moon2 are each a single QUADRANT of a symmetric radial glow (bright at one
// corner, black at the other three) — the "~MIRROR" tag mirror-expands the quadrant 2x2 into a
// full centred halo at load (see loadBillboard's mirrorExpandQuadrant, zelda3d_model.cpp). Without
// it the raw quadrant painted the whole quad: halo invisible at some camera angles, a hard-edged
// bright rectangle at others (debug_journal/2026-07-10-moon-epona-fade-attribution.md §1).
static int Zelda3D_MoonInnerHaloId(void) {
    return Zelda3D_AutoModelId("BILLBOARDADD:/kankyo/BlueSky.zar|tex/fine_moon1.ctxb~MIRROR");
}
static int Zelda3D_MoonOuterHaloId(void) {
    return Zelda3D_AutoModelId("BILLBOARDADD:/kankyo/BlueSky.zar|tex/fine_moon2.ctxb~MIRROR");
}

// Task #16 title-atmosphere: STUB (open RE arc).
//
// Az's title-demo composites a visible landscape (green grass, dark mountains, dim sky) OVER a
// 3D scene mesh whose triangles all output color=0 through the TEV combiner (MODULATE(primary,
// tex) with primary=0). Traced via the SW-rasterizer draw log (task16_lighting.log): out of 34
// unique textures at settled title, only two have non-zero primary_color — 0x2095aa00
// (common_bg01.ctxb, the ZELDA-logo UI overlay drawn on top screen) and 0x2091a900
// (ura.ctxb, a small UI strip). Neither is the atmospheric background — an initial port of them
// as full-screen billboards revealed the Zelda title logo overlaying the scene at title, NOT
// the landscape colours.
//
// So the visible landscape colours must come from a NON-SW-rasterizer path — likely a 2D bg
// image copied to the framebuffer via PICA200 DisplayTransfer, or the bg-image scanout layer
// that composites under the 3D top-screen output. The draw-log substrate doesn't capture those
// paths (they don't go through ProcessTriangle). Next step: instrument Az's
// video_core/renderer_software/sw_framebuffer + DisplayTransferConfig to log large writes to
// the top-screen scanout region during title. See oot3d-decomp/docs/title_landscape_atmospheric_layer.md.
int Zelda3D_TryDrawTitleAtmos(PlayState* play) {
    (void)play;
    return 0;
}

int Zelda3D_TryDrawSunMoon(PlayState* play) {
    f32 y, color, scale, temp, alpha;
    int sunId, moonId;

    if (!gZelda3dSky || !Zelda3D_Enabled()) {
        return 0;
    }
    // Title-demo bypasses the skyboxId + scene-name guards so the moon
    // draw is at least ATTEMPTED during Az-parity shot 1 even though
    // SoH's title-cs sets a non-NORMAL skybox. Task #16.
    // NOTE: after this unblock the moon opcode is emitted (moonId=2004,
    // alpha≈191 at midnight) but at a WORLD position derived from
    // sunPos alone (formula: eye - sunPos, sunPos = ±sin/cos(dayTime)
    // *120*25). For the OoT3D title cam (eye≈(-4072,58,5217), forward
    // ≈(-0.45,+0.09,-0.89), i.e. facing NW-ish across Hyrule Field),
    // the resulting moon lies ~66° off the left of the forward axis
    // (i.e. way outside the FOV), so it never appears on-frame. Az's
    // title moon is fixed in the FRAMING (top-right of the shot) and
    // is almost certainly baked into OoT3D's title BlueSky.zar night
    // dome variant — NOT the environment sun/moon path. Follow-on
    // work: identify the OoT3D title-sky asset and render it in place
    // of the N64 sky; the dyn sun/moon path is likely a dead end here.
    if (!Zelda3D_Title_IsActive()) {
        if (play->skyboxId != SKYBOX_NORMAL_SKY) {
            return 0;
        }
        if (Zelda3D_SceneName(play) == NULL) {
            return 0;
        }
    }

    // Update sunPos exactly as Environment_DrawSunAndMoon does (we skip the N64 draw, so we must
    // keep advancing the position other code reads — lens flare, lighting). Cutscene path uses the
    // same smooth-step easing; gameplay path snaps.
    if (play->csCtx.state != 0) {
        Math_SmoothStepToF(&play->envCtx.sunPos.x,
                           -(Math_SinS(((void)0, gSaveContext.dayTime) - 0x8000) * 120.0f) * 25.0f, 1.0f, 0.8f, 0.8f);
        Math_SmoothStepToF(&play->envCtx.sunPos.y,
                           (Math_CosS(((void)0, gSaveContext.dayTime) - 0x8000) * 120.0f) * 25.0f, 1.0f, 0.8f, 0.8f);
        Math_SmoothStepToF(&play->envCtx.sunPos.y,
                           (Math_CosS(((void)0, gSaveContext.dayTime) - 0x8000) * 20.0f) * 25.0f, 1.0f, 0.8f, 0.8f);
    } else {
        play->envCtx.sunPos.x = -(Math_SinS(((void)0, gSaveContext.dayTime) - 0x8000) * 120.0f) * 25.0f;
        play->envCtx.sunPos.y = +(Math_CosS(((void)0, gSaveContext.dayTime) - 0x8000) * 120.0f) * 25.0f;
        play->envCtx.sunPos.z = +(Math_CosS(((void)0, gSaveContext.dayTime) - 0x8000) * 20.0f) * 25.0f;
    }

    // The one entrance/setup where the N64 draws nothing (Hyrule Field past-bridge cutscene). Match
    // it: skip both sprites but still own the call (return 1) so the N64 path stays off.
    if (gSaveContext.entranceIndex != ENTR_HYRULE_FIELD_PAST_BRIDGE_SPAWN ||
        ((void)0, gSaveContext.sceneSetupIndex) != 5) {
        // Frame interpolation (#149 moon jitter): this draw runs at ROOT level of the recording
        // tree, where matrices only pair POSITIONALLY between consecutive logic frames — any op
        // drift elsewhere in the frame breaks the pairing and the moon snaps at logic rate for
        // that pair (measured: moon holds + 1.5px catch-ups every ~6 presented frames while the
        // world interpolates smoothly). A keyed child pairs deterministically.
        FrameInterpolation_RecordOpenChild("Zelda3D SunMoon", 0);
        sunId = Zelda3D_SunModelId();
        moonId = Zelda3D_MoonModelId();
        y = play->envCtx.sunPos.y / 25.0f;

        OPEN_DISPS(play->state.gfxCtx);
        Zelda3D_EnsureModelProvider();
        Gfx_SetupDL_25Opa(play->state.gfxCtx);

        // Sun: glow disc at eye + sunPos. scale = (color * 2) + 10, color = clamp(y / 80, 0, 1)
        // (matches N64). Additive over the sky; far-plane pinned (bit 30) so terrain occludes it
        // when it dips below the horizon, exactly like the N64 sprite.
        color = y / 80.0f;
        if (color < 0.0f)
            color = 0.0f;
        if (color > 1.0f)
            color = 1.0f;
        scale = (color * 2.0f) + 10.0f;
        Matrix_Translate(play->view.eye.x + play->envCtx.sunPos.x, play->view.eye.y + play->envCtx.sunPos.y,
                         play->view.eye.z + play->envCtx.sunPos.z, MTXMODE_NEW);
        Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
        Matrix_Scale(scale, scale, scale, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
        if (sunId >= 0) {
            gSPZelda3DDraw(POLY_OPA_DISP++, sunId | (1 << 30), 255, 255, 255);
        }

        // Moon: 3-layer disc+halo composite at eye - sunPos. `alpha` (the N64
        // night fade-in, clamp(min(-y/80,1)*255)) is used ONLY as the night
        // VISIBILITY gate here — see kMoonDiscAlpha/full-white below for opacity.
        // modulate the draw. color/scale kept for the N64-derived base scale.
        color = -y / 120.0f;
        if (color < 0.0f)
            color = 0.0f;
        if (Zelda3D_Title_IsActive()) {
            // Title: the N64 dayTime scale curve below is superseded by the RE'd parametric
            // transform (kMoonRay* constants at the draw) — scale is unused on this path.
            scale = 0.0f;
        } else {
            scale = (-15.0f * color) + 25.0f;
        }
        temp = -y / 80.0f;
        if (temp > 1.0f)
            temp = 1.0f;
        alpha = temp * 255.0f;
        // Moon transform — PARAMETRIC GROUND TRUTH (oot3d-decomp/docs/title_sequence_full_re.md §3
        // + env_sun_moon_draw.md session 4 uniform readback): all three layers lie on ONE view ray:
        //   disc  : distance D = 2684.47, authored model scale 640.0 (exact; unit ±0.5 quad)
        //   haloA : distance D·(1+1/30), scale 1280.0 (exact 2×) — fine_moon1, additive
        //   haloB : distance D·(1−1/30), scale 1280.0            — fine_moon2, additive
        // Our N64 sprite quad spans -31..32 (63 world units at scale 1), so the per-layer draw
        // scale = authoredScale / 63. Color is (255,255,255,255) — zero modulation; the textures
        // carry all shape/alpha (disc RGBA4, halos RGB565 with falloff baked into RGB).
        const f32 kMoonRayDist = 2684.47f;           // D, view-ray distance (uniform readback)
        const f32 kMoonRayDepthSplit = 1.0f / 30.0f; // halo depth offsets = ±D/30
        const f32 kMoonN64QuadWidth = 63.0f;         // our sprite mesh: VTX -31..32
        const f32 kMoonDiscDrawScale = 640.0f / kMoonN64QuadWidth;
        const f32 kMoonHaloDrawScale = 1280.0f / kMoonN64QuadWidth;
        // Disc opacity STOPGAP (not faithful): faithful is 255, but SoH decodes fine_moon0 (RGBA4)
        // ~brighter than the console texture, so 255 clips the crescent to white (peak 255 vs Az
        // ~235). 205 matches Az's disc peak; the REAL fix is the fine_moon0 decode
        // (oot3d-decomp/docs/env_sun_moon_draw.md session 4, quantified caveat).
        const u8 kMoonDiscAlpha = 205;
        if (alpha > 0.0f && moonId >= 0) {
            // One shared view ray from the camera toward the moon (moon direction = -sunPos;
            // envCtx.sunPos is the SUN offset from the eye, N64-identical trig).
            f32 mdx = -play->envCtx.sunPos.x, mdy = -play->envCtx.sunPos.y, mdz = -play->envCtx.sunPos.z;
            const f32 mlen = sqrtf(mdx * mdx + mdy * mdy + mdz * mdz);
            if (mlen > 1e-3f) {
                mdx /= mlen;
                mdy /= mlen;
                mdz /= mlen;
            }
            const f32 ex = play->view.eye.x, ey = play->view.eye.y, ez = play->view.eye.z;
            const u8 aA = kMoonDiscAlpha;

            // Layer 1: fine_moon1 (inner glow) — ADDITIVE, farther along the ray (behind the disc).
            int m1 = Zelda3D_MoonInnerHaloId();
            if (m1 >= 0) {
                const f32 d1 = kMoonRayDist * (1.0f + kMoonRayDepthSplit);
                Matrix_Translate(ex + mdx * d1, ey + mdy * d1, ez + mdz * d1, MTXMODE_NEW);
                Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
                Matrix_Scale(kMoonHaloDrawScale, kMoonHaloDrawScale, kMoonHaloDrawScale, MTXMODE_APPLY);
                gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
                gSPZelda3DDrawA(POLY_OPA_DISP++, m1 | (1 << 30), 255, 255, 255, 255);
            }

            // Layer 2: fine_moon0 (crescent disc) — ALPHA-blend, at D.
            Matrix_Translate(ex + mdx * kMoonRayDist, ey + mdy * kMoonRayDist, ez + mdz * kMoonRayDist, MTXMODE_NEW);
            Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
            Matrix_Scale(kMoonDiscDrawScale, kMoonDiscDrawScale, kMoonDiscDrawScale, MTXMODE_APPLY);
            gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
            gSPZelda3DDrawA(POLY_OPA_DISP++, moonId | (1 << 30), aA, 255, 255, 255);

            // Layer 3: fine_moon2 (outer glow) — ADDITIVE, nearer along the ray (in front).
            int m2 = Zelda3D_MoonOuterHaloId();
            if (m2 >= 0) {
                const f32 d2 = kMoonRayDist * (1.0f - kMoonRayDepthSplit);
                Matrix_Translate(ex + mdx * d2, ey + mdy * d2, ez + mdz * d2, MTXMODE_NEW);
                Matrix_Mult(&play->billboardMtxF, MTXMODE_APPLY);
                Matrix_Scale(kMoonHaloDrawScale, kMoonHaloDrawScale, kMoonHaloDrawScale, MTXMODE_APPLY);
                gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
                gSPZelda3DDrawA(POLY_OPA_DISP++, m2 | (1 << 30), 255, 255, 255, 255);
            }
        }

        FrameInterpolation_RecordCloseChild();
        CLOSE_DISPS(play->state.gfxCtx);
    }
    return 1;
}
