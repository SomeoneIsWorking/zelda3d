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

Logic::Logic() {
}

uint8_t Logic::BottleCount() {
    uint8_t count = 0;
    for (int i = SLOT_BOTTLE_1; i <= SLOT_BOTTLE_4; i++) {
        uint8_t item = GetSaveContext()->inventory.items[i];
        switch (item) {
            case ITEM_LETTER_RUTO:
                if (Get(LOGIC_DELIVER_RUTOS_LETTER)) {
                    count++;
                }
                break;
            case ITEM_BIG_POE:
                if (Get(LOGIC_CAN_EMPTY_BIG_POES)) {
                    count++;
                }
                break;
            case ITEM_NONE:
                break;
            default:
                count++;
                break;
        }
    }
    return count;
}

uint8_t Logic::OcarinaButtons() {
    return HasItem(RG_OCARINA_A_BUTTON) + HasItem(RG_OCARINA_C_LEFT_BUTTON) + HasItem(RG_OCARINA_C_RIGHT_BUTTON) +
           HasItem(RG_OCARINA_C_UP_BUTTON) + HasItem(RG_OCARINA_C_DOWN_BUTTON);
}

bool Logic::HasBottle() {
    return BottleCount() >= 1;
}

bool Logic::CanUseSword() {
    return CanUse(RG_KOKIRI_SWORD) || CanUse(RG_MASTER_SWORD) || CanUse(RG_BIGGORON_SWORD);
}

bool Logic::CanJumpslashExceptHammer() {
    // Not including hammer as hammer jump attacks can be weird;
    return CanUse(RG_STICKS) || CanUseSword();
}

bool Logic::CanJumpslash() {
    return CanJumpslashExceptHammer() || CanUse(RG_MEGATON_HAMMER);
}

bool Logic::CanClearStalagmite() {
    return CanJumpslash() || HasExplosives() || CanUse(RG_GIANTS_KNIFE) ||
           (ctx->GetTrickOption(RT_ICE_STALAGMITE_HOOKSHOT) && CanUse(RG_HOOKSHOT));
}

bool Logic::CanHitSwitch(EnemyDistance distance, bool inWater) {
    bool hit = false;
    switch (distance) {
        case ED_CLOSE:
        case ED_SHORT_JUMPSLASH:
            hit = CanUse(RG_KOKIRI_SWORD) || CanUse(RG_MEGATON_HAMMER) || CanUse(RG_GIANTS_KNIFE);
            [[fallthrough]];
        case ED_MASTER_SWORD_JUMPSLASH:
            hit = hit || CanUse(RG_MASTER_SWORD);
            [[fallthrough]];
        case ED_LONG_JUMPSLASH:
            hit = hit || CanUse(RG_BIGGORON_SWORD) || CanUse(RG_STICKS);
            [[fallthrough]];
        case ED_BOMB_THROW:
            hit = hit || (!inWater && CanUse(RG_BOMB_BAG));
            [[fallthrough]];
        case ED_BOOMERANG:
            hit = hit || CanUse(RG_BOOMERANG);
            [[fallthrough]];
        case ED_HOOKSHOT:
            // RANDOTODO test chu range in a practical example
            hit = hit || CanUse(RG_HOOKSHOT) || CanUse(RG_BOMBCHU_5);
            [[fallthrough]];
        case ED_LONGSHOT:
            hit = hit || CanUse(RG_LONGSHOT);
            [[fallthrough]];
        case ED_FAR:
            hit = hit || CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_FAIRY_BOW);
            break;
    }
    return hit;
}

bool Logic::CanDamage() {
    return CanUse(RG_FAIRY_SLINGSHOT) || CanJumpslash() || HasExplosives() || CanUse(RG_DINS_FIRE) ||
           CanUse(RG_FAIRY_BOW);
}

bool Logic::CanAttack() {
    return CanDamage() || CanUse(RG_BOOMERANG) || CanUse(RG_HOOKSHOT);
}

bool Logic::BombchusEnabled() {
    return ctx->GetOption(RSK_BOMBCHU_BAG).IsNot(RO_BOMBCHU_BAG_NONE) ? CheckInventory(ITEM_BOMBCHU, true)
                                                                      : HasItem(RG_BOMB_BAG);
}

