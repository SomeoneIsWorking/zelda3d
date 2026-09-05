#include "debugSaveEditorInternal.h"

#include "soh/OTRGlobals.h"
#include "soh/Enhancements/randomizer/randomizerTypes.h"
#include "soh/Enhancements/randomizer/randomizer.h"
#include "soh/util.h"
#include "soh/SohGui/ImGuiUtils.h"
#include "soh/SohGui/SohGui.hpp"
#include "soh/SaveManager.h"

#include <array>
#include <bit>
#include <map>
#include <string>
#include <vector>

#include <libultraship/bridge.h>
#include <libultraship/libultraship.h>
#include <soh_assets.h>
#include <spdlog/fmt/fmt.h>

#include "message_data_static.h"

extern "C" {
#include <z64.h>
#include "variables.h"
#include "functions/game_state.h"
#include "macros.h"
#include "soh/cvar_prefixes.h"
extern PlayState* gPlayState;
}

using namespace UIWidgets;

extern "C" MessageTableEntry* sGerMessageEntryTablePtr;
extern "C" MessageTableEntry* sFraMessageEntryTablePtr;
extern "C" MessageTableEntry* sJpnMessageEntryTablePtr;

char z2ASCII(int code) {
    int ret;
    if (code < 10) { // Digits
        ret = code + 0x30;
    } else if (code >= 10 && code < 36) { // Uppercase letters
        ret = code + 0x37;
    } else if (code >= 36 && code < 62) { // Lowercase letters
        ret = code + 0x3D;
    } else if (code == 62) { // Space
        ret = code - 0x1E;
    } else if (code == 63 || code == 64) { // _ and .
        ret = code - 0x12;
    } else {
        ret = code;
    }
    return char(ret);
}

std::string decodeNTSCPlayerNameChar(int code) {
    const std::string charmap[] = {
        "0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",  // 10
        "あ", "い", "う", "え", "お", "か", "き", "く", "け", "こ", // 20
        "さ", "し", "す", "せ", "そ", "た", "ち", "つ", "て", "と", // 30
        "な", "に", "ぬ", "ね", "の", "は", "ひ", "ふ", "へ", "ほ", // 40
        "ま", "み", "む", "め", "も", "や", "ゆ", "よ", "ら", "り", // 50
        "る", "れ", "ろ", "わ", "を", "ん", "ぁ", "ぃ", "ぅ", "ぇ", // 60
        "ぉ", "っ", "ゃ", "ゅ", "ょ", "が", "ぎ", "ぐ", "げ", "ご", // 70
        "ざ", "じ", "ず", "ぜ", "ぞ", "だ", "ぢ", "づ", "で", "ど", // 80
        "ば", "び", "ぶ", "べ", "ぼ", "ぱ", "ぴ", "ぷ", "ぺ", "ぽ", // 90
        "ア", "イ", "ウ", "エ", "オ", "カ", "キ", "ク", "ケ", "コ", // 100
        "サ", "シ", "ス", "セ", "ソ", "タ", "チ", "ツ", "テ", "ト", // 110
        "ナ", "ニ", "ヌ", "ネ", "ノ", "ハ", "ヒ", "フ", "ヘ", "ホ", // 120
        "マ", "ミ", "ム", "メ", "モ", "ヤ", "ユ", "ヨ", "ラ", "リ", // 130
        "ル", "レ", "ロ", "ワ", "ヲ", "ン", "ァ", "ィ", "ゥ", "ェ", // 140
        "ォ", "ッ", "ャ", "ュ", "ョ", "ガ", "ギ", "グ", "ゲ", "ゴ", // 150
        "ザ", "ジ", "ズ", "ゼ", "ゾ", "ダ", "ヂ", "ヅ", "デ", "ド", // 160
        "バ", "ビ", "ブ", "ベ", "ボ", "パ", "ピ", "プ", "ペ", "ポ", // 170
        "ヴ",
    };
    std::string ret;

    if (code < 171) { // Digits and Japanese
        ret = charmap[code];
    } else if (code >= 171 && code < 197) { // Uppercase letters
        ret.assign(1, (char)(code - 171 + 65));
    } else if (code >= 197 && code < 223) { // Lowercase letters
        ret.assign(1, (char)(code - 197 + 97));
    } else if (code == 223) { // Space
        ret = " ";
    } else if (code == 228) { // -
        ret = "-";
    } else if (code == 234) { // .
        ret = ".";
    } else {
        ret = "?";
    }

    return ret;
}

