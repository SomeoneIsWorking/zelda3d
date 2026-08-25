#include "controller_feedback.h"

#include "functions/ui.h"
#include "macros.h"
#include "soh/Enhancements/gameconsole.h"
#include "soh/cvar_prefixes.h"
#include "variables.h"
#include "z64.h"

#include <libultraship/bridge/consolevariablebridge.h>
#include <ship/Context.h>
#include <ship/controller/controldeck/ControlDeck.h>
#include <ship/controller/controldevice/controller/Controller.h>

namespace {

const Color_RGB8 kKokiriTunicColor = { 0x1E, 0x69, 0x1B };
const Color_RGB8 kGoronTunicColor = { 0x64, 0x14, 0x00 };
const Color_RGB8 kZoraTunicColor = { 0x00, 0xEC, 0x64 };

// Same as NaviColor from OoT's z_actor.c, but without alpha for controller LEDs.
struct NaviColorRgb8 {
    Color_RGB8 inner;
    Color_RGB8 outer;
};

const NaviColorRgb8 kDefaultIdleColor = { { 255, 255, 255 }, { 0, 0, 255 } };
const NaviColorRgb8 kDefaultNpcColor = { { 150, 150, 255 }, { 150, 150, 255 } };
const NaviColorRgb8 kDefaultEnemyColor = { { 255, 255, 0 }, { 200, 155, 0 } };
const NaviColorRgb8 kDefaultPropsColor = { { 0, 255, 0 }, { 0, 255, 0 } };

// Indexed by ActorCategory.
const NaviColorRgb8 kDefaultNaviColors[] = {
    kDefaultPropsColor, // ACTORCAT_SWITCH       Switch
    kDefaultPropsColor, // ACTORCAT_BG           Background (Prop type 1)
    kDefaultIdleColor,  // ACTORCAT_PLAYER       Player
    kDefaultPropsColor, // ACTORCAT_EXPLOSIVE    Bomb
    kDefaultNpcColor,   // ACTORCAT_NPC          NPC
    kDefaultEnemyColor, // ACTORCAT_ENEMY        Enemy
    kDefaultPropsColor, // ACTORCAT_PROP         Prop type 2
    kDefaultPropsColor, // ACTORCAT_ITEMACTION   Item/Action
    kDefaultPropsColor, // ACTORCAT_MISC         Misc.
    kDefaultEnemyColor, // ACTORCAT_BOSS         Boss
    kDefaultPropsColor, // ACTORCAT_DOOR         Door
    kDefaultPropsColor, // ACTORCAT_CHEST        Chest
    kDefaultPropsColor, // ACTORCAT_MAX
};

} // namespace

