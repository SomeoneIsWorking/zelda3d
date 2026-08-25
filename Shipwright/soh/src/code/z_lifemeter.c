#include "global.h"
#include "textures/parameter_static/parameter_static.h"
#include "soh/frame_interpolation.h"

#include "soh/Enhancements/cosmetics/cosmeticsTypes.h"
#include "soh/Enhancements/game-interactor/GameInteractor_Hooks.h"
#include "zelda3d/hud/zelda3d_hud_assets.h" // #31 — crisp higher-res HUD heart textures
#include "zelda3d/hud/zelda3d_hud.h" // #205 — native HUD: record quads instead of display lists

// #31 — map an N64 heart texture symbol to a ZELDA3D_HEART_* kind, or -1 if it isn't a heart we
// replace. The DD (double-defense) variants reuse the same crisp shapes (tint differs via PRIM/ENV).
static int Zelda3D_HeartKind(void* tex) {
    if (tex == (void*)gHeartFullTex || tex == (void*)gDefenseHeartFullTex)
        return ZELDA3D_HEART_FULL;
    if (tex == (void*)gHeartThreeQuarterTex || tex == (void*)gDefenseHeartThreeQuarterTex)
        return ZELDA3D_HEART_THREEQUARTER;
    if (tex == (void*)gHeartHalfTex || tex == (void*)gDefenseHeartHalfTex)
        return ZELDA3D_HEART_HALF;
    if (tex == (void*)gHeartQuarterTex || tex == (void*)gDefenseHeartQuarterTex)
        return ZELDA3D_HEART_QUARTER;
    if (tex == (void*)gHeartEmptyTex || tex == (void*)gDefenseHeartEmptyTex)
        return ZELDA3D_HEART_EMPTY;
    return -1;
}

s16 Top_LM_Margin = 0;
s16 Left_LM_Margin = 0;
s16 Right_LM_Margin = 0;
s16 Bottom_LM_Margin = 0;

static s16 sHeartsPrimColors[3][3] = {
    { HEARTS_PRIM_R, HEARTS_PRIM_G, HEARTS_PRIM_B },
    { HEARTS_BURN_PRIM_R, HEARTS_BURN_PRIM_G, HEARTS_BURN_PRIM_B },    // unused
    { HEARTS_DROWN_PRIM_R, HEARTS_DROWN_PRIM_G, HEARTS_DROWN_PRIM_B }, // unused
};

static s16 sHeartsEnvColors[3][3] = {
    { HEARTS_ENV_R, HEARTS_ENV_G, HEARTS_ENV_B },
    { HEARTS_BURN_ENV_R, HEARTS_BURN_ENV_G },                       // unused
    { HEARTS_DROWN_ENV_R, HEARTS_DROWN_ENV_G, HEARTS_DROWN_ENV_B }, // unused
};

static s16 sHeartsPrimFactors[3][3] = {
    {
        HEARTS_PRIM_R - HEARTS_PRIM_R,
        HEARTS_PRIM_G - HEARTS_PRIM_G,
        HEARTS_PRIM_B - HEARTS_PRIM_B,
    },
    // unused
    {
        HEARTS_BURN_PRIM_R - HEARTS_PRIM_R,
        HEARTS_BURN_PRIM_G - HEARTS_PRIM_G,
        HEARTS_BURN_PRIM_B - HEARTS_PRIM_B,
    },
    // unused
    {
        HEARTS_DROWN_PRIM_R - HEARTS_PRIM_R,
        HEARTS_DROWN_PRIM_G - HEARTS_PRIM_G,
        HEARTS_DROWN_PRIM_B - HEARTS_PRIM_B,
    },
};

static s16 sHeartsEnvFactors[3][3] = {
    {
        HEARTS_ENV_R - HEARTS_ENV_R,
        HEARTS_ENV_G - HEARTS_ENV_G,
        HEARTS_ENV_B - HEARTS_ENV_B,
    },
    // unused
    {
        HEARTS_BURN_ENV_R - HEARTS_ENV_R,
        HEARTS_BURN_ENV_G - HEARTS_ENV_G,
        HEARTS_BURN_ENV_B - HEARTS_ENV_B,
    },
    // unused
    {
        HEARTS_DROWN_ENV_R - HEARTS_ENV_R,
        HEARTS_DROWN_ENV_G - HEARTS_ENV_G,
        HEARTS_DROWN_ENV_B - HEARTS_ENV_B,
    },
};

static s16 sHeartsDDPrimColors[3][3] = {
    { HEARTS_DD_PRIM_R, HEARTS_DD_PRIM_G, HEARTS_DD_PRIM_B },
    { HEARTS_BURN_PRIM_R, HEARTS_BURN_PRIM_G, HEARTS_BURN_PRIM_B },    // unused
    { HEARTS_DROWN_PRIM_R, HEARTS_DROWN_PRIM_G, HEARTS_DROWN_PRIM_B }, // unused
};

static s16 sHeartsDDEnvColors[3][3] = {
    { HEARTS_DD_ENV_R, HEARTS_DD_ENV_G, HEARTS_DD_ENV_B },
    { HEARTS_BURN_ENV_R, HEARTS_BURN_ENV_G, HEARTS_BURN_ENV_B },    // unused
    { HEARTS_DROWN_ENV_R, HEARTS_DROWN_ENV_G, HEARTS_DROWN_ENV_B }, // unused
};