enum MagicLevel { MAGIC_LEVEL_NONE, MAGIC_LEVEL_SINGLE, MAGIC_LEVEL_DOUBLE };

std::map<int8_t, const char*> magicLevelMap = {
    { MAGIC_LEVEL_NONE, "None" },
    { MAGIC_LEVEL_SINGLE, "Single" },
    { MAGIC_LEVEL_DOUBLE, "Double" },
};

enum AudioOutput {
    AUDIO_STEREO,
    AUDIO_MONO,
    AUDIO_HEADSET,
    AUDIO_SURROUND,
};

std::map<uint8_t, const char*> audioMap = {
    { AUDIO_STEREO, "Stereo" },
    { AUDIO_MONO, "Mono" },
    { AUDIO_HEADSET, "Headset" },
    { AUDIO_SURROUND, "Surround" },
};

enum ZTarget {
    Z_TARGET_SWITCH,
    Z_TARGET_HOLD,
};

std::map<uint8_t, const char*> zTargetMap = {
    { Z_TARGET_SWITCH, "Switch" },
    { Z_TARGET_HOLD, "Hold" },
};

std::map<int32_t, const char*> fileNumMap = {
    { 0, "File 1" },
    { 1, "File 2" },
    { 2, "File 3" },
};

std::map<uint8_t, const char*> filenameLanguageMap = {
    { NAME_LANGUAGE_PAL, "PAL" },
    { NAME_LANGUAGE_NTSC_JPN, "NTSC JPN" },
    { NAME_LANGUAGE_NTSC_ENG, "NTSC ENG" },
};

std::map<uint8_t, const char*> filenameLanguageMapNTSCOnly = {
    { NAME_LANGUAGE_NTSC_JPN, "NTSC JPN" },
    { NAME_LANGUAGE_NTSC_ENG, "NTSC ENG" },
};

