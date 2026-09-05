#include "soh/OTRGlobals.h"
#include "logic.h"
#include "../debugger/performanceTimer.h"

#include <string>
#include <vector>

#include "dungeon.h"
#include "SeedContext.h"
#include "macros.h"
#include "variables.h"
#include <spdlog/spdlog.h>
#include <ship/utils/StringHelper.h>
#include "soh/resource/type/Scene.h"
#include "soh/resource/type/scenecommand/SetTransitionActorList.h"
#include "src/overlays/actors/ovl_En_Door/z_en_door.h"
#include "src/overlays/actors/ovl_Door_Shutter/z_door_shutter.h"
#include "location_access.h"

namespace Rando {

uint8_t GetDifficultyValueFromString(Rando::Option& glitchOption) {
    return 0;
}

// todo rewrite glitch section

bool Logic::CanEquipSwap(RandomizerGet itemName) {
    if (!HasItem(itemName))
        return false;

    if (CanDoGlitch(GlitchType::EquipSwapDins) || CanDoGlitch(GlitchType::EquipSwap))
        return true;

    return false;
}

bool Logic::CanDoGlitch(GlitchType glitch) {
    // TODO: Uncomment when glitches are implemented
    switch (glitch) {
        case GlitchType::EquipSwapDins:
            return ((IsAdult && HasItem(RG_DINS_FIRE)) || (IsChild && (HasItem(RG_STICKS) || HasItem(RG_DINS_FIRE)))) &&
                   false;           // GlitchEquipSwapDins;
        case GlitchType::EquipSwap: // todo: add bunny hood to adult item equippable list and child trade item to child
                                    // item equippable list
            return ((IsAdult && (HasItem(RG_DINS_FIRE) || HasItem(RG_FARORES_WIND) || HasItem(RG_NAYRUS_LOVE))) ||
                    (IsChild && (HasItem(RG_STICKS) || HasItem(RG_FAIRY_SLINGSHOT) || HasItem(RG_BOOMERANG) ||
                                 HasBottle() || CanUse(RG_NUTS) || HasItem(RG_FAIRY_OCARINA) ||
                                 HasItem(RG_LENS_OF_TRUTH) || HasExplosives() || GetAmmo(ITEM_BEAN) > 0 ||
                                 HasItem(RG_DINS_FIRE) || HasItem(RG_FARORES_WIND) || HasItem(RG_NAYRUS_LOVE)))) &&
                   false; // GlitchEquipSwap;
    }

    // Shouldn't be reached
    return false;
}

// RANDOTODO quantity is a placeholder for proper ammo use calculation logic. in time will want updating to account for
// ammo capacity
bool Logic::CanKillEnemy(RandomizerEnemy enemy, EnemyDistance distance, bool wallOrFloor, uint8_t quantity, bool timer,
                         bool inWater) {
    bool killed = false;
    switch (enemy) {
        case RE_GERUDO_GUARD:
        case RE_BREAK_ROOM_GUARD:
            return false;
        case RE_GOLD_SKULLTULA:
            switch (distance) {
                case ED_CLOSE:
                    // hammer jumpslash cannot damage these, but hammer swing can
                    killed = CanUse(RG_MEGATON_HAMMER);
                    [[fallthrough]];
                case ED_SHORT_JUMPSLASH:
                    killed = killed || CanUse(RG_KOKIRI_SWORD);
                    [[fallthrough]];
                case ED_MASTER_SWORD_JUMPSLASH:
                    killed = killed || CanUse(RG_MASTER_SWORD);
                    [[fallthrough]];
                case ED_LONG_JUMPSLASH:
                    killed = killed || CanUse(RG_BIGGORON_SWORD) || CanUse(RG_STICKS);
                    [[fallthrough]];
                case ED_BOMB_THROW:
                    killed = killed || CanUse(RG_BOMB_BAG);
                    [[fallthrough]];
                case ED_BOOMERANG:
                    killed = killed || CanUse(RG_BOOMERANG) || CanUse(RG_DINS_FIRE);
                    [[fallthrough]];
                case ED_HOOKSHOT:
                    // RANDOTODO test dins and chu range in a practical example
                    killed = killed || CanUse(RG_HOOKSHOT);
                    [[fallthrough]];
                case ED_LONGSHOT:
                    killed = killed || CanUse(RG_LONGSHOT) || (wallOrFloor && CanUse(RG_BOMBCHU_5));
                    [[fallthrough]];
                case ED_FAR:
                    killed = killed || CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_FAIRY_BOW);
                    break;
            }
            return killed;
        case RE_GOHMA_LARVA:
        case RE_MAD_SCRUB:
        case RE_DEKU_BABA:
        case RE_POE:
            return CanAttack();
        case RE_BIG_SKULLTULA:
            switch (distance) {
                case ED_CLOSE:
                    // hammer jumpslash cannot damage these, but hammer swing can
                    killed = CanUse(RG_MEGATON_HAMMER);
                    [[fallthrough]];
                case ED_SHORT_JUMPSLASH:
                    killed = killed || CanUse(RG_KOKIRI_SWORD);
                    [[fallthrough]];
                case ED_MASTER_SWORD_JUMPSLASH:
                    killed = killed || CanUse(RG_MASTER_SWORD);
                    [[fallthrough]];
                case ED_LONG_JUMPSLASH:
                    killed = killed || CanUse(RG_BIGGORON_SWORD) || CanUse(RG_STICKS);
                    [[fallthrough]];
                case ED_BOMB_THROW:
                    killed = killed || CanUse(RG_BOMB_BAG) || CanUse(RG_DINS_FIRE);
                    [[fallthrough]];
                case ED_BOOMERANG:
                case ED_HOOKSHOT:
                    // RANDOTODO test chu range in a practical example
                    killed = killed || CanUse(RG_HOOKSHOT) || (wallOrFloor && CanUse(RG_BOMBCHU_5));
                    [[fallthrough]];
                case ED_LONGSHOT:
                    killed = killed || CanUse(RG_LONGSHOT);
                    [[fallthrough]];
                case ED_FAR:
                    killed = killed || CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_FAIRY_BOW);
                    break;
            }
            return killed;
        case RE_DODONGO:
            return CanUseSword() || CanUse(RG_MEGATON_HAMMER) || (quantity <= 5 && CanUse(RG_STICKS)) ||
                   HasExplosives() || CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_FAIRY_BOW);
        case RE_LIZALFOS:
            return CanJumpslash() || HasExplosives() || CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_FAIRY_BOW);
        case RE_KEESE:
        case RE_FIRE_KEESE:
        case RE_GUAY:
            switch (distance) {
                case ED_CLOSE:
                case ED_SHORT_JUMPSLASH:
                    killed = CanUse(RG_MEGATON_HAMMER) || CanUse(RG_KOKIRI_SWORD);
                    [[fallthrough]];
                case ED_MASTER_SWORD_JUMPSLASH:
                    killed = killed || CanUse(RG_MASTER_SWORD);
                    [[fallthrough]];
                case ED_LONG_JUMPSLASH:
                    killed = killed || CanUse(RG_BIGGORON_SWORD) || CanUse(RG_STICKS);
                    [[fallthrough]];
                case ED_BOMB_THROW:
                    // RANDOTODO test chu range in a practical example
                    killed = killed || (!inWater && CanUse(RG_BOMB_BAG)) || (enemy == RE_GUAY && CanUse(RG_DINS_FIRE));
                    [[fallthrough]];
                case ED_BOOMERANG:
                    // RANDOTODO test chu range in a practical example
                    killed = killed || CanUse(RG_BOOMERANG);
                    [[fallthrough]];
                case ED_HOOKSHOT:
                    // RANDOTODO test chu range in a practical example
                    killed = killed || CanUse(RG_HOOKSHOT) || (wallOrFloor && CanUse(RG_BOMBCHU_5));
                    [[fallthrough]];
                case ED_LONGSHOT:
                    killed = killed || CanUse(RG_LONGSHOT);
                    [[fallthrough]];
                case ED_FAR:
                    killed = killed || CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_FAIRY_BOW);
                    break;
            }
            return killed;
        case RE_BLUE_BUBBLE:
            // RANDOTODO Trick to use shield hylian shield as child to stun these guys
            // RANDOTODO check hammer damage
            return BlastOrSmash() || CanUse(RG_FAIRY_BOW) ||
                   ((CanJumpslashExceptHammer() || CanUse(RG_FAIRY_SLINGSHOT)) &&
                    (CanUse(RG_NUTS) || HookshotOrBoomerang() || CanStandingShield()));
        case RE_DEAD_HAND:
            // RANDOTODO change Dead Hand trick to be sticks Dead Hand
            return CanUseSword() || (CanUse(RG_STICKS) && ctx->GetTrickOption(RT_BOTW_CHILD_DEADHAND));
        case RE_WITHERED_DEKU_BABA:
            return CanUseSword() || CanUse(RG_BOOMERANG);
        case RE_LIKE_LIKE:
        case RE_FLOORMASTER:
            return CanDamage();
        case RE_STALFOS:
            // RANDOTODO Add trick to kill stalfos with sticks, and a second one for bombs without stunning. Higher ammo
            // logic for bombs is also plausible
            switch (distance) {
                case ED_CLOSE:
                case ED_SHORT_JUMPSLASH:
                    killed = CanUse(RG_MEGATON_HAMMER) || CanUse(RG_KOKIRI_SWORD);
                    [[fallthrough]];
                case ED_MASTER_SWORD_JUMPSLASH:
                    killed = killed || CanUse(RG_MASTER_SWORD);
                    [[fallthrough]];
                case ED_LONG_JUMPSLASH:
                    killed = killed || CanUse(RG_BIGGORON_SWORD) || (quantity <= 1 && CanUse(RG_STICKS));
                    [[fallthrough]];
                case ED_BOMB_THROW:
                    killed = killed || (quantity <= 2 && !timer && !inWater &&
                                        (CanUse(RG_NUTS) || HookshotOrBoomerang()) && CanUse(RG_BOMB_BAG));
                    [[fallthrough]];
                case ED_BOOMERANG:
                case ED_HOOKSHOT:
                    // RANDOTODO test chu range in a practical example
                    killed = killed || (wallOrFloor && CanUse(RG_BOMBCHU_5));
                    [[fallthrough]];
                case ED_LONGSHOT:
                case ED_FAR:
                    killed = killed || CanUse(RG_FAIRY_BOW);
                    break;
            }
            return killed;
        // Needs 16 bombs, but is in default logic in N64, probably because getting the hits is quite easy.
        // bow and sling can wake them and damage after they shed their armour, so could reduce ammo requirements for
        // explosives to 10. requires 8 sticks to kill so would be a trick unless we apply higher stick bag logic
        case RE_IRON_KNUCKLE:
            return CanUseSword() || CanUse(RG_MEGATON_HAMMER) || HasExplosives();
        // To stun flare dancer with chus, you have to hit the flame under it while it is spinning. It should eventually
        // return to spinning after dashing for a while if you miss the window it is possible to damage the core with
        // explosives, but difficult to get all 4 hits in even with chus, and if it reconstructs the core heals, so it
        // would be a trick. the core takes damage from hookshot even if it doesn't show Dins killing isn't hard, but is
        // obscure and tight on single magic, so is a trick
        case RE_FLARE_DANCER:
            return CanUse(RG_MEGATON_HAMMER) || CanUse(RG_HOOKSHOT) ||
                   (HasExplosives() && (CanJumpslashExceptHammer() || CanUse(RG_FAIRY_BOW) ||
                                        CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_BOOMERANG)));
        case RE_WOLFOS:
        case RE_WHITE_WOLFOS:
        case RE_WALLMASTER:
            return CanJumpslash() || CanUse(RG_FAIRY_BOW) || CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_BOMBCHU_5) ||
                   CanUse(RG_DINS_FIRE) ||
                   (CanUse(RG_BOMB_BAG) && (CanUse(RG_NUTS) || CanUse(RG_HOOKSHOT) || CanUse(RG_BOOMERANG)));
        case RE_GERUDO_WARRIOR:
            return CanJumpslash() || CanUse(RG_FAIRY_BOW) ||
                   (ctx->GetTrickOption(RT_GF_WARRIOR_WITH_DIFFICULT_WEAPON) &&
                    (CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_BOMBCHU_5)));
        case RE_GIBDO:
        case RE_REDEAD:
            return CanJumpslash() || CanUse(RG_DINS_FIRE);
        case RE_MEG:
            return CanUse(RG_FAIRY_BOW) || CanUse(RG_HOOKSHOT) || HasExplosives();
        case RE_ARMOS:
            return BlastOrSmash() || CanUse(RG_MASTER_SWORD) || CanUse(RG_BIGGORON_SWORD) || CanUse(RG_STICKS) ||
                   CanUse(RG_FAIRY_BOW) ||
                   ((CanUse(RG_NUTS) || CanUse(RG_HOOKSHOT) || CanUse(RG_BOOMERANG)) &&
                    (CanUse(RG_KOKIRI_SWORD) || CanUse(RG_FAIRY_SLINGSHOT)));
        case RE_GREEN_BUBBLE:
            // does not technically need to be stunned to kill with dins, but the flame must be off and timing it is
            // awkward Also they don't trigger the kill room in ganons MQ if they die from dins? Vanilla bug?
            return CanJumpslash() || CanUse(RG_FAIRY_BOW) || CanUse(RG_FAIRY_SLINGSHOT) ||
                   HasExplosives() /* || (CanUse(RG_DINS_FIRE) && (CanUse(RG_NUTS) || CanUse(RG_HOOKSHOT) ||
                                      CanUse(RG_BOOMERANG)))*/
                ;
        case RE_DINOLFOS:
            // stunning + bombs is possible but painful, as it loves to dodge the bombs and hookshot. it also dodges
            // chus but if you cook it so it detonates under the dodge it usually gets caught on landing
            return CanJumpslash() || CanUse(RG_FAIRY_BOW) || CanUse(RG_FAIRY_SLINGSHOT) ||
                   (!timer && CanUse(RG_BOMBCHU_5));
        case RE_TORCH_SLUG:
            return CanJumpslash() || HasExplosives() || CanUse(RG_FAIRY_BOW);
        case RE_FREEZARD:
            return CanUse(RG_MASTER_SWORD) || CanUse(RG_BIGGORON_SWORD) || CanUse(RG_MEGATON_HAMMER) ||
                   CanUse(RG_STICKS) || HasExplosives() || CanUse(RG_HOOKSHOT) || CanUse(RG_DINS_FIRE) ||
                   CanUse(RG_FIRE_ARROWS);
        case RE_SHELL_BLADE:
            return CanJumpslash() || HasExplosives() || CanUse(RG_HOOKSHOT) || CanUse(RG_FAIRY_BOW) ||
                   CanUse(RG_DINS_FIRE);
        case RE_SPIKE:
            return CanUse(RG_MASTER_SWORD) || CanUse(RG_BIGGORON_SWORD) || CanUse(RG_MEGATON_HAMMER) ||
                   CanUse(RG_STICKS) || HasExplosives() || CanUse(RG_HOOKSHOT) || CanUse(RG_FAIRY_BOW) ||
                   CanUse(RG_DINS_FIRE);
        case RE_STINGER:
            switch (distance) {
                case ED_CLOSE:
                case ED_SHORT_JUMPSLASH:
                    killed = CanUse(RG_MEGATON_HAMMER) || CanUse(RG_KOKIRI_SWORD);
                    [[fallthrough]];
                case ED_MASTER_SWORD_JUMPSLASH:
                    killed = killed || CanUse(RG_MASTER_SWORD);
                    [[fallthrough]];
                case ED_LONG_JUMPSLASH:
                    killed = killed || CanUse(RG_BIGGORON_SWORD) || CanUse(RG_STICKS);
                    [[fallthrough]];
                case ED_BOMB_THROW:
                    killed = killed || (!inWater && CanUse(RG_BOMB_BAG));
                    [[fallthrough]];
                case ED_BOOMERANG:
                case ED_HOOKSHOT:
                    // RANDOTODO test chu range in a practical example
                    killed = killed || CanUse(RG_HOOKSHOT) || (wallOrFloor && CanUse(RG_BOMBCHU_5));
                    [[fallthrough]];
                case ED_LONGSHOT:
                    killed = killed || CanUse(RG_LONGSHOT);
                    [[fallthrough]];
                case ED_FAR:
                    killed = killed || CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_FAIRY_BOW);
                    break;
            }
            return killed;
        case RE_BIG_OCTO:
            // If chasing octo is annoying but with rolls you can catch him, and you need rang to get into this room
            // without shenanigans anyway. Bunny makes it free
            return CanUse(RG_KOKIRI_SWORD) || CanUse(RG_STICKS) || CanUse(RG_MASTER_SWORD);
        case RE_GOHMA:
            return HasBossSoul(RG_GOHMA_SOUL) && CanJumpslash() &&
                   (CanUse(RG_NUTS) || CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_FAIRY_BOW) || HookshotOrBoomerang());
        case RE_KING_DODONGO:
            return HasBossSoul(RG_KING_DODONGO_SOUL) && CanJumpslash() &&
                   (CanUse(RG_BOMB_BAG) || HasItem(RG_GORONS_BRACELET) ||
                    (ctx->GetTrickOption(RT_DC_DODONGO_CHU) && IsAdult && CanUse(RG_BOMBCHU_5)));
        case RE_BARINADE:
            return HasBossSoul(RG_BARINADE_SOUL) && CanUse(RG_BOOMERANG) &&
                   (CanJumpslashExceptHammer() ||
                    (ctx->GetTrickOption(RT_JABU_BARINADE_POTS) && HasItem(RG_POWER_BRACELET)));
        case RE_PHANTOM_GANON:
            return HasBossSoul(RG_PHANTOM_GANON_SOUL) && CanUseSword() &&
                   (CanUse(RG_HOOKSHOT) || CanUse(RG_FAIRY_BOW) || CanUse(RG_FAIRY_SLINGSHOT));
        case RE_VOLVAGIA:
            return HasBossSoul(RG_VOLVAGIA_SOUL) && CanUse(RG_MEGATON_HAMMER);
        case RE_MORPHA:
            return HasBossSoul(RG_MORPHA_SOUL) &&
                   (CanUse(RG_HOOKSHOT) ||
                    (ctx->GetTrickOption(RT_WATER_MORPHA_WITHOUT_HOOKSHOT) && HasItem(RG_BRONZE_SCALE))) &&
                   (CanUseSword() || CanUse(RG_MEGATON_HAMMER));
        case RE_BONGO_BONGO:
            return HasBossSoul(RG_BONGO_BONGO_SOUL) &&
                   (CanUse(RG_LENS_OF_TRUTH) || ctx->GetTrickOption(RT_LENS_BONGO)) && CanUseSword() &&
                   (CanUse(RG_HOOKSHOT) || CanUse(RG_FAIRY_BOW) || CanUse(RG_FAIRY_SLINGSHOT) ||
                    ctx->GetTrickOption(RT_SHADOW_BONGO));
        case RE_TWINROVA:
            return HasBossSoul(RG_TWINROVA_SOUL) && CanUse(RG_MIRROR_SHIELD) &&
                   (CanUseSword() || CanUse(RG_MEGATON_HAMMER));
        case RE_GANONDORF:
            // RANDOTODO: Trick to use hammer (no jumpslash) or stick (only jumpslash) instead of a sword to reflect the
            // energy ball and either of them regardless of jumpslashing to damage and kill ganondorf

            // Bottle is not taken into account since a sword, hammer or stick are required
            // for killing ganondorf and all of those can reflect the energy ball
            // This will not be the case once ammo logic in taken into account as
            // sticks are limited and using a bottle might become a requirement in that case
            return HasBossSoul(RG_GANON_SOUL) && CanUse(RG_LIGHT_ARROWS) && CanUseSword();
        case RE_GANON:
            return HasBossSoul(RG_GANON_SOUL) && CanUse(RG_MASTER_SWORD);
        case RE_DARK_LINK:
            // RANDOTODO make a function to track our ammo vs his HP when ammo capacity is taken into account in logic
            //  all swords can at least trade blows with dark link, and even with 1 damage a slash it works out
            return CanUseSword() ||
                   // Boomerang is a relaible, infinite ammo stun, so it enables any way to get enough damage with the
                   // ammo we have Max HP dark link has 40 HP, bows and bombs do 2 so 20 ammo, stick jumpslash does 4 so
                   // 10 sticks
                   (CanUse(RG_BOOMERANG) &&
                    (CanUse(RG_FAIRY_BOW) || CanUse(RG_STICKS) || CanUse(RG_MEGATON_HAMMER) || HasExplosives())) ||
                   // By using deku nuts against the wall, you can stun him roughly half the time, which makes 4 damage
                   // attacks reliable on base nuts
                   (CanUse(RG_NUTS) && (CanUse(RG_STICKS) || CanUse(RG_MEGATON_HAMMER)));
            // Dins does 2 damage, but is reliable, so would need 20 casts for max HP dark link. normal magic gives 4
            // casts, double 8, and then potions can add more
        case RE_ANUBIS:
            // there's a restoration that allows beating them with mirror shield + some way to trigger their attack
            return HasFireSource();
        case RE_BEAMOS:
            return HasExplosives();
        case RE_PURPLE_LEEVER:
            // dies on it's own, so this is the conditions to spawn it (killing 10 normal leevers)
            // Sticks and Ice arrows work but will need ammo capacity logic
            // other methods can damage them but not kill them, and they run when hit, making them impractical
            return CanUse(RG_MASTER_SWORD) || CanUse(RG_BIGGORON_SWORD);
        case RE_TENTACLE:
            return CanUse(RG_BOOMERANG);
        case RE_BARI:
            return HookshotOrBoomerang() || CanUse(RG_FAIRY_BOW) || HasExplosives() || CanUse(RG_MEGATON_HAMMER) ||
                   CanUse(RG_STICKS) || CanUse(RG_DINS_FIRE) || (TakeDamage() && CanUseSword());
        case RE_SHABOM:
            return CanUse(RG_BOOMERANG) || CanUse(RG_NUTS) || CanJumpslash() || CanUse(RG_DINS_FIRE) ||
                   CanUse(RG_ICE_ARROWS) || EffectiveHealth() * 2 > quantity;
        case RE_OCTOROK:
            return CanReflectNuts() || HookshotOrBoomerang() || CanUse(RG_FAIRY_BOW) || CanUse(RG_FAIRY_SLINGSHOT) ||
                   CanUse(RG_BOMB_BAG) || (wallOrFloor && CanUse(RG_BOMBCHU_5));
        case RE_WALLTULA:
            switch (distance) {
                case ED_CLOSE:
                case ED_SHORT_JUMPSLASH:
                    killed = CanUse(RG_KOKIRI_SWORD);
                    [[fallthrough]];
                case ED_MASTER_SWORD_JUMPSLASH:
                    killed = killed || CanUse(RG_MASTER_SWORD);
                    [[fallthrough]];
                case ED_LONG_JUMPSLASH:
                    killed = killed || CanUse(RG_BIGGORON_SWORD) || CanUse(RG_STICKS);
                    [[fallthrough]];
                case ED_BOMB_THROW:
                    killed = killed || (!inWater && CanUse(RG_BOMB_BAG)) || CanUse(RG_DINS_FIRE);
                    [[fallthrough]];
                case ED_BOOMERANG:
                    killed = killed || CanUse(RG_BOOMERANG);
                    [[fallthrough]];
                case ED_HOOKSHOT:
                    killed = killed || CanUse(RG_HOOKSHOT) || CanUse(RG_BOMBCHU_5) || CanUse(RG_MEGATON_HAMMER);
                    [[fallthrough]];
                case ED_LONGSHOT:
                    killed = killed || CanUse(RG_LONGSHOT);
                    [[fallthrough]];
                case ED_FAR:
                    killed = killed || CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_FAIRY_BOW);
                    break;
            }
            return killed;
        default:
            SPDLOG_ERROR("CanKillEnemy reached `default`.");
            assert(false);
            return false;
    }
}