static s16 sHeartsDDPrimFactors[3][3] = {
    {
        HEARTS_DD_PRIM_R - HEARTS_DD_PRIM_R,
        HEARTS_DD_PRIM_G - HEARTS_DD_PRIM_G,
        HEARTS_DD_PRIM_B - HEARTS_DD_PRIM_B,
    },
    // unused
    {
        HEARTS_BURN_PRIM_R - HEARTS_DD_PRIM_R,
        HEARTS_BURN_PRIM_G - HEARTS_DD_PRIM_G,
        HEARTS_BURN_PRIM_B - HEARTS_DD_PRIM_B,
    },
    // unused
    {
        HEARTS_DROWN_PRIM_R - HEARTS_DD_PRIM_R,
        HEARTS_DROWN_PRIM_G - HEARTS_DD_PRIM_G,
        HEARTS_DROWN_PRIM_B - HEARTS_DD_PRIM_B,
    },
};

static s16 sHeartsDDEnvFactors[3][3] = {
    {
        HEARTS_DD_ENV_R - HEARTS_DD_ENV_R,
        HEARTS_DD_ENV_G - HEARTS_DD_ENV_G,
        HEARTS_DD_ENV_B - HEARTS_DD_ENV_B,
    },
    // unused
    {
        HEARTS_BURN_ENV_R - HEARTS_DD_ENV_R,
        HEARTS_BURN_ENV_G - HEARTS_DD_ENV_G,
        HEARTS_BURN_ENV_B - HEARTS_DD_ENV_B,
    },
    // unused
    {
        HEARTS_DROWN_ENV_R - HEARTS_DD_ENV_R,
        HEARTS_DROWN_ENV_G - HEARTS_DD_ENV_G,
        HEARTS_DROWN_ENV_B - HEARTS_DD_ENV_B,
    },
};

// Current colors for the double defense hearts
s16 sBeatingHeartsDDPrim[3];
s16 sBeatingHeartsDDEnv[3];
s16 sHeartsDDPrim[2][3];
s16 sHeartsDDEnv[2][3];

void HealthMeter_Init(PlayState* play) {
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    Color_RGB8 mainColor = { HEARTS_PRIM_R, HEARTS_PRIM_G, HEARTS_PRIM_B };
    if (CVarGetInteger(CVAR_COSMETIC("Consumable.Hearts.Changed"), 0)) {
        mainColor = CVarGetColor24(CVAR_COSMETIC("Consumable.Hearts.Value"), mainColor);
    }
    Color_RGB8 mainBorder = { HEARTS_ENV_R, HEARTS_ENV_G, HEARTS_ENV_B };
    if (CVarGetInteger(CVAR_COSMETIC("Consumable.HeartBorder.Changed"), 0)) {
        mainBorder = CVarGetColor24(CVAR_COSMETIC("Consumable.HeartBorder.Value"), mainBorder);
    }
    Color_RGB8 ddColor = { HEARTS_DD_ENV_R, HEARTS_DD_ENV_G, HEARTS_DD_ENV_B };
    if (CVarGetInteger(CVAR_COSMETIC("Consumable.DDHearts.Changed"), 0)) {
        ddColor = CVarGetColor24(CVAR_COSMETIC("Consumable.DDHearts.Value"), ddColor);
    }
    Color_RGB8 ddBorder = { HEARTS_DD_PRIM_R, HEARTS_DD_PRIM_G, HEARTS_DD_PRIM_B };
    if (CVarGetInteger(CVAR_COSMETIC("Consumable.DDHeartBorder.Changed"), 0)) {
        ddBorder = CVarGetColor24(CVAR_COSMETIC("Consumable.DDHeartBorder.Value"), ddBorder);
    }

    interfaceCtx->unk_228 = 0x140;
    interfaceCtx->unk_226 = gSaveContext.health;
    interfaceCtx->unk_22A = interfaceCtx->unk_1FE = 0;
    interfaceCtx->unk_22C = interfaceCtx->unk_200 = 0;

    interfaceCtx->heartsPrimR[0] = mainColor.r;
    interfaceCtx->heartsPrimG[0] = mainColor.g;
    interfaceCtx->heartsPrimB[0] = mainColor.b;

    interfaceCtx->heartsEnvR[0] = mainBorder.r;
    interfaceCtx->heartsEnvG[0] = mainBorder.g;
    interfaceCtx->heartsEnvB[0] = mainBorder.b;

    interfaceCtx->heartsPrimR[1] = mainColor.r;
    interfaceCtx->heartsPrimG[1] = mainColor.g;
    interfaceCtx->heartsPrimB[1] = mainColor.b;

    interfaceCtx->heartsEnvR[1] = mainBorder.r;
    interfaceCtx->heartsEnvG[1] = mainBorder.g;
    interfaceCtx->heartsEnvB[1] = mainBorder.b;

    sHeartsDDPrim[0][0] = sHeartsDDPrim[1][0] = ddBorder.r;
    sHeartsDDPrim[0][1] = sHeartsDDPrim[1][1] = ddBorder.g;
    sHeartsDDPrim[0][2] = sHeartsDDPrim[1][2] = ddBorder.b;

    sHeartsDDEnv[0][0] = sHeartsDDEnv[1][0] = ddColor.r;
    sHeartsDDEnv[0][1] = sHeartsDDEnv[1][1] = ddColor.g;
    sHeartsDDEnv[0][2] = sHeartsDDEnv[1][2] = ddColor.b;
}