void DrawInfoTab() {
    if (gSaveContext.gameMode == GAMEMODE_TITLE_SCREEN) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Title Screen");
    } else if (gSaveContext.gameMode == GAMEMODE_FILE_SELECT) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "File Select");
    } else if (gPlayState == nullptr) {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Game Inactive");
    } else if (gSaveContext.fileNum >= 0 && gSaveContext.fileNum <= 2) {
        Combobox("File Number", &gSaveContext.fileNum, fileNumMap, comboboxOptionsBase.Tooltip("Current File Number"));
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Debug File");
    }

    // TODO Needs a better method for name changing but for now this will work.
    std::string name;
    ImU16 one = 1;

    if (gSaveContext.ship.filenameLanguage == NAME_LANGUAGE_PAL) {
        for (int i = 0; i < 8; i++) {
            char letter = z2ASCII(gSaveContext.playerName[i]);
            name += letter;
        }
        name += '\0';
    } else {
        for (int i = 0; i < 8; i++) {
            name += decodeNTSCPlayerNameChar(gSaveContext.playerName[i]);
        }
        name += '\0';
    }

    ImGui::PushItemWidth(ImGui::GetFontSize() * 6);

    if (gSaveContext.ship.filenameLanguage == NAME_LANGUAGE_PAL) {
        ImGui::Text("Name: %s", name.c_str());
    } else {
        ImGui::PushFont(OTRGlobals::Instance->fontJapanese);
        ImGui::Text("Name: %s", name.c_str());
        ImGui::PopFont();
    }

    Tooltip("Player Name");
    std::string nameID;
    for (int i = 0; i < 8; i++) {
        nameID = z2ASCII(i);
        if (i % 4 != 0) {
            ImGui::SameLine();
        }
        PushStyleInput(THEME_COLOR);
        ImGui::InputScalar(nameID.c_str(), ImGuiDataType_U8, &gSaveContext.playerName[i], &one, NULL);
        PopStyleInput();
    }

    // Filename encoding
    const bool hasPAL = (sGerMessageEntryTablePtr != nullptr) && (sFraMessageEntryTablePtr != nullptr);
    const bool hasNTSC = (sJpnMessageEntryTablePtr != nullptr);
    if (hasPAL && hasNTSC) {
        // Full
        Combobox("Player Name Language", &gSaveContext.ship.filenameLanguage, filenameLanguageMap,
                 comboboxOptionsBase.Tooltip("Encoding used for Player Name"));
    } else if (hasNTSC && (gSaveContext.ship.filenameLanguage != NAME_LANGUAGE_PAL)) {
        // NTSC only
        Combobox("Player Name Language", &gSaveContext.ship.filenameLanguage, filenameLanguageMapNTSCOnly,
                 comboboxOptionsBase.Tooltip("Encoding used for Player Name"));
    } else {
        // PAL only (read only)
        ImGui::BeginDisabled();
        Combobox("Player Name Language", &gSaveContext.ship.filenameLanguage, filenameLanguageMap,
                 comboboxOptionsBase.Tooltip("Encoding used for Player Name"));
        ImGui::EndDisabled();
    }

    // Use an intermediary to keep the health from updating (and potentially killing the player)
    // until it is done being edited
    int16_t healthIntermediary = gSaveContext.healthCapacity;
    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Max Health", ImGuiDataType_S16, &healthIntermediary);
    PopStyleInput();
    if (ImGui::IsItemDeactivated()) {
        gSaveContext.healthCapacity = healthIntermediary;
    }
    Tooltip("Maximum health. 16 units per full heart");
    if (gSaveContext.health > gSaveContext.healthCapacity) {
        gSaveContext.health = gSaveContext.healthCapacity; // Clamp health to new max
    }
    int32_t health = (int32_t)gSaveContext.health;
    if (SliderInt("Health", &health,
                  intSliderOptionsBase.Tooltip("Current health. 16 units per full heart")
                      .Min(0)
                      .Max(gSaveContext.healthCapacity))) {
        gSaveContext.health = (int16_t)health;
    }

    bool isDoubleDefenseAcquired = gSaveContext.isDoubleDefenseAcquired != 0;
    if (Checkbox("Double Defense", &isDoubleDefenseAcquired,
                 checkboxOptionsBase.Tooltip("Is double defense unlocked?"))) {
        gSaveContext.isDoubleDefenseAcquired = isDoubleDefenseAcquired;
        gSaveContext.inventory.defenseHearts =
            gSaveContext.isDoubleDefenseAcquired ? 20 : 0; // Set to get the border drawn in the UI
    }
    if (Combobox("Magic Level", &gSaveContext.magicLevel, magicLevelMap,
                 comboboxOptionsBase.Tooltip("Current magic level"))) {
        gSaveContext.isMagicAcquired = gSaveContext.magicLevel > 0;
        gSaveContext.isDoubleMagicAcquired = gSaveContext.magicLevel == 2;
    }
    gSaveContext.magicCapacity = gSaveContext.magicLevel * 0x30; // Set to get the bar drawn in the UI
    if (gSaveContext.magic > gSaveContext.magicCapacity) {
        gSaveContext.magic = static_cast<s8>(gSaveContext.magicCapacity); // Clamp magic to new max
    }

    int32_t magic = (int32_t)gSaveContext.magic;
    if (SliderInt("Magic", &magic,
                  intSliderOptionsBase.Min(0)
                      .Max(gSaveContext.magicCapacity)
                      .Tooltip("Current magic. 48 units per magic level"))) {
        gSaveContext.magic = (int8_t)magic;
    }

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Rupees", ImGuiDataType_S16, &gSaveContext.rupees);
    Tooltip("Current rupees");
    PopStyleInput();

    SliderInt("Time", (int32_t*)&gSaveContext.dayTime, intSliderOptionsBase.Min(0).Max(0xFFFF).Tooltip("Time of day"));
    if (Button("Dawn", buttonOptionsBase)) {
        gSaveContext.dayTime = 0x4000;
    }
    ImGui::SameLine();
    if (Button("Noon", buttonOptionsBase)) {
        gSaveContext.dayTime = 0x8000;
    }
    ImGui::SameLine();
    if (Button("Sunset", buttonOptionsBase)) {
        gSaveContext.dayTime = 0xC001;
    }
    ImGui::SameLine();
    if (Button("Midnight", buttonOptionsBase)) {
        gSaveContext.dayTime = 0;
    }

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Total Days", ImGuiDataType_S32, &gSaveContext.totalDays);
    Tooltip("Total number of days elapsed since the start of the game");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Deaths", ImGuiDataType_U16, &gSaveContext.deaths);
    Tooltip("Total number of deaths");
    PopStyleInput();

    Checkbox("Has BGS", (bool*)&gSaveContext.bgsFlag,
             checkboxOptionsBase.Tooltip("Is Biggoron sword unlocked? Replaces Giant's knife"));

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Sword Health", ImGuiDataType_U16, &gSaveContext.swordHealth);
    Tooltip("Giant's knife health. Default is 8. Must be >0 for Biggoron sword to work");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Bgs Day Count", ImGuiDataType_S32, &gSaveContext.bgsDayCount);
    Tooltip("Total number of days elapsed since receiving claim check from Biggoron");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Entrance Index", ImGuiDataType_S32, &gSaveContext.entranceIndex);
    Tooltip("From which entrance did Link arrive?");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Cutscene Index", ImGuiDataType_S32, &gSaveContext.cutsceneIndex);
    Tooltip("Which cutscene is this?");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Navi Timer", ImGuiDataType_U16, &gSaveContext.naviTimer);
    Tooltip("Navi wants to talk at 600 units, decides not to at 3000.");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Timer State", ImGuiDataType_S16, &gSaveContext.timerState);
    Tooltip("Heat timer, race timer, etc. Has white font");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Timer Seconds", ImGuiDataType_S16, &gSaveContext.timerSeconds, &one, NULL);
    Tooltip("Time, in seconds");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Sub-Timer State", ImGuiDataType_S16, &gSaveContext.subTimerState);
    Tooltip("Trade timer, Ganon collapse timer, etc. Has yellow font");
    PopStyleInput();

    PushStyleInput(THEME_COLOR);
    ImGui::InputScalar("Sub-Timer Seconds", ImGuiDataType_S16, &gSaveContext.subTimerSeconds, &one, NULL);
    Tooltip("Time, in seconds");
    PopStyleInput();

    Combobox("Audio", &gSaveContext.audioSetting, audioMap, comboboxOptionsBase.Tooltip("Sound setting"));

    Checkbox("64 DD file?", (bool*)&gSaveContext.n64ddFlag,
             checkboxOptionsBase.Tooltip("WARNING! If you save, your file may be locked! Use caution!"));

    Combobox("Z Target Mode", &gSaveContext.zTargetSetting, zTargetMap,
             comboboxOptionsBase.Tooltip("Z-Targeting behavior"));

    if (IS_RANDO &&
        (OTRGlobals::Instance->gRandomizer->GetRandoSettingValue(RSK_TRIFORCE_HUNT) != RO_TRIFORCE_HUNT_OFF)) {
        PushStyleInput(THEME_COLOR);
        ImGui::InputScalar("Triforce Pieces", ImGuiDataType_U8,
                           &gSaveContext.ship.quest.data.randomizer.triforcePiecesCollected);
        Tooltip("Currently obtained Triforce Pieces. For Triforce Hunt.");
        PopStyleInput();
    }

    ImGui::PushItemWidth(ImGui::GetFontSize() * 10);
    static std::array<const char*, 7> minigameHS = { "Horseback Archery", "Big Poe Points",
                                                     "Fishing",           "Malon's Obstacle Course",
                                                     "Running Man Race",  "?",
                                                     "Dampe's Race" };

    if (ImGui::TreeNode("Minigames")) {
        for (int i = 0; i < 7; i++) {
            if (i == 2 && ImGui::TreeNode("Fishing")) { // fishing has a few more flags to it
                u8 fishSize = gSaveContext.highScores[i] & 0x7F;
                PushStyleInput(THEME_COLOR);
                if (ImGui::InputScalar("Child Size Record", ImGuiDataType_U8, &fishSize)) {
                    gSaveContext.highScores[i] &= ~0x7F;
                    gSaveContext.highScores[i] |= fishSize & 0x7F;
                }
                char fishMsg[64];
                std::snprintf(fishMsg, 64, "Weight: %2.0f lbs", ((SQ(fishSize) * .0036) + .5));
                Tooltip(fishMsg);
                PopStyleInput();
                bool FishBool = gSaveContext.highScores[i] & 0x80;
                if (Checkbox("Cheated as Child", &FishBool,
                             checkboxOptionsBase.Tooltip("Used the Sinking lure to catch it."))) {
                    gSaveContext.highScores[i] &= ~0x80;
                    gSaveContext.highScores[i] |= (0x80 * FishBool);
                }
                fishSize = (gSaveContext.highScores[i] & 0x7F000000) >> 0x18;
                PushStyleInput(THEME_COLOR);
                if (ImGui::InputScalar("Adult Size Record", ImGuiDataType_U8, &fishSize)) {
                    gSaveContext.highScores[i] &= ~0x7F000000;
                    gSaveContext.highScores[i] |= (fishSize & 0x7F) << 0x18;
                }
                std::snprintf(fishMsg, 64, "Weight: %2.0f lbs", ((SQ(fishSize) * .0036) + .5));
                Tooltip(fishMsg);
                PopStyleInput();
                FishBool = gSaveContext.highScores[i] & 0x80000000;
                if (Checkbox("Cheated as Adult", &FishBool,
                             checkboxOptionsBase.Tooltip("Used the Sinking lure to catch it."))) {
                    gSaveContext.highScores[i] &= ~0x80000000;
                    gSaveContext.highScores[i] |= (0x80000000 * FishBool);
                }
                FishBool = gSaveContext.highScores[i] & 0x100;
                if (Checkbox("Played as Child", &FishBool,
                             checkboxOptionsBase.Tooltip("Played at least one game as a child"))) {
                    gSaveContext.highScores[i] &= ~0x100;
                    gSaveContext.highScores[i] |= (0x100 * FishBool);
                }
                FishBool = gSaveContext.highScores[i] & 0x200;
                if (Checkbox("Played as Adult", &FishBool,
                             checkboxOptionsBase.Tooltip("Played at least one game as an adult"))) {
                    gSaveContext.highScores[i] &= ~0x200;
                    gSaveContext.highScores[i] |= (0x200 * FishBool);
                }
                FishBool = gSaveContext.highScores[i] & 0x400;
                if (Checkbox(
                        "Got Prize as Child", &FishBool,
                        checkboxOptionsBase.Tooltip(
                            "Got the prize item (Heart Piece, unless rando.)\nunlocks Sinking Lure for Child Link."))) {
                    gSaveContext.highScores[i] &= ~0x400;
                    gSaveContext.highScores[i] |= (0x400 * FishBool);
                }
                FishBool = gSaveContext.highScores[i] & 0x800;
                if (Checkbox("Got Prize as Adult", &FishBool,
                             checkboxOptionsBase.Tooltip("Got the prize item (Golden Scale, unless rando.)\nUnlocks "
                                                         "Sinking Lure for Adult Link."))) {
                    gSaveContext.highScores[i] &= ~0x800;
                    gSaveContext.highScores[i] |= (0x800 * FishBool);
                }
                FishBool = gSaveContext.highScores[i] & 0x1000;
                if (Checkbox("Stole Owner's Hat", &FishBool,
                             checkboxOptionsBase.Tooltip("The owner's now visibly bald when Adult Link."))) {
                    gSaveContext.highScores[i] &= ~0x1000;
                    gSaveContext.highScores[i] |= (0x1000 * FishBool);
                }
                fishSize = (gSaveContext.highScores[i] & 0xFF0000) >> 16;
                PushStyleInput(THEME_COLOR);
                if (ImGui::InputScalar("Times Played", ImGuiDataType_U8, &fishSize)) {
                    gSaveContext.highScores[i] &= ~0xFF0000;
                    gSaveContext.highScores[i] |= (fishSize) << 16;
                }
                Tooltip("Determines weather and school size during dawn/dusk.");
                PopStyleInput();

                ImGui::TreePop();
                continue;
            }

            if (i == 5 || i == 2) { // HS_UNK_05 is unused
                continue;
            }
            std::string minigameLbl = minigameHS[i];
            PushStyleInput(THEME_COLOR);
            ImGui::InputScalar(minigameLbl.c_str(), ImGuiDataType_S32, &gSaveContext.highScores[i], &one, NULL);
            PopStyleInput();
        }

        ImGui::TreePop();
    }

    ImGui::PopItemWidth();
}