// It is rare for Pass Enemy to need distance, this only happens when the enemy blocks a platform and you can't reach it
// before it blocks you an example is the Big Skulltula in water room of MQ deku, which is out of sword swing height but
// blocks off the whole SoT block Can we get past this enemy in a tight space?
bool Logic::CanPassEnemy(RandomizerEnemy enemy, EnemyDistance distance, bool wallOrFloor) {
    if (CanKillEnemy(enemy, distance, wallOrFloor)) {
        return true;
    }
    switch (enemy) {
        case RE_GOLD_SKULLTULA:
        case RE_GOHMA_LARVA:
        case RE_LIZALFOS:
        case RE_DODONGO: // RANDOTODO do dodongos block the way in tight corridors?
        case RE_MAD_SCRUB:
        case RE_KEESE:
        case RE_FIRE_KEESE:
        case RE_BLUE_BUBBLE:
        case RE_DEAD_HAND:
        case RE_DEKU_BABA:
        case RE_WITHERED_DEKU_BABA:
        case RE_STALFOS:
        case RE_FLARE_DANCER:
        case RE_WOLFOS:
        case RE_WHITE_WOLFOS:
        case RE_FLOORMASTER:
        case RE_MEG:
        case RE_ARMOS:
        case RE_FREEZARD:
        case RE_SPIKE:
        case RE_DARK_LINK:
        case RE_ANUBIS:
        case RE_WALLMASTER:
        case RE_PURPLE_LEEVER:
        case RE_OCTOROK:
            return true;
        case RE_GERUDO_GUARD:
            return ctx->GetTrickOption(RT_PASS_GUARDS_WITH_NOTHING) || HasItem(RG_GERUDO_MEMBERSHIP_CARD) ||
                   CanUse(RG_FAIRY_BOW) || CanUse(RG_HOOKSHOT);
        case RE_BREAK_ROOM_GUARD:
            return HasItem(RG_GERUDO_MEMBERSHIP_CARD) || CanUse(RG_FAIRY_BOW) || CanUse(RG_HOOKSHOT);
        case RE_BIG_SKULLTULA:
            // hammer jumpslash can pass, but only on flat land where you can kill with hammer swing
            return CanUse(RG_NUTS) || CanUse(RG_BOOMERANG) ||
                   (ctx->GetTrickOption(RT_BIG_SKULLTULA_PAUSE_LIFT) && wallOrFloor && distance == ED_CLOSE);
        case RE_LIKE_LIKE:
            return CanUse(RG_HOOKSHOT) || CanUse(RG_BOOMERANG);
        case RE_GIBDO:
        case RE_REDEAD:
            // You can move slowly to avoid getting screamed at
            return true; // CanUse(RG_HOOKSHOT) || CanUse(RG_SUNS_SONG);
        case RE_IRON_KNUCKLE:
        case RE_BIG_OCTO:
        case RE_WALLTULA: // consistent with RT_SPIRIT_WALL
            return false;
        case RE_GREEN_BUBBLE:
            return TakeDamage() || CanUse(RG_NUTS) || CanUse(RG_BOOMERANG) || CanUse(RG_HOOKSHOT);
        default:
            SPDLOG_ERROR("CanPassEnemy reached `default`.");
            assert(false);
            return false;
    }
}