void HealthMeter_Update(PlayState* play) {
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    f32 factor = interfaceCtx->unk_1FE * 0.1f;
    f32 ddFactor;
    s32 type = 0;
    s32 ddType;
    s16 rFactor;
    s16 gFactor;
    s16 bFactor;

    Top_LM_Margin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.T"), 0);
    Left_LM_Margin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.L"), 0);
    Right_LM_Margin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.R"), 0);
    Bottom_LM_Margin = CVarGetInteger(CVAR_COSMETIC("HUD.Margin.B"), 0);

    Color_RGB8 mainColor = { HEARTS_PRIM_R, HEARTS_PRIM_G, HEARTS_PRIM_B };
    if (CVarGetInteger(CVAR_COSMETIC("Consumable.Hearts.Changed"), 0)) {
        mainColor = CVarGetColor24(CVAR_COSMETIC("Consumable.Hearts.Value"), mainColor);
    }
    Color_RGB8 mainBorder = { HEARTS_ENV_R, HEARTS_ENV_G, HEARTS_ENV_B };
    if (CVarGetInteger(CVAR_COSMETIC("Consumable.HeartBorder.Changed"), 0)) {
        mainBorder = CVarGetColor24(CVAR_COSMETIC("Consumable.HeartBorder.Value"), mainBorder);
    }
    Color_RGB8 ddColor = { HEARTS_DD_ENV_R, HEARTS_DD_ENV_G, HEARTS_DD_ENV_B };
    if (CVarGetInteger(CVAR_COSMETIC("Consumable.DDHearts.Changed"), 0)) {
        ddColor = CVarGetColor24(CVAR_COSMETIC("Consumable.DDHearts.Value"), ddColor);
    }
    Color_RGB8 ddBorder = { HEARTS_DD_PRIM_R, HEARTS_DD_PRIM_G, HEARTS_DD_PRIM_B };
    if (CVarGetInteger(CVAR_COSMETIC("Consumable.DDHeartBorder.Changed"), 0)) {
        ddBorder = CVarGetColor24(CVAR_COSMETIC("Consumable.DDHeartBorder.Value"), ddBorder);
    }

    if (interfaceCtx->unk_200 != 0) {
        interfaceCtx->unk_1FE--;
        if (interfaceCtx->unk_1FE <= 0) {
            interfaceCtx->unk_1FE = 0;
            interfaceCtx->unk_200 = 0;
        }
    } else {
        interfaceCtx->unk_1FE++;
        if (interfaceCtx->unk_1FE >= 10) {
            interfaceCtx->unk_1FE = 10;
            interfaceCtx->unk_200 = 1;
        }
    }

    ddFactor = factor;

    interfaceCtx->heartsPrimR[0] = mainColor.r;
    interfaceCtx->heartsPrimG[0] = mainColor.g;
    interfaceCtx->heartsPrimB[0] = mainColor.b;

    interfaceCtx->heartsEnvR[0] = mainBorder.r;
    interfaceCtx->heartsEnvG[0] = mainBorder.g;
    interfaceCtx->heartsEnvB[0] = mainBorder.b;

    interfaceCtx->heartsPrimR[1] = mainColor.r;
    interfaceCtx->heartsPrimG[1] = mainColor.g;
    interfaceCtx->heartsPrimB[1] = mainColor.b;

    interfaceCtx->heartsEnvR[1] = mainBorder.r;
    interfaceCtx->heartsEnvG[1] = mainBorder.g;
    interfaceCtx->heartsEnvB[1] = mainBorder.b;

    rFactor = sHeartsPrimFactors[0][0] * factor;
    gFactor = sHeartsPrimFactors[0][1] * factor;
    bFactor = sHeartsPrimFactors[0][2] * factor;

    interfaceCtx->beatingHeartPrim[0] = (u8)(rFactor + mainColor.r) & 0xFF;
    interfaceCtx->beatingHeartPrim[1] = (u8)(gFactor + mainColor.g) & 0xFF;
    interfaceCtx->beatingHeartPrim[2] = (u8)(bFactor + mainColor.b) & 0xFF;

    rFactor = sHeartsEnvFactors[0][0] * factor;
    gFactor = sHeartsEnvFactors[0][1] * factor;
    bFactor = sHeartsEnvFactors[0][2] * factor;

    if (1) {}
    ddType = type;

    interfaceCtx->beatingHeartEnv[0] = (u8)(rFactor + mainBorder.r) & 0xFF;
    interfaceCtx->beatingHeartEnv[1] = (u8)(gFactor + mainBorder.g) & 0xFF;
    interfaceCtx->beatingHeartEnv[2] = (u8)(bFactor + mainBorder.b) & 0xFF;

    sHeartsDDPrim[0][0] = ddBorder.r;
    sHeartsDDPrim[0][1] = ddBorder.g;
    sHeartsDDPrim[0][2] = ddBorder.b;

    sHeartsDDEnv[0][0] = ddColor.r;
    sHeartsDDEnv[0][1] = ddColor.g;
    sHeartsDDEnv[0][2] = ddColor.b;

    sHeartsDDPrim[1][0] = ddBorder.r;
    sHeartsDDPrim[1][1] = ddBorder.g;
    sHeartsDDPrim[1][2] = ddBorder.b;

    sHeartsDDEnv[1][0] = ddColor.r;
    sHeartsDDEnv[1][1] = ddColor.g;
    sHeartsDDEnv[1][2] = ddColor.b;

    rFactor = sHeartsDDPrimFactors[ddType][0] * ddFactor;
    gFactor = sHeartsDDPrimFactors[ddType][1] * ddFactor;
    bFactor = sHeartsDDPrimFactors[ddType][2] * ddFactor;

    sBeatingHeartsDDPrim[0] = (u8)(rFactor + ddBorder.r) & 0xFF;
    sBeatingHeartsDDPrim[1] = (u8)(gFactor + ddBorder.g) & 0xFF;
    sBeatingHeartsDDPrim[2] = (u8)(bFactor + ddBorder.b) & 0xFF;

    rFactor = sHeartsDDEnvFactors[ddType][0] * ddFactor;
    gFactor = sHeartsDDEnvFactors[ddType][1] * ddFactor;
    bFactor = sHeartsDDEnvFactors[ddType][2] * ddFactor;

    sBeatingHeartsDDEnv[0] = (u8)(rFactor + ddColor.r) & 0xFF;
    sBeatingHeartsDDEnv[1] = (u8)(gFactor + ddColor.g) & 0xFF;
    sBeatingHeartsDDEnv[2] = (u8)(bFactor + ddColor.b) & 0xFF;
}

