#include "debugconsole_internal.h"

#include "soh/OTRGlobals.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

#include <ship/utils/Utils.h>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "soh/cvar_prefixes.h"
#include <libultraship/bridge.h>

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions/actors.h"
#include "functions/game_state.h"
#include "macros.h"
extern PlayState* gPlayState;
}

static bool InvisibleHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                             std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    uint8_t state;

    try {
        state = std::stoi(args[1], nullptr, 10) == 0 ? 0 : 1;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Invisible value must be a number.");
        return 1;
    }

    GameInteractionEffect::InvisibleLink effect;
    GameInteractionEffectQueryResult result =
        state ? GameInteractor::ApplyEffect(effect) : GameInteractor::RemoveEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Invisible Link %s", state ? "enabled" : "disabled");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not %s Invisible Link.", state ? "enable" : "disable");
        return 1;
    }
}

static bool GiantLinkHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                             std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    uint8_t state;

    try {
        state = std::stoi(args[1], nullptr, 10) == 0 ? 0 : 1;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Giant value must be a number.");
        return 1;
    }

    GameInteractionEffect::ModifyLinkSize effect;
    effect.parameters[0] = GI_LINK_SIZE_GIANT;
    GameInteractionEffectQueryResult result =
        state ? GameInteractor::ApplyEffect(effect) : GameInteractor::RemoveEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Giant Link %s", state ? "enabled" : "disabled");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not %s Giant Link.", state ? "enable" : "disable");
        return 1;
    }
}

static bool MinishLinkHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                              std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    uint8_t state;

    try {
        state = std::stoi(args[1], nullptr, 10) == 0 ? 0 : 1;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Minish value must be a number.");
        return 1;
    }

    GameInteractionEffect::ModifyLinkSize effect;
    effect.parameters[0] = GI_LINK_SIZE_MINISH;
    GameInteractionEffectQueryResult result =
        state ? GameInteractor::ApplyEffect(effect) : GameInteractor::RemoveEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Minish Link %s", state ? "enabled" : "disabled");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not %s Minish Link.", state ? "enable" : "disable");
        return 1;
    }
}

static bool AddHeartContainerHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                                     std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    int hearts;

    try {
        hearts = std::stoi(args[1]);
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Hearts value must be an integer.");
        return 1;
    }

    if (hearts < 0) {
        ERROR_MESSAGE("[SOH] Hearts value must be a positive integer");
        return 1;
    }

    GameInteractionEffect::ModifyHeartContainers effect;
    effect.parameters[0] = hearts;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Added %d heart containers", hearts);
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not add heart containers.");
        return 1;
    }
}

static bool RemoveHeartContainerHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                                        std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    int hearts;

    try {
        hearts = std::stoi(args[1]);
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Hearts value must be an integer.");
        return 1;
    }

    if (hearts < 0) {
        ERROR_MESSAGE("[SOH] Hearts value must be a positive integer");
        return 1;
    }

    GameInteractionEffect::ModifyHeartContainers effect;
    effect.parameters[0] = -hearts;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Removed %d heart containers", hearts);
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not remove heart containers.");
        return 1;
    }
}

static bool GravityHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                           std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }

    GameInteractionEffect::ModifyGravity effect;

    try {
        effect.parameters[0] = static_cast<int32_t>(Ship::Math::clamp(
            static_cast<float>(std::stoi(args[1], nullptr, 10)), GI_GRAVITY_LEVEL_LIGHT, GI_GRAVITY_LEVEL_HEAVY));
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Gravity value must be a number.");
        return 1;
    }

    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Updated gravity.");
        return 0;
    } else {
        ERROR_MESSAGE("[SOH] Command failed: Could not update gravity.");
        return 1;
    }
}

static bool NoUIHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                        std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    uint8_t state;

    try {
        state = std::stoi(args[1], nullptr, 10) == 0 ? 0 : 1;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] No UI value must be a number.");
        return 1;
    }

    GameInteractionEffect::NoUI effect;
    GameInteractionEffectQueryResult result =
        state ? GameInteractor::ApplyEffect(effect) : GameInteractor::RemoveEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] No UI %s", state ? "enabled" : "disabled");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not %s No UI.", state ? "enable" : "disable");
        return 1;
    }
}

static bool FreezeHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                          std::string* output) {
    GameInteractionEffect::FreezePlayer effect;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Player frozen");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not freeze player.");
        return 1;
    }
}

static bool DefenseModifierHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                                   std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    GameInteractionEffect::ModifyDefenseModifier effect;

    try {
        effect.parameters[0] = std::stoi(args[1], nullptr, 10);
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Defense modifier value must be a number.");
        return 1;
    }

    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Defense modifier set to %d", effect.parameters[0]);
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not set defense modifier.");
        return 1;
    }
}