// Can we avoid this enemy while climbing up a wall, or doing a difficult platforming challenge?
// use grounded if the challenge is such that the enemy interfears even if it cannot hit link out of the air
bool Logic::CanAvoidEnemy(RandomizerEnemy enemy, bool grounded, uint8_t quantity) {
    // DISTANCE AND WALL ASSUMED, add more arguments later if needed
    if (CanKillEnemy(enemy, ED_CLOSE, true, quantity)) {
        return true;
    }
    switch (enemy) {
        case RE_GOLD_SKULLTULA:
        case RE_GOHMA_LARVA:
        case RE_LIZALFOS:
        case RE_DODONGO:
        case RE_BIG_SKULLTULA:
        case RE_DEAD_HAND:
        case RE_DEKU_BABA:
        case RE_WITHERED_DEKU_BABA:
        case RE_LIKE_LIKE:
        case RE_STALFOS:
        case RE_IRON_KNUCKLE:
        case RE_FLARE_DANCER:
        case RE_WOLFOS:
        case RE_WHITE_WOLFOS:
        case RE_FLOORMASTER:
        case RE_REDEAD:
        case RE_MEG:
        case RE_ARMOS:
        case RE_GREEN_BUBBLE:
        case RE_FREEZARD:
        case RE_SHELL_BLADE:
        case RE_SPIKE:
        case RE_BIG_OCTO:
        case RE_GIBDO:
        case RE_DARK_LINK:
        case RE_WALLMASTER:
        case RE_ANUBIS:
        case RE_PURPLE_LEEVER:
        case RE_WALLTULA:
            return true;
        case RE_BEAMOS:
            return !grounded || CanUse(RG_NUTS) || CanUse(RG_DINS_FIRE) ||
                   (quantity == 1 && (CanUse(RG_FAIRY_BOW) || CanUse(RG_FAIRY_SLINGSHOT)));
        case RE_MAD_SCRUB:
            return !grounded || CanUse(RG_NUTS);
        case RE_KEESE:
        case RE_FIRE_KEESE:
        case RE_GUAY:
            return CanUse(RG_NUTS) || CanUse(RG_SKULL_MASK);
        case RE_BLUE_BUBBLE:
            // RANDOTODO Trick to use shield hylian shield as child to stun these guys
            return !grounded || CanUse(RG_NUTS) || HookshotOrBoomerang() || CanStandingShield();
        case RE_TORCH_SLUG:
            return !grounded || CanUse(RG_NUTS) || CanUse(RG_HOOKSHOT) || CanUse(RG_DINS_FIRE);
        default:
            SPDLOG_ERROR("CanAvoidEnemy reached `default`.");
            assert(false);
            return false;
    }
}