s32 func_80078E18(PlayState* play) {
    gSaveContext.health = play->interfaceCtx.unk_226;
    return 1;
}

s32 func_80078E34(PlayState* play) {
    InterfaceContext* interfaceCtx = &play->interfaceCtx;

    interfaceCtx->unk_228 = 0x140;
    interfaceCtx->unk_226 += 0x10;

    if (interfaceCtx->unk_226 >= gSaveContext.health) {
        interfaceCtx->unk_226 = gSaveContext.health;
        return 1;
    }

    return 0;
}

s32 func_80078E84(PlayState* play) {
    InterfaceContext* interfaceCtx = &play->interfaceCtx;

    if (interfaceCtx->unk_228 != 0) {
        interfaceCtx->unk_228--;
    } else {
        interfaceCtx->unk_228 = 0x140;
        interfaceCtx->unk_226 -= 0x10;
        if (interfaceCtx->unk_226 <= 0) {
            interfaceCtx->unk_226 = 0;
            play->damagePlayer(play, -(gSaveContext.health + 1));
            return 1;
        }
    }
    return 0;
}

static void* sHeartTextures[] = {
    gHeartFullTex,         gHeartQuarterTex,      gHeartQuarterTex,      gHeartQuarterTex,
    gHeartQuarterTex,      gHeartQuarterTex,      gHeartHalfTex,         gHeartHalfTex,
    gHeartHalfTex,         gHeartHalfTex,         gHeartHalfTex,         gHeartThreeQuarterTex,
    gHeartThreeQuarterTex, gHeartThreeQuarterTex, gHeartThreeQuarterTex, gHeartThreeQuarterTex,
};

static void* sHeartDDTextures[] = {
    gDefenseHeartFullTex,         gDefenseHeartQuarterTex,      gDefenseHeartQuarterTex,
    gDefenseHeartQuarterTex,      gDefenseHeartQuarterTex,      gDefenseHeartQuarterTex,
    gDefenseHeartHalfTex,         gDefenseHeartHalfTex,         gDefenseHeartHalfTex,
    gDefenseHeartHalfTex,         gDefenseHeartHalfTex,         gDefenseHeartThreeQuarterTex,
    gDefenseHeartThreeQuarterTex, gDefenseHeartThreeQuarterTex, gDefenseHeartThreeQuarterTex,
    gDefenseHeartThreeQuarterTex,
};

s16 getHealthMeterXOffset() {
    s16 X_Margins;
    if (CVarGetInteger(CVAR_COSMETIC("HUD.Hearts.UseMargins"), 0) != 0)
        X_Margins = Left_LM_Margin;
    else
        X_Margins = 0;

    if (CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosType"), 0) != ORIGINAL_LOCATION) {
        if (CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosType"), 0) == ANCHOR_LEFT) {
            return OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosX"), 0) + X_Margins +
                                               70.0f);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosType"), 0) == ANCHOR_RIGHT) {
            X_Margins = Right_LM_Margin;
            return OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosX"), 0) + X_Margins +
                                                70.0f);
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosType"), 0) == ANCHOR_NONE) {
            return CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosX"), 0) + 70.0f;
        } else if (CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosType"), 0) == HIDDEN) {
            return -9999;
        }
    } else {
        return OTRGetDimensionFromLeftEdge(0.0f) + X_Margins;
    }
}

s16 getHealthMeterYOffset() {
    s16 Y_Margins;
    if (CVarGetInteger(CVAR_COSMETIC("HUD.Hearts.UseMargins"), 0) != 0)
        Y_Margins = (Top_LM_Margin * -1);
    else
        Y_Margins = 0;

    f32 HeartsScale = 0.7f;
    if (CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosType"), 0) != ORIGINAL_LOCATION) {
        HeartsScale = CVarGetFloat(CVAR_COSMETIC("HUD.HeartsCount.Scale"), 0.7f);
        return CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosY"), 0) + Y_Margins + (HeartsScale * 15);
    } else {
        return 0.0f + Y_Margins;
    }
}