// With the shop shield/tunic gate enabled, a shop slot selling a shield/tunic is considered not-for-sale
// in logic until the matching item has been found in the world (which sets its RandomizerInf). Shop slots
// are randomized, so this keys off the item actually placed in the slot rather than a fixed location.
bool Logic::ShopItemNotForSale(RandomizerCheck loc) {
    if (ctx->GetOption(RSK_SHOP_SHIELDS_AND_TUNICS_ONLY_REFILL).IsNot(RO_GENERIC_ON) ||
        StaticData::GetLocation(loc)->GetRCType() != RCTYPE_SHOP) {
        return false;
    }
    switch (ctx->GetItemLocation(loc)->GetPlacedRandomizerGet()) {
        case RG_BUY_DEKU_SHIELD:
            return !CheckRandoInf(RAND_INF_HAS_FOUND_DEKU_SHIELD);
        case RG_BUY_HYLIAN_SHIELD:
            return !CheckRandoInf(RAND_INF_HAS_FOUND_HYLIAN_SHIELD);
        case RG_BUY_GORON_TUNIC:
            return !CheckRandoInf(RAND_INF_HAS_FOUND_GORON_TUNIC);
        case RG_BUY_ZORA_TUNIC:
            return !CheckRandoInf(RAND_INF_HAS_FOUND_ZORA_TUNIC);
        default:
            return false;
    }
}

// TODO: Implement Ammo Drop Setting in place of bombchu drops
bool Logic::BombchuRefill() {
    return Get(LOGIC_BUY_BOMBCHUS) || Get(LOGIC_COULD_PLAY_BOWLING) || Get(LOGIC_CARPET_MERCHANT) ||
           (ctx->GetOption(RSK_ENABLE_BOMBCHU_DROPS).Is(RO_AMMO_DROPS_ON /*_PLUS_BOMBCHU*/));
}

bool Logic::HookshotOrBoomerang() {
    return CanUse(RG_HOOKSHOT) || CanUse(RG_BOOMERANG);
}

bool Logic::ScarecrowsSong() {
    return (ctx->GetOption(RSK_SKIP_SCARECROWS_SONG) && HasItem(RG_FAIRY_OCARINA) && OcarinaButtons() >= 2) ||
           (Get(LOGIC_CHILD_SCARECROW) && Get(LOGIC_ADULT_SCARECROW));
}

bool Logic::BlueFire() {
    return CanUse(RG_BOTTLE_WITH_BLUE_FIRE) || (ctx->GetOption(RSK_BLUE_FIRE_ARROWS) && CanUse(RG_ICE_ARROWS));
}

bool Logic::CanBreakPots(EnemyDistance distance, bool wallOrFloor, bool inWater) {
    bool hit = false;
    switch (distance) {
        case ED_CLOSE:
            hit = HasItem(RG_POWER_BRACELET);
            [[fallthrough]];
        case ED_SHORT_JUMPSLASH:
            hit = hit || CanUse(RG_KOKIRI_SWORD) || CanUse(RG_MEGATON_HAMMER) || CanUse(RG_GIANTS_KNIFE);
            [[fallthrough]];
        case ED_MASTER_SWORD_JUMPSLASH:
            hit = hit || CanUse(RG_MASTER_SWORD);
            [[fallthrough]];
        case ED_LONG_JUMPSLASH:
            hit = hit || CanUse(RG_BIGGORON_SWORD) || CanUse(RG_STICKS);
            [[fallthrough]];
        case ED_BOMB_THROW:
            hit = hit || (!inWater && CanUse(RG_BOMB_BAG));
            [[fallthrough]];
        case ED_BOOMERANG:
            hit = hit || CanUse(RG_BOOMERANG);
            [[fallthrough]];
        case ED_HOOKSHOT:
            hit = hit || CanUse(RG_HOOKSHOT);
            [[fallthrough]];
        case ED_LONGSHOT:
            hit = hit || CanUse(RG_LONGSHOT);
            [[fallthrough]];
        case ED_FAR:
            hit = hit || CanUse(RG_FAIRY_SLINGSHOT) || CanUse(RG_FAIRY_BOW);
            break;
    }
    return hit || (wallOrFloor && CanUse(RG_BOMBCHU_5));
}

bool Logic::CanBreakCrates() {
    return true;
}

bool Logic::CanBreakSmallCrates() {
    return CanJumpslash() || HasExplosives() || HasItem(RG_POWER_BRACELET);
}

bool Logic::CanBreakRocks() {
    return BlastOrSmash() || HasItem(RG_POWER_BRACELET);
}

bool Logic::CanBonkTrees() {
    return true;
}

bool Logic::CanRead() {
    return true;
}

bool Logic::HasExplosives() {
    return CanUse(RG_BOMB_BAG) || CanUse(RG_BOMBCHU_5);
}

bool Logic::BlastOrSmash() {
    return HasExplosives() || CanUse(RG_MEGATON_HAMMER);
}

bool Logic::CanSpawnSoilSkull(RandomizerGet bean) {
    return IsChild && CanUse(RG_BOTTLE_WITH_BUGS) && HasItem(bean);
}

bool Logic::CanReflectNuts() {
    return CanUse(RG_DEKU_SHIELD) || (IsAdult && HasItem(RG_HYLIAN_SHIELD));
}