bool Logic::CanGetEnemyDrop(RandomizerEnemy enemy, EnemyDistance distance, bool aboveLink) {
    if (!CanKillEnemy(enemy, distance)) {
        return false;
    }
    // RANDOTODO assumption broke with RC_WATER_TEMPLE_GS_BEHIND_GATE, redesign GS helpers
    if (distance <= ED_MASTER_SWORD_JUMPSLASH) {
        return true;
    }
    bool drop = false;
    switch (enemy) {
        case RE_GOLD_SKULLTULA:
            switch (distance) {
                case ED_CLOSE:
                case ED_SHORT_JUMPSLASH:
                case ED_MASTER_SWORD_JUMPSLASH:
                case ED_LONG_JUMPSLASH:
                case ED_BOMB_THROW:
                case ED_BOOMERANG:
                    drop = drop || CanUse(RG_BOOMERANG);
                    [[fallthrough]];
                case ED_HOOKSHOT:
                    drop = drop || CanUse(RG_HOOKSHOT);
                    [[fallthrough]];
                case ED_LONGSHOT:
                    drop = drop || CanUse(RG_LONGSHOT);
                    [[fallthrough]];
                case ED_FAR:
                    break;
                    // RANDOTODO double check all jumpslash kills that might be out of jump/backflip range
            }
            return drop;
            break;
        case RE_KEESE:
        case RE_FIRE_KEESE:
        case RE_GUAY:
            return true;
        default:
            return aboveLink || (distance <= ED_BOOMERANG && CanUse(RG_BOOMERANG));
    }
}