// #205 — the heart row's PRIM/ENV pair for a given colour set, mirroring the eight
// gDPSetPrimColor/gDPSetEnvColor branches in HealthMeter_Draw. The display-list path pushes those
// straight into the RDP; the native path needs the values, and reading them back off one enum keeps
// the two routes from drifting apart the way a second copy of the branch chain would.
//   0/2 normal, 1 beating, 3 low-health, 4/6 double-defense, 5 DD beating, 7 DD low-health.
static void Zelda3D_HeartColorSet(InterfaceContext* interfaceCtx, s32 set, u32* outPrimRGB, u32* outEnvRGB) {
    s16 pr, pg, pb, er, eg, eb;
    switch (set) {
        case 1:
            pr = interfaceCtx->beatingHeartPrim[0]; pg = interfaceCtx->beatingHeartPrim[1];
            pb = interfaceCtx->beatingHeartPrim[2];
            er = interfaceCtx->beatingHeartEnv[0]; eg = interfaceCtx->beatingHeartEnv[1];
            eb = interfaceCtx->beatingHeartEnv[2];
            break;
        case 3:
            pr = interfaceCtx->heartsPrimR[1]; pg = interfaceCtx->heartsPrimG[1]; pb = interfaceCtx->heartsPrimB[1];
            er = interfaceCtx->heartsEnvR[1]; eg = interfaceCtx->heartsEnvG[1]; eb = interfaceCtx->heartsEnvB[1];
            break;
        case 4:
        case 6:
            pr = sHeartsDDPrim[0][0]; pg = sHeartsDDPrim[0][1]; pb = sHeartsDDPrim[0][2];
            er = sHeartsDDEnv[0][0]; eg = sHeartsDDEnv[0][1]; eb = sHeartsDDEnv[0][2];
            break;
        case 5:
            pr = sBeatingHeartsDDPrim[0]; pg = sBeatingHeartsDDPrim[1]; pb = sBeatingHeartsDDPrim[2];
            er = sBeatingHeartsDDEnv[0]; eg = sBeatingHeartsDDEnv[1]; eb = sBeatingHeartsDDEnv[2];
            break;
        case 7:
            pr = sHeartsDDPrim[1][0]; pg = sHeartsDDPrim[1][1]; pb = sHeartsDDPrim[1][2];
            er = sHeartsDDEnv[1][0]; eg = sHeartsDDEnv[1][1]; eb = sHeartsDDEnv[1][2];
            break;
        default:
            pr = interfaceCtx->heartsPrimR[0]; pg = interfaceCtx->heartsPrimG[0]; pb = interfaceCtx->heartsPrimB[0];
            er = interfaceCtx->heartsEnvR[0]; eg = interfaceCtx->heartsEnvG[0]; eb = interfaceCtx->heartsEnvB[0];
            break;
    }
    *outPrimRGB = ((u32)(u8)pr << 16) | ((u32)(u8)pg << 8) | (u32)(u8)pb;
    *outEnvRGB = ((u32)(u8)er << 16) | ((u32)(u8)eg << 8) | (u32)(u8)eb;
}

// #205 — record one heart as a native quad.
//
// PLACEMENT: the heart matrix translates to ortho (-130 + offsetX, -(-94 + offsetY)) and the HUD
// ortho maps ortho->virtual as (160 + ox, 120 - oy), so the centre is (30 + offsetX, 26 + offsetY) —
// the same derivation the A button uses, and the quad (beatingHeartVtx spans +/-8) is 16 units
// square before `scale`.
//
// COMBINE: normal hearts are (PRIM-ENV)*TEXEL0+ENV = mix(env, prim, t), which is Zelda3D_HudQuadLerp
// directly. Double-defense hearts use the SWAPPED combine (ENV-PRIM)*TEXEL0+PRIM = mix(prim, env, t),
// which is the same operation with the two colours exchanged — but the alpha combine is TEXEL0*PRIM
// in BOTH cases, so the swap must carry the real PRIM alpha, not env's (env alpha is a constant 255
// and would defeat the health-meter fade).
static void Zelda3D_HeartQuad(InterfaceContext* interfaceCtx, const void* tex, int tw, int th, s32 colorSet,
                              s32 swapped, f32 offsetX, f32 offsetY, f32 scale) {
    u32 primRGB, envRGB;
    f32 side;
    Zelda3D_HeartColorSet(interfaceCtx, colorSet, &primRGB, &envRGB);
    side = 16.0f * scale;
    if (swapped) {
        u32 t = primRGB;
        primRGB = envRGB;
        envRGB = t;
    }
    Zelda3D_HudQuadEx(tex, tw, th, 0, 0, tw, th, (30.0f + offsetX) - side / 2.0f,
                      (26.0f + offsetY) - side / 2.0f, side, side,
                      (primRGB << 8) | (u32)(u8)interfaceCtx->healthAlpha, envRGB, ZELDA3D_HUD_LERP);
}

