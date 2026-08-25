#include "../core/zelda3d_runtime.h"
#include "functions/math.h"
#include "functions/rendering.h"
#include "../scene/sky_control.h"
#include "../scene/scene_draw.h"
#include "../behaviors/title/title_cloud_vortex.h"
#include "sky_render.h"
#include "model_queries.h"

#include "soh/frame_interpolation.h"

#include <cmath>

// #28 — map a N64 normal-sky index (envCtx.skybox1Index, 0..8 into the game's sSkyboxTable:
// Fine sunrise/day/sunset/night, Cloud sunrise/day/sunset/night, Holy) to the matching OoT3D
// celestial-dome CMB in /kankyo/BlueSky.zar. The OoT3D fine_tenkyu_0..3 baked vertex colours line
// up 1:1 with the N64 order (0=sunrise yellow-green, 1=day blue, 2=sunset red, 3=night dark-blue;
// verified by dumping the dome vertex colours). The "SKY:" key prefix loads it with baked vertex
// colour + depth-write off (see loadAutoModel). Returns a stable, deduped Zelda3D model id.
int Zelda3D_SkyModelId(int idx) {
    static const char* const kTenkyu[9] = {
        "fine_tenkyu_0",  "fine_tenkyu_1",  "fine_tenkyu_2",  "fine_tenkyu_3", "cloud_tenkyu_0",
        "cloud_tenkyu_1", "cloud_tenkyu_2", "cloud_tenkyu_3", "holy_tenkyu0",
    };
    char key[128];
    if (idx < 0 || idx > 8) {
        idx = 1; // default to clear day
    }
    snprintf(key, sizeof(key), "SKY:/kankyo/BlueSky.zar|%s", kTenkyu[idx]);
    return Zelda3D_AutoModelId(key);
}

// The cloud layer (kumo) that sits over the dome — a textured, alpha-blended band near the horizon.
// fine/cloud/holy share the per-time _a0.._a3 set; matched to the dome's weather variant.
static int Zelda3D_SkyCloudModelId(int idx) {
    static const char* const kKumo[9] = {
        "fine_kumo_a0",  "fine_kumo_a1",  "fine_kumo_a2",  "fine_kumo_a3", "cloud_kumo_a0",
        "cloud_kumo_a1", "cloud_kumo_a2", "cloud_kumo_a3", "holy_kumo_a0",
    };
    char key[128];
    if (idx < 0 || idx > 8) {
        idx = 1;
    }
    snprintf(key, sizeof(key), "SKY:/kankyo/BlueSky.zar|%s", kKumo[idx]);
    return Zelda3D_AutoModelId(key);
}

// #28b cloud drift: OoT3D scrolls the kumo cloud band's texcoords via a small .cmab in BlueSky.zar
// (misc/<group>_kumo_a.cmab). Each is a single linear texcoord-U translation looping over the cmab
// `duration`; the channel-0 (base layer) rate per skybox group, DERIVED FROM THE ASSET via
// tools/cmab.py (do NOT fabricate — re-run `python3 tools/cmab.py` to reproduce these):
//   fine  (idx 0..3): dU = -1.0 / 900 per frame   (fine_kumo_a.cmab, duration 900)
//   cloud (idx 4..7): dU = -1.0 / 900 per frame   (cloud_kumo_a.cmab channel 0; ch1 -4/900 is a
//                                                   2nd multitex layer our single-texcoord path omits)
//   holy  (idx 8):    dU = -1.0 / 600 per frame   (holy_kumo_a.cmab, duration 600)
// All scroll in U only (dV = 0). The offset is wrapped into [0,1) (texture WRAP_S repeats it) and
// driven by the game's own logic-frame clock (play->gameplayFrames) so it advances at OoT3D's rate.
static float Zelda3D_SkyCloudScrollU(int idx) {
    if (idx >= 8) {
        return -1.0f / 600.0f; // holy
    }
    return -1.0f / 900.0f; // fine / cloud
}

// #28c stars: the OoT3D night sky carries a separate star dome (model/fine_star.cmb in
// BlueSky.zar) layered over the dark gradient dome — our dome replacement is just the gradient,
// so the night sky has been STARLESS. fine_star.cmb is an L8 (luminance) textured dome cap with
// ADDITIVE blend (src=GL_SRC_ALPHA, dst=GL_ONE), so it adds the star points over the dome. There
// is exactly one star dome (no per-weather variant). The "SKY:" forced-CMB key loads it like the
// kumo band (depth-write off, pinned to the far plane via handle bit 30). Returns the model id, or
// -1 if the index is not a night variant (stars only show at night; fade in/out WITH the night
// dome via the same cross-fade alpha as the gradient — no fabricated star-alpha curve).
static int Zelda3D_SkyIsNight(int idx) {
    return idx == 3 || idx == 7; // fine-night / cloud-night (sSkyboxTable order; matched in #28)
}