bool Logic::CanBreakMudWalls() {
    return BlastOrSmash() || (ctx->GetTrickOption(RT_BLUE_FIRE_MUD_WALLS) && BlueFire());
}

bool Logic::CanGetDekuBabaSticks() {
    return CanUseSword() || CanUse(RG_BOOMERANG);
}

bool Logic::CanGetDekuBabaNuts() {
    return CanJumpslash() || CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_FAIRY_BOW) || HasExplosives() ||
           CanUse(RG_DINS_FIRE);
}

bool Logic::CanHitEyeTargets() {
    return CanUse(RG_FAIRY_BOW) || CanUse(RG_FAIRY_SLINGSHOT);
}

bool Logic::CanDetonateBombFlowers() {
    return CanUse(RG_FAIRY_BOW) || HasExplosives() || CanUse(RG_DINS_FIRE);
}

bool Logic::CanDetonateUprightBombFlower() {
    return CanDetonateBombFlowers() || HasItem(RG_GORONS_BRACELET) ||
           (ctx->GetTrickOption(RT_BLUE_FIRE_MUD_WALLS) && CanUse(RG_BOTTLE_WITH_BLUE_FIRE) &&
            (EffectiveHealth() != 1 || CanUse(RG_NAYRUS_LOVE)));
}

bool Logic::CanHammerRecoilHover(bool needShield) {
    return CanUse(RG_HOVER_BOOTS) && ctx->GetTrickOption(RT_HOVER_BOOST_SIMPLE) && CanUse(RG_MEGATON_HAMMER) &&
           (!needShield || CanStandingShield());
}