bool Logic::CanCutShrubs() {
    return CanUse(RG_KOKIRI_SWORD) || CanUse(RG_BOOMERANG) || HasExplosives() || CanUse(RG_MASTER_SWORD) ||
           CanUse(RG_MEGATON_HAMMER) || CanUse(RG_BIGGORON_SWORD) || CanUse(RG_GIANTS_KNIFE) ||
           HasItem(RG_GORONS_BRACELET);
}

bool Logic::CanStunDeku() {
    return CanAttack() || CanUse(RG_NUTS) || CanReflectNuts();
}

bool Logic::CallGossipFairyExceptSuns() {
    return CanUse(RG_ZELDAS_LULLABY) || CanUse(RG_EPONAS_SONG) || CanUse(RG_SONG_OF_TIME);
}

bool Logic::CallGossipFairy() {
    return CallGossipFairyExceptSuns() || CanUse(RG_SUNS_SONG);
}

// the number returned by this is in half heart hits taken.
// RANDOTODO work in OoT side health instead for greater applicability (16 per heart)
uint8_t Logic::EffectiveHealth() {
    /* Multiplier will be:
    0 for half daamge
    1 for normal damage
    2 for double damage
    3 for quad damage
    4 for 8* damage
    5 for 16* damage
    10 for OHKO.
    This is the number of shifts to apply, not a real multiplier
    */
    uint8_t Multiplier =
        (ctx->GetOption(RSK_DAMAGE_MULTIPLIER).Get() < 6) ? ctx->GetOption(RSK_DAMAGE_MULTIPLIER).Get() : 10;
    //(Hearts() << (2 + HasItem(RG_DOUBLE_DEFENSE))) is quarter hearts after DD
    //>> Multiplier halves on normal and does nothing on half, meaning we're working with half hearts on normal damage
    return ((Hearts() << (2 + HasItem(RG_DOUBLE_DEFENSE))) >> Multiplier) +
           // As 1 is a quarter heart, (1 << Multiplier) is effectivly half-hearts of unmodified damage
           // Adds an extra hit if the damage is not exact lethal
           ((Hearts() << (2 + HasItem(RG_DOUBLE_DEFENSE))) % (1 << Multiplier) > 0);
}

uint8_t Logic::Hearts() {
    return GetSaveContext()->healthCapacity / 16;
}

uint8_t Logic::DungeonCount() {
    if (CalculatingAvailableChecks) {
        return CheckEventChkInf(EVENTCHKINF_USED_DEKU_TREE_BLUE_WARP) +
               CheckEventChkInf(EVENTCHKINF_USED_DODONGOS_CAVERN_BLUE_WARP) +
               CheckEventChkInf(EVENTCHKINF_USED_JABU_JABUS_BELLY_BLUE_WARP) +
               CheckEventChkInf(EVENTCHKINF_USED_FOREST_TEMPLE_BLUE_WARP) +
               CheckEventChkInf(EVENTCHKINF_USED_FIRE_TEMPLE_BLUE_WARP) +
               CheckEventChkInf(EVENTCHKINF_USED_WATER_TEMPLE_BLUE_WARP) +
               CheckRandoInf(RAND_INF_DUNGEONS_DONE_SPIRIT_TEMPLE) +
               CheckRandoInf(RAND_INF_DUNGEONS_DONE_SHADOW_TEMPLE);
    } else {
        return Get(LOGIC_DEKU_TREE_CLEAR) + Get(LOGIC_DODONGOS_CAVERN_CLEAR) + Get(LOGIC_JABU_JABUS_BELLY_CLEAR) +
               Get(LOGIC_FOREST_TEMPLE_CLEAR) + Get(LOGIC_FIRE_TEMPLE_CLEAR) + Get(LOGIC_WATER_TEMPLE_CLEAR) +
               Get(LOGIC_SPIRIT_TEMPLE_CLEAR) + Get(LOGIC_SHADOW_TEMPLE_CLEAR);
    }
}

uint8_t Logic::StoneCount() {
    return HasItem(RG_KOKIRI_EMERALD) + HasItem(RG_GORON_RUBY) + HasItem(RG_ZORA_SAPPHIRE);
}

uint8_t Logic::MedallionCount() {
    return HasItem(RG_FOREST_MEDALLION) + HasItem(RG_FIRE_MEDALLION) + HasItem(RG_WATER_MEDALLION) +
           HasItem(RG_SPIRIT_MEDALLION) + HasItem(RG_SHADOW_MEDALLION) + HasItem(RG_LIGHT_MEDALLION);
}

uint8_t Logic::FireTimer() {
    return CanUse(RG_GORON_TUNIC) ? 255 : (ctx->GetTrickOption(RT_FEWER_TUNIC_REQUIREMENTS)) ? (Hearts() * 8) : 0;
}