static int Zelda3D_SkyStarModelId(int idx) {
    if (!Zelda3D_SkyIsNight(idx)) {
        return -1;
    }
    return Zelda3D_AutoModelId("SKY:/kankyo/BlueSky.zar|fine_star");
}

// Query (NO draw, NO side effects): is the Zelda3D OoT3D sky dome handling the skybox this frame?
// When this is true, Play_Draw's skybox point bypasses the N64 SkyboxDraw_Draw — which is the only
// place sSkyboxDrawMatrix is allocated — so that global stays NULL. The later SkyboxDraw_UpdateMatrix
// call (fired when the view changes, e.g. first-person engaging sets view.unk_124) would then deref
// that NULL and crash (#16 early-load first-person SIGSEGV in guMtxF2L). Callers MUST skip the N64
// SkyboxDraw_UpdateMatrix when this returns 1 (its result is dead work anyway — we draw our own sky).
// Mirrors exactly the accept conditions of Zelda3D_TryDrawSky.
// #135 (debug_journal/2026-07-02-market-day-parity-sweep.md): Map non-NORMAL_SKY skyboxIds to a
// BlueSky.zar tenkyu dome variant so the visible-sky scenes (Market Day/Night, Market Adult,
// Overcast Sunset) don't render as a black void. The N64 handles these via a full-screen prerender
// image drawn AFTER the room (which paints over Zelda3D room geometry — see Zelda3D_ShouldSuppress-
// BgImageSkybox). Using the dome path keeps the sky BEHIND world geometry (far-plane draw, no depth
// write). Only OUTDOOR skybox ids get a dome — interiors (all HOUSE_/SHOP_/BAZAAR/TENT) return -1
// so the caller no-ops (their OoT3D CMB rooms are fully enclosed; no sky visible).
//
// Residual: this maps to a stock BlueSky variant, not the OoT3D per-scene VR backdrop (the 3DS
// remake replaces Market Adult with a distinct desolate-sky asset, not a cloud-night dome). Full
// parity here needs the OoT3D scene-specific vrbox asset — that's a follow-up when the OoT3D romfs
// extraction lands (no docs/tool for it yet).
static int Zelda3D_SkyBoxToTenkyuIndex(int skyboxId) {
    switch (skyboxId) {
        case SKYBOX_MARKET_CHILD_DAY:
            return 1; // fine day (clear blue)
        case SKYBOX_MARKET_CHILD_NIGHT:
            return 3; // fine night (dark blue + stars)
        case SKYBOX_MARKET_ADULT:
            return 7; // cloud night (desolate overcast)
        case SKYBOX_OVERCAST_SUNSET:
            return 2; // fine sunset (warm)
        default:
            return -1;
    }
}

// The skybox1Index to feed Zelda3D_SkyModelId this frame. Falls back to a scene-derived dome for
// non-NORMAL skyboxIds; NORMAL passes the game's own envCtx index through unchanged.
int Zelda3D_ActiveSkyIndex(PlayState* play) {
    if (play->skyboxId == SKYBOX_NORMAL_SKY) {
        return play->envCtx.skybox1Index;
    }
    return Zelda3D_SkyBoxToTenkyuIndex(play->skyboxId);
}

int Zelda3D_SkyActive(PlayState* play) {
    if (!gZelda3dSky || !Zelda3D_Enabled()) {
        return 0;
    }
    if (play->skyboxId != SKYBOX_NORMAL_SKY && Zelda3D_SkyBoxToTenkyuIndex(play->skyboxId) < 0) {
        return 0;
    }
    if (Zelda3D_SceneName(play) == NULL) {
        return 0;
    }
    if (Zelda3D_SkyModelId(Zelda3D_ActiveSkyIndex(play)) < 0) {
        return 0;
    }
    return 1;
}

