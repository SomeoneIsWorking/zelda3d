#include "zelda3d_lighting.h"

#include "../behaviors/title/title_activity.h"
#include "../behaviors/title/title_lighting.h"

void Zelda3D_Fog3dSet(float camNear, float zFar, float fogNear, float fogFar, const float eyeWorld[3],
                      const float fwdWorld[3]);
void Zelda3D_Fog3dOff(void);

int gZelda3dScenePaletteN = 0;
const Zelda3dLightSlot* gZelda3dScenePalette = NULL;
Zelda3dEnvBlend gZelda3dEnvBlend;
Zelda3dEnvColors gZelda3dEnvColors;
float gZelda3dTintDiff = 0.5f;
float gZelda3dTintMul = 1.0f;

void Zelda3D_TitleLightSettingsOverride(PlayState* play) {
    Zelda3D_Title_ApplyLightOverride(play);
}

void Zelda3D_SceneLightSettingsOverride(PlayState* play) {
    EnvironmentContext* envCtx = &play->envCtx;
    const Zelda3dLightSlot* pal = gZelda3dScenePalette;
    s32 n = gZelda3dScenePaletteN;
    const Zelda3dEnvBlend* b = &gZelda3dEnvBlend;
    s32 a0, a1, b0, b1, i;
    f32 wT, wC;

    // The title runs its own palette + schedule + fog (Zelda3D_TitleLightSettingsOverride).
    if (Zelda3D_Title_IsActive()) {
        gZelda3dEnvColors.valid = 0; // title colors live in envCtx.lightSettings (title override)
        return;
    }
    if (pal == NULL || n <= 0 || !b->valid) {
        gZelda3dEnvColors.valid = 0; // no OoT3D palette -> CMB feed falls back to the N64 rows
        Zelda3D_Fog3dOff();
        return;
    }
    a0 = (s32)b->idx[0];
    a1 = (s32)b->idx[1];
    b0 = (s32)b->idx[2];
    b1 = (s32)b->idx[3];
    if (a0 >= n || a1 >= n || b0 >= n || b1 >= n) {
        gZelda3dEnvColors.valid = 0;
        return; // out-of-range slot: leave the N64 values rather than read past the palette
    }
    wT = b->wTime;
    wT = (wT < 0.0f) ? 0.0f : (wT > 1.0f) ? 1.0f : wT;
    wC = b->wConfig;
    wC = (wC < 0.0f) ? 0.0f : (wC > 1.0f) ? 1.0f : wC;

    for (i = 0; i < 3; i++) {
        f32 cfgA, cfgB;
        // COLORS go to the CMB renderer feed (gZelda3dEnvColors), NOT into envCtx.lightSettings:
        // the N64 rows z_kankyo just blended stay in envCtx -> lightCtx and keep lighting the
        // N64-format draws in their own calibration space (unified-shading decision — see the
        // Zelda3dEnvColors comment in zelda3d_lighting.h; the u8 quantization is kept so the values are
        // bit-identical to what the old envCtx round-trip delivered to the renderer).
        cfgA = LERP(pal[a0].amb[i], pal[a1].amb[i], wT);
        cfgB = LERP(pal[b0].amb[i], pal[b1].amb[i], wT);
        gZelda3dEnvColors.amb[i] = (f32)((u8)LERP(cfgA, cfgB, wC)) / 255.0f;
        cfgA = LERP(pal[a0].l0col[i], pal[a1].l0col[i], wT);
        cfgB = LERP(pal[b0].l0col[i], pal[b1].l0col[i], wT);
        gZelda3dEnvColors.l1col[i] = (f32)((u8)LERP(cfgA, cfgB, wC)) / 255.0f;
        cfgA = LERP(pal[a0].l1col[i], pal[a1].l1col[i], wT);
        cfgB = LERP(pal[b0].l1col[i], pal[b1].l1col[i], wT);
        gZelda3dEnvColors.l2col[i] = (f32)((u8)LERP(cfgA, cfgB, wC)) / 255.0f;
        if (!b->timeBased) {
            // The ZSI record's colour block is N64's EnvLightSettings byte-for-byte, so the dirs
            // are ALREADY in the N64 (toward-light) convention — copy them straight through.
            // FALSIFIED, do not reinstate: these used to be negated ("3DS stores light-TRAVEL
            // dirs"). That compensated a 3-byte field-map error (l0dir was really light2Dir, and
            // light2Dir == -light1Dir in every record, so the negation cancelled it); with the
            // offsets corrected (gen_oot3d_scene_lighting.py docstring) a negation would flip
            // both lights. Live check, Zora's Domain: record light1Dir = (72,72,72).
            envCtx->lightSettings.light1Dir[i] = (s8)LERP16(pal[a0].l0dir[i], pal[a1].l0dir[i], wT);
            envCtx->lightSettings.light2Dir[i] = (s8)LERP16(pal[a0].l1dir[i], pal[a1].l1dir[i], wT);
        }
        // OoT3D's own per-scene FOG COLOUR (record +0x19), blended by the same schedule. The 3DS
        // hazes every fog-enabled material toward this; SoH was hazing toward the N64 scene's
        // fogColor, which is a different colour entirely in several scenes and is the whole of
        // the Zora's Domain divergence (N64 (25,100,100) dark teal vs the oracle's live PICA
        // fog_color (104,135,181) light blue — harness vsuni_log, entrance 0x109 @0x6000).
        // Written into envCtx so the single existing feed in zelda3d_render.cpp (which converts
        // lightSettings.fogColor -> gZelda3dFogColor -> the shader's uFog.xyz) picks it up; the
        // N64 F3DEX ramp that also reads it is off (#113), so this only drives the 3DS LUT path.
        cfgA = LERP(pal[a0].fogCol[i], pal[a1].fogCol[i], wT);
        cfgB = LERP(pal[b0].fogCol[i], pal[b1].fogCol[i], wT);
        envCtx->lightSettings.fogColor[i] = (u8)LERP(cfgA, cfgB, wC);
    }
    gZelda3dEnvColors.valid = 1;

    // OoT3D PICA distance fog, gameplay feed. Ground truth (Kokiri gameplay, harness az_fog +
    // per-draw vsuni): the 3DS renders every fog-enabled CMB material (fog_mode=5) through the
    // hardware fog LUT with the scene's per-slot window — the LUT solves EXACTLY as the eye-linear
    // window fogNear..fogFar under the 3DS projection (camNear 7.0, zFar = slot zFar; measured
    // proj2 = (1.0006, 7.0041) -> near 7.0, far 12000): LUT entry 127 = (2400-834)/(2400-800) =
    // 0.979, byte-exact vs the live dump. The renderer's uFog.w==2 path (RE'd for the title,
    // title_env_lighting.md §13) replays that same curve; this feeds it the gameplay window,
    // blended by the SAME schedule as the colors above. NOTE this is NOT the F3DEX fog ramp that
    // #113 turned off (hand-wired N64 fog-space values, pale-wedge artifact) — it is the 3DS's own
    // fog with the ROM's own per-scene values, part of the oracle's world render (distant-terrain
    // A/B: oracle (172,169,93) vs un-fogged (80,77,33) at rows 0.14-0.20).
    {
        f32 cfgA, cfgB, fogNear, fogFar, zFar;
        const f32 kGameplayCamNear3ds = 7.0f; // measured from the oracle's live projection (above)
        f32 fwd[3];
        f32 eye[3] = { play->view.eye.x, play->view.eye.y, play->view.eye.z };
        cfgA = LERP((f32)pal[a0].fogNear, (f32)pal[a1].fogNear, wT);
        cfgB = LERP((f32)pal[b0].fogNear, (f32)pal[b1].fogNear, wT);
        fogNear = LERP(cfgA, cfgB, wC);
        cfgA = LERP(pal[a0].fogFar, pal[a1].fogFar, wT);
        cfgB = LERP(pal[b0].fogFar, pal[b1].fogFar, wT);
        fogFar = LERP(cfgA, cfgB, wC);
        cfgA = LERP(pal[a0].zFar, pal[a1].zFar, wT);
        cfgB = LERP(pal[b0].zFar, pal[b1].zFar, wT);
        zFar = LERP(cfgA, cfgB, wC);
        fwd[0] = play->view.lookAt.x - play->view.eye.x;
        fwd[1] = play->view.lookAt.y - play->view.eye.y;
        fwd[2] = play->view.lookAt.z - play->view.eye.z;
        if (fogFar > fogNear && zFar > kGameplayCamNear3ds) {
            Zelda3D_Fog3dSet(kGameplayCamNear3ds, zFar, fogNear, fogFar, eye, fwd);
        } else {
            Zelda3D_Fog3dOff();
        }
    }
}