// Tunic is not required if you are using irons to do something that a simple gold scale dive could do, and you are not
// in water temple. (celing swimming and long walks through water do not count)
uint8_t Logic::WaterTimer() {
    return CanUse(RG_ZORA_TUNIC) ? 255 : (ctx->GetTrickOption(RT_FEWER_TUNIC_REQUIREMENTS)) ? (Hearts() * 8) : 0;
}

bool Logic::TakeDamage() {
    return CanUse(RG_BOTTLE_WITH_FAIRY) || EffectiveHealth() != 1 || CanUse(RG_NAYRUS_LOVE);
}

bool Logic::CanOpenBombGrotto() {
    return BlastOrSmash() && (HasItem(RG_STONE_OF_AGONY) || ctx->GetTrickOption(RT_GROTTOS_WITHOUT_AGONY));
}

bool Logic::CanOpenStormsGrotto() {
    return CanUse(RG_SONG_OF_STORMS) && (HasItem(RG_STONE_OF_AGONY) || ctx->GetTrickOption(RT_GROTTOS_WITHOUT_AGONY));
}

bool Logic::CanGetNightTimeGS() {
    return AtNight && (CanUse(RG_SUNS_SONG) || !ctx->GetOption(RSK_SKULLS_SUNS_SONG));
}

bool Logic::CanBreakUpperBeehives() {
    return HookshotOrBoomerang() || (ctx->GetTrickOption(RT_BOMBCHU_BEEHIVES) && CanUse(RG_BOMBCHU_5)) ||
           (ctx->GetOption(RSK_SLINGBOW_BREAK_BEEHIVES) && (CanUse(RG_FAIRY_BOW) || CanUse(RG_FAIRY_SLINGSHOT)));
}

bool Logic::CanBreakLowerBeehives() {
    return CanBreakUpperBeehives() || CanUse(RG_BOMB_BAG);
}

bool Logic::HasFireSource() {
    return CanUse(RG_DINS_FIRE) || CanUse(RG_FIRE_ARROWS);
}

bool Logic::HasFireSourceWithTorch() {
    return HasFireSource() || CanUse(RG_STICKS);
}

bool Logic::SunlightArrows() {
    return ctx->GetOption(RSK_SUNLIGHT_ARROWS) && CanUse(RG_LIGHT_ARROWS);
}

bool Logic::CanStandingShield() {
    return CanUse(RG_MIRROR_SHIELD) || (IsAdult && HasItem(RG_HYLIAN_SHIELD)) || CanUse(RG_DEKU_SHIELD);
}

bool Logic::CanShield() {
    return CanUse(RG_MIRROR_SHIELD) || HasItem(RG_HYLIAN_SHIELD) || CanUse(RG_DEKU_SHIELD);
}

bool Logic::CanUseProjectile() {
    return HasExplosives() || CanUse(RG_FAIRY_BOW) || CanUse(RG_HOOKSHOT) || CanUse(RG_FAIRY_SLINGSHOT) ||
           CanUse(RG_BOOMERANG);
}

bool Logic::CanBuildRainbowBridge() {
    return ctx->GetOption(RSK_RAINBOW_BRIDGE).Is(RO_BRIDGE_ALWAYS_OPEN) ||
           (ctx->GetOption(RSK_RAINBOW_BRIDGE).Is(RO_BRIDGE_VANILLA) && HasItem(RG_SHADOW_MEDALLION) &&
            HasItem(RG_SPIRIT_MEDALLION) && CanUse(RG_LIGHT_ARROWS)) ||
           (ctx->GetOption(RSK_RAINBOW_BRIDGE).Is(RO_BRIDGE_STONES) &&
            StoneCount() + (HasItem(RG_GREG_RUPEE) && ctx->GetOption(RSK_BRIDGE_OPTIONS).Is(RO_BRIDGE_GREG_REWARD)) >=
                ctx->GetOption(RSK_RAINBOW_BRIDGE_STONE_COUNT).Get()) ||
           (ctx->GetOption(RSK_RAINBOW_BRIDGE).Is(RO_BRIDGE_MEDALLIONS) &&
            MedallionCount() +
                    (HasItem(RG_GREG_RUPEE) && ctx->GetOption(RSK_BRIDGE_OPTIONS).Is(RO_BRIDGE_GREG_REWARD)) >=
                ctx->GetOption(RSK_RAINBOW_BRIDGE_MEDALLION_COUNT).Get()) ||
           (ctx->GetOption(RSK_RAINBOW_BRIDGE).Is(RO_BRIDGE_DUNGEON_REWARDS) &&
            StoneCount() + MedallionCount() +
                    (HasItem(RG_GREG_RUPEE) && ctx->GetOption(RSK_BRIDGE_OPTIONS).Is(RO_BRIDGE_GREG_REWARD)) >=
                ctx->GetOption(RSK_RAINBOW_BRIDGE_REWARD_COUNT).Get()) ||
           (ctx->GetOption(RSK_RAINBOW_BRIDGE).Is(RO_BRIDGE_DUNGEONS) &&
            DungeonCount() + (HasItem(RG_GREG_RUPEE) && ctx->GetOption(RSK_BRIDGE_OPTIONS).Is(RO_BRIDGE_GREG_REWARD)) >=
                ctx->GetOption(RSK_RAINBOW_BRIDGE_DUNGEON_COUNT).Get()) ||
           (ctx->GetOption(RSK_RAINBOW_BRIDGE).Is(RO_BRIDGE_TOKENS) &&
            GetGSCount() >= ctx->GetOption(RSK_RAINBOW_BRIDGE_TOKEN_COUNT).Get()) ||
           (ctx->GetOption(RSK_RAINBOW_BRIDGE).Is(RO_BRIDGE_GREG) && HasItem(RG_GREG_RUPEE));
}

