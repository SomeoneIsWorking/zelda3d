#include "CosmeticsCatalog.h"
#include "CosmeticsEditor.h"

#include "global.h"
#include "soh/cvar_prefixes.h"
#include "objects/object_link_boy/object_link_boy.h"
#include "objects/object_link_child/object_link_child.h"
#include "objects/object_gi_shield_3/object_gi_shield_3.h"
#include "objects/object_gi_bow/object_gi_bow.h"
#include "objects/object_gi_bracelet/object_gi_bracelet.h"
#include "objects/object_gi_rupy/object_gi_rupy.h"
#include "objects/object_gi_magicpot/object_gi_magicpot.h"
#include "objects/object_gi_gloves/object_gi_gloves.h"
#include "objects/object_gi_hammer/object_gi_hammer.h"
#include "objects/object_gi_sutaru/object_gi_sutaru.h"
#include "objects/object_st/object_st.h"
#include "objects/object_gi_boomerang/object_gi_boomerang.h"
#include "objects/object_gi_liquid/object_gi_liquid.h"
#include "objects/object_gi_hearts/object_gi_hearts.h"
#include "objects/gameplay_keep/gameplay_keep.h"
#include "objects/object_gi_sword_1/object_gi_sword_1.h"
#include "objects/object_gi_longsword/object_gi_longsword.h"
#include "objects/object_gi_clothes/object_gi_clothes.h"
#include "objects/object_gi_bomb_2/object_gi_bomb_2.h"
#include "objects/object_gla/object_gla.h"
#include "objects/object_toki_objects/object_toki_objects.h"
#include "objects/object_gi_pachinko/object_gi_pachinko.h"
#include "objects/object_trap/object_trap.h"
#include "overlays/ovl_Boss_Ganon2/ovl_Boss_Ganon2.h"
#include "objects/object_gjyo_objects/object_gjyo_objects.h"
#include "textures/nintendo_rogo_static/nintendo_rogo_static.h"
#include "objects/object_gi_rabit_mask/object_gi_rabit_mask.h"
#include "overlays/ovl_Magic_Wind/ovl_Magic_Wind.h"
#include "soh/host/item_randomizer_bridge.h"

extern "C" {
extern PlayState* gPlayState;
void ResourceMgr_PatchGfxByName(const char* path, const char* patchName, int index, Gfx instruction);
void ResourceMgr_PatchGfxCopyCommandByName(const char* path, const char* patchName, int destinationIndex,
                                           int sourceIndex);
void ResourceMgr_UnpatchGfxByName(const char* path, const char* patchName);
}

#define PATCH_GFX(path, name, cvar, index, instruction)             \
    if (CVarGetInteger(cvar, 0)) {                                  \
        ResourceMgr_PatchGfxByName(path, name, index, instruction); \
    } else {                                                        \
        ResourceMgr_UnpatchGfxByName(path, name);                   \
    }

#define dgEndGrayscaleAndEndDlistDL "__OTR__helpers/cosmetics/gEndGrayscaleAndEndDlistDL"
static const ALIGN_ASSET(2) char gEndGrayscaleAndEndDlistDL[] = dgEndGrayscaleAndEndDlistDL;

namespace {
auto& cosmeticOptions = CosmeticOptions();
}