bool Logic::Water3FCentralToHighEmblem() {
    return (IsAdult && (CanUse(RG_HOVER_BOOTS) ||
                        (ctx->GetTrickOption(RT_DAMAGE_BOOST_SIMPLE) && CanUse(RG_BOMB_BAG) && TakeDamage()))) ||
           CanMiddairGroundJump() || (Get(LOGIC_WATER_SCARECROW) && CanUse(RG_HOOKSHOT));
}

bool Logic::WaterRisingTargetTo3FCentral() {
    return CanUse(RG_LONGSHOT) ||
           (ctx->GetTrickOption(RT_HOVER_BOOST_SIMPLE) && ctx->GetTrickOption(RT_DAMAGE_BOOST_SIMPLE) &&
            HasExplosives() && CanUse(RG_HOVER_BOOTS));
}

/* Water level has 7 events that govern it's logic.
 * LOGIC_WATER_LOW, LOGIC_WATER_MIDDLE say that the player for sure can set the water to this level
 * the COULD varients of these 2, as well as LOGIC_WATER_HIGH instead check for if using those emblems would be possible
 * if the player had a specific water level and ZL
 * - LOGIC_WATER_COULD_LOW checks if the water level could be set low with water agnostic access
 * - LOGIC_WATER_COULD_MIDDLE checks if the water level could be set mid if it was set to low
 * - LOGIC_WATER_HIGH checks if the water level could be set high with water agnostic access,
 *   HIGH is the default, so we don't need to check if we can really set it, only that we could reset it if it was
 * changed out of logic
 *
 * Extending from these 3, LOGIC_WATER_COULD_LOW_FROM_HIGH and LOGIC_WATER_COULD_HIGH_FROM_MID tell us if we can move
 * from 1 level to the next, without us first having to confirm we can always do the preceeding level first. These allow
 * us to check for conditions where we can complete a water level loop and reach any level from any level before we know
 * for sure we have real access. MIDDLE_EMBLEM always requires low water, so FROM_LOW is implied in
 * LOGIC_WATER_COULD_MIDDLE.
 *
 * These exist because we can deduce we have access from knowing we always have access to a water level, and can then
 * change it as needed
 */