bool Logic::CanTriggerLACS() {
    return (ctx->LACSCondition() == RO_LACS_VANILLA && HasItem(RG_SHADOW_MEDALLION) && HasItem(RG_SPIRIT_MEDALLION)) ||
           (ctx->LACSCondition() == RO_LACS_STONES &&
            StoneCount() + (HasItem(RG_GREG_RUPEE) && ctx->GetOption(RSK_LACS_OPTIONS).Is(RO_LACS_GREG_REWARD)) >=
                ctx->GetOption(RSK_LACS_STONE_COUNT).Get()) ||
           (ctx->LACSCondition() == RO_LACS_MEDALLIONS &&
            MedallionCount() + (HasItem(RG_GREG_RUPEE) && ctx->GetOption(RSK_LACS_OPTIONS).Is(RO_LACS_GREG_REWARD)) >=
                ctx->GetOption(RSK_LACS_MEDALLION_COUNT).Get()) ||
           (ctx->LACSCondition() == RO_LACS_REWARDS &&
            StoneCount() + MedallionCount() +
                    (HasItem(RG_GREG_RUPEE) && ctx->GetOption(RSK_LACS_OPTIONS).Is(RO_LACS_GREG_REWARD)) >=
                ctx->GetOption(RSK_LACS_REWARD_COUNT).Get()) ||
           (ctx->LACSCondition() == RO_LACS_DUNGEONS &&
            DungeonCount() + (HasItem(RG_GREG_RUPEE) && ctx->GetOption(RSK_LACS_OPTIONS).Is(RO_LACS_GREG_REWARD)) >=
                ctx->GetOption(RSK_LACS_DUNGEON_COUNT).Get()) ||
           (ctx->LACSCondition() == RO_LACS_TOKENS && GetGSCount() >= ctx->GetOption(RSK_LACS_TOKEN_COUNT).Get());
}

bool Logic::SmallKeys(s16 scene, uint8_t requiredAmount) {
    if (HasItem(RG_SKELETON_KEY)) {
        return true;
    }
    return GetSmallKeyCount(scene) >= requiredAmount;
}

void Logic::SetContext(std::shared_ptr<Context> _ctx) {
    // Deliberately stores the raw pointer: see the member's declaration for the cycle this breaks.
    // The parameter stays a shared_ptr so the one caller (Context::CreateInstance) is unchanged and
    // so it is obvious at the call site that the Context is alive at the moment it is handed over.
    ctx = _ctx.get();
}

bool Logic::Get(LogicVal logicVal) {
    return inLogic[logicVal];
}

void Logic::Set(LogicVal logicVal, bool value) {
    inLogic[logicVal] = value;
}

bool Logic::IsFireLoopLocked() {
    return ctx->GetOption(RSK_KEYSANITY).Is(RO_DUNGEON_ITEM_LOC_ANYWHERE) ||
           ctx->GetOption(RSK_KEYSANITY).Is(RO_DUNGEON_ITEM_LOC_OVERWORLD) ||
           ctx->GetOption(RSK_KEYSANITY).Is(RO_DUNGEON_ITEM_LOC_ANY_DUNGEON);
}

bool Logic::ReachScarecrow() {
    return ScarecrowsSong() && CanUse(RG_HOOKSHOT);
}

bool Logic::ReachDistantScarecrow() {
    return ScarecrowsSong() && CanUse(RG_LONGSHOT);
}

bool Logic::CanClimbLadder() {
    return HasItem(RG_CLIMB) || (ctx->GetTrickOption(RT_HOOKSHOT_LADDERS) && CanUse(RG_HOOKSHOT));
}