void ApplyOrResetCustomGfxPatches(bool manualChange) {
    static CosmeticOption& magicFaroresPrimary = cosmeticOptions.at("Magic.FaroresPrimary");
    if (manualChange || CVarGetInteger(magicFaroresPrimary.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(magicFaroresPrimary.valuesCvar, magicFaroresPrimary.defaultColor);
        PATCH_GFX(sInnerCylinderDL, "Magic_FaroresPrimary1", magicFaroresPrimary.changedCvar, 24,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(sOuterCylinderDL, "Magic_FaroresPrimary2", magicFaroresPrimary.changedCvar, 24,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }

    static CosmeticOption& magicFaroresSecondary = cosmeticOptions.at("Magic.FaroresSecondary");
    if (manualChange || CVarGetInteger(magicFaroresSecondary.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(magicFaroresSecondary.valuesCvar, magicFaroresSecondary.defaultColor);
        PATCH_GFX(sInnerCylinderDL, "Magic_FaroresSecondary1", magicFaroresSecondary.changedCvar, 25,
                  gsDPSetEnvColor(color.r, color.g, color.b, 255));
        PATCH_GFX(sOuterCylinderDL, "Magic_FaroresSecondary2", magicFaroresSecondary.changedCvar, 25,
                  gsDPSetEnvColor(color.r, color.g, color.b, 255));
    }

    static CosmeticOption& linkGoronTunic = cosmeticOptions.at("Link.GoronTunic");
    if (manualChange || CVarGetInteger(linkGoronTunic.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(linkGoronTunic.valuesCvar, linkGoronTunic.defaultColor);
        PATCH_GFX(gGiGoronTunicColorDL, "Link_GoronTunic1", linkGoronTunic.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiGoronCollarColorDL, "Link_GoronTunic2", linkGoronTunic.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gGiGoronTunicColorDL, "Link_GoronTunic3", linkGoronTunic.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gGiGoronCollarColorDL, "Link_GoronTunic4", linkGoronTunic.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
    }

    static CosmeticOption& linkZoraTunic = cosmeticOptions.at("Link.ZoraTunic");
    if (manualChange || CVarGetInteger(linkZoraTunic.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(linkZoraTunic.valuesCvar, linkZoraTunic.defaultColor);
        PATCH_GFX(gGiZoraTunicColorDL, "Link_ZoraTunic1", linkZoraTunic.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiZoraCollarColorDL, "Link_ZoraTunic2", linkZoraTunic.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gGiZoraTunicColorDL, "Link_ZoraTunic3", linkZoraTunic.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gGiZoraCollarColorDL, "Link_ZoraTunic4", linkZoraTunic.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
    }

    static CosmeticOption& linkHair = cosmeticOptions.at("Link.Hair");
    if (manualChange || CVarGetInteger(linkHair.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(linkHair.valuesCvar, linkHair.defaultColor);
        PATCH_GFX(gLinkChildHeadNearDL, "Link_Hair1", linkHair.changedCvar, 10,
                  gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkChildHeadFarDL, "Link_Hair2", linkHair.changedCvar, 10,
                  gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultHeadNearDL, "Link_Hair3", linkHair.changedCvar, 10,
                  gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultHeadFarDL, "Link_Hair4", linkHair.changedCvar, 10,
                  gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));

        if (manualChange) {
            PATCH_GFX(gLinkChildHeadNearDL, "Link_Hair5", linkHair.changedCvar, 46, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildHeadNearDL, "Link_Hair6", linkHair.changedCvar, 54, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildHeadNearDL, "Link_Hair7", linkHair.changedCvar, 136, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildHeadNearDL, "Link_Hair8", linkHair.changedCvar, 162, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildHeadFarDL, "Link_Hair9", linkHair.changedCvar, 101, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildHeadFarDL, "Link_Hair10", linkHair.changedCvar, 118, gsSPGrayscale(false));
            PATCH_GFX(gLinkAdultHeadNearDL, "Link_Hair11", linkHair.changedCvar, 125, gsSPGrayscale(true));
            PATCH_GFX(gLinkAdultHeadNearDL, "Link_Hair12", linkHair.changedCvar, 159, gsSPGrayscale(false));
            PATCH_GFX(gLinkAdultHeadFarDL, "Link_Hair13", linkHair.changedCvar, 102, gsSPGrayscale(true));
            PATCH_GFX(gLinkAdultHeadFarDL, "Link_Hair14", linkHair.changedCvar, 122, gsSPGrayscale(false));
        }
    }

    static CosmeticOption& linkLinen = cosmeticOptions.at("Link.Linen");
    if (manualChange || CVarGetInteger(linkLinen.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(linkLinen.valuesCvar, linkLinen.defaultColor);
        PATCH_GFX(gLinkAdultLeftArmNearDL, "Link_Linen1", linkLinen.changedCvar, 30,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftArmNearDL, "Link_Linen2", linkLinen.changedCvar, 83,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftArmOutNearDL, "Link_Linen3", linkLinen.changedCvar, 25,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftArmFarDL, "Link_Linen4", linkLinen.changedCvar, 30,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftArmFarDL, "Link_Linen5", linkLinen.changedCvar, 70,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightArmFarDL, "Link_Linen6", linkLinen.changedCvar, 30,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightArmFarDL, "Link_Linen7", linkLinen.changedCvar, 70,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightArmNearDL, "Link_Linen8", linkLinen.changedCvar, 30,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftShoulderFarDL, "Link_Linen9", linkLinen.changedCvar, 55,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftShoulderNearDL, "Link_Linen10", linkLinen.changedCvar, 57,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightShoulderNearDL, "Link_Linen11", linkLinen.changedCvar, 57,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightShoulderFarDL, "Link_Linen12", linkLinen.changedCvar, 55,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultTorsoNearDL, "Link_Linen13", linkLinen.changedCvar, 66,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultTorsoFarDL, "Link_Linen14", linkLinen.changedCvar, 57,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightThighNearDL, "Link_Linen15", linkLinen.changedCvar, 53,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftThighNearDL, "Link_Linen16", linkLinen.changedCvar, 53,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightThighFarDL, "Link_Linen17", linkLinen.changedCvar, 54,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftThighFarDL, "Link_Linen18", linkLinen.changedCvar, 54,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightLegNearDL, "Link_Linen19", linkLinen.changedCvar, 30,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftLegNearDL, "Link_Linen20", linkLinen.changedCvar, 30,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightLegFarDL, "Link_Linen21", linkLinen.changedCvar, 30,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftLegFarDL, "Link_Linen22", linkLinen.changedCvar, 30,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));

        if (manualChange) {
            PATCH_GFX(gLinkAdultLeftArmFarDL, "Link_Linen23", linkLinen.changedCvar, 35,
                      gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
            PATCH_GFX(gLinkAdultLeftArmOutNearDL, "Link_Linen24", linkLinen.changedCvar, 45,
                      gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
            PATCH_GFX(gLinkAdultLeftArmNearDL, "Link_Linen25", linkLinen.changedCvar, 40,
                      gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
            PATCH_GFX(gLinkAdultLeftArmFarDL, "Link_Linen26", linkLinen.changedCvar, 77,
                      gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
            PATCH_GFX(gLinkAdultRightArmFarDL, "Link_Linen27", linkLinen.changedCvar, 35,
                      gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
            PATCH_GFX(gLinkAdultRightArmFarDL, "Link_Linen28", linkLinen.changedCvar, 77,
                      gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
            PATCH_GFX(gLinkAdultRightArmNearDL, "Link_Linen29", linkLinen.changedCvar, 42,
                      gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
            PATCH_GFX(gLinkAdultRightLegNearDL, "Link_Linen30", linkLinen.changedCvar, 43,
                      gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
            PATCH_GFX(gLinkAdultLeftLegNearDL, "Link_Linen31", linkLinen.changedCvar, 43,
                      gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
            PATCH_GFX(gLinkAdultRightLegFarDL, "Link_Linen32", linkLinen.changedCvar, 38,
                      gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
            PATCH_GFX(gLinkAdultLeftLegFarDL, "Link_Linen33", linkLinen.changedCvar, 38,
                      gsDPSetPrimColor(0, 0, 255, 255, 255, 255));
        }
    }

    static CosmeticOption& linkBoots = cosmeticOptions.at("Link.Boots");
    if (manualChange || CVarGetInteger(linkBoots.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(linkBoots.valuesCvar, linkBoots.defaultColor);
        PATCH_GFX(gLinkChildRightShinNearDL, "Link_Boots1", linkBoots.changedCvar, 10,
                  gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkChildRightShinFarDL, "Link_Boots2", linkBoots.changedCvar, 10,
                  gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightLegNearDL, "Link_Boots3", linkBoots.changedCvar, 10,
                  gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightLegFarDL, "Link_Boots4", linkBoots.changedCvar, 10,
                  gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));

        if (manualChange) {
            PATCH_GFX(gLinkChildRightShinNearDL, "Link_Boots5", linkBoots.changedCvar, 53, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildRightShinNearDL, "Link_Boots6", linkBoots.changedCvar, 69, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildRightShinFarDL, "Link_Boots7", linkBoots.changedCvar, 52, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildRightShinFarDL, "Link_Boots8", linkBoots.changedCvar, 61, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildLeftShinNearDL, "Link_Boots9", linkBoots.changedCvar, 53, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildLeftShinNearDL, "Link_Boots10", linkBoots.changedCvar, 69, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildLeftShinFarDL, "Link_Boots11", linkBoots.changedCvar, 52, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildLeftShinFarDL, "Link_Boots12", linkBoots.changedCvar, 61, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildRightFootNearDL, "Link_Boots13", linkBoots.changedCvar, 30, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildRightFootFarDL, "Link_Boots14", linkBoots.changedCvar, 30, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildLeftFootNearDL, "Link_Boots15", linkBoots.changedCvar, 30, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildLeftFootFarDL, "Link_Boots16", linkBoots.changedCvar, 30, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildLeftThighNearDL, "Link_Boots17", linkBoots.changedCvar, 10, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildLeftThighFarDL, "Link_Boots18", linkBoots.changedCvar, 10, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildHeadNearDL, "Link_Boots19", linkBoots.changedCvar, 20, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildHeadFarDL, "Link_Boots20", linkBoots.changedCvar, 20, gsSPGrayscale(false));
            PATCH_GFX(gLinkAdultRightLegNearDL, "Link_Boots21", linkBoots.changedCvar, 57, gsSPGrayscale(true));
            PATCH_GFX(gLinkAdultRightLegFarDL, "Link_Boots22", linkBoots.changedCvar, 52, gsSPGrayscale(true));
            PATCH_GFX(gLinkAdultLeftLegNearDL, "Link_Boots23", linkBoots.changedCvar, 57, gsSPGrayscale(true));
            PATCH_GFX(gLinkAdultLeftLegFarDL, "Link_Boots24", linkBoots.changedCvar, 52, gsSPGrayscale(true));
            PATCH_GFX(gLinkAdultLeftThighNearDL, "Link_Boots25", linkBoots.changedCvar, 10, gsSPGrayscale(false));
            PATCH_GFX(gLinkAdultLeftThighFarDL, "Link_Boots26", linkBoots.changedCvar, 10, gsSPGrayscale(false));
            PATCH_GFX(gLinkAdultHeadNearDL, "Link_Boots27", linkBoots.changedCvar, 20, gsSPGrayscale(false));
            PATCH_GFX(gLinkAdultHeadFarDL, "Link_Boots28", linkBoots.changedCvar, 20, gsSPGrayscale(false));
        }
    }

    static CosmeticOption& mirrorShieldBody = cosmeticOptions.at("MirrorShield.Body");
    if (manualChange || CVarGetInteger(mirrorShieldBody.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(mirrorShieldBody.valuesCvar, mirrorShieldBody.defaultColor);
        PATCH_GFX(gGiMirrorShieldDL, "MirrorShield_Body1", mirrorShieldBody.changedCvar, 5,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiMirrorShieldDL, "MirrorShield_Body2", mirrorShieldBody.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathNearDL, "MirrorShield_Body3", mirrorShieldBody.changedCvar, 28,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathFarDL, "MirrorShield_Body4", mirrorShieldBody.changedCvar, 17,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultMirrorShieldAndSheathNearDL, "MirrorShield_Body5", mirrorShieldBody.changedCvar, 28,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultMirrorShieldAndSheathFarDL, "MirrorShield_Body6", mirrorShieldBody.changedCvar, 17,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingMirrorShieldNearDL, "MirrorShield_Body7", mirrorShieldBody.changedCvar, 28,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingMirrorShieldFarDL, "MirrorShield_Body8", mirrorShieldBody.changedCvar, 95,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }
    static CosmeticOption& mirrorShieldMirror = cosmeticOptions.at("MirrorShield.Mirror");
    if (manualChange || CVarGetInteger(mirrorShieldMirror.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(mirrorShieldMirror.valuesCvar, mirrorShieldMirror.defaultColor);
        PATCH_GFX(gGiMirrorShieldDL, "MirrorShield_Mirror1", mirrorShieldMirror.changedCvar, 47,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiMirrorShieldDL, "MirrorShield_Mirror2", mirrorShieldMirror.changedCvar, 48,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathNearDL, "MirrorShield_Mirror3", mirrorShieldMirror.changedCvar,
                  17, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathFarDL, "MirrorShield_Mirror4", mirrorShieldMirror.changedCvar, 33,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultMirrorShieldAndSheathNearDL, "MirrorShield_Mirror5", mirrorShieldMirror.changedCvar, 17,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultMirrorShieldAndSheathFarDL, "MirrorShield_Mirror6", mirrorShieldMirror.changedCvar, 33,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingMirrorShieldNearDL, "MirrorShield_Mirror7", mirrorShieldMirror.changedCvar,
                  17, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingMirrorShieldFarDL, "MirrorShield_Mirror8", mirrorShieldMirror.changedCvar,
                  111, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }
    static CosmeticOption& mirrorShieldEmblem = cosmeticOptions.at("MirrorShield.Emblem");
    if (manualChange || CVarGetInteger(mirrorShieldEmblem.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(mirrorShieldEmblem.valuesCvar, mirrorShieldEmblem.defaultColor);
        PATCH_GFX(gGiMirrorShieldSymbolDL, "MirrorShield_Emblem1", mirrorShieldEmblem.changedCvar, 5,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 140));
        PATCH_GFX(gGiMirrorShieldSymbolDL, "MirrorShield_Emblem2", mirrorShieldEmblem.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathNearDL, "MirrorShield_Emblem3", mirrorShieldEmblem.changedCvar,
                  165, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathFarDL, "MirrorShield_Emblem4", mirrorShieldEmblem.changedCvar,
                  135, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultMirrorShieldAndSheathNearDL, "MirrorShield_Emblem5", mirrorShieldEmblem.changedCvar, 129,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultMirrorShieldAndSheathFarDL, "MirrorShield_Emblem6", mirrorShieldEmblem.changedCvar, 103,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingMirrorShieldNearDL, "MirrorShield_Emblem7", mirrorShieldEmblem.changedCvar,
                  162, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingMirrorShieldFarDL, "MirrorShield_Emblem8", mirrorShieldEmblem.changedCvar,
                  133, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }

    static CosmeticOption& swordsKokiriBlade = cosmeticOptions.at("Swords.KokiriBlade");
    if (manualChange || CVarGetInteger(swordsKokiriBlade.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(swordsKokiriBlade.valuesCvar, swordsKokiriBlade.defaultColor);
        PATCH_GFX(gLinkChildLeftFistAndKokiriSwordNearDL, "Swords_KokiriBlade1", swordsKokiriBlade.changedCvar, 79,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkChildLeftFistAndKokiriSwordFarDL, "Swords_KokiriBlade2", swordsKokiriBlade.changedCvar, 75,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiKokiriSwordDL, "Swords_KokiriBlade3", swordsKokiriBlade.changedCvar, 5,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiKokiriSwordDL, "Swords_KokiriBlade4", swordsKokiriBlade.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 4, color.g / 4, color.b / 4, 255));
    }
    /*
    static CosmeticOption& swordsKokiriHilt = cosmeticOptions.at("Swords.KokiriHilt");
    if (manualChange || CVarGetInteger(swordsKokiriHilt.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(swordsKokiriHilt.valuesCvar, swordsKokiriHilt.defaultColor);
        PATCH_GFX(gLinkChildLeftFistAndKokiriSwordNearDL,         "Swords_KokiriHilt1", swordsKokiriHilt.changedCvar, 4,
    gsDPSetGrayscaleColor(color.r, color.g, color.b, 255)); PATCH_GFX(gLinkChildLeftFistAndKokiriSwordFarDL,
    "Swords_KokiriHilt2",       swordsKokiriHilt.changedCvar,         4, gsDPSetGrayscaleColor(color.r, color.g,
    color.b, 255)); PATCH_GFX(gLinkChildSwordAndSheathNearDL,                 "Swords_KokiriHilt3",
    swordsKokiriHilt.changedCvar,         4, gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkChildSwordAndSheathFarDL,                  "Swords_KokiriHilt4", swordsKokiriHilt.changedCvar, 4,
    gsDPSetGrayscaleColor(color.r, color.g, color.b, 255)); PATCH_GFX(gLinkChildDekuShieldSwordAndSheathNearDL,
    "Swords_KokiriHilt5",       swordsKokiriHilt.changedCvar,         4,  gsDPSetGrayscaleColor(color.r, color.g,
    color.b, 255)); PATCH_GFX(gLinkChildDekuShieldSwordAndSheathFarDL,        "Swords_KokiriHilt6",
    swordsKokiriHilt.changedCvar,         4,  gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkChildHylianShieldSwordAndSheathNearDL,     "Swords_KokiriHilt7", swordsKokiriHilt.changedCvar, 4,
    gsDPSetGrayscaleColor(color.r, color.g, color.b, 255)); PATCH_GFX(gLinkChildHylianShieldSwordAndSheathFarDL,
    "Swords_KokiriHilt8",       swordsKokiriHilt.changedCvar,         4,  gsDPSetGrayscaleColor(color.r, color.g,
    color.b, 255)); PATCH_GFX(gGiKokiriSwordDL,                               "Swords_KokiriHilt9",
    swordsKokiriHilt.changedCvar,        64,  gsDPSetPrimColor(0, 0, MAX(color.r - 50, 0), MAX(color.g - 50, 0),
    MAX(color.b - 50, 0), 255)); PATCH_GFX(gGiKokiriSwordDL,                               "Swords_KokiriHilt10",
    swordsKokiriHilt.changedCvar,        66,  gsDPSetEnvColor(MAX(color.r - 50, 0) / 3, MAX(color.g - 50, 0) / 3,
    MAX(color.b - 50, 0) / 3, 255)); PATCH_GFX(gGiKokiriSwordDL,                               "Swords_KokiriHilt11",
    swordsKokiriHilt.changedCvar,       162,  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiKokiriSwordDL,                               "Swords_KokiriHilt12", swordsKokiriHilt.changedCvar,
    164,  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));

        if (manualChange) {
            PATCH_GFX(gLinkChildLeftFistAndKokiriSwordNearDL,     "Swords_KokiriHilt13", swordsKokiriHilt.changedCvar,
    108, gsSPGrayscale(true)); PATCH_GFX(gLinkChildLeftFistAndKokiriSwordNearDL,     "Swords_KokiriHilt14",
    swordsKokiriHilt.changedCvar,       134, gsSPGrayscale(false)); PATCH_GFX(gLinkChildLeftFistAndKokiriSwordFarDL,
    "Swords_KokiriHilt15",      swordsKokiriHilt.changedCvar,       106, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildLeftFistAndKokiriSwordFarDL,      "Swords_KokiriHilt16", swordsKokiriHilt.changedCvar,
    126, gsSPGrayscale(false)); PATCH_GFX(gLinkChildSwordAndSheathNearDL,             "Swords_KokiriHilt17",
    swordsKokiriHilt.changedCvar,       100, gsSPGrayscale(true)); PATCH_GFX(gLinkChildSwordAndSheathNearDL,
    "Swords_KokiriHilt18",      swordsKokiriHilt.changedCvar,       126, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildSwordAndSheathNearDL,             "Swords_KokiriHilt19", swordsKokiriHilt.changedCvar,
    128, gsSPEndDisplayList()); PATCH_GFX(gLinkChildSwordAndSheathFarDL,              "Swords_KokiriHilt20",
    swordsKokiriHilt.changedCvar,        98, gsSPGrayscale(true)); PATCH_GFX(gLinkChildSwordAndSheathFarDL,
    "Swords_KokiriHilt21",      swordsKokiriHilt.changedCvar,       118, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildSwordAndSheathFarDL,              "Swords_KokiriHilt22", swordsKokiriHilt.changedCvar,
    120, gsSPEndDisplayList()); PATCH_GFX(gLinkChildDekuShieldSwordAndSheathNearDL,   "Swords_KokiriHilt23",
    swordsKokiriHilt.changedCvar,       166, gsSPGrayscale(true)); PATCH_GFX(gLinkChildDekuShieldSwordAndSheathNearDL,
    "Swords_KokiriHilt24",      swordsKokiriHilt.changedCvar,       192, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildDekuShieldSwordAndSheathNearDL,   "Swords_KokiriHilt25", swordsKokiriHilt.changedCvar,
    194, gsSPEndDisplayList()); PATCH_GFX(gLinkChildDekuShieldSwordAndSheathFarDL,    "Swords_KokiriHilt26",
    swordsKokiriHilt.changedCvar,       156, gsSPGrayscale(true)); PATCH_GFX(gLinkChildDekuShieldSwordAndSheathFarDL,
    "Swords_KokiriHilt27",      swordsKokiriHilt.changedCvar,       176, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildDekuShieldSwordAndSheathFarDL,    "Swords_KokiriHilt28", swordsKokiriHilt.changedCvar,
    178, gsSPEndDisplayList()); PATCH_GFX(gLinkChildHylianShieldSwordAndSheathNearDL, "Swords_KokiriHilt29",
    swordsKokiriHilt.changedCvar,       162, gsSPGrayscale(true)); PATCH_GFX(gLinkChildHylianShieldSwordAndSheathNearDL,
    "Swords_KokiriHilt30",      swordsKokiriHilt.changedCvar,       188, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildHylianShieldSwordAndSheathNearDL, "Swords_KokiriHilt31", swordsKokiriHilt.changedCvar,
    190, gsSPEndDisplayList()); PATCH_GFX(gLinkChildHylianShieldSwordAndSheathFarDL,  "Swords_KokiriHilt32",
    swordsKokiriHilt.changedCvar,        98, gsSPGrayscale(true)); PATCH_GFX(gLinkChildHylianShieldSwordAndSheathFarDL,
    "Swords_KokiriHilt33",      swordsKokiriHilt.changedCvar,       118, gsSPGrayscale(false));
        }
    }
    */
    static CosmeticOption& swordsMasterBlade = cosmeticOptions.at("Swords.MasterBlade");
    if (manualChange || CVarGetInteger(swordsMasterBlade.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(swordsMasterBlade.valuesCvar, swordsMasterBlade.defaultColor);
        PATCH_GFX(gLinkAdultLeftHandHoldingMasterSwordFarDL, "Swords_MasterBlade1", swordsMasterBlade.changedCvar, 60,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftHandHoldingMasterSwordNearDL, "Swords_MasterBlade2", swordsMasterBlade.changedCvar, 17,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(object_toki_objects_DL_001BD0, "Swords_MasterBlade3", swordsMasterBlade.changedCvar, 13,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(object_toki_objects_DL_001BD0, "Swords_MasterBlade4", swordsMasterBlade.changedCvar, 14,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gGanonMasterSwordDL, "Swords_MasterBlade5", swordsMasterBlade.changedCvar, 13,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGanonMasterSwordDL, "Swords_MasterBlade6", swordsMasterBlade.changedCvar, 14,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
    }
    /*
    static CosmeticOption& swordsMasterHilt = cosmeticOptions.at("Swords.MasterHilt");
    if (manualChange || CVarGetInteger(swordsMasterHilt.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(swordsMasterHilt.valuesCvar, swordsMasterHilt.defaultColor);
        PATCH_GFX(gLinkAdultLeftHandHoldingMasterSwordNearDL,     "Swords_MasterHilt1", swordsMasterHilt.changedCvar,
    20, gsDPSetGrayscaleColor(color.r, color.g, color.b, 255)); PATCH_GFX(gLinkAdultLeftHandHoldingMasterSwordFarDL,
    "Swords_MasterHilt2",       swordsMasterHilt.changedCvar,        20, gsDPSetGrayscaleColor(color.r, color.g,
    color.b, 255)); PATCH_GFX(object_toki_objects_DL_001BD0,                  "Swords_MasterHilt3",
    swordsMasterHilt.changedCvar,        16, gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultMasterSwordAndSheathNearDL,           "Swords_MasterHilt4", swordsMasterHilt.changedCvar, 4,
    gsDPSetGrayscaleColor(color.r, color.g, color.b, 255)); PATCH_GFX(gLinkAdultMasterSwordAndSheathFarDL,
    "Swords_MasterHilt5",       swordsMasterHilt.changedCvar,         4, gsDPSetGrayscaleColor(color.r, color.g,
    color.b, 255)); PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathNearDL,     "Swords_MasterHilt6",
    swordsMasterHilt.changedCvar,         4, gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathFarDL,      "Swords_MasterHilt7", swordsMasterHilt.changedCvar, 4,
    gsDPSetGrayscaleColor(color.r, color.g, color.b, 255)); PATCH_GFX(gLinkAdultHylianShieldSwordAndSheathNearDL,
    "Swords_MasterHilt8",       swordsMasterHilt.changedCvar,         4, gsDPSetGrayscaleColor(color.r, color.g,
    color.b, 255)); PATCH_GFX(gLinkAdultHylianShieldSwordAndSheathFarDL,      "Swords_MasterHilt9",
    swordsMasterHilt.changedCvar,         4, gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gGanonMasterSwordDL,                            "Swords_MasterHilt10", swordsMasterHilt.changedCvar,
    16, gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));

        if (manualChange) {
            PATCH_GFX(gLinkAdultMasterSwordAndSheathFarDL,        "Swords_MasterHilt11", swordsMasterHilt.changedCvar,
    38, gsSPGrayscale(true)); PATCH_GFX(gLinkAdultMasterSwordAndSheathFarDL,        "Swords_MasterHilt12",
    swordsMasterHilt.changedCvar,        64, gsSPGrayscale(false)); PATCH_GFX(gLinkAdultMasterSwordAndSheathFarDL,
    "Swords_MasterHilt13",      swordsMasterHilt.changedCvar,       106, gsSPGrayscale(true));
            PATCH_GFX(gLinkAdultMasterSwordAndSheathFarDL,        "Swords_MasterHilt14", swordsMasterHilt.changedCvar,
    120, gsSPGrayscale(false)); PATCH_GFX(gLinkAdultMasterSwordAndSheathNearDL,       "Swords_MasterHilt15",
    swordsMasterHilt.changedCvar,       104, gsSPGrayscale(true)); PATCH_GFX(gLinkAdultMasterSwordAndSheathNearDL,
    "Swords_MasterHilt16",      swordsMasterHilt.changedCvar,       182, gsSPGrayscale(false));
            PATCH_GFX(gLinkAdultMasterSwordAndSheathNearDL,       "Swords_MasterHilt17", swordsMasterHilt.changedCvar,
    184, gsSPEndDisplayList()); PATCH_GFX(gLinkAdultHylianShieldSwordAndSheathFarDL,  "Swords_MasterHilt18",
    swordsMasterHilt.changedCvar,        80, gsSPGrayscale(true)); PATCH_GFX(gLinkAdultHylianShieldSwordAndSheathFarDL,
    "Swords_MasterHilt19",      swordsMasterHilt.changedCvar,        94, gsSPGrayscale(false));
            PATCH_GFX(gLinkAdultHylianShieldSwordAndSheathFarDL,  "Swords_MasterHilt20", swordsMasterHilt.changedCvar,
    162, gsSPGrayscale(true)); PATCH_GFX(gLinkAdultHylianShieldSwordAndSheathFarDL,  "Swords_MasterHilt21",
    swordsMasterHilt.changedCvar,       180, gsSPGrayscale(false));
            PATCH_GFX(gLinkAdultHylianShieldSwordAndSheathNearDL, "Swords_MasterHilt22", swordsMasterHilt.changedCvar,
    154, gsSPGrayscale(true)); PATCH_GFX(gLinkAdultHylianShieldSwordAndSheathNearDL, "Swords_MasterHilt23",
    swordsMasterHilt.changedCvar,       232, gsSPGrayscale(false)); PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathFarDL,
    "Swords_MasterHilt24",      swordsMasterHilt.changedCvar,       112, gsSPGrayscale(true));
            PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathFarDL,  "Swords_MasterHilt25", swordsMasterHilt.changedCvar,
    130, gsSPGrayscale(false)); PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathFarDL,  "Swords_MasterHilt26",
    swordsMasterHilt.changedCvar,       172, gsSPGrayscale(true)); PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathFarDL,
    "Swords_MasterHilt27",      swordsMasterHilt.changedCvar,       186, gsSPGrayscale(false));
            PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathNearDL, "Swords_MasterHilt28", swordsMasterHilt.changedCvar,
    220, gsSPGrayscale(true)); PATCH_GFX(gLinkAdultMirrorShieldSwordAndSheathNearDL, "Swords_MasterHilt29",
    swordsMasterHilt.changedCvar,       298, gsSPGrayscale(false)); PATCH_GFX(gLinkAdultLeftHandHoldingMasterSwordFarDL,
    "Swords_MasterHilt30",      swordsMasterHilt.changedCvar,        38, gsSPGrayscale(true));
            PATCH_GFX(gLinkAdultLeftHandHoldingMasterSwordFarDL,  "Swords_MasterHilt31", swordsMasterHilt.changedCvar,
    112, gsSPGrayscale(false)); PATCH_GFX(gLinkAdultLeftHandHoldingMasterSwordNearDL, "Swords_MasterHilt32",
    swordsMasterHilt.changedCvar,        86, gsSPGrayscale(true)); PATCH_GFX(gLinkAdultLeftHandHoldingMasterSwordNearDL,
    "Swords_MasterHilt33",      swordsMasterHilt.changedCvar,       208, gsSPGrayscale(false));
            PATCH_GFX(object_toki_objects_DL_001BD0,              "Swords_MasterHilt34", swordsMasterHilt.changedCvar,
    112, gsSPGrayscale(true)); PATCH_GFX(object_toki_objects_DL_001BD0,              "Swords_MasterHilt35",
    swordsMasterHilt.changedCvar,       278, gsSPGrayscale(false)); PATCH_GFX(object_toki_objects_DL_001BD0,
    "Swords_MasterHilt36",      swordsMasterHilt.changedCvar,       280, gsSPEndDisplayList());
            PATCH_GFX(gGanonMasterSwordDL,                        "Swords_MasterHilt37", swordsMasterHilt.changedCvar,
    112, gsSPGrayscale(true)); PATCH_GFX(gGanonMasterSwordDL,                        "Swords_MasterHilt38",
    swordsMasterHilt.changedCvar,       278, gsSPGrayscale(false)); PATCH_GFX(gGanonMasterSwordDL,
    "Swords_MasterHilt39",      swordsMasterHilt.changedCvar,       280, gsSPEndDisplayList());
        }
    }
    */
    static CosmeticOption& swordsBiggoronBlade = cosmeticOptions.at("Swords.BiggoronBlade");
    if (manualChange || CVarGetInteger(swordsBiggoronBlade.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(swordsBiggoronBlade.valuesCvar, swordsBiggoronBlade.defaultColor);
        PATCH_GFX(gLinkAdultLeftHandHoldingBgsFarDL, "Swords_BiggoronBlade1", swordsBiggoronBlade.changedCvar, 108,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftHandHoldingBgsNearDL, "Swords_BiggoronBlade2", swordsBiggoronBlade.changedCvar, 63,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBiggoronSwordDL, "Swords_BiggoronBlade3", swordsBiggoronBlade.changedCvar, 5,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBiggoronSwordDL, "Swords_BiggoronBlade4", swordsBiggoronBlade.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
    }
    /*
    static CosmeticOption& swordsBiggoronHilt = cosmeticOptions.at("Swords.BiggoronHilt");
    if (manualChange || CVarGetInteger(swordsBiggoronHilt.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(swordsBiggoronHilt.valuesCvar, swordsBiggoronHilt.defaultColor);
        PATCH_GFX(gLinkAdultLeftHandHoldingBgsNearDL,             "Swords_BiggoronHilt1",
    swordsBiggoronHilt.changedCvar,      20, gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftHandHoldingBgsFarDL,              "Swords_BiggoronHilt2",
    swordsBiggoronHilt.changedCvar,      20, gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBiggoronSwordDL,                             "Swords_BiggoronHilt3",
    swordsBiggoronHilt.changedCvar,      74, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBiggoronSwordDL,                             "Swords_BiggoronHilt4",
    swordsBiggoronHilt.changedCvar,      76, gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gGiBiggoronSwordDL,                             "Swords_BiggoronHilt5",
    swordsBiggoronHilt.changedCvar,     154, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBiggoronSwordDL,                             "Swords_BiggoronHilt6",
    swordsBiggoronHilt.changedCvar,     156, gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));

        if (manualChange) {
            PATCH_GFX(gLinkAdultLeftHandHoldingBgsFarDL,          "Swords_BiggoronHilt7",
    swordsBiggoronHilt.changedCvar,     278, gsSPGrayscale(true)); PATCH_GFX(gLinkAdultLeftHandHoldingBgsFarDL,
    "Swords_BiggoronHilt8",     swordsBiggoronHilt.changedCvar,     332, gsSPGrayscale(false));
            PATCH_GFX(gLinkAdultLeftHandHoldingBgsFarDL,          "Swords_BiggoronHilt9",
    swordsBiggoronHilt.changedCvar,     334, gsSPEndDisplayList()); PATCH_GFX(gLinkAdultLeftHandHoldingBgsNearDL,
    "Swords_BiggoronHilt10",    swordsBiggoronHilt.changedCvar,      38, gsSPGrayscale(true));
            PATCH_GFX(gLinkAdultLeftHandHoldingBgsNearDL,         "Swords_BiggoronHilt11",
    swordsBiggoronHilt.changedCvar,     118, gsSPGrayscale(false));
        }
    }
    */
    static CosmeticOption& glovesGoronBracelet = cosmeticOptions.at("Gloves.GoronBracelet");
    if (manualChange || CVarGetInteger(glovesGoronBracelet.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(glovesGoronBracelet.valuesCvar, glovesGoronBracelet.defaultColor);
        PATCH_GFX(gGiGoronBraceletDL, "Gloves_GoronBracelet1", glovesGoronBracelet.changedCvar, 5,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiGoronBraceletDL, "Gloves_GoronBracelet2", glovesGoronBracelet.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gLinkChildGoronBraceletDL, "Gloves_GoronBracelet3", glovesGoronBracelet.changedCvar, 3,
                  gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));

        if (manualChange) {
            PATCH_GFX(gLinkChildGoronBraceletDL, "Gloves_GoronBracelet4", glovesGoronBracelet.changedCvar, 11,
                      gsSPGrayscale(true));
            PATCH_GFX(gLinkChildGoronBraceletDL, "Gloves_GoronBracelet5", glovesGoronBracelet.changedCvar, 39,
                      gsSPGrayscale(false));
        }
    }
    static CosmeticOption& glovesSilverGauntlets = cosmeticOptions.at("Gloves.SilverGauntlets");
    if (manualChange || CVarGetInteger(glovesSilverGauntlets.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(glovesSilverGauntlets.valuesCvar, glovesSilverGauntlets.defaultColor);
        PATCH_GFX(gGiSilverGauntletsColorDL, "Gloves_SilverGauntlets1", glovesSilverGauntlets.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiSilverGauntletsColorDL, "Gloves_SilverGauntlets2", glovesSilverGauntlets.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
    }
    static CosmeticOption& glovesGoldenGauntlets = cosmeticOptions.at("Gloves.GoldenGauntlets");
    if (manualChange || CVarGetInteger(glovesGoldenGauntlets.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(glovesGoldenGauntlets.valuesCvar, glovesGoldenGauntlets.defaultColor);
        PATCH_GFX(gGiGoldenGauntletsColorDL, "Gloves_GoldenGauntlets1", glovesGoldenGauntlets.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiGoldenGauntletsColorDL, "Gloves_GoldenGauntlets2", glovesGoldenGauntlets.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
    }
    static CosmeticOption& glovesGauntletsGem = cosmeticOptions.at("Gloves.GauntletsGem");
    if (manualChange || CVarGetInteger(glovesGauntletsGem.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(glovesGauntletsGem.valuesCvar, glovesGauntletsGem.defaultColor);
        PATCH_GFX(gGiGauntletsDL, "Gloves_GauntletsGem1", glovesGauntletsGem.changedCvar, 84,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiGauntletsDL, "Gloves_GauntletsGem2", glovesGauntletsGem.changedCvar, 85,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gLinkAdultLeftGauntletPlate2DL, "Gloves_GauntletsGem3", glovesGauntletsGem.changedCvar, 42,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightGauntletPlate2DL, "Gloves_GauntletsGem4", glovesGauntletsGem.changedCvar, 42,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftGauntletPlate3DL, "Gloves_GauntletsGem5", glovesGauntletsGem.changedCvar, 42,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightGauntletPlate3DL, "Gloves_GauntletsGem6", glovesGauntletsGem.changedCvar, 42,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }

    static CosmeticOption& equipmentBoomerangBody = cosmeticOptions.at("Equipment.BoomerangBody");
    if (manualChange || CVarGetInteger(equipmentBoomerangBody.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentBoomerangBody.valuesCvar, equipmentBoomerangBody.defaultColor);
        PATCH_GFX(gGiBoomerangDL, "Equipment_BoomerangBody1", equipmentBoomerangBody.changedCvar, 5,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBoomerangDL, "Equipment_BoomerangBody2", equipmentBoomerangBody.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gLinkChildLeftFistAndBoomerangNearDL, "Equipment_BoomerangBody3", equipmentBoomerangBody.changedCvar,
                  34, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkChildLeftFistAndBoomerangFarDL, "Equipment_BoomerangBody4", equipmentBoomerangBody.changedCvar,
                  9, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gBoomerangDL, "Equipment_BoomerangBody5", equipmentBoomerangBody.changedCvar, 39,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }
    static CosmeticOption& equipmentBoomerangGem = cosmeticOptions.at("Equipment.BoomerangGem");
    if (manualChange || CVarGetInteger(equipmentBoomerangGem.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentBoomerangGem.valuesCvar, equipmentBoomerangGem.defaultColor);
        PATCH_GFX(gGiBoomerangDL, "Equipment_BoomerangGem1", equipmentBoomerangGem.changedCvar, 84,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBoomerangDL, "Equipment_BoomerangGem2", equipmentBoomerangGem.changedCvar, 85,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gLinkChildLeftFistAndBoomerangNearDL, "Equipment_BoomerangGem3", equipmentBoomerangGem.changedCvar,
                  16, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gBoomerangDL, "Equipment_BoomerangGem4", equipmentBoomerangGem.changedCvar, 23,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        // There appears to be no gem rendered on the far LOD variant, not sure if this is an SOH bug or what.
        // PATCH_GFX(gLinkChildLeftFistAndBoomerangFarDL,  "Equipment_BoomerangGem5", equipmentBoomerangGem.changedCvar,
        // 32, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }
    /*
    static CosmeticOption& equipmentSlingshotBody = cosmeticOptions.at("Equipment.SlingshotBody");
    if (manualChange || CVarGetInteger(equipmentSlingshotBody.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentSlingshotBody.valuesCvar, equipmentSlingshotBody.defaultColor);
        PATCH_GFX(gGiSlingshotDL,                                 "Equipment_SlingshotBody1",
    equipmentSlingshotBody.changedCvar,  10, gsDPSetPrimColor(0, 0, MAX(color.r - 100, 0), MAX(color.g - 100, 0),
    MAX(color.b - 100, 0), 255)); PATCH_GFX(gGiSlingshotDL,                                 "Equipment_SlingshotBody2",
    equipmentSlingshotBody.changedCvar,  12, gsDPSetEnvColor(MAX(color.r - 100, 0) / 3, MAX(color.g - 100, 0) / 3,
    MAX(color.b - 100, 0) / 3, 255)); PATCH_GFX(gGiSlingshotDL, "Equipment_SlingshotBody3",
    equipmentSlingshotBody.changedCvar,  74, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiSlingshotDL,                                 "Equipment_SlingshotBody4",
    equipmentSlingshotBody.changedCvar,  76, gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gGiSlingshotDL,                                 "Equipment_SlingshotBody5",
    equipmentSlingshotBody.changedCvar, 128, gsDPSetPrimColor(0, 0, MAX(color.r - 100, 0), MAX(color.g - 100, 0),
    MAX(color.b - 100, 0), 255)); PATCH_GFX(gGiSlingshotDL,                                 "Equipment_SlingshotBody6",
    equipmentSlingshotBody.changedCvar, 130, gsDPSetEnvColor(MAX(color.r - 100, 0) / 3, MAX(color.g - 100, 0) / 3,
    MAX(color.b - 100, 0) / 3, 255)); PATCH_GFX(gLinkChildRightArmStretchedSlingshotDL, "Equipment_SlingshotBody7",
    equipmentSlingshotBody.changedCvar,   4, gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkChildRightHandHoldingSlingshotNearDL,      "Equipment_SlingshotBody8",
    equipmentSlingshotBody.changedCvar,   4, gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkChildRightHandHoldingSlingshotFarDL,       "Equipment_SlingshotBody9",
    equipmentSlingshotBody.changedCvar,   4, gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));

        if (manualChange) {
            PATCH_GFX(gLinkChildRightArmStretchedSlingshotDL,
    "Equipment_SlingshotBody10",equipmentSlingshotBody.changedCvar,  20, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildRightArmStretchedSlingshotDL,
    "Equipment_SlingshotBody11",equipmentSlingshotBody.changedCvar,  74, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildRightHandHoldingSlingshotFarDL,
    "Equipment_SlingshotBody12",equipmentSlingshotBody.changedCvar,  20, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildRightHandHoldingSlingshotFarDL,
    "Equipment_SlingshotBody13",equipmentSlingshotBody.changedCvar,  66, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildRightHandHoldingSlingshotNearDL,
    "Equipment_SlingshotBody14",equipmentSlingshotBody.changedCvar,  96, gsSPGrayscale(true));
            PATCH_GFX(gLinkChildRightHandHoldingSlingshotNearDL,
    "Equipment_SlingshotBody15",equipmentSlingshotBody.changedCvar, 136, gsSPGrayscale(false));
            PATCH_GFX(gLinkChildRightHandHoldingSlingshotNearDL,
    "Equipment_SlingshotBody16",equipmentSlingshotBody.changedCvar, 138, gsSPEndDisplayList());
        }
    }
    */
    static CosmeticOption& equipmentSlingshotString = cosmeticOptions.at("Equipment.SlingshotString");
    if (manualChange || CVarGetInteger(equipmentSlingshotString.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentSlingshotString.valuesCvar, equipmentSlingshotString.defaultColor);
        PATCH_GFX(gGiSlingshotDL, "Equipment_SlingshotString1", equipmentSlingshotString.changedCvar, 75,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiSlingshotDL, "Equipment_SlingshotString2", equipmentSlingshotString.changedCvar, 76,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gLinkChildSlingshotStringDL, "Equipment_SlingshotString3", equipmentSlingshotString.changedCvar, 9,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }

    static CosmeticOption& equipmentBowTips = cosmeticOptions.at("Equipment.BowTips");
    if (manualChange || CVarGetInteger(equipmentBowTips.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentBowTips.valuesCvar, equipmentBowTips.defaultColor);
        PATCH_GFX(gGiBowDL, "Equipment_BowTips1", equipmentBowTips.changedCvar, 86,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBowDL, "Equipment_BowTips2", equipmentBowTips.changedCvar, 87,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingBowFirstPersonDL, "Equipment_BowTips3", equipmentBowTips.changedCvar, 34,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingBowNearDL, "Equipment_BowTips4", equipmentBowTips.changedCvar, 26,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingBowFarDL, "Equipment_BowTips5", equipmentBowTips.changedCvar, 25,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }
    static CosmeticOption& equipmentBowString = cosmeticOptions.at("Equipment.BowString");
    if (manualChange || CVarGetInteger(equipmentBowString.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentBowString.valuesCvar, equipmentBowString.defaultColor);
        PATCH_GFX(gGiBowDL, "Equipment_BowString1", equipmentBowString.changedCvar, 105,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBowDL, "Equipment_BowString2", equipmentBowString.changedCvar, 106,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gLinkAdultBowStringDL, "Equipment_BowString3", equipmentBowString.changedCvar, 9,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }
    static CosmeticOption& equipmentBowBody = cosmeticOptions.at("Equipment.BowBody");
    if (manualChange || CVarGetInteger(equipmentBowBody.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentBowBody.valuesCvar, equipmentBowBody.defaultColor);
        PATCH_GFX(gGiBowDL, "Equipment_BowBody1", equipmentBowBody.changedCvar, 5,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBowDL, "Equipment_BowBody2", equipmentBowBody.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingBowFirstPersonDL, "Equipment_BowBody3", equipmentBowBody.changedCvar, 42,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingBowNearDL, "Equipment_BowBody4", equipmentBowBody.changedCvar, 33,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingBowFarDL, "Equipment_BowBody5", equipmentBowBody.changedCvar, 31,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }
    static CosmeticOption& equipmentBowHandle = cosmeticOptions.at("Equipment.BowHandle");
    if (manualChange || CVarGetInteger(equipmentBowHandle.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentBowHandle.valuesCvar, equipmentBowHandle.defaultColor);
        PATCH_GFX(gGiBowDL, "Equipment_BowHandle1", equipmentBowHandle.changedCvar, 51,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBowDL, "Equipment_BowHandle2", equipmentBowHandle.changedCvar, 52,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingBowFirstPersonDL, "Equipment_BowHandle3", equipmentBowHandle.changedCvar,
                  18, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingBowNearDL, "Equipment_BowHandle4", equipmentBowHandle.changedCvar, 18,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultRightHandHoldingBowFarDL, "Equipment_BowHandle5", equipmentBowHandle.changedCvar, 18,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }

    static CosmeticOption& equipmentHammerHead = cosmeticOptions.at("Equipment.HammerHead");
    if (manualChange || CVarGetInteger(equipmentHammerHead.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentHammerHead.valuesCvar, equipmentHammerHead.defaultColor);
        PATCH_GFX(gGiHammerDL, "Equipment_HammerHead1", equipmentHammerHead.changedCvar, 5,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiHammerDL, "Equipment_HammerHead2", equipmentHammerHead.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 5, color.g / 5, color.b / 5, 255));
        PATCH_GFX(gGiHammerDL, "Equipment_HammerHead3", equipmentHammerHead.changedCvar, 68,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiHammerDL, "Equipment_HammerHead4", equipmentHammerHead.changedCvar, 69,
                  gsDPSetEnvColor(color.r / 5, color.g / 5, color.b / 5, 255));
        PATCH_GFX(gLinkAdultLeftHandHoldingHammerNearDL, "Equipment_HammerHead5", equipmentHammerHead.changedCvar, 38,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftHandHoldingHammerFarDL, "Equipment_HammerHead6", equipmentHammerHead.changedCvar, 38,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }
    static CosmeticOption& equipmentHammerHandle = cosmeticOptions.at("Equipment.HammerHandle");
    if (manualChange || CVarGetInteger(equipmentHammerHandle.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentHammerHandle.valuesCvar, equipmentHammerHandle.defaultColor);
        PATCH_GFX(gGiHammerDL, "Equipment_HammerHandle1", equipmentHammerHandle.changedCvar, 84,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiHammerDL, "Equipment_HammerHandle2", equipmentHammerHandle.changedCvar, 85,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gLinkAdultLeftHandHoldingHammerNearDL, "Equipment_HammerHandle5", equipmentHammerHandle.changedCvar,
                  18, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gLinkAdultLeftHandHoldingHammerFarDL, "Equipment_HammerHandle6", equipmentHammerHandle.changedCvar,
                  18, gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }

    static CosmeticOption& equipmentHookshotChain = cosmeticOptions.at("Equipment.HookshotChain");
    if (manualChange || CVarGetInteger(equipmentHookshotChain.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentHookshotChain.valuesCvar, equipmentHookshotChain.defaultColor);
        PATCH_GFX(gLinkAdultHookshotChainDL, "Equipment_HookshotChain1", equipmentHookshotChain.changedCvar, 17,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }

    static CosmeticOption& equipmentChuFace = cosmeticOptions.at("Equipment.ChuFace");
    if (manualChange || CVarGetInteger(equipmentChuFace.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentChuFace.valuesCvar, equipmentChuFace.defaultColor);
        PATCH_GFX(gGiBombchuDL, "Equipment_ChuFace1", equipmentChuFace.changedCvar, 5,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBombchuDL, "Equipment_ChuFace2", equipmentChuFace.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gBombchuDL, "Equipment_ChuFace3", equipmentChuFace.changedCvar, 2,
                  gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));

        if (manualChange) {
            PATCH_GFX(gBombchuDL, "Equipment_ChuFace4", equipmentChuFace.changedCvar, 10, gsSPGrayscale(true));
            PATCH_GFX(gBombchuDL, "Equipment_ChuFace5", equipmentChuFace.changedCvar, 27, gsSPGrayscale(false));
        }
    }
    static CosmeticOption& equipmentChuBody = cosmeticOptions.at("Equipment.ChuBody");
    if (manualChange || CVarGetInteger(equipmentChuBody.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentChuBody.valuesCvar, equipmentChuBody.defaultColor);
        PATCH_GFX(gGiBombchuDL, "Equipment_ChuBody1", equipmentChuBody.changedCvar, 39,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBombchuDL, "Equipment_ChuBody2", equipmentChuBody.changedCvar, 40,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gGiBombchuDL, "Equipment_ChuBody3", equipmentChuBody.changedCvar, 60,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBombchuDL, "Equipment_ChuBody4", equipmentChuBody.changedCvar, 61,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gBombchuDL, "Equipment_ChuBody5", equipmentChuBody.changedCvar, 46,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }

    static CosmeticOption& equipmentBunnyHood = cosmeticOptions.at("Equipment.BunnyHood");
    if (manualChange || CVarGetInteger(equipmentBunnyHood.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(equipmentBunnyHood.valuesCvar, equipmentBunnyHood.defaultColor);
        PATCH_GFX(gGiBunnyHoodDL, "Equipment_BunnyHood1", equipmentBunnyHood.changedCvar, 5,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBunnyHoodDL, "Equipment_BunnyHood2", equipmentBunnyHood.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gGiBunnyHoodDL, "Equipment_BunnyHood3", equipmentBunnyHood.changedCvar, 83,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBunnyHoodDL, "Equipment_BunnyHood4", equipmentBunnyHood.changedCvar, 84,
                  gsDPSetEnvColor(color.r / 3, color.g / 3, color.b / 3, 255));
        PATCH_GFX(gLinkChildBunnyHoodDL, "Equipment_BunnyHood5", equipmentBunnyHood.changedCvar, 4,
                  gsDPSetGrayscaleColor(color.r, color.g, color.b, 255));

        if (manualChange) {
            PATCH_GFX(gLinkChildBunnyHoodDL, "Equipment_BunnyHood6", equipmentBunnyHood.changedCvar, 13,
                      gsSPGrayscale(true));
            if (CVarGetInteger(equipmentBunnyHood.changedCvar, 0)) {
                ResourceMgr_PatchGfxByName(gLinkChildBunnyHoodDL, "Equipment_BunnyHood7", 125,
                                           gsSPBranchListOTRFilePath(gEndGrayscaleAndEndDlistDL));
            } else {
                ResourceMgr_UnpatchGfxByName(gLinkChildBunnyHoodDL, "Equipment_BunnyHood7");
            }
        }
    }

    static CosmeticOption& consumableGreenRupee = cosmeticOptions.at("Consumable.GreenRupee");
    if (manualChange || CVarGetInteger(consumableGreenRupee.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(consumableGreenRupee.valuesCvar, consumableGreenRupee.defaultColor);
        PATCH_GFX(gGiGreenRupeeInnerColorDL, "Consumable_GreenRupee1", consumableGreenRupee.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiGreenRupeeInnerColorDL, "Consumable_GreenRupee2", consumableGreenRupee.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 5, color.g / 5, color.b / 5, 255));
        PATCH_GFX(
            gGiGreenRupeeOuterColorDL, "Consumable_GreenRupee3", consumableGreenRupee.changedCvar, 3,
            gsDPSetPrimColor(0, 0, MIN(color.r + 100, 255), MIN(color.g + 100, 255), MIN(color.b + 100, 255), 255));
        PATCH_GFX(gGiGreenRupeeOuterColorDL, "Consumable_GreenRupee4", consumableGreenRupee.changedCvar, 4,
                  gsDPSetEnvColor(color.r * 0.75f, color.g * 0.75f, color.b * 0.75f, 255));

        // Greg Bridge
        if (Randomizer_GetSettingValue(RSK_RAINBOW_BRIDGE) == RO_BRIDGE_GREG) {
            ResourceMgr_PatchGfxByName(gRainbowBridgeDL, "RainbowBridge_StartGrayscale", 2, gsSPGrayscale(true));
            ResourceMgr_PatchGfxByName(gRainbowBridgeDL, "RainbowBridge_MakeGreen", 10,
                                       gsDPSetGrayscaleColor(color.r, color.g, color.b, color.a));
            ResourceMgr_PatchGfxByName(gRainbowBridgeDL, "RainbowBridge_EndGrayscaleAndEndDlist", 79,
                                       gsSPBranchListOTRFilePath(gEndGrayscaleAndEndDlistDL));
        } else {
            ResourceMgr_UnpatchGfxByName(gRainbowBridgeDL, "RainbowBridge_StartGrayscale");
            ResourceMgr_UnpatchGfxByName(gRainbowBridgeDL, "RainbowBridge_MakeGreen");
            ResourceMgr_UnpatchGfxByName(gRainbowBridgeDL, "RainbowBridge_EndGrayscaleAndEndDlist");
        }
    }
    static CosmeticOption& consumableBlueRupee = cosmeticOptions.at("Consumable.BlueRupee");
    if (manualChange || CVarGetInteger(consumableBlueRupee.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(consumableBlueRupee.valuesCvar, consumableBlueRupee.defaultColor);
        PATCH_GFX(gGiBlueRupeeInnerColorDL, "Consumable_BlueRupee1", consumableBlueRupee.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiBlueRupeeInnerColorDL, "Consumable_BlueRupee2", consumableBlueRupee.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 5, color.g / 5, color.b / 5, 255));
        PATCH_GFX(
            gGiBlueRupeeOuterColorDL, "Consumable_BlueRupee3", consumableBlueRupee.changedCvar, 3,
            gsDPSetPrimColor(0, 0, MIN(color.r + 100, 255), MIN(color.g + 100, 255), MIN(color.b + 100, 255), 255));
        PATCH_GFX(gGiBlueRupeeOuterColorDL, "Consumable_BlueRupee4", consumableBlueRupee.changedCvar, 4,
                  gsDPSetEnvColor(color.r * 0.75f, color.g * 0.75f, color.b * 0.75f, 255));
    }
    static CosmeticOption& consumableRedRupee = cosmeticOptions.at("Consumable.RedRupee");
    if (manualChange || CVarGetInteger(consumableRedRupee.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(consumableRedRupee.valuesCvar, consumableRedRupee.defaultColor);
        PATCH_GFX(gGiRedRupeeInnerColorDL, "Consumable_RedRupee1", consumableRedRupee.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiRedRupeeInnerColorDL, "Consumable_RedRupee2", consumableRedRupee.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 5, color.g / 5, color.b / 5, 255));
        PATCH_GFX(
            gGiRedRupeeOuterColorDL, "Consumable_RedRupee3", consumableRedRupee.changedCvar, 3,
            gsDPSetPrimColor(0, 0, MIN(color.r + 100, 255), MIN(color.g + 100, 255), MIN(color.b + 100, 255), 255));
        PATCH_GFX(gGiRedRupeeOuterColorDL, "Consumable_RedRupee4", consumableRedRupee.changedCvar, 4,
                  gsDPSetEnvColor(color.r * 0.75f, color.g * 0.75f, color.b * 0.75f, 255));
    }
    static CosmeticOption& consumablePurpleRupee = cosmeticOptions.at("Consumable.PurpleRupee");
    if (manualChange || CVarGetInteger(consumablePurpleRupee.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(consumablePurpleRupee.valuesCvar, consumablePurpleRupee.defaultColor);
        PATCH_GFX(gGiPurpleRupeeInnerColorDL, "Consumable_PurpleRupee1", consumablePurpleRupee.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiPurpleRupeeInnerColorDL, "Consumable_PurpleRupee2", consumablePurpleRupee.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 5, color.g / 5, color.b / 5, 255));
        PATCH_GFX(
            gGiPurpleRupeeOuterColorDL, "Consumable_PurpleRupee3", consumablePurpleRupee.changedCvar, 3,
            gsDPSetPrimColor(0, 0, MIN(color.r + 100, 255), MIN(color.g + 100, 255), MIN(color.b + 100, 255), 255));
        PATCH_GFX(gGiPurpleRupeeOuterColorDL, "Consumable_PurpleRupee4", consumablePurpleRupee.changedCvar, 4,
                  gsDPSetEnvColor(color.r * 0.75f, color.g * 0.75f, color.b * 0.75f, 255));
    }
    static CosmeticOption& consumableGoldRupee = cosmeticOptions.at("Consumable.GoldRupee");
    if (manualChange || CVarGetInteger(consumableGoldRupee.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(consumableGoldRupee.valuesCvar, consumableGoldRupee.defaultColor);
        PATCH_GFX(gGiGoldRupeeInnerColorDL, "Consumable_GoldRupee1", consumableGoldRupee.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiGoldRupeeInnerColorDL, "Consumable_GoldRupee2", consumableGoldRupee.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 5, color.g / 5, color.b / 5, 255));
        PATCH_GFX(
            gGiGoldRupeeOuterColorDL, "Consumable_GoldRupee3", consumableGoldRupee.changedCvar, 3,
            gsDPSetPrimColor(0, 0, MIN(color.r + 100, 255), MIN(color.g + 100, 255), MIN(color.b + 100, 255), 255));
        PATCH_GFX(gGiGoldRupeeOuterColorDL, "Consumable_GoldRupee4", consumableGoldRupee.changedCvar, 4,
                  gsDPSetEnvColor(color.r * 0.75f, color.g * 0.75f, color.b * 0.75f, 255));
    }

    static CosmeticOption& consumableHearts = cosmeticOptions.at("Consumable.Hearts");
    if (manualChange || CVarGetInteger(consumableHearts.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(consumableHearts.valuesCvar, consumableHearts.defaultColor);
        /*
        PATCH_GFX(gGiRecoveryHeartDL,                             "Consumable_Hearts1", consumableHearts.changedCvar, 4,
        gsDPSetGrayscaleColor(color.r, color.g, color.b, 255)); PATCH_GFX(gGiRecoveryHeartDL, "Consumable_Hearts2",
        consumableHearts.changedCvar,        26, gsSPGrayscale(true)); PATCH_GFX(gGiRecoveryHeartDL,
        "Consumable_Hearts3",       consumableHearts.changedCvar,        72, gsSPGrayscale(false));
        PATCH_GFX(gGiRecoveryHeartDL,                             "Consumable_Hearts4", consumableHearts.changedCvar,
        74, gsSPEndDisplayList());
        */
        PATCH_GFX(gGiHeartPieceDL, "Consumable_Hearts5", consumableHearts.changedCvar, 2,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiHeartPieceDL, "Consumable_Hearts6", consumableHearts.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gGiHeartContainerDL, "Consumable_Hearts7", consumableHearts.changedCvar, 2,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiHeartContainerDL, "Consumable_Hearts8", consumableHearts.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gGiRedPotColorDL, "Consumable_Hearts9", consumableHearts.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiRedPotColorDL, "Consumable_Hearts10", consumableHearts.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
    }
    static CosmeticOption& consumableMagic = cosmeticOptions.at("Consumable.Magic");
    if (manualChange || CVarGetInteger(consumableMagic.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(consumableMagic.valuesCvar, consumableMagic.defaultColor);
        PATCH_GFX(gGiMagicJarSmallDL, "Consumable_Magic1", consumableMagic.changedCvar, 31,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiMagicJarSmallDL, "Consumable_Magic2", consumableMagic.changedCvar, 32,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gGiMagicJarLargeDL, "Consumable_Magic3", consumableMagic.changedCvar, 31,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiMagicJarLargeDL, "Consumable_Magic4", consumableMagic.changedCvar, 32,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gGiGreenPotColorDL, "Consumable_Magic5", consumableMagic.changedCvar, 3,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiGreenPotColorDL, "Consumable_Magic6", consumableMagic.changedCvar, 4,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
    }

    static CosmeticOption& npcGoldenSkulltula = cosmeticOptions.at("NPC.GoldenSkulltula");
    if (manualChange || CVarGetInteger(npcGoldenSkulltula.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(npcGoldenSkulltula.valuesCvar, npcGoldenSkulltula.defaultColor);
        PATCH_GFX(gSkulltulaTokenDL, "NPC_GoldenSkulltula1", npcGoldenSkulltula.changedCvar, 5,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gSkulltulaTokenDL, "NPC_GoldenSkulltula2", npcGoldenSkulltula.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gSkulltulaTokenFlameDL, "NPC_GoldenSkulltula3", npcGoldenSkulltula.changedCvar, 32,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gSkulltulaTokenFlameDL, "NPC_GoldenSkulltula4", npcGoldenSkulltula.changedCvar, 33,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gGiSkulltulaTokenDL, "NPC_GoldenSkulltula5", npcGoldenSkulltula.changedCvar, 5,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiSkulltulaTokenDL, "NPC_GoldenSkulltula6", npcGoldenSkulltula.changedCvar, 6,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(gGiSkulltulaTokenFlameDL, "NPC_GoldenSkulltula7", npcGoldenSkulltula.changedCvar, 32,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGiSkulltulaTokenFlameDL, "NPC_GoldenSkulltula8", npcGoldenSkulltula.changedCvar, 33,
                  gsDPSetEnvColor(color.r / 2, color.g / 2, color.b / 2, 255));
        PATCH_GFX(object_st_DL_003FB0, "NPC_GoldenSkulltula9", npcGoldenSkulltula.changedCvar, 118,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(object_st_DL_003FB0, "NPC_GoldenSkulltula10", npcGoldenSkulltula.changedCvar, 119,
                  gsDPSetEnvColor(color.r / 4, color.g / 4, color.b / 4, 255));
    }

    static CosmeticOption& npcGerudo = cosmeticOptions.at("NPC.Gerudo");
    if (manualChange || CVarGetInteger(npcGerudo.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(npcGerudo.valuesCvar, npcGerudo.defaultColor);
        PATCH_GFX(gGerudoPurpleTorsoDL, "NPC_Gerudo1", npcGerudo.changedCvar, 139,
                  gsDPSetEnvColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gGerudoPurpleRightThighDL, "NPC_Gerudo2", npcGerudo.changedCvar, 11,
                  gsDPSetEnvColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gGerudoPurpleLeftThighDL, "NPC_Gerudo3", npcGerudo.changedCvar, 11,
                  gsDPSetEnvColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gGerudoPurpleVeilDL, "NPC_Gerudo4", npcGerudo.changedCvar, 17,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGerudoPurpleLeftShinDL, "NPC_Gerudo5", npcGerudo.changedCvar, 11,
                  gsDPSetEnvColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gGerudoPurpleRightShinDL, "NPC_Gerudo6", npcGerudo.changedCvar, 11,
                  gsDPSetEnvColor(color.r, color.g, color.b, 255));
        PATCH_GFX(gGerudoPurpleLeftHandDL, "NPC_Gerudo7", npcGerudo.changedCvar, 17,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
        PATCH_GFX(gGerudoPurpleRightHandDL, "NPC_Gerudo8", npcGerudo.changedCvar, 17,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }

    static CosmeticOption& npcMetalTrap = cosmeticOptions.at("NPC.MetalTrap");
    if (manualChange || CVarGetInteger(npcMetalTrap.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(npcMetalTrap.valuesCvar, npcMetalTrap.defaultColor);
        PATCH_GFX(gSlidingBladeTrapDL, "NPC_MetalTrap1", npcMetalTrap.changedCvar, 59,
                  gsDPSetPrimColor(0, 0, color.r, color.g, color.b, 255));
    }

    static CosmeticOption& n64LogoRed = cosmeticOptions.at("Title.N64LogoRed");
    if (manualChange || CVarGetInteger(n64LogoRed.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(n64LogoRed.valuesCvar, n64LogoRed.defaultColor);
        PATCH_GFX(gNintendo64LogoDL, "Title_N64LogoRed1", n64LogoRed.changedCvar, 17,
                  gsDPSetPrimColor(0, 0, 255, 255, 255, 255))
        PATCH_GFX(gNintendo64LogoDL, "Title_N64LogoRed2", n64LogoRed.changedCvar, 18,
                  gsDPSetEnvColor(color.r, color.g, color.b, 128));
    }
    static CosmeticOption& n64LogoBlue = cosmeticOptions.at("Title.N64LogoBlue");
    if (manualChange || CVarGetInteger(n64LogoBlue.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(n64LogoBlue.valuesCvar, n64LogoBlue.defaultColor);
        PATCH_GFX(gNintendo64LogoDL, "Title_N64LogoBlue1", n64LogoBlue.changedCvar, 29,
                  gsDPSetPrimColor(0, 0, 255, 255, 255, 255))
        PATCH_GFX(gNintendo64LogoDL, "Title_N64LogoBlue2", n64LogoBlue.changedCvar, 30,
                  gsDPSetEnvColor(color.r, color.g, color.b, 128));
    }
    static CosmeticOption& n64LogoGreen = cosmeticOptions.at("Title.N64LogoGreen");
    if (manualChange || CVarGetInteger(n64LogoGreen.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(n64LogoGreen.valuesCvar, n64LogoGreen.defaultColor);
        PATCH_GFX(gNintendo64LogoDL, "Title_N64LogoGreen1", n64LogoGreen.changedCvar, 56,
                  gsDPSetPrimColor(0, 0, 255, 255, 255, 255))
        PATCH_GFX(gNintendo64LogoDL, "Title_N64LogoGreen2", n64LogoGreen.changedCvar, 57,
                  gsDPSetEnvColor(color.r, color.g, color.b, 128));
    }
    static CosmeticOption& n64LogoYellow = cosmeticOptions.at("Title.N64LogoYellow");
    if (manualChange || CVarGetInteger(n64LogoYellow.rainbowCvar, 0)) {
        Color_RGBA8 color = CVarGetColor(n64LogoYellow.valuesCvar, n64LogoYellow.defaultColor);
        PATCH_GFX(gNintendo64LogoDL, "Title_N64LogoYellow1", n64LogoYellow.changedCvar, 81,
                  gsDPSetPrimColor(0, 0, 255, 255, 255, 255))
        PATCH_GFX(gNintendo64LogoDL, "Title_N64LogoYellow2", n64LogoYellow.changedCvar, 82,
                  gsDPSetEnvColor(color.r, color.g, color.b, 128));
    }

    if (gPlayState != nullptr) {
        if (CVarGetInteger(CVAR_COSMETIC("Link.BodySize.Changed"), 0)) {
            // NOT static. A function-local static with a dynamic initialiser is evaluated exactly
            // once for the lifetime of the PROCESS, so this captured the Player of whichever scene
            // happened to be loaded the first time the option was ticked -- and then wrote three
            // floats through that pointer on every frame afterwards. Player is reallocated on every
            // scene load, so it dangled within a single run, never mind across two. GET_PLAYER is a
            // field read; there was nothing to cache.
            Player* player = GET_PLAYER(gPlayState);
            float scale = CVarGetFloat(CVAR_COSMETIC("Link.BodySize.Value"), 0.01f);
            player->actor.scale.x = scale;
            player->actor.scale.y = scale;
            player->actor.scale.z = scale;
        }
    }
}