void HealthMeter_Draw(PlayState* play) {
    s32 pad[5];
    void* heartBgImg;
    u32 curColorSet;
    f32 PosX_anchor;
    f32 offsetX;
    f32 offsetY;
    s32 i;
    f32 temp1;
    f32 temp2;
    f32 temp3;
    f32 temp4;
    InterfaceContext* interfaceCtx = &play->interfaceCtx;
    GraphicsContext* gfxCtx = play->state.gfxCtx;
    Vtx* sp154 = interfaceCtx->beatingHeartVtx;
    s32 curHeartFraction = gSaveContext.health % FULL_HEART_HEALTH;
    s16 totalHeartCount = gSaveContext.healthCapacity / FULL_HEART_HEALTH;
    s16 fullHeartCount = gSaveContext.health / FULL_HEART_HEALTH;
    s32 pad2;
    f32 sp144 = interfaceCtx->unk_22A * 0.1f;
    s32 curCombineModeSet = 0;
    u8* curBgImgLoaded = NULL;
    // #205 — the crisp RGBA heart resolved for the tile currently loaded, carried to the native quad
    // path. NULL means the vanilla IA8 heart is in play, in which case the native path stands down
    // (it draws RGBA only) and the display list keeps this element — see nativeHearts below.
    const void* curHeartTex = NULL;
    int curHeartTexW = 0, curHeartTexH = 0;
    s32 ddHeartCountMinusOne = gSaveContext.isDoubleDefenseAcquired ? totalHeartCount - 1 : -1;
    f32 HeartsScale = 0.7f;
    if (CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosType"), 0) != ORIGINAL_LOCATION) {
        HeartsScale = CVarGetFloat(CVAR_COSMETIC("HUD.HeartsCount.Scale"), 0.7f);
    }
    static u32 epoch = 0;
    epoch++;

    OPEN_DISPS(gfxCtx);

    if (!(gSaveContext.health % FULL_HEART_HEALTH)) {
        fullHeartCount--;
    }

    // #31 — when crisp HUD hearts are enabled, the replacement textures are higher-res (e.g. 64x64
    // RGBA) instead of the N64 16x16 IA8. The shared heart quad's texcoords are baked to span 16
    // texels (tc 0..512 == 16.0 in s10.5); rescale the far tc to the replacement's real size so the
    // FULL texture maps onto the quad (same fix as the #32 A-button quad). Probe the FULL heart once;
    // if it decodes we use the crisp set, else fall back to the byte-identical N64 path.
    int s3HeartW = 0, s3HeartH = 0;
    const void* s3HeartProbe = Zelda3D_HudTexEnabled() ? Zelda3D_HeartTex(ZELDA3D_HEART_FULL, &s3HeartW, &s3HeartH) : NULL;
    s32 useSoh3dHearts = (s3HeartProbe != NULL);
    {
        s16 farTcX = useSoh3dHearts ? (s16)(s3HeartW << 5) : 512;
        s16 farTcY = useSoh3dHearts ? (s16)(s3HeartH << 5) : 512;
        sp154[1].v.tc[0] = sp154[3].v.tc[0] = farTcX; // far-x in vtx1 & vtx3
        sp154[2].v.tc[1] = sp154[3].v.tc[1] = farTcY; // far-y in vtx2 & vtx3
    }

    // #205 — the native HUD draws the heart row when it owns the element AND the crisp RGBA hearts
    // are available (the vanilla source is IA8, which the quad path does not decode).
    const s32 nativeHearts = Zelda3D_HudOwns(ZELDA3D_HUD_HEALTH) && useSoh3dHearts;

    curColorSet = -1;
    /*
        s16 X_Margins;
        s16 Y_Margins;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.Hearts.UseMargins"), 0) != 0) {
            X_Margins = Left_LM_Margin;
            Y_Margins = (Top_LM_Margin*-1);
        } else {
            X_Margins = 0;
            Y_Margins = 0;
        }
        s16 PosX_original = OTRGetDimensionFromLeftEdge(0.0f)+X_Margins;
        s16 PosY_original = 0.0f+Y_Margins;
        if (CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosType"), 0) != ORIGINAL_LOCATION) {
            offsetY = CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosY"), 0)+Y_Margins+(HeartsScale*15);
            if (CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosType"), 0) == ANCHOR_LEFT) {
                offsetX = OTRGetDimensionFromLeftEdge(CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosX"),
       0)+X_Margins+70.0f); } else if (CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosType"), 0) == ANCHOR_RIGHT) {
                X_Margins = Right_LM_Margin;
                offsetX = OTRGetDimensionFromRightEdge(CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosX"),
       0)+X_Margins+70.0f); } else if (CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosType"), 0) == ANCHOR_NONE) {
                offsetX = CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosX"), 0)+70.0f;
            } else if (CVarGetInteger(CVAR_COSMETIC("HUD.HeartsCount.PosType"), 0) == HIDDEN) {
                offsetX = -9999;
            }
        } else {
            offsetY = PosY_original;
            offsetX = PosX_original;
        }
    */
    offsetX = PosX_anchor = getHealthMeterXOffset();
    offsetY = getHealthMeterYOffset();

    for (i = 0; i < totalHeartCount; i++) {
        FrameInterpolation_RecordOpenChild("HealthMeter Heart", i);

        if ((ddHeartCountMinusOne < 0) || (i > ddHeartCountMinusOne)) {
            if (i < fullHeartCount) {
                if (curColorSet != 0) {
                    curColorSet = 0;
                    gDPPipeSync(OVERLAY_DISP++);
                    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, interfaceCtx->heartsPrimR[0], interfaceCtx->heartsPrimG[0],
                                    interfaceCtx->heartsPrimB[0], interfaceCtx->healthAlpha);
                    gDPSetEnvColor(OVERLAY_DISP++, interfaceCtx->heartsEnvR[0], interfaceCtx->heartsEnvG[0],
                                   interfaceCtx->heartsEnvB[0], 255);
                }
            } else if (i == fullHeartCount) {
                if (curColorSet != 1) {
                    curColorSet = 1;
                    gDPPipeSync(OVERLAY_DISP++);
                    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, interfaceCtx->beatingHeartPrim[0],
                                    interfaceCtx->beatingHeartPrim[1], interfaceCtx->beatingHeartPrim[2],
                                    interfaceCtx->healthAlpha);
                    gDPSetEnvColor(OVERLAY_DISP++, interfaceCtx->beatingHeartEnv[0], interfaceCtx->beatingHeartEnv[1],
                                   interfaceCtx->beatingHeartEnv[2], 255);
                }
            } else if (i > fullHeartCount) {
                if (curColorSet != 2) {
                    curColorSet = 2;
                    gDPPipeSync(OVERLAY_DISP++);
                    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, interfaceCtx->heartsPrimR[0], interfaceCtx->heartsPrimG[0],
                                    interfaceCtx->heartsPrimB[0], interfaceCtx->healthAlpha);
                    gDPSetEnvColor(OVERLAY_DISP++, interfaceCtx->heartsEnvR[0], interfaceCtx->heartsEnvG[0],
                                   interfaceCtx->heartsEnvB[0], 255);
                }
            } else {
                if (curColorSet != 3) {
                    curColorSet = 3;
                    gDPPipeSync(OVERLAY_DISP++);
                    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, interfaceCtx->heartsPrimR[1], interfaceCtx->heartsPrimG[1],
                                    interfaceCtx->heartsPrimB[1], interfaceCtx->healthAlpha);
                    gDPSetEnvColor(OVERLAY_DISP++, interfaceCtx->heartsEnvR[1], interfaceCtx->heartsEnvG[1],
                                   interfaceCtx->heartsEnvB[1], 255);
                }
            }

            if (i < fullHeartCount) {
                heartBgImg = gHeartFullTex;
            } else if (i == fullHeartCount) {
                heartBgImg = sHeartTextures[curHeartFraction];
            } else {
                heartBgImg = gHeartEmptyTex;
            }
        } else {
            if (i < fullHeartCount) {
                if (curColorSet != 4) {
                    curColorSet = 4;
                    gDPPipeSync(OVERLAY_DISP++);
                    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, sHeartsDDPrim[0][0], sHeartsDDPrim[0][1], sHeartsDDPrim[0][2],
                                    interfaceCtx->healthAlpha);
                    gDPSetEnvColor(OVERLAY_DISP++, sHeartsDDEnv[0][0], sHeartsDDEnv[0][1], sHeartsDDEnv[0][2], 255);
                }
            } else if (i == fullHeartCount) {
                if (curColorSet != 5) {
                    curColorSet = 5;
                    gDPPipeSync(OVERLAY_DISP++);
                    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, sBeatingHeartsDDPrim[0], sBeatingHeartsDDPrim[1],
                                    sBeatingHeartsDDPrim[2], interfaceCtx->healthAlpha);
                    gDPSetEnvColor(OVERLAY_DISP++, sBeatingHeartsDDEnv[0], sBeatingHeartsDDEnv[1],
                                   sBeatingHeartsDDEnv[2], 255);
                }
            } else if (i > fullHeartCount) {
                if (curColorSet != 6) {
                    curColorSet = 6;
                    gDPPipeSync(OVERLAY_DISP++);
                    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, sHeartsDDPrim[0][0], sHeartsDDPrim[0][1], sHeartsDDPrim[0][2],
                                    interfaceCtx->healthAlpha);
                    gDPSetEnvColor(OVERLAY_DISP++, sHeartsDDEnv[0][0], sHeartsDDEnv[0][1], sHeartsDDEnv[0][2], 255);
                }
            } else {
                if (curColorSet != 7) {
                    curColorSet = 7;
                    gDPPipeSync(OVERLAY_DISP++);
                    gDPSetPrimColor(OVERLAY_DISP++, 0, 0, sHeartsDDPrim[1][0], sHeartsDDPrim[1][1], sHeartsDDPrim[1][2],
                                    interfaceCtx->healthAlpha);
                    gDPSetEnvColor(OVERLAY_DISP++, sHeartsDDEnv[1][0], sHeartsDDEnv[1][1], sHeartsDDEnv[1][2], 255);
                }
            }

            if (i < fullHeartCount) {
                heartBgImg = gDefenseHeartFullTex;
            } else if (i == fullHeartCount) {
                heartBgImg = sHeartDDTextures[curHeartFraction];
            } else {
                heartBgImg = gDefenseHeartEmptyTex;
            }
        }

        if (curBgImgLoaded != heartBgImg) {
            curBgImgLoaded = heartBgImg;
            // #31 — swap in the crisp higher-res heart (raw RGBA32 pointer) when enabled; the combine
            // reads TEXEL0.rgb as the PRIM<->ENV lerp so the grayscale heart tints exactly as IA8 did.
            int s3w = 0, s3h = 0;
            const void* s3 = NULL;
            if (useSoh3dHearts) {
                int kind = Zelda3D_HeartKind(heartBgImg);
                if (kind >= 0) {
                    s3 = Zelda3D_HeartTex(kind, &s3w, &s3h);
                }
            }
            curHeartTex = s3;
            curHeartTexW = s3w;
            curHeartTexH = s3h;
            if (s3 != NULL) {
                gDPLoadTextureBlock(OVERLAY_DISP++, s3, G_IM_FMT_RGBA, G_IM_SIZ_32b, s3w, s3h, 0,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
            } else {
                gDPLoadTextureBlock(OVERLAY_DISP++, heartBgImg, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 16, 0,
                                    G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOMASK,
                                    G_TX_NOLOD, G_TX_NOLOD);
            }
        }

        if (i != fullHeartCount) {
            if ((ddHeartCountMinusOne < 0) || (i > ddHeartCountMinusOne)) {
                if (curCombineModeSet != 1) {
                    curCombineModeSet = 1;
                    Gfx_SetupDL_39Overlay(gfxCtx);
                    gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE,
                                      0, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
                }
            } else {
                if (curCombineModeSet != 3) {
                    curCombineModeSet = 3;
                    Gfx_SetupDL_39Overlay(gfxCtx);
                    gDPSetCombineLERP(OVERLAY_DISP++, ENVIRONMENT, PRIMITIVE, TEXEL0, PRIMITIVE, TEXEL0, 0, PRIMITIVE,
                                      0, ENVIRONMENT, PRIMITIVE, TEXEL0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
                }
            }

            temp3 = offsetY;
            temp2 = offsetX;
            temp4 = 1.0f;   // Heart texture size
            temp4 /= 0.68f; // Hearts Scaled size
            temp4 *= 1 << 10;
            temp1 = 8.0f;
            temp1 *= 0.68f;
            /*gSPWideTextureRectangle(OVERLAY_DISP++, (s32)((temp2 - temp1) * 4), (s32)((temp3 - temp1) * 4),
                                (s32)((temp2 + temp1) * 4), (s32)((temp3 + temp1) * 4), G_TX_RENDERTILE, 0, 0,
                                (s32)temp4, (s32)temp4);*/
            Mtx* matrix = Graph_Alloc(gfxCtx, sizeof(Mtx));
            Matrix_SetTranslateScaleMtx2(matrix,
                                         HeartsScale,          // Scale X
                                         HeartsScale,          // Scale Y
                                         HeartsScale,          // Scale Z
                                         -130 + offsetX,       // Pos X
                                         (-94 + offsetY) * -1, // Pos Y
                                         0.0f);                // Pos Z
            if (nativeHearts) {
                Zelda3D_HeartQuad(interfaceCtx, curHeartTex, curHeartTexW, curHeartTexH, curColorSet,
                                  curCombineModeSet == 3, offsetX, offsetY, HeartsScale);
            } else {
                gSPMatrix(OVERLAY_DISP++, matrix, G_MTX_MODELVIEW | G_MTX_LOAD);
                gSPVertex(OVERLAY_DISP++, sp154, 4, 0);
                gSP1Quadrangle(OVERLAY_DISP++, 0, 2, 3, 1, 0);
            }
        } else {
            if ((ddHeartCountMinusOne < 0) || (i > ddHeartCountMinusOne)) {
                if (curCombineModeSet != 2) {
                    curCombineModeSet = 2;
                    Gfx_SetupDL_42Overlay(gfxCtx);
                    gDPSetCombineLERP(OVERLAY_DISP++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE,
                                      0, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, TEXEL0, 0, PRIMITIVE, 0);
                }
            } else {
                if (curCombineModeSet != 4) {
                    curCombineModeSet = 4;
                    Gfx_SetupDL_42Overlay(gfxCtx);
                    gDPSetCombineLERP(OVERLAY_DISP++, ENVIRONMENT, PRIMITIVE, TEXEL0, PRIMITIVE, TEXEL0, 0, PRIMITIVE,
                                      0, ENVIRONMENT, PRIMITIVE, TEXEL0, PRIMITIVE, TEXEL0, 0, PRIMITIVE, 0);
                }
            }

            {
                Mtx* matrix = Graph_Alloc(gfxCtx, sizeof(Mtx));

                if (CVarGetInteger(CVAR_ENHANCEMENT("NoHUDHeartAnimation"), 0)) {
                    Matrix_SetTranslateScaleMtx2(matrix,
                                                 HeartsScale,          // Scale X
                                                 HeartsScale,          // Scale Y
                                                 HeartsScale,          // Scale Z
                                                 -130 + offsetX,       // Pos X
                                                 (-94 + offsetY) * -1, // Pos Y
                                                 0.0f);
                } else {
                    Matrix_SetTranslateScaleMtx2(matrix, HeartsScale + (HeartsScale / 3) - ((HeartsScale / 3) * sp144),
                                                 HeartsScale + (HeartsScale / 3) - ((HeartsScale / 3) * sp144),
                                                 HeartsScale + (HeartsScale / 3) - ((HeartsScale / 3) * sp144),
                                                 -130 + offsetX,       // Pos X
                                                 (-94 + offsetY) * -1, // Pos Y
                                                 0.0f);
                }

                if (nativeHearts) {
                    // The beating heart pulses via its matrix scale; the native quad takes the same
                    // factor so the pulse is preserved rather than flattened to a static heart.
                    f32 beatScale = CVarGetInteger(CVAR_ENHANCEMENT("NoHUDHeartAnimation"), 0)
                                        ? HeartsScale
                                        : HeartsScale + (HeartsScale / 3) - ((HeartsScale / 3) * sp144);
                    Zelda3D_HeartQuad(interfaceCtx, curHeartTex, curHeartTexW, curHeartTexH, curColorSet,
                                      curCombineModeSet == 4, offsetX, offsetY, beatScale);
                } else {
                    gSPMatrix(OVERLAY_DISP++, matrix, G_MTX_MODELVIEW | G_MTX_LOAD);
                    gSPVertex(OVERLAY_DISP++, sp154, 4, 0);
                    gSP1Quadrangle(OVERLAY_DISP++, 0, 2, 3, 1, 0);
                }
            }
        }

        offsetX += 10.0f;
        s32 lineLength = CVarGetInteger(CVAR_COSMETIC("HUD.Hearts.LineLength"), 10);
        if (lineLength != 0 && (i + 1) % lineLength == 0) {
            offsetX = PosX_anchor;
            offsetY += 10.0f;
        }

        FrameInterpolation_RecordCloseChild();
    }

    CLOSE_DISPS(gfxCtx);
}