bool Logic::CanClimbHighLadder() {
    return HasItem(RG_CLIMB) || (ctx->GetTrickOption(RT_HOOKSHOT_LADDERS) && CanUse(RG_LONGSHOT));
}

bool Logic::SummonEpona() {
    return IsAdult && Get(LOGIC_FREED_EPONA) && CanUse(RG_EPONAS_SONG);
}

bool Logic::IsReverseAccessPossible() {
    // If we ever allow dungeon entrances to connect to boss rooms directly in dungeon chains, or for 1 boss door to
    // lead to another dungeons boss door, add RSK_MIX_DUNGEON_ENTRANCES to the final condition
    // RANDOTODO Check for Age-Locked Boss entrances + decoupled + Ganon's tower when it is shuffled
    return !ctx->GetOption(RSK_SHUFFLE_BOSS_ENTRANCES).Is(RO_BOSS_ROOM_ENTRANCE_SHUFFLE_OFF) &&
           ((ctx->GetOption(RSK_DECOUPLED_ENTRANCES) &&
             ctx->GetOption(RSK_SHUFFLE_BOSS_ENTRANCES).Is(RO_BOSS_ROOM_ENTRANCE_SHUFFLE_FULL)) ||
            (ctx->GetOption(RSK_MIX_BOSS_ENTRANCES) &&
             (ctx->GetOption(RSK_MIX_OVERWORLD_ENTRANCES) || ctx->GetOption(RSK_MIX_INTERIOR_ENTRANCES))));
}

bool Logic::DMCUpperToPots() {
    return CanUse(RG_HOVER_BOOTS) || (IsAdult && ((Get(LOGIC_DMC_BOULDER)) ||
                                                  (ctx->GetTrickOption(RT_DMC_BOULDER_SKIP) /* && CanUse(RG_ROLL)*/)));
}

bool Logic::DMCPotsToPad() {
    return (CanUse(RG_HOVER_BOOTS) || CanUse(RG_HOOKSHOT) ||
            (IsAdult && CanShield() && ctx->GetTrickOption(RT_DMC_BOLERO_JUMP) && CanUse(RG_POWER_BRACELET)));
}

bool Logic::DMCPadToPots() {
    return ((CanUse(RG_HOVER_BOOTS) && (IsAdult || (HasItem(RG_CLIMB) /*&& CanUse(RG_ROLL)*/))) || CanUse(RG_HOOKSHOT));
}

// via scarecrow
bool Logic::DMCUpperToPad() {
    return IsAdult && TakeDamage() && ctx->GetTrickOption(RT_UNINTUITIVE_JUMPS) && ReachDistantScarecrow();
}

bool Logic::SpiritExplosiveKeyLogic() {
    return SmallKeys(SCENE_SPIRIT_TEMPLE, HasExplosives() ? 1 : 2);
}

bool Logic::SpiritWestToSkull() {
    return (IsAdult && ctx->GetTrickOption(RT_SPIRIT_STATUE_JUMP)) || CanUse(RG_HOVER_BOOTS) || ReachScarecrow();
}

bool Logic::SpiritSunBlockSouthLedge() {
    // also possible to do a backwalk hover + backflip if you equip hovers as you start backwalk to accelerate faster
    return HasItem(RG_POWER_BRACELET) || IsAdult || CanKillEnemy(RE_BEAMOS) /*|| BunnyHovers()*/ ||
           (CanUse(RG_HOOKSHOT) &&
            (HasFireSource() ||
             (Get(LOGIC_SPIRIT_SUN_BLOCK_TORCH) &&
              (CanUse(RG_STICKS) || (ctx->GetTrickOption(RT_SPIRIT_SUN_CHEST) && CanUse(RG_FAIRY_BOW))))));
}

bool Logic::SpiritEastToSwitch() {
    return (IsAdult && ctx->GetTrickOption(RT_SPIRIT_STATUE_JUMP)) || CanUse(RG_HOVER_BOOTS) ||
           (CanUse(RG_ZELDAS_LULLABY) && CanUse(RG_HOOKSHOT));
}

// Combines crossing the ledge directly and the jump from the hand
bool Logic::MQSpiritWestToPots() {
    return (IsAdult && ctx->GetTrickOption(RT_SPIRIT_STATUE_JUMP)) || CanUse(RG_HOVER_BOOTS) || CanUse(RG_SONG_OF_TIME);
}

bool Logic::MQSpiritStatueToSunBlock() {
    return (IsAdult || ctx->GetTrickOption(RT_SPIRIT_MQ_SUN_BLOCK_SOT) ||
            CanUse(RG_SONG_OF_TIME) /* || CanBunnyJump()*/) &&
           HasItem(RG_POWER_BRACELET);
}