Color_RGB8 GetColorForControllerLED() {
    auto brightness = CVarGetFloat(CVAR_SETTING("LEDBrightness"), 1.0f) / 1.0f;
    Color_RGB8 color = { 0, 0, 0 };
    if (brightness > 0.0f) {
        LEDColorSource source =
            static_cast<LEDColorSource>(CVarGetInteger(CVAR_SETTING("LEDColorSource"), LED_SOURCE_TUNIC_ORIGINAL));
        bool criticalOverride = CVarGetInteger(CVAR_SETTING("LEDCriticalOverride"), 1);
        if (gPlayState && (source == LED_SOURCE_TUNIC_ORIGINAL || source == LED_SOURCE_TUNIC_COSMETICS)) {
            switch (CUR_EQUIP_VALUE(EQUIP_TYPE_TUNIC)) {
                case EQUIP_VALUE_TUNIC_KOKIRI:
                    color = source == LED_SOURCE_TUNIC_COSMETICS
                                ? CVarGetColor24(CVAR_COSMETIC("Link.KokiriTunic.Value"), kKokiriTunicColor)
                                : kKokiriTunicColor;
                    break;
                case EQUIP_VALUE_TUNIC_GORON:
                    color = source == LED_SOURCE_TUNIC_COSMETICS
                                ? CVarGetColor24(CVAR_COSMETIC("Link.GoronTunic.Value"), kGoronTunicColor)
                                : kGoronTunicColor;
                    break;
                case EQUIP_VALUE_TUNIC_ZORA:
                    color = source == LED_SOURCE_TUNIC_COSMETICS
                                ? CVarGetColor24(CVAR_COSMETIC("Link.ZoraTunic.Value"), kZoraTunicColor)
                                : kZoraTunicColor;
                    break;
            }
        }
        if (gPlayState && (source == LED_SOURCE_NAVI_ORIGINAL || source == LED_SOURCE_NAVI_COSMETICS)) {
            Actor* arrowPointedActor = gPlayState->actorCtx.targetCtx.arrowPointedActor;
            if (arrowPointedActor) {
                ActorCategory category = static_cast<ActorCategory>(arrowPointedActor->category);
                switch (category) {
                    case ACTORCAT_PLAYER:
                        if (source == LED_SOURCE_NAVI_COSMETICS &&
                            CVarGetInteger(CVAR_COSMETIC("Navi.IdlePrimary.Changed"), 0)) {
                            color = CVarGetColor24(CVAR_COSMETIC("Navi.IdlePrimary.Value"), kDefaultIdleColor.inner);
                            break;
                        }
                        color = kDefaultNaviColors[category].inner;
                        break;
                    case ACTORCAT_NPC:
                        if (source == LED_SOURCE_NAVI_COSMETICS &&
                            CVarGetInteger(CVAR_COSMETIC("Navi.NPCPrimary.Changed"), 0)) {
                            color = CVarGetColor24(CVAR_COSMETIC("Navi.NPCPrimary.Value"), kDefaultNpcColor.inner);
                            break;
                        }
                        color = kDefaultNaviColors[category].inner;
                        break;
                    case ACTORCAT_ENEMY:
                    case ACTORCAT_BOSS:
                        if (source == LED_SOURCE_NAVI_COSMETICS &&
                            CVarGetInteger(CVAR_COSMETIC("Navi.EnemyPrimary.Changed"), 0)) {
                            color = CVarGetColor24(CVAR_COSMETIC("Navi.EnemyPrimary.Value"), kDefaultEnemyColor.inner);
                            break;
                        }
                        color = kDefaultNaviColors[category].inner;
                        break;
                    default:
                        if (source == LED_SOURCE_NAVI_COSMETICS &&
                            CVarGetInteger(CVAR_COSMETIC("Navi.PropsPrimary.Changed"), 0)) {
                            color = CVarGetColor24(CVAR_COSMETIC("Navi.PropsPrimary.Value"), kDefaultPropsColor.inner);
                            break;
                        }
                        color = kDefaultNaviColors[category].inner;
                }
            } else {
                if (source == LED_SOURCE_NAVI_COSMETICS &&
                    CVarGetInteger(CVAR_COSMETIC("Navi.IdlePrimary.Changed"), 0)) {
                    color = CVarGetColor24(CVAR_COSMETIC("Navi.IdlePrimary.Value"), kDefaultIdleColor.inner);
                } else {
                    color = kDefaultNaviColors[ACTORCAT_PLAYER].inner;
                }
            }
        }
        if (source == LED_SOURCE_CUSTOM) {
            color = CVarGetColor24(CVAR_SETTING("LEDPort1Color"), { 255, 255, 255 });
        }
        if (gPlayState && (criticalOverride || source == LED_SOURCE_HEALTH)) {
            if (HealthMeter_IsCritical()) {
                color = { 0xFF, 0, 0 };
            } else if (gSaveContext.healthCapacity != 0 && source == LED_SOURCE_HEALTH) {
                if (gSaveContext.health / static_cast<float>(gSaveContext.healthCapacity) <= 0.4f) {
                    color = { 0xFF, 0xFF, 0 };
                } else {
                    color = { 0, 0xFF, 0 };
                }
            }
        }
        color.r = static_cast<u8>(color.r * brightness);
        color.g = static_cast<u8>(color.g * brightness);
        color.b = static_cast<u8>(color.b * brightness);
    }

    return color;
}

extern "C" void OTRControllerCallback(uint8_t rumble) {
    // We call this every tick; SDL accounts for this use and prevents driver spam.
    // https://github.com/libsdl-org/SDL/blob/f17058b562c8a1090c0c996b42982721ace90903/src/joystick/SDL_joystick.c#L1114-L1144
    Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0)->GetLED()->SetLEDColor(
        GetColorForControllerLED());

    // The former TestingRumble() gate suppressed rumble while the removed Dear ImGui input editor
    // tested a mapping. Its uninitialised timer could instead suppress ordinary rumble (claim C063).
    if (rumble) {
        Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0)->GetRumble()->StartRumble();
    } else {
        Ship::Context::GetRawInstance()->GetControlDeck()->GetControllerByPort(0)->GetRumble()->StopRumble();
    }
}

extern "C" int Controller_ShouldRumble(size_t slot) {
    if (Ship::Context::GetRawInstance()
            ->GetControlDeck()
            ->GetControllerByPort(static_cast<uint8_t>(slot))
            ->GetRumble()
            ->GetAllRumbleMappings()
            .empty()) {
        return 0;
    }

    if (Ship::Context::GetRawInstance()
            ->GetControlDeck()
            ->GetConnectedPhysicalDeviceManager()
            ->GetConnectedSDLGamepadsForPort(static_cast<s32>(slot))
            .empty()) {
        return 0;
    }

    return 1;
}