static bool DamageHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                          std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    GameInteractionEffect::ModifyHealth effect;

    try {
        int value = std::stoi(args[1], nullptr, 10);
        if (value < 0) {
            ERROR_MESSAGE("[SOH] Invalid value passed. Value must be greater than 0");
            return 1;
        }

        effect.parameters[0] = -value;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Damage value must be a number.");
        return 1;
    }

    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Player damaged");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not damage player.");
        return 1;
    }
}

static bool HealHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                        std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    GameInteractionEffect::ModifyHealth effect;

    try {
        int value = std::stoi(args[1], nullptr, 10);
        if (value < 0) {
            ERROR_MESSAGE("[SOH] Invalid value passed. Value must be greater than 0");
            return 1;
        }

        effect.parameters[0] = value;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Damage value must be a number.");
        return 1;
    }

    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Player healed");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not heal player.");
        return 1;
    }
}

static bool FillMagicHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                             std::string* output) {
    GameInteractionEffect::FillMagic effect;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Magic filled");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not fill magic.");
        return 1;
    }
}

static bool EmptyMagicHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                              std::string* output) {
    GameInteractionEffect::EmptyMagic effect;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Magic emptied");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not empty magic.");
        return 1;
    }
}

static bool NoZHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                       std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    uint8_t state;

    try {
        state = std::stoi(args[1], nullptr, 10) == 0 ? 0 : 1;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] NoZ value must be a number.");
        return 1;
    }

    GameInteractionEffect::DisableZTargeting effect;
    GameInteractionEffectQueryResult result =
        state ? GameInteractor::ApplyEffect(effect) : GameInteractor::RemoveEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] NoZ " + std::string(state ? "enabled" : "disabled"));
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not " + std::string(state ? "enable" : "disable") + " NoZ.");
        return 1;
    }
}

static bool OneHitKOHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                            std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    uint8_t state;

    try {
        state = std::stoi(args[1], nullptr, 10) == 0 ? 0 : 1;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] One-hit KO value must be a number.");
        return 1;
    }

    GameInteractionEffect::OneHitKO effect;
    GameInteractionEffectQueryResult result =
        state ? GameInteractor::ApplyEffect(effect) : GameInteractor::RemoveEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] One-hit KO " + std::string(state ? "enabled" : "disabled"));
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not " + std::string(state ? "enable" : "disable") + " One-hit KO.");
        return 1;
    }
}

static bool PacifistHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                            std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    uint8_t state;

    try {
        state = std::stoi(args[1], nullptr, 10) == 0 ? 0 : 1;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Pacifist value must be a number.");
        return 1;
    }

    GameInteractionEffect::PacifistMode effect;
    GameInteractionEffectQueryResult result =
        state ? GameInteractor::ApplyEffect(effect) : GameInteractor::RemoveEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Pacifist " + std::string(state ? "enabled" : "disabled"));
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not " + std::string(state ? "enable" : "disable") + " Pacifist.");
        return 1;
    }
}

static bool PaperLinkHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                             std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    uint8_t state;

    try {
        state = std::stoi(args[1], nullptr, 10) == 0 ? 0 : 1;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Paper Link value must be a number.");
        return 1;
    }

    GameInteractionEffect::ModifyLinkSize effect;
    effect.parameters[0] = GI_LINK_SIZE_PAPER;
    GameInteractionEffectQueryResult result =
        state ? GameInteractor::ApplyEffect(effect) : GameInteractor::RemoveEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Paper Link " + std::string(state ? "enabled" : "disabled"));
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not " + std::string(state ? "enable" : "disable") + " Paper Link.");
        return 1;
    }
}

static bool RainstormHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                             std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    uint8_t state;

    try {
        state = std::stoi(args[1], nullptr, 10) == 0 ? 0 : 1;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Rainstorm value must be a number.");
        return 1;
    }

    GameInteractionEffect::WeatherRainstorm effect;
    GameInteractionEffectQueryResult result =
        state ? GameInteractor::ApplyEffect(effect) : GameInteractor::RemoveEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Rainstorm " + std::string(state ? "enabled" : "disabled"));
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not " + std::string(state ? "enable" : "disable") + " Rainstorm.");
        return 1;
    }
}

static bool ReverseControlsHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                                   std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    uint8_t state;

    try {
        state = std::stoi(args[1], nullptr, 10) == 0 ? 0 : 1;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Reverse controls value must be a number.");
        return 1;
    }

    GameInteractionEffect::ReverseControls effect;
    GameInteractionEffectQueryResult result =
        state ? GameInteractor::ApplyEffect(effect) : GameInteractor::RemoveEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Reverse controls " + std::string(state ? "enabled" : "disabled"));
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not " + std::string(state ? "enable" : "disable") +
                     " Reverse controls.");
        return 1;
    }
}

