#include "CosmeticsCatalog.h"

#include "soh/Enhancements/enhancementTypes.h"
#include "soh/cvar_prefixes.h"

const CosmeticGroupLabelMap& CosmeticGroupLabels() {
    static const CosmeticGroupLabelMap labels = {
        { COSMETICS_GROUP_LINK, "Link" },
        { COSMETICS_GROUP_MIRRORSHIELD, "Mirror Shield" },
        { COSMETICS_GROUP_SWORDS, "Swords" },
        { COSMETICS_GROUP_GLOVES, "Gloves" },
        { COSMETICS_GROUP_EQUIPMENT, "Equipment" },
        { COSMETICS_GROUP_KEYRING, "Keyring" },
        { COSMETICS_GROUP_SMALL_KEYS, "Small Keys" },
        { COSMETICS_GROUP_BOSS_KEYS, "Boss Keys" },
        { COSMETICS_GROUP_CONSUMABLE, "Consumables" },
        { COSMETICS_GROUP_HUD, "HUD" },
        { COSMETICS_GROUP_KALEIDO, "Pause Menu" },
        { COSMETICS_GROUP_TITLE, "Title Screen" },
        { COSMETICS_GROUP_NPC, "NPCs" },
        { COSMETICS_GROUP_WORLD, "World" },
        { COSMETICS_GROUP_MAGIC, "Magic Effects" },
        { COSMETICS_GROUP_ARROWS, "Arrow Effects" },
        { COSMETICS_GROUP_SPIN_ATTACK, "Spin Attack" },
        { COSMETICS_GROUP_TRAILS, "Trails" },
        { COSMETICS_GROUP_NAVI, "Navi" },
        { COSMETICS_GROUP_IVAN, "Ivan" },
        { COSMETICS_GROUP_MESSAGE, "Message" },
    };
    return labels;
}

const CosmeticsRandomizerModeMap& CosmeticsRandomizerModes() {
    static const CosmeticsRandomizerModeMap modes = {
        { RANDOMIZE_OFF, "Manual" },
        { RANDOMIZE_ON_NEW_SCENE, "On New Scene" },
        { RANDOMIZE_ON_RANDO_GEN_ONLY, "On Rando Gen Only" },
        { RANDOMIZE_ON_FILE_LOAD, "On File Load" },
        { RANDOMIZE_ON_FILE_LOAD_SEEDED, "On File Load (Seeded)" },
    };
    return modes;
}

Color_RGBA8 ColorRGBA8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Color_RGBA8 color = { r, g, b, a };
    return color;
}

#define COSMETIC_OPTION(id, label, group, defaultColor, supportsAlpha, supportsRainbow, advancedOption)               \
    {                                                                                                                 \
        id, {                                                                                                         \
            CVAR_COSMETIC(id), CVAR_COSMETIC(id ".Value"), CVAR_COSMETIC(id ".Rainbow"), CVAR_COSMETIC(id ".Locked"), \
                CVAR_COSMETIC(id ".Changed"), label, group,                                                           \
                ImVec4(defaultColor.r / 255.0f, defaultColor.g / 255.0f, defaultColor.b / 255.0f,                     \
                       defaultColor.a / 255.0f),                                                                      \
                defaultColor, supportsAlpha, supportsRainbow, advancedOption                                          \
        }                                                                                                             \
    }