bool Logic::MQSpiritStatueSouthDoor() {
    return HasFireSource() || (ctx->GetTrickOption(RT_SPIRIT_MQ_FROZEN_EYE) && CanUse(RG_FAIRY_BOW) &&
                               CanUse(RG_SONG_OF_TIME) && (HasItem(RG_CLIMB) || CanUse(RG_HOOKSHOT)));
}

bool Logic::MQSpirit4KeyColossus() {
    // !QUANTUM LOGIC!
    // We only need 4 keys and the ability to reach both hands for adult to logically be able to drop down onto Desert
    // Colossus This is because there are only 3 keys that can be wasted without opening up either this lock to East
    // hand, or the West Hand lock through Sun Block Room and both directions allow you to drop onto colossus
    // logic->CanKillEnemy(RE_FLOORMASTER) is implied
    return CanAvoidEnemy(RE_BEAMOS, true, 4) && CanUse(RG_SONG_OF_TIME) && CanJumpslash() &&
           (HasItem(RG_POWER_BRACELET) || SunlightArrows()) &&
           (ctx->GetTrickOption(RT_LENS_SPIRIT_MQ) || CanUse(RG_LENS_OF_TRUTH)) && CanKillEnemy(RE_IRON_KNUCKLE) &&
           CanUse(RG_HOOKSHOT);
}

bool Logic::MQSpirit4KeyWestHand() {
    // !QUANTUM LOGIC!
    // Continuing from MQSpirit4KeyColossus, if we also have a longshot, we can go from the East hand to the West hand,
    // meaning we always have access to East Hand
    return CanUse(RG_LONGSHOT) && MQSpirit4KeyColossus();
}
// This version of the function handles Shared Access for child, based on what adult could do if they existed
bool Logic::CouldMQSpirit4KeyWestHand() {
    return CanAvoidEnemy(RE_BEAMOS, true, 4) && CanUse(RG_SONG_OF_TIME) &&
           (HasItem(RG_MASTER_SWORD) || HasItem(RG_BIGGORON_SWORD) || HasItem(RG_MEGATON_HAMMER)) &&
           (HasItem(RG_POWER_BRACELET) || SunlightArrows()) &&
           (ctx->GetTrickOption(RT_LENS_SPIRIT_MQ) || CanUse(RG_LENS_OF_TRUTH)) && HasItem(RG_LONGSHOT);
}

// !QUANTUM LOGIC!
// With 3 keys, you cannot lock adult out of leaving spirit onto the hands and jumping down, as you would have to
// open the west hand door and then adult could climb through sun block room to jump down from there
// This requires that adult can complete both routes
// If we have the longshot, we can also guarantee access to the outer west hand as you can longshot from the east hand
// to the west Implies CanKillEnemy(RE_IRON_KNUCKLE)
bool Logic::OuterWestHandLogic() {
    return HasExplosives() && (HasItem(RG_CLIMB) || CanUse(RG_LONGSHOT)) && HasItem(RG_POWER_BRACELET) &&
           SmallKeys(SCENE_SPIRIT_TEMPLE, HasItem(RG_LONGSHOT) ? 3 : 5);
}

bool Logic::OuterWestHandMQLogic() {
    return MQSpiritStatueToSunBlock() && SmallKeys(SCENE_SPIRIT_TEMPLE, CouldMQSpirit4KeyWestHand() ? 4 : 7);
}

bool Logic::StatueRoomMQKeyLogic() {
    // !QUANTUM LOGIC!
    // If child enters in reverse, then they have access to Certain Access to Broken Wall room in 6 keys,
    // the ability to hit switches and the ability to climb because only child can reach the initial child lock
    // without opening the Statue room to Broken Wall Room lock first
    // if adult can ever cross crawlspaces this becomes more complicated.
    return SmallKeys(SCENE_SPIRIT_TEMPLE, IsChild && Get(LOGIC_REVERSE_SPIRIT_CHILD) && CanHitSwitch() &&
                                                  (HasItem(RG_CLIMB) || CanUse(RG_LONGSHOT))
                                              ? 6
                                              : 7);
}