static bool UpdateRupeesHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                                std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    GameInteractionEffect::ModifyRupees effect;

    try {
        effect.parameters[0] = std::stoi(args[1], nullptr, 10);
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Rupee value must be a number.");
        return 1;
    }

    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Rupees updated");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not update rupees.");
        return 1;
    }
}

static bool SpeedModifierHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                                 std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    GameInteractionEffect::ModifyMovementSpeedMultiplier effect;

    try {
        effect.parameters[0] = std::stoi(args[1], nullptr, 10);
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Speed modifier value must be a number.");
        return 1;
    }

    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Speed modifier updated");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not update speed modifier.");
        return 1;
    }
}

const static std::map<std::string, uint16_t> boots{
    { "kokiri", EQUIP_VALUE_BOOTS_KOKIRI },
    { "iron", EQUIP_VALUE_BOOTS_IRON },
    { "hover", EQUIP_VALUE_BOOTS_HOVER },
};

static bool BootsHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                         std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }

    const auto& it = boots.find(args[1]);
    if (it == boots.end()) {
        ERROR_MESSAGE("Invalid boot type. Options are 'kokiri', 'iron' and 'hover'");
        return 1;
    }

    GameInteractionEffect::ForceEquipBoots effect;
    effect.parameters[0] = it->second;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Boots updated.");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not update boots.");
        return 1;
    }
}

const static std::map<std::string, uint16_t> shields{
    { "deku", ITEM_SHIELD_DEKU },
    { "hylian", ITEM_SHIELD_HYLIAN },
    { "mirror", ITEM_SHIELD_MIRROR },
};

static bool GiveShieldHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                              std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }

    const auto& it = shields.find(args[1]);
    if (it == shields.end()) {
        ERROR_MESSAGE("Invalid shield type. Options are 'deku', 'hylian' and 'mirror'");
        return 1;
    }

    GameInteractionEffect::GiveOrTakeShield effect;
    effect.parameters[0] = it->second;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Gave shield.");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not give shield.");
        return 1;
    }
}

static bool TakeShieldHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                              std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }

    const auto& it = shields.find(args[1]);
    if (it == shields.end()) {
        ERROR_MESSAGE("Invalid shield type. Options are 'deku', 'hylian' and 'mirror'");
        return 1;
    }

    GameInteractionEffect::GiveOrTakeShield effect;
    effect.parameters[0] = it->second * -1;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Took shield.");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not take shield.");
        return 1;
    }
}

static bool KnockbackHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                             std::string* output) {
    if (args.size() < 2) {
        ERROR_MESSAGE("[SOH] Unexpected arguments passed");
        return 1;
    }
    GameInteractionEffect::KnockbackPlayer effect;

    try {
        int value = std::stoi(args[1], nullptr, 10);
        if (value < 0) {
            ERROR_MESSAGE("[SOH] Invalid value passed. Value must be greater than 0");
            return 1;
        }

        effect.parameters[0] = value;
    } catch ([[maybe_unused]] std::invalid_argument const& ex) {
        ERROR_MESSAGE("[SOH] Knockback value must be a number.");
        return 1;
    }

    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);
    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Knockback applied");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not apply knockback.");
        return 1;
    }
}

static bool ElectrocuteHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                               std::string* output) {
    GameInteractionEffect::ElectrocutePlayer effect;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Electrocuted player");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not electrocute player.");
        return 1;
    }
}

static bool BurnHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                        std::string* output) {
    GameInteractionEffect::BurnPlayer effect;
    GameInteractionEffectQueryResult result = GameInteractor::ApplyEffect(effect);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Burned player");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not burn player.");
        return 1;
    }
}

static bool CuccoStormHandler(std::shared_ptr<Ship::Console> Console, const std::vector<std::string>& args,
                              std::string* output) {
    GameInteractionEffectQueryResult result = GameInteractor::RawAction::SpawnActor(ACTOR_EN_NIW, 0);

    if (result == GameInteractionEffectQueryResult::Possible) {
        INFO_MESSAGE("[SOH] Spawned cucco storm");
        return 0;
    } else {
        INFO_MESSAGE("[SOH] Command failed: Could not spawn cucco storm.");
        return 1;
    }
}