// clang-format off
/*
    So, you would like to add a new cosmetic option? BUCKLE UP

    To preface this, if you have any questions or concerns ping @ProxySaw on discord, if I'm no longer available ask around in the #soh-development

    # Silly Options
    Lets get this one out of the way, probably the only thing that will be consistent between silly options is how they are rendered
    on the ImGui tab. So when adding one just make sure it follows the same general pattern as the rest. Notably:
    - Make sure to SaveConsoleVariablesNextFrame(), forgetting this will not persist your changes
    - Make sure reset properly resets the value
    - Depending on your use case you may or may not have to split the cvar into two values (cvar.Changed & cvar.Value)

    # Finding your color
    So the first order of business is finding the source of the color you are trying to change. There are four scenarios to be aware
    of, in order of difficulty from easiest to hardest:
    1. Color is in code
    2. Color is in DList with gsDPSetPrimColor/gsDPSetEnvColor commands
    3. Color is embedded in a TLUT, which is applied to the texture
    4. Color is embedded in the texture itself

    I would recommend first finding the draw function for whatever you are looking for. In most cases this will be an actor, and the actor's draw
    func will be at the bottom of their overlay file, `EnCow_Draw` for ACTOR_EN_COW is one example. There can also be additional nested draw methods
    like drawing each limb of an actor for instance that you will also want to inspect. What you are looking for is any sort of RGB values, or calls
    directly to gDPSetPrimColor/gDPSetEnvColor in code. If you find one, try changing the arguments and see if that's what you are looking for.

    If this fails, and you aren't able to find any colors within the source of the actor/whatever you will now need to investigate the DLists 
    that are being rendered. The easiest way to do this is to use the experimental Display List Viewer in the developer tools options. An
    alternative to this is to dig through the source of the DLists after you have built the zeldaret/oot repository, but this will be much more
    manual, and I can't provide instructions for it.

    Assuming you are planning on using the Display List Viewer, you need to find the name of the DList to inspect. In the same areas you were looking
    for RGB values you now want to look for calls to gSPDisplayList, or variables that end in "DL". Once you have this name start typing parts of 
    it into the dlist-viewer (in the developer dropdown) and select the desired dlist in the dropdown, there may be many. You will now see a
    list of commands associated with the DList you have selected. If you are lucky, there will be calls to gsDPSetPrimColor/gsDPSetEnvColor with
    the RGB values editable, and you can edit those to determine if that is the DList you are looking for. If it is, make note of the name and
    the index of the DList command you just edited, as you will need that going forward.

    If you are unlucky, this means it is very likely the color you want to change is embedded in a TLUT or the texture itself. We can work around
    this by using grayscale coloring, but this is advanced and I won't be providing a walkthrough for it here, you'll just have to read through
    the existing cosmetic options to get an understanding of how to do this.

    # Add your option to the editor
    This step should be as simple as adding a single line in the map below, ensure you add it to the appropriate place and with the default colors

    # Applying your color
    If you have determined your color is in code, this should just be as simple as replacing it, or the call it's used in if and only if it has
    been changed. Example from the moon cosmetic option:

    ```cpp
    // original
    gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 240, 255, 180, alpha);
    gDPSetEnvColor(POLY_OPA_DISP++, 80, 70, 20, alpha);

    // with cosmetics change
    if (CVarGetInteger(CVAR_COSMETIC("World.Moon.Changed"), 0)) {
        Color_RGB8 moonColor = CVarGetColor24(CVAR_COSMETIC("World.Moon.Value"), (Color_RGB8){ 0, 0, 240 });
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, moonColor.r, moonColor.g, moonColor.b, alpha);
        gDPSetEnvColor(POLY_OPA_DISP++, moonColor.r / 2, moonColor.g / 2, moonColor.b / 2, alpha);
    } else {
        gDPSetPrimColor(POLY_OPA_DISP++, 0, 0, 240, 255, 180, alpha);
        gDPSetEnvColor(POLY_OPA_DISP++, 80, 70, 20, alpha);
    }
    ```

    If instead what you found was that your color was set via a gsDPSetPrimColor command within a DList, you will need to follow the pattern 
    displayed in `ApplyOrResetCustomGfxPatches`, using the name of the Dlist, and index of the command you want to replace appropriately.

    # Applying variants of the same color
    This applies to all of the different kinds of cosmetics, in some cases you will need to apply multiple shades of the same color, otherwise
    you end up with a weird color between the original color and the new color for example. One demonstration on how to handle this is shown above
    in the moon cosmetic, where for the gDPSetEnvColor color we are halving the RGB values, to make them a bit darker similar to how the original
    colors were darker than the gDPSetPrimColor. You will see many more examples of this below in the `ApplyOrResetCustomGfxPatches` method
*/
CosmeticOptionMap& CosmeticOptions() {
    static CosmeticOptionMap options = {
    COSMETIC_OPTION("Link.KokiriTunic",             "Kokiri Tunic",             COSMETICS_GROUP_LINK,         ColorRGBA8( 30, 105,  27, 255), false, true, false),
    COSMETIC_OPTION("Link.GoronTunic",              "Goron Tunic",              COSMETICS_GROUP_LINK,         ColorRGBA8(100,  20,   0, 255), false, true, false),
    COSMETIC_OPTION("Link.ZoraTunic",               "Zora Tunic",               COSMETICS_GROUP_LINK,         ColorRGBA8(  0,  60, 100, 255), false, true, false),
    COSMETIC_OPTION("Link.Hair",                    "Hair",                     COSMETICS_GROUP_LINK,         ColorRGBA8(255, 173,  27, 255), false, true, true),
    COSMETIC_OPTION("Link.Linen",                   "Linen",                    COSMETICS_GROUP_LINK,         ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("Link.Boots",                   "Boots",                    COSMETICS_GROUP_LINK,         ColorRGBA8( 93,  44,  18, 255), false, true, true),

    COSMETIC_OPTION("MirrorShield.Body",            "Body",                     COSMETICS_GROUP_MIRRORSHIELD, ColorRGBA8(215,   0,   0, 255), false, true, false),
    COSMETIC_OPTION("MirrorShield.Mirror",          "Mirror",                   COSMETICS_GROUP_MIRRORSHIELD, ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("MirrorShield.Emblem",          "Emblem",                   COSMETICS_GROUP_MIRRORSHIELD, ColorRGBA8(205, 225, 255, 255), false, true, true),

    COSMETIC_OPTION("Swords.KokiriBlade",           "Kokiri Sword Blade",       COSMETICS_GROUP_SWORDS,       ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Swords.MasterBlade",           "Master Sword Blade",       COSMETICS_GROUP_SWORDS,       ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Swords.BiggoronBlade",         "Biggoron Sword Blade",     COSMETICS_GROUP_SWORDS,       ColorRGBA8(255, 255, 255, 255), false, true, false),
    /* Todo (Cosmetics): Broken, need a better way to grayscale
    COSMETIC_OPTION("Swords.KokiriHilt",            "Kokiri Sword Hilt",        COSMETICS_GROUP_SWORDS,       ColorRGBA8(160, 100,  15, 255), false, true, true),
    COSMETIC_OPTION("Swords.MasterHilt",            "Master Sword Hilt",        COSMETICS_GROUP_SWORDS,       ColorRGBA8( 80,  80, 168, 255), false, true, true),
    COSMETIC_OPTION("Swords.BiggoronHilt",          "Biggoron Sword Hilt",      COSMETICS_GROUP_SWORDS,       ColorRGBA8( 80,  80, 168, 255), false, true, true),
    */

    COSMETIC_OPTION("Gloves.GoronBracelet",         "Goron Bracelet",           COSMETICS_GROUP_GLOVES,       ColorRGBA8(255, 255, 170, 255), false, true, false),
    COSMETIC_OPTION("Gloves.SilverGauntlets",       "Silver Gauntlets",         COSMETICS_GROUP_GLOVES,       ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Gloves.GoldenGauntlets",       "Golden Gauntlets",         COSMETICS_GROUP_GLOVES,       ColorRGBA8(254, 207,  15, 255), false, true, false),
    COSMETIC_OPTION("Gloves.GauntletsGem",          "Gauntlets Gem",            COSMETICS_GROUP_GLOVES,       ColorRGBA8(255,  60, 100, 255), false, true, true),

    COSMETIC_OPTION("Equipment.BoomerangBody",      "Boomerang Body",           COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(160, 100,   0, 255), false, true, false),
    COSMETIC_OPTION("Equipment.BoomerangGem",       "Boomerang Gem",            COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(255,  50, 150, 255), false, true, true),
    /* Todo (Cosmetics): Broken, need a better way to grayscale
    COSMETIC_OPTION("Equipment.SlingshotBody",      "Slingshot Body",           COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(160, 100,   0, 255), false, true, true),
    */
    COSMETIC_OPTION("Equipment.SlingshotString",    "Slingshot String",         COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("Equipment.HammerHead",         "Hammer Head",              COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(155, 192, 201, 255), false, true, false),
    COSMETIC_OPTION("Equipment.HammerHandle",       "Hammer Handle",            COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(110,  60,   0, 255), false, true, true),
    COSMETIC_OPTION("Equipment.HookshotChain",      "Hookshot Chain",           COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(255, 255, 255, 255), false, true, true),
    /* Todo (Cosmetics): Implement
    COSMETIC_OPTION("Equipment.HookshotTip",        "Hookshot Tip",             COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(255, 255, 255, 255), false, true, false),
    */
    COSMETIC_OPTION("HookshotReticle.Target",       "Hookshotable Reticle",     COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(  0, 255,   0, 255), false, true, false),
    COSMETIC_OPTION("HookshotReticle.NonTarget",    "Non-Hookshotable Reticle", COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(255,   0,   0, 255), false, true, false),
    COSMETIC_OPTION("Equipment.BowTips",            "Bow Tips",                 COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(200,   0,   0, 255), false, true, true),
    COSMETIC_OPTION("Equipment.BowString",          "Bow String",               COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("Equipment.BowBody",            "Bow Body",                 COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(140,  90,  10, 255), false, true, false),
    COSMETIC_OPTION("Equipment.BowHandle",          "Bow Handle",               COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8( 50, 150, 255, 255), false, true, true),
    COSMETIC_OPTION("Equipment.ChuFace",            "Bombchu Face",             COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(  0, 100, 150, 255), false, true, true),
    COSMETIC_OPTION("Equipment.ChuBody",            "Bombchu Body",             COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(180, 130,  50, 255), false, true, true), 
    COSMETIC_OPTION("Equipment.BunnyHood",          "Bunny Hood",               COSMETICS_GROUP_EQUIPMENT,    ColorRGBA8(255, 235, 109, 255), false, true, true), 

    COSMETIC_OPTION("Consumable.Hearts",            "Hearts",                   COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8(255,  70,  50, 255), false, true, false),
    COSMETIC_OPTION("Consumable.HeartBorder",       "Heart Border",             COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8( 50,  40,  60, 255), false, true, true),
    COSMETIC_OPTION("Consumable.DDHearts",          "DD Hearts",                COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8(200,   0,   0, 255), false, true, false),
    COSMETIC_OPTION("Consumable.DDHeartBorder",     "DD Heart Border",          COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("Consumable.Magic",             "Magic",                    COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8(  0, 200,   0, 255), false, true, false),
    COSMETIC_OPTION("Consumable.MagicActive",       "Magic Active",             COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8(250, 250,   0, 255), false, true, true),
    COSMETIC_OPTION("Consumable_MagicInfinite",     "Infinite Magic",           COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8(  0,   0, 200, 255), false, true, true),
    COSMETIC_OPTION("Consumable.MagicBorder",       "Magic Border",             COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("Consumable.MagicBorderActive", "Magic Border Active",      COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("Consumable.GreenRupee",        "Green Rupee",              COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8( 50, 255,  50, 255), false, true, true),
    COSMETIC_OPTION("Consumable.BlueRupee",         "Blue Rupee",               COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8( 50,  50, 255, 255), false, true, true),
    COSMETIC_OPTION("Consumable.RedRupee",          "Red Rupee",                COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8(255,  50,  50, 255), false, true, true),
    COSMETIC_OPTION("Consumable.PurpleRupee",       "Purple Rupee",             COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8(150,  50, 255, 255), false, true, true),
    COSMETIC_OPTION("Consumable.GoldRupee",         "Gold Rupee",               COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8(255, 190,  55, 255), false, true, true),
    COSMETIC_OPTION("Consumable.SilverRupee",       "Silver Rupee",             COSMETICS_GROUP_CONSUMABLE,   ColorRGBA8(255, 255, 255, 255), false, true, true),

    COSMETIC_OPTION("Key.KeyringRing",              "Key Ring Ring",            COSMETICS_GROUP_KEYRING,      ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Key.ForestSmallBody",          "Forest Small Key Body",    COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Key.ForestSmallEmblem",        "Forest Small Key Emblem",  COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(4,   195, 46,  255), false, true, false),
    COSMETIC_OPTION("Key.ForestBossBody",           "Forest Boss Key Body",     COSMETICS_GROUP_BOSS_KEYS,    ColorRGBA8(255, 255, 0,   255), false, true, false),
    COSMETIC_OPTION("Key.ForestBossGem",            "Forest Boss Key Gem",      COSMETICS_GROUP_BOSS_KEYS,    ColorRGBA8(255, 0,   0,   255), false, true, false),
    COSMETIC_OPTION("Key.FireSmallBody",            "Fire Small Key Body",      COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Key.FireSmallEmblem",          "Fire Small Key Emblem",    COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(237, 95,  95,  255), false, true, false),
    COSMETIC_OPTION("Key.FireBossBody",             "Fire Boss Key Body",       COSMETICS_GROUP_BOSS_KEYS,    ColorRGBA8(255, 255, 0,   255), false, true, false),
    COSMETIC_OPTION("Key.FireBossGem",              "Fire Boss Key Gem",        COSMETICS_GROUP_BOSS_KEYS,    ColorRGBA8(255, 0,   0,   255), false, true, false),
    COSMETIC_OPTION("Key.WaterSmallBody",           "Water Small Key Body",     COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Key.WaterSmallEmblem",         "Water Small Key Emblem",   COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(85,  180, 223, 255), false, true, false),
    COSMETIC_OPTION("Key.WaterBossBody",            "Water Boss Key Body",      COSMETICS_GROUP_BOSS_KEYS,    ColorRGBA8(255, 255, 0,   255), false, true, false),
    COSMETIC_OPTION("Key.WaterBossGem",             "Water Boss Key Gem",       COSMETICS_GROUP_BOSS_KEYS,    ColorRGBA8(255, 0,   0,   255), false, true, false),
    COSMETIC_OPTION("Key.SpiritSmallBody",          "Spirit Small Key Body",    COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Key.SpiritSmallEmblem",        "Spirit Small Key Emblem",  COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(222, 158, 47,  255), false, true, false),
    COSMETIC_OPTION("Key.SpiritBossBody",           "Spirit Boss Key Body",     COSMETICS_GROUP_BOSS_KEYS,    ColorRGBA8(255, 255, 0,   255), false, true, false),
    COSMETIC_OPTION("Key.SpiritBossGem",            "Spirit Boss Key Gem",      COSMETICS_GROUP_BOSS_KEYS,    ColorRGBA8(255, 0,   0,   255), false, true, false),
    COSMETIC_OPTION("Key.ShadowSmallBody",          "Shadow Small Key Body",    COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Key.ShadowSmallEmblem",        "Shadow Small Key Emblem",  COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(126, 16,  177, 255), false, true, false),
    COSMETIC_OPTION("Key.ShadowBossBody",           "Shadow Boss Key Body",     COSMETICS_GROUP_BOSS_KEYS,    ColorRGBA8(255, 255, 0,   255), false, true, false),
    COSMETIC_OPTION("Key.ShadowBossGem",            "Shadow Boss Key Gem",      COSMETICS_GROUP_BOSS_KEYS,    ColorRGBA8(255, 0,   0,   255), false, true, false),
    COSMETIC_OPTION("Key.GanonsSmallBody",          "Ganons Small Key Body",    COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Key.GanonsSmallEmblem",        "Ganons Small Key Emblem",  COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(80,  80,  80,  255), false, true, false),
    COSMETIC_OPTION("Key.GanonsBossBody",           "Ganons Boss Key Body",     COSMETICS_GROUP_BOSS_KEYS,    ColorRGBA8(255, 255, 0,   255), false, true, false),
    COSMETIC_OPTION("Key.GanonsBossGem",            "Ganons Boss Key Gem",      COSMETICS_GROUP_BOSS_KEYS,    ColorRGBA8(255, 0,   0,   255), false, true, false),

    COSMETIC_OPTION("Key.WellSmallBody",            "Well Small Key",           COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Key.WellSmallEmblem",          "Well Small Key Emblem",    COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(227, 110, 255, 255), false, true, false),
    COSMETIC_OPTION("Key.FortSmallBody",            "Fortress Small Key",       COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Key.FortSmallEmblem",          "Fortress Small Key Emblem",COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Key.GTGSmallBody",             "GTG Small Key",            COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Key.GTGSmallEmblem",           "GTG Small Key Emblem",     COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(221, 212, 60,  255), false, true, false),
    //COSMETIC_OPTION("Key.ChestGameSmallBody",     "Chest Game Key",           COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 255, 255, 255), false, true, false),
    //COSMETIC_OPTION("Key.ChestGameEmblem",        "Chest Game Key Emblem",    COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 0,   0,   255), false, true, false),
    COSMETIC_OPTION("Key.Skeleton",                 "Skeleton Key",             COSMETICS_GROUP_SMALL_KEYS,   ColorRGBA8(255, 255, 170, 255), false, true, false),

    COSMETIC_OPTION("HUD.AButton",                  "A Button",                 COSMETICS_GROUP_HUD,          ColorRGBA8( 90,  90, 255, 255), false, true, false),
    COSMETIC_OPTION("HUD.BButton",                  "B Button",                 COSMETICS_GROUP_HUD,          ColorRGBA8(  0, 150,   0, 255), false, true, false),
    COSMETIC_OPTION("HUD.CButtons",                 "C Buttons",                COSMETICS_GROUP_HUD,          ColorRGBA8(255, 160,   0, 255), false, true, false),
    COSMETIC_OPTION("HUD.CUpButton",                "C Up Button",              COSMETICS_GROUP_HUD,          ColorRGBA8(255, 160,   0, 255), false, true, true),
    COSMETIC_OPTION("HUD.CDownButton",              "C Down Button",            COSMETICS_GROUP_HUD,          ColorRGBA8(255, 160,   0, 255), false, true, true),
    COSMETIC_OPTION("HUD.CLeftButton",              "C Left Button",            COSMETICS_GROUP_HUD,          ColorRGBA8(255, 160,   0, 255), false, true, true),
    COSMETIC_OPTION("HUD.CRightButton",             "C Right Button",           COSMETICS_GROUP_HUD,          ColorRGBA8(255, 160,   0, 255), false, true, true),
    COSMETIC_OPTION("HUD.StartButton",              "Start Button",             COSMETICS_GROUP_HUD,          ColorRGBA8(200,   0,   0, 255), false, true, false),
    COSMETIC_OPTION("HUD.Dpad",                     "Dpad",                     COSMETICS_GROUP_HUD,          ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("HUD.KeyCount",                 "Key Count",                COSMETICS_GROUP_HUD,          ColorRGBA8(200, 230, 255, 255), false, true, true),
    COSMETIC_OPTION("HUD.StoneOfAgony",             "Stone of Agony",           COSMETICS_GROUP_HUD,          ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("HUD.Minimap",                  "Minimap",                  COSMETICS_GROUP_HUD,          ColorRGBA8(  0, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("HUD.MinimapPosition",          "Minimap Position",         COSMETICS_GROUP_HUD,          ColorRGBA8(200, 255,   0, 255), false, true, true),
    COSMETIC_OPTION("HUD.MinimapEntrance",          "Minimap Entrance",         COSMETICS_GROUP_HUD,          ColorRGBA8(200,   0,   0, 255), false, true, true),
    COSMETIC_OPTION("HUD.EnemyHealthBar",           "Enemy Health Bar",         COSMETICS_GROUP_HUD,          ColorRGBA8(255,   0,   0, 255), true,  true, false),
    COSMETIC_OPTION("HUD.EnemyHealthBorder",        "Enemy Health Border",      COSMETICS_GROUP_HUD,          ColorRGBA8(255, 255, 255, 255), true,  true, true),
    COSMETIC_OPTION("HUD.NameTagActorText",         "Nametag Text",             COSMETICS_GROUP_HUD,          ColorRGBA8(255, 255, 255, 255), true,  true, false),
    COSMETIC_OPTION("HUD.NameTagActorBackground",   "Nametag Background",       COSMETICS_GROUP_HUD,          ColorRGBA8(  0,   0,   0,  80), true,  true, true),
    COSMETIC_OPTION("HUD.TitleCard.Map",            "Map Title Card",           COSMETICS_GROUP_HUD,          ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("HUD.TitleCard.Boss",           "Boss Title Card",          COSMETICS_GROUP_HUD,          ColorRGBA8(255, 255, 255, 255), false, true, false),

    #define MESSAGE_COSMETIC_OPTION(id, label, r, g, b) COSMETIC_OPTION("Message." id, label, COSMETICS_GROUP_MESSAGE,  ColorRGBA8(r, g, b, 255), false, true, true)

    MESSAGE_COSMETIC_OPTION("Default.Normal",                   "Message Default Color",                     255, 255, 255),
    MESSAGE_COSMETIC_OPTION("Default.NoneNoShadow",             "Message Default (None No Shadow)",            0,   0,   0),
    MESSAGE_COSMETIC_OPTION("Red.Normal",                       "Message Red Color",                         255,  60,  60),
    MESSAGE_COSMETIC_OPTION("Red.Wooden",                       "Message Red (Wooden) Color",                255, 120,   0),
    MESSAGE_COSMETIC_OPTION("Adjustable.Normal",                "Message Adjustable Color",                   70, 255,  80),
    MESSAGE_COSMETIC_OPTION("Adjustable.Wooden",                "Message Adjustable (Wooden) Color",          70, 255,  80),
    MESSAGE_COSMETIC_OPTION("Blue.Normal",                      "Message Blue Color",                         80,  90, 255),
    MESSAGE_COSMETIC_OPTION("Blue.Wooden",                      "Message Blue (Wooden) Color",                80, 110, 255),
    MESSAGE_COSMETIC_OPTION("LightBlue.Normal",                 "Message Light Blue Color",                  100, 180, 255),
    MESSAGE_COSMETIC_OPTION("LightBlue.Wooden",                 "Message Light Blue (Wooden) Color",          90, 180, 255),
    MESSAGE_COSMETIC_OPTION("LightBlue.LightBlue.NoneNoShadow", "Message Light Blue (None No Shadow)",        80, 150, 180),
    MESSAGE_COSMETIC_OPTION("Purple.Normal",                    "Message Purple Color",                      255, 150, 180),
    MESSAGE_COSMETIC_OPTION("Purple.Wooden",                    "Message Purple (Wooden) Color",             210, 100, 255),
    MESSAGE_COSMETIC_OPTION("Yellow.Normal",                    "Message Yellow Color",                      255, 255,  50),
    MESSAGE_COSMETIC_OPTION("Yellow.Wooden",                    "Message Yellow (Wooden) Color",             255, 255,  30),
    MESSAGE_COSMETIC_OPTION("Black",                            "Message Black Color",                         0,   0,   0),

#undef MESSAGE_COSMETIC_OPTION

    COSMETIC_OPTION("Kaleido.ItemSelA",             "Item Select Color",        COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 10,  50,  80, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.ItemSelB",             "Item Select Color B",      COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 70, 100, 130, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.ItemSelC",             "Item Select Color C",      COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 70, 100, 130, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.ItemSelD",             "Item Select Color D",      COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 10,  50,  80, 255), false, true, true),

    COSMETIC_OPTION("Kaleido.EquipSelA",            "Equip Select Color",       COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 10,  50,  40, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.EquipSelB",            "Equip Select Color B",     COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 90, 100,  60, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.EquipSelC",            "Equip Select Color C",     COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 90, 100,  60, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.EquipSelD",            "Equip Select Color D",     COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 10,  50,  80, 255), false, true, true),

    COSMETIC_OPTION("Kaleido.MapSelDunA",           "Map Dungeon Color",        COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 80,  40,  30, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.MapSelDunB",           "Map Dungeon Color B",      COSMETICS_GROUP_KALEIDO,      ColorRGBA8(140,  60,  60, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.MapSelDunC",           "Map Dungeon Color C",      COSMETICS_GROUP_KALEIDO,      ColorRGBA8(140,  60,  60, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.MapSelDunD",           "Map Dungeon Color D",      COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 80,  40,  30, 255), false, true, true),

    COSMETIC_OPTION("Kaleido.QuestStatusA",         "Quest Status Color",       COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 80,  80,  50, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.QuestStatusB",         "Quest Status Color B",     COSMETICS_GROUP_KALEIDO,      ColorRGBA8(120, 120,  70, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.QuestStatusC",         "Quest Status Color C",     COSMETICS_GROUP_KALEIDO,      ColorRGBA8(120, 120,  70, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.QuestStatusD",         "Quest Status Color D",     COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 80,  80,  50, 255), false, true, true),

    COSMETIC_OPTION("Kaleido.MapSelectA",           "Map Color",                COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 80,  40,  30, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.MapSelectB",           "Map Color B",              COSMETICS_GROUP_KALEIDO,      ColorRGBA8(140,  60,  60, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.MapSelectC",           "Map Color C",              COSMETICS_GROUP_KALEIDO,      ColorRGBA8(140,  60,  60, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.MapSelectD",           "Map Color D",              COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 80,  40,  30, 255), false, true, true),

    COSMETIC_OPTION("Kaleido.SaveA",                "Save Color",               COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 50,  50,  50, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.SaveB",                "Save Color B",             COSMETICS_GROUP_KALEIDO,      ColorRGBA8(110, 110, 110, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.SaveC",                "Save Color C",             COSMETICS_GROUP_KALEIDO,      ColorRGBA8(110, 110, 110, 255), false, true, true),
    COSMETIC_OPTION("Kaleido.SaveD",                "Save Color D",             COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 50,  50,  50, 255), false, true, true),

    COSMETIC_OPTION("Kaleido.NamePanel",            "Name Panel",               COSMETICS_GROUP_KALEIDO,      ColorRGBA8( 90, 100, 130, 255), true,  true, true),

    COSMETIC_OPTION("Title.FileChoose",             "File Choose",              COSMETICS_GROUP_TITLE,        ColorRGBA8(100, 150, 255, 255), false, true, false),
    COSMETIC_OPTION("Title.NintendoLogo",           "Nintendo Logo",            COSMETICS_GROUP_TITLE,        ColorRGBA8(  0,   0, 255, 255), false, true, true),
    COSMETIC_OPTION("Title.N64LogoRed",             "N64 Red",                  COSMETICS_GROUP_TITLE,        ColorRGBA8(150,   0,   0, 255), false, true, true),
    COSMETIC_OPTION("Title.N64LogoBlue",            "N64 Blue",                 COSMETICS_GROUP_TITLE,        ColorRGBA8(  0,  50, 150, 255), false, true, true),
    COSMETIC_OPTION("Title.N64LogoGreen",           "N64 Green",                COSMETICS_GROUP_TITLE,        ColorRGBA8( 50, 100,   0, 255), false, true, true),
    COSMETIC_OPTION("Title.N64LogoYellow",          "N64 Yellow",               COSMETICS_GROUP_TITLE,        ColorRGBA8(200, 150,   0, 255), false, true, true),

    /* Todo (Cosmetics): Kinda complicated
    COSMETIC_OPTION("Title.FirePrimary",            "Title Fire Primary",       COSMETICS_GROUP_TITLE,        ColorRGBA8(255, 255, 170, 255), false, true, false),
    COSMETIC_OPTION("Title.FireSecondary",          "Title Fire Secondary",     COSMETICS_GROUP_TITLE,        ColorRGBA8(255, 100,   0, 255), false, true, true),
    */
    COSMETIC_OPTION("Title.Copyright",              "Copyright Text",           COSMETICS_GROUP_TITLE,        ColorRGBA8(255, 255, 255, 255), true,  true, false),

    COSMETIC_OPTION("Arrows.NormalPrimary",         "Normal Primary",           COSMETICS_GROUP_ARROWS,       ColorRGBA8(  0, 150,   0,   0), false, true, false),
    COSMETIC_OPTION("Arrows.NormalSecondary",       "Normal Secondary",         COSMETICS_GROUP_ARROWS,       ColorRGBA8(255, 255, 170, 255), false, true, true),
    COSMETIC_OPTION("Arrows.FirePrimary",           "Fire Primary",             COSMETICS_GROUP_ARROWS,       ColorRGBA8(255, 200,   0,   0), false, true, false),
    COSMETIC_OPTION("Arrows.FireSecondary",         "Fire Secondary",           COSMETICS_GROUP_ARROWS,       ColorRGBA8(255,   0,   0, 255), false, true, true),
    COSMETIC_OPTION("Arrows.IcePrimary",            "Ice Primary",              COSMETICS_GROUP_ARROWS,       ColorRGBA8(  0,   0, 255, 255), false, true, false),
    COSMETIC_OPTION("Arrows.IceSecondary",          "Ice Secondary",            COSMETICS_GROUP_ARROWS,       ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("Arrows.LightPrimary",          "Light Primary",            COSMETICS_GROUP_ARROWS,       ColorRGBA8(255, 255,   0, 255), false, true, false),
    COSMETIC_OPTION("Arrows.LightSecondary",        "Light Secondary",          COSMETICS_GROUP_ARROWS,       ColorRGBA8(255, 255, 170,   0), false, true, true),

    COSMETIC_OPTION("Magic.DinsPrimary",            "Din's Primary",            COSMETICS_GROUP_MAGIC,        ColorRGBA8(255, 200,   0, 255), false, true, false),
    COSMETIC_OPTION("Magic.DinsSecondary",          "Din's Secondary",          COSMETICS_GROUP_MAGIC,        ColorRGBA8(255,   0,   0, 255), false, true, true),
    COSMETIC_OPTION("Magic.FaroresPrimary",         "Farore's Primary",         COSMETICS_GROUP_MAGIC,        ColorRGBA8(255, 255,   0, 255), false, true, false),
    COSMETIC_OPTION("Magic.FaroresSecondary",       "Farore's Secondary",       COSMETICS_GROUP_MAGIC,        ColorRGBA8(100, 200,   0, 255), false, true, true),
    COSMETIC_OPTION("Magic.NayrusPrimary",          "Nayru's Primary",          COSMETICS_GROUP_MAGIC,        ColorRGBA8(170, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Magic.NayrusSecondary",        "Nayru's Secondary",        COSMETICS_GROUP_MAGIC,        ColorRGBA8(  0, 100, 255, 255), false, true, true),

    COSMETIC_OPTION("SpinAttack.Level1Primary",     "Level 1 Primary",          COSMETICS_GROUP_SPIN_ATTACK,  ColorRGBA8(170, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("SpinAttack.Level1Secondary",   "Level 1 Secondary",        COSMETICS_GROUP_SPIN_ATTACK,  ColorRGBA8(  0, 100, 255, 255), false, true, false),
    COSMETIC_OPTION("SpinAttack.Level2Primary",     "Level 2 Primary",          COSMETICS_GROUP_SPIN_ATTACK,  ColorRGBA8(255, 255, 170, 255), false, true, true),
    COSMETIC_OPTION("SpinAttack.Level2Secondary",   "Level 2 Secondary",        COSMETICS_GROUP_SPIN_ATTACK,  ColorRGBA8(255, 100,   0, 255), false, true, false),

    COSMETIC_OPTION("Trails.Bombchu",               "Bombchu",                  COSMETICS_GROUP_TRAILS,       ColorRGBA8(250,   0,   0, 255), false, true, true),
    COSMETIC_OPTION("Trails.Boomerang",             "Boomerang",                COSMETICS_GROUP_TRAILS,       ColorRGBA8(255, 255, 100, 255), false, true, true),
    COSMETIC_OPTION("Trails.KokiriSword",           "Kokiri Sword",             COSMETICS_GROUP_TRAILS,       ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Trails.MasterSword",           "Master Sword",             COSMETICS_GROUP_TRAILS,       ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Trails.BiggoronSword",         "Biggoron Sword",           COSMETICS_GROUP_TRAILS,       ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("Trails.Stick",                 "Stick",                    COSMETICS_GROUP_TRAILS,       ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("Trails.Hammer",                "Hammer",                   COSMETICS_GROUP_TRAILS,       ColorRGBA8(255, 255, 255, 255), false, true, true),

    COSMETIC_OPTION("World.BlockOfTime",            "Block of Time",            COSMETICS_GROUP_WORLD,        ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("World.Moon",                   "Moon",                     COSMETICS_GROUP_WORLD,        ColorRGBA8(240, 255, 180, 255), false, true, true),
    COSMETIC_OPTION("World.GossipStone",            "Gossip Stone",             COSMETICS_GROUP_WORLD,        ColorRGBA8(200, 200, 200, 255), false, true, true),
    COSMETIC_OPTION("World.RedIce",                 "Red Ice",                  COSMETICS_GROUP_WORLD,        ColorRGBA8(255,   0,   0, 255), false, true, false),
    COSMETIC_OPTION("World.MysteryItem",            "Mystery Item",             COSMETICS_GROUP_WORLD,        ColorRGBA8(  0,  60, 100, 255), false, true, false),

    COSMETIC_OPTION("Navi.IdlePrimary",             "Idle Primary",             COSMETICS_GROUP_NAVI,         ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Navi.IdleSecondary",           "Idle Secondary",           COSMETICS_GROUP_NAVI,         ColorRGBA8(  0,   0, 255,   0), false, true, true),
    COSMETIC_OPTION("Navi.NPCPrimary",              "NPC Primary",              COSMETICS_GROUP_NAVI,         ColorRGBA8(150, 150, 255, 255), false, true, false),
    COSMETIC_OPTION("Navi.NPCSecondary",            "NPC Secondary",            COSMETICS_GROUP_NAVI,         ColorRGBA8(150, 150, 255,   0), false, true, true),
    COSMETIC_OPTION("Navi.EnemyPrimary",            "Enemy Primary",            COSMETICS_GROUP_NAVI,         ColorRGBA8(255, 255,   0, 255), false, true, false),
    COSMETIC_OPTION("Navi.EnemySecondary",          "Enemy Secondary",          COSMETICS_GROUP_NAVI,         ColorRGBA8(200, 155,   0,   0), false, true, true),
    COSMETIC_OPTION("Navi.PropsPrimary",            "Props Primary",            COSMETICS_GROUP_NAVI,         ColorRGBA8(  0, 255,   0, 255), false, true, false),
    COSMETIC_OPTION("Navi.PropsSecondary",          "Props Secondary",          COSMETICS_GROUP_NAVI,         ColorRGBA8(  0, 255,   0,   0), false, true, true),

    COSMETIC_OPTION("Ivan.IdlePrimary",             "Ivan Idle Primary",        COSMETICS_GROUP_IVAN,         ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("Ivan.IdleSecondary",           "Ivan Idle Secondary",      COSMETICS_GROUP_IVAN,         ColorRGBA8(  0, 255,   0, 255), false, true, true),

    COSMETIC_OPTION("NPC.FireKeesePrimary",         "Fire Keese Primary",       COSMETICS_GROUP_NPC,          ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("NPC.FireKeeseSecondary",       "Fire Keese Secondary",     COSMETICS_GROUP_NPC,          ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("NPC.IceKeesePrimary",          "Ice Keese Primary",        COSMETICS_GROUP_NPC,          ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("NPC.IceKeeseSecondary",        "Ice Keese Secondary",      COSMETICS_GROUP_NPC,          ColorRGBA8(255, 255, 255, 255), false, true, true),
    // Todo (Cosmetics): Health fairy
    COSMETIC_OPTION("NPC.Dog1",                     "Dog 1",                    COSMETICS_GROUP_NPC,          ColorRGBA8(255, 255, 200, 255), false, true, true),
    COSMETIC_OPTION("NPC.Dog2",                     "Dog 2",                    COSMETICS_GROUP_NPC,          ColorRGBA8(150, 100,  50, 255), false, true, true),
    COSMETIC_OPTION("NPC.GoldenSkulltula",          "Golden Skulltula",         COSMETICS_GROUP_NPC,          ColorRGBA8(255, 255, 255, 255), false, true, false),
    COSMETIC_OPTION("NPC.Kokiri",                   "Kokiri",                   COSMETICS_GROUP_NPC,          ColorRGBA8(  0, 130,  70, 255), false, true, false),
    COSMETIC_OPTION("NPC.Gerudo",                   "Gerudo",                   COSMETICS_GROUP_NPC,          ColorRGBA8( 90,   0, 140, 255), false, true, false),
    COSMETIC_OPTION("NPC.MetalTrap",                "Metal Trap",               COSMETICS_GROUP_NPC,          ColorRGBA8(255, 255, 255, 255), false, true, true),
    COSMETIC_OPTION("NPC.IronKnuckles",             "Iron Knuckles",            COSMETICS_GROUP_NPC,          ColorRGBA8(245, 255, 205, 255), false, true, false),
    };
    return options;
}
// clang-format on

extern "C" Color_RGBA8 CosmeticsEditor_GetDefaultValue(const char* id) {
    return Color_RGBA8{ (uint8_t)(CosmeticOptions()[id].defaultColor.r * 255.0f),
                        (uint8_t)(CosmeticOptions()[id].defaultColor.g * 255.0f),
                        (uint8_t)(CosmeticOptions()[id].defaultColor.b * 255.0f),
                        (uint8_t)(CosmeticOptions()[id].defaultColor.a * 255.0f) };
}