void Logic::Reset(bool resetSaveContext /*= true*/) {
    if (resetSaveContext) {
        NewSaveContext();
    }
    StartPerformanceTimer(PT_LOGIC_RESET);
    memset(inLogic, false, sizeof(inLogic));

    if (resetSaveContext) {
        // Ocarina C Buttons
        bool ocBtnShuffle = ctx->GetOption(RSK_SHUFFLE_OCARINA_BUTTONS).Is(true);
        SetRandoInf(RAND_INF_HAS_OCARINA_A, !ocBtnShuffle);
        SetRandoInf(RAND_INF_HAS_OCARINA_C_UP, !ocBtnShuffle);
        SetRandoInf(RAND_INF_HAS_OCARINA_C_DOWN, !ocBtnShuffle);
        SetRandoInf(RAND_INF_HAS_OCARINA_C_LEFT, !ocBtnShuffle);
        SetRandoInf(RAND_INF_HAS_OCARINA_C_RIGHT, !ocBtnShuffle);

        // Progressive Items
        SetUpgrade(UPG_STICKS, ctx->GetOption(RSK_SHUFFLE_DEKU_STICK_BAG).Is(true) ? 0 : 1);
        SetUpgrade(UPG_NUTS, ctx->GetOption(RSK_SHUFFLE_DEKU_NUT_BAG).Is(true) ? 0 : 1);

        if (ctx->GetOption(RSK_SHUFFLE_SWIM).Is(false)) {
            SetRandoInf(RAND_INF_CAN_SWIM, true);
        }

        if (ctx->GetOption(RSK_SHUFFLE_GRAB).Is(false)) {
            SetRandoInf(RAND_INF_CAN_GRAB, true);
        }

        if (ctx->GetOption(RSK_SHUFFLE_CLIMB).Is(false)) {
            SetRandoInf(RAND_INF_CAN_CLIMB, true);
        }

        if (ctx->GetOption(RSK_SHUFFLE_CRAWL).Is(false)) {
            SetRandoInf(RAND_INF_CAN_CRAWL, true);
        }

        if (ctx->GetOption(RSK_SHUFFLE_OPEN_CHEST).Is(false)) {
            SetRandoInf(RAND_INF_CAN_OPEN_CHEST, true);
        }

        if (ctx->GetOption(RSK_SHUFFLE_SPEAK).Is(false)) {
            SetRandoInf(RAND_INF_CAN_SPEAK_DEKU, true);
            SetRandoInf(RAND_INF_CAN_SPEAK_GERUDO, true);
            SetRandoInf(RAND_INF_CAN_SPEAK_GORON, true);
            SetRandoInf(RAND_INF_CAN_SPEAK_HYLIAN, true);
            SetRandoInf(RAND_INF_CAN_SPEAK_KOKIRI, true);
            SetRandoInf(RAND_INF_CAN_SPEAK_ZORA, true);
        }

        if (ctx->GetOption(RSK_SHUFFLE_CHILD_WALLET).Is(false)) {
            SetRandoInf(RAND_INF_HAS_WALLET, true);
        }

        // If we're not shuffling fishing pole, we start with it
        if (ctx->GetOption(RSK_SHUFFLE_FISHING_POLE).Is(false)) {
            SetRandoInf(RAND_INF_FISHING_POLE_FOUND, true);
        }

        if (ctx->GetOption(RSK_SHUFFLE_BEAN_SOULS).Is(false)) {
            SetRandoInf(RAND_INF_DEATH_MOUNTAIN_CRATER_BEAN_SOUL, true);
            SetRandoInf(RAND_INF_DEATH_MOUNTAIN_TRAIL_BEAN_SOUL, true);
            SetRandoInf(RAND_INF_DESERT_COLOSSUS_BEAN_SOUL, true);
            SetRandoInf(RAND_INF_GERUDO_VALLEY_BEAN_SOUL, true);
            SetRandoInf(RAND_INF_GRAVEYARD_BEAN_SOUL, true);
            SetRandoInf(RAND_INF_KOKIRI_FOREST_BEAN_SOUL, true);
            SetRandoInf(RAND_INF_LAKE_HYLIA_BEAN_SOUL, true);
            SetRandoInf(RAND_INF_LOST_WOODS_BRIDGE_BEAN_SOUL, true);
            SetRandoInf(RAND_INF_LOST_WOODS_BEAN_SOUL, true);
            SetRandoInf(RAND_INF_ZORAS_RIVER_BEAN_SOUL, true);
        }

        // If not keysanity, start with 1 logical key to account for automatically unlocking the basement door in
        // vanilla FiT
        if (!IsFireLoopLocked() && ctx->GetDungeon(Rando::FIRE_TEMPLE)->IsVanilla()) {
            SetSmallKeyCount(SCENE_FIRE_TEMPLE, 1);
        }
    }

    Bottles = 0;
    NumBottles = 0;
    PieceOfHeart = 0;
    HeartContainer = 0;

    IsChild = false;
    IsAdult = false;
    BigPoes = 0;

    BaseHearts = ctx->GetOption(RSK_STARTING_HEARTS).Get() + 1;

    AtDay = false;
    AtNight = false;
    if (resetSaveContext) {
        GetSaveContext()->linkAge = !ctx->GetOption(RSK_SELECTED_STARTING_AGE).Get();
    }

    CalculatingAvailableChecks = false;

    StopPerformanceTimer(PT_LOGIC_RESET);
}
} // namespace Rando