void DebugConsole_RegisterPlayerCommands() {
    CMD_REGISTER("invisible", { InvisibleHandler,
                                "Activate Link's Elvish cloak, making him appear invisible.",
                                {
                                    { "value", Ship::ArgumentType::NUMBER },
                                } });

    CMD_REGISTER("giant_link", { GiantLinkHandler,
                                 "Turn Link into a giant Lonky boi.",
                                 {
                                     { "value", Ship::ArgumentType::NUMBER },
                                 } });

    CMD_REGISTER("minish_link", { MinishLinkHandler,
                                  "Turn Link into a minish boi.",
                                  {
                                      { "value", Ship::ArgumentType::NUMBER },
                                  } });

    CMD_REGISTER("add_heart_container",
                 { AddHeartContainerHandler, "Give Link a heart! The maximum amount of hearts is 20!" });

    CMD_REGISTER("remove_heart_container",
                 { RemoveHeartContainerHandler, "Remove a heart from Link. The minimal amount of hearts is 3." });

    CMD_REGISTER("gravity", { GravityHandler,
                              "Set gravity level.",
                              {
                                  { "value", Ship::ArgumentType::NUMBER },
                              } });

    CMD_REGISTER("no_ui", { NoUIHandler,
                            "Disables the UI.",
                            {
                                { "value", Ship::ArgumentType::NUMBER },
                            } });

    CMD_REGISTER("freeze", { FreezeHandler, "Freezes Link in place" });

    CMD_REGISTER("defense_modifier", { DefenseModifierHandler,
                                       "Sets the defense modifier.",
                                       {
                                           { "value", Ship::ArgumentType::NUMBER },
                                       } });

    CMD_REGISTER("damage", { DamageHandler,
                             "Deal damage to Link.",
                             {
                                 { "value", Ship::ArgumentType::NUMBER },
                             } });

    CMD_REGISTER("heal", { HealHandler,
                           "Heals Link.",
                           {
                               { "value", Ship::ArgumentType::NUMBER },
                           } });

    CMD_REGISTER("fill_magic", { FillMagicHandler, "Fills magic." });

    CMD_REGISTER("empty_magic", { EmptyMagicHandler, "Empties magic." });

    CMD_REGISTER("no_z", { NoZHandler,
                           "Disables Z-button presses.",
                           {
                               { "value", Ship::ArgumentType::NUMBER },
                           } });

    CMD_REGISTER("ohko", { OneHitKOHandler,
                           "Activates one hit KO. Any damage kills Link and he cannot gain health in this mode.",
                           {
                               { "value", Ship::ArgumentType::NUMBER },
                           } });

    CMD_REGISTER("pacifist", { PacifistHandler,
                               "Activates pacifist mode. Prevents Link from using his weapon.",
                               {
                                   { "value", Ship::ArgumentType::NUMBER },
                               } });

    CMD_REGISTER("paper_link", { PaperLinkHandler,
                                 "Link but made out of paper.",
                                 {
                                     { "value", Ship::ArgumentType::NUMBER },
                                 } });

    CMD_REGISTER("rainstorm", { RainstormHandler, "Activates rainstorm." });

    CMD_REGISTER("reverse_controls", { ReverseControlsHandler,
                                       "Reverses the controls.",
                                       {
                                           { "value", Ship::ArgumentType::NUMBER },
                                       } });

    CMD_REGISTER("update_rupees", { UpdateRupeesHandler,
                                    "Adds rupees.",
                                    {
                                        { "value", Ship::ArgumentType::NUMBER },
                                    } });

    CMD_REGISTER("speed_modifier", { SpeedModifierHandler,
                                     "Sets the speed modifier.",
                                     {
                                         { "value", Ship::ArgumentType::NUMBER },
                                     } });

    CMD_REGISTER("boots", { BootsHandler,
                            "Activates boots.",
                            {
                                { "kokiri|iron|hover", Ship::ArgumentType::TEXT },
                            } });

    CMD_REGISTER("giveshield", { GiveShieldHandler,
                                 "Gives a shield and equips it when Link is the right age for it.",
                                 {
                                     { "deku|hylian|mirror", Ship::ArgumentType::TEXT },
                                 } });

    CMD_REGISTER("takeshield", { TakeShieldHandler,
                                 "Takes a shield and unequips it if Link is wearing it.",
                                 {
                                     { "deku|hylian|mirror", Ship::ArgumentType::TEXT },
                                 } });

    CMD_REGISTER("knockback", { KnockbackHandler,
                                "Knocks Link back.",
                                {
                                    { "value", Ship::ArgumentType::NUMBER },
                                } });

    CMD_REGISTER("electrocute", { ElectrocuteHandler, "Electrocutes Link." });

    CMD_REGISTER("burn", { BurnHandler, "Burns Link." });

    CMD_REGISTER("cucco_storm", { CuccoStormHandler, "Cucco Storm" });
}