void HealthMeter_HandleCriticalAlarm(PlayState* play) {
    InterfaceContext* interfaceCtx = &play->interfaceCtx;

    if (interfaceCtx->unk_22C != 0) {
        interfaceCtx->unk_22A--;
        if (interfaceCtx->unk_22A <= 0) {
            interfaceCtx->unk_22A = 0;
            interfaceCtx->unk_22C = 0;
            if (CVarGetInteger(CVAR_AUDIO("LowHpAlarm"), 0) == 0 && !Player_InCsMode(play) &&
                (play->pauseCtx.state == 0) && (play->pauseCtx.debugState == 0) && HealthMeter_IsCritical() &&
                !Play_InCsMode(play)) {
                Sfx_PlaySfxCentered(NA_SE_SY_HITPOINT_ALARM);
            }
        }
    } else {
        interfaceCtx->unk_22A++;
        if (interfaceCtx->unk_22A >= 10) {
            interfaceCtx->unk_22A = 10;
            interfaceCtx->unk_22C = 1;
        }
    }
}

u32 HealthMeter_IsCritical(void) {
    s32 var;

    if (gSaveContext.healthCapacity <= 0x50) {
        var = 0x10;
    } else if (gSaveContext.healthCapacity <= 0xA0) {
        var = 0x18;
    } else if (gSaveContext.healthCapacity <= 0xF0) {
        var = 0x20;
    } else {
        var = 0x2C;
    }

    if (GameInteractor_Should(VB_HEALTH_METER_BE_CRITICAL, var >= gSaveContext.health && gSaveContext.health > 0)) {
        return true;
    } else {
        return false;
    }
}