bool Logic::WaterLevel(RandoWaterLevel level) {
    switch (level) {
        case WL_LOW:
            return Get(LOGIC_WATER_LOW) ||
                   // if we could get LOW from HIGH and HIGH from MID, then we can move to LOW from any water level
                   (Get(LOGIC_WATER_COULD_LOW_FROM_HIGH) &&
                    (Get(LOGIC_WATER_COULD_HIGH_FROM_MID) || Get(LOGIC_WATER_HIGH)) && CanUse(RG_ZELDAS_LULLABY));
        case WL_LOW_OR_MID:
            return Get(LOGIC_WATER_LOW) || Get(LOGIC_WATER_MIDDLE) ||
                   // The water level is either at HIGH, in which case we can set it to LOW, LOW, or MID, so we only
                   // have to check COULD_LOW and ZL
                   ((Get(LOGIC_WATER_COULD_LOW_FROM_HIGH) || Get(LOGIC_WATER_LOW)) && CanUse(RG_ZELDAS_LULLABY));
        case WL_MID:
            return Get(LOGIC_WATER_MIDDLE) ||
                   // LOGIC_WATER_COULD_MIDDLE is LOGIC_WATER_COULD_MIDDLE_FROM_LOW in practice, due to WL_LOW being a
                   // hard requirement for WL_MID
                   (Get(LOGIC_WATER_LOW) && Get(LOGIC_WATER_COULD_MIDDLE)) ||
                   // If we have COULD_MIDDLE, we know we could move to LOW from HIGH,
                   // we're either already MID, on LOW can set MID, or on HIGH so you can set LOW and thus MID.
                   ((Get(LOGIC_WATER_COULD_LOW_FROM_HIGH) || Get(LOGIC_WATER_COULD_LOW)) &&
                    Get(LOGIC_WATER_COULD_MIDDLE) && CanUse(RG_ZELDAS_LULLABY));
        case WL_HIGH:
            // If we don't have ZL, we're stuck on high anyway, so we only need to check for if we can reset it to high
            return Get(LOGIC_WATER_HIGH) ||
                   // If water is MID and we COULD_HIGH_FROM_MID, then if water is MID we can set it HIGH
                   // so we only need to check if we could make it MID from LOW
                   (Get(LOGIC_WATER_COULD_HIGH_FROM_MID) && Get(LOGIC_WATER_COULD_MIDDLE));
        case WL_HIGH_OR_MID:
            // If we don't have ZL, we're stuck on high anyway, so we only need to check for if we can reset it to high
            return Get(LOGIC_WATER_MIDDLE) || Get(LOGIC_WATER_HIGH) ||
                   // The water level is either at LOW, in which case COULD_MIDDLE can set it to MID, MID, or HIGH, so
                   // we only have to check COULD_MIDDLE if we don't have ZL, then we are at high, so we can skip that
                   // too
                   (Get(LOGIC_WATER_COULD_MIDDLE));
    }
    SPDLOG_ERROR("WaterLevel reached `return false;`. Missing case for a Water Level");
    assert(false);
    return false;
}

} // namespace Rando