int Zelda3D_TryDrawSky(PlayState* play) {
    int modelId;
    // Only the normal day/night gradient sky, in an OoT3D-mapped scene, with a valid dome variant.
    if (!Zelda3D_SkyActive(play)) {
        return 0;
    }
    int activeIdx = Zelda3D_ActiveSkyIndex(play);
    modelId = Zelda3D_SkyModelId(activeIdx);
    {
        // Dawn/dusk the game cross-fades two sky variants: skybox2Index drawn over skybox1Index at
        // alpha = skyboxBlend (0..255). Mirror that with our domes instead of snapping to the
        // dominant one, so the OoT3D sky transitions through the intermediate colour the way the
        // N64 skybox did (e.g. blue day -> red sunset). Only meaningful for SKYBOX_NORMAL_SKY —
        // the mapped non-NORMAL skyboxes have a single fixed dome variant (no blend companion).
        int idx2 = play->envCtx.skybox2Index;
        int blend = play->envCtx.skyboxBlend; // alpha of the upper (skybox2) variant
        int doBlend = (play->skyboxId == SKYBOX_NORMAL_SKY) &&
                      (blend > 0 && idx2 >= 0 && idx2 <= 8 && idx2 != play->envCtx.skybox1Index);
        OPEN_DISPS(play->state.gfxCtx);
        Zelda3D_EnsureModelProvider();
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        // Centre the dome on the camera eye (follows the camera; no parallax). The camera is folded
        // into the projection matrix, so this model matrix is model->world only; the shader pins the
        // dome to the far plane regardless of gZelda3dSkyScale.
        Matrix_Translate(play->view.eye.x, play->view.eye.y, play->view.eye.z, MTXMODE_NEW);
        Matrix_Scale(gZelda3dSkyScale, gZelda3dSkyScale, gZelda3dSkyScale, MTXMODE_APPLY);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_MODELVIEW | G_MTX_LOAD);
        // Bit 30 of the handle = sky flag (far-plane depth, no shadow/AO; see the draw opcode handler).
        // Lower layer first (opaque dome gradient + its cloud band), then — at dawn/dusk — the upper
        // variant's dome + clouds over it at alpha=skyboxBlend. All four pin to the far plane, so they
        // composite back-to-front and none occludes the world.
        gSPZelda3DDraw(POLY_OPA_DISP++, modelId | (1 << 30), 255, 255, 255);
        {
            // Stars sit just above their night gradient dome and BELOW the cloud band (clouds are
            // nearer). Drawn at the same alpha as the dome layer so they cross-fade in/out with the
            // night dome (no separate star-alpha curve to fabricate). #28c.
            int starId = Zelda3D_SkyStarModelId(activeIdx);
            if (starId >= 0) {
                gSPZelda3DDraw(POLY_OPA_DISP++, starId | (1 << 30), 255, 255, 255);
            }
        }
        {
            // Cloud band: drift its texcoords per the .cmab scroll rate (#28b). Wrap the per-frame
            // U offset into [0,1) (WRAP_S repeats it) and pack as 16-bit fixed (offset*65536).
            int cloudId = Zelda3D_SkyCloudModelId(activeIdx);
            if (cloudId >= 0) {
                float u = (float)play->gameplayFrames * Zelda3D_SkyCloudScrollU(activeIdx);
                u -= floorf(u);
                int uFx = (int)(u * 65536.0f) & 0xFFFF;
                gSPZelda3DDrawUV(POLY_OPA_DISP++, cloudId | (1 << 30), 255, uFx, 0, 255, 255, 255);
            }
        }
        if (doBlend) {
            int dome2 = Zelda3D_SkyModelId(idx2);
            int cloud2 = Zelda3D_SkyCloudModelId(idx2);
            int star2 = Zelda3D_SkyStarModelId(idx2);
            if (dome2 >= 0) {
                gSPZelda3DDrawA(POLY_OPA_DISP++, dome2 | (1 << 30), blend, 255, 255, 255);
            }
            if (star2 >= 0) {
                gSPZelda3DDrawA(POLY_OPA_DISP++, star2 | (1 << 30), blend, 255, 255, 255);
            }
            if (cloud2 >= 0) {
                float u = (float)play->gameplayFrames * Zelda3D_SkyCloudScrollU(idx2);
                u -= floorf(u);
                int uFx = (int)(u * 65536.0f) & 0xFFFF;
                gSPZelda3DDrawUV(POLY_OPA_DISP++, cloud2 | (1 << 30), blend, uFx, 0, 255, 255, 255);
            }
        }
        CLOSE_DISPS(play->state.gfxCtx);
    }
    return 1;
}
