#include "randomizer_item_tracker_persistence.h"

#include <array>
#include <cfloat>
#include <string>
#include <unordered_map>

#include "randomizer_item_tracker_model.h"
#include "soh/SaveManager.h"
#include "soh/SohGui/UIWidgets.hpp"
#include "soh/cvar_prefixes.h"
#include "soh/util.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"

extern "C" {
#include <z64.h>
#include "variables.h"
}

namespace {
constexpr std::array<const char*, 15> kItemTrackerWindowIds = {
    "Item Tracker",
    "Inventory Items Tracker",
    "Equipment Items Tracker",
    "Misc Items Tracker",
    "Dungeon Rewards Tracker",
    "Songs Tracker",
    "Dungeon Items Tracker",
    "Greg Tracker",
    "Triforce Piece Tracker",
    "Boss Soul Tracker",
    "Ocarina Button Tracker",
    "Overworld Key Tracker",
    "Fishing Pole Tracker",
    "Personal Notes",
    "Total Checks",
};

ImVector<char> itemTrackerNotes;
uint32_t notesIdleFrames = 0;
bool notesNeedSave = false;
constexpr uint32_t kNotesMaxIdleFrames = 40;

bool presetLoaded = false;
std::unordered_map<std::string, ImVec2> presetPos;
std::unordered_map<std::string, ImVec2> presetSize;
int itemTrackerSectionId = 0;

void ItemTrackerInitFile(bool isDebug) {
    itemTrackerNotes.clear();
    itemTrackerNotes.push_back(0);
}

void ItemTrackerSaveFile(SaveContext* saveContext, int sectionID, bool fullSave) {
    SaveManager::Instance->SaveData("personalNotes",
                                    std::string(std::begin(itemTrackerNotes), std::end(itemTrackerNotes)).c_str());
}

void ItemTrackerLoadFile() {
    std::string initialTrackerNotes;
    SaveManager::Instance->LoadData("personalNotes", initialTrackerNotes);
    itemTrackerNotes.resize(static_cast<int>(initialTrackerNotes.length() + 1));
    if (!initialTrackerNotes.empty()) {
        SohUtils::CopyStringToCharArray(itemTrackerNotes.Data, initialTrackerNotes.c_str(), itemTrackerNotes.size());
    } else {
        itemTrackerNotes.push_back(0);
    }
}

struct ItemTrackerNotesInput {
    static int ResizeCallback(ImGuiInputTextCallbackData* data) {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
            auto* notes = static_cast<ImVector<char>*>(data->UserData);
            IM_ASSERT(notes->begin() == data->Buf);
            notes->resize(data->BufSize);
            data->Buf = notes->begin();
        }
        return 0;
    }

    static bool Draw(const char* label, ImVector<char>* notes, const ImVec2& size, ImGuiInputTextFlags flags) {
        IM_ASSERT((flags & ImGuiInputTextFlags_CallbackResize) == 0);
        return ImGui::InputTextMultiline(label, notes->begin(), static_cast<size_t>(notes->size()), size,
                                         flags | ImGuiInputTextFlags_CallbackResize, ResizeCallback, notes);
    }
};
} // namespace

void ItemTrackerOnFrame() {
    if (notesNeedSave && notesIdleFrames <= kNotesMaxIdleFrames) {
        notesIdleFrames++;
    }
}

void ItemTracker_LoadFromPreset(nlohmann::json trackerInfo) {
    presetLoaded = true;
    for (const char* window : kItemTrackerWindowIds) {
        if (trackerInfo.contains(window)) {
            presetPos[window] = { trackerInfo[window]["pos"]["x"], trackerInfo[window]["pos"]["y"] };
            presetSize[window] = { trackerInfo[window]["size"]["width"], trackerInfo[window]["size"]["height"] };
        }
    }
}

void ItemTracker_SaveToPreset(nlohmann::json& windows) {
    for (const char* window : kItemTrackerWindowIds) {
        const ImGuiWindow* imguiWindow = ImGui::FindWindowByName(window);
        if (imguiWindow == nullptr) {
            continue;
        }
        windows[window]["size"]["width"] = imguiWindow->Size.x;
        windows[window]["size"]["height"] = imguiWindow->Size.y;
        windows[window]["pos"]["x"] = imguiWindow->Pos.x;
        windows[window]["pos"]["y"] = imguiWindow->Pos.y;
    }
}

void ApplyItemTrackerPresetPlacement(const char* uniqueName) {
    if (presetLoaded && presetPos.contains(uniqueName)) {
        ImGui::SetNextWindowSize(presetSize[uniqueName]);
        ImGui::SetNextWindowPos(presetPos[uniqueName]);
        presetSize.erase(uniqueName);
        presetPos.erase(uniqueName);
    }
}

void FinishItemTrackerPresetPlacement() {
    if (presetLoaded) {
        shouldUpdateVectors = true;
        presetLoaded = false;
    }
}

void DrawItemTrackerNotes(bool separateWindow) {
    ImGui::BeginGroup();
    float iconSize = static_cast<float>(CVarGetInteger(CVAR_TRACKER_ITEM("IconSize"), 36));
    int iconSpacing = CVarGetInteger(CVAR_TRACKER_ITEM("IconSpacing"), 12);
    ImVec2 size = separateWindow ? ImVec2(-FLT_MIN, ImGui::GetContentRegionAvail().y)
                                 : ImVec2(((iconSize + iconSpacing) * 6) - 8.0f, 200.0f);
    if (GameInteractor::IsSaveLoaded()) {
        if (ItemTrackerNotesInput::Draw("##ItemTrackerNotes", &itemTrackerNotes, size,
                                        ImGuiInputTextFlags_AllowTabInput)) {
            notesNeedSave = true;
            notesIdleFrames = 0;
        }
        if ((ImGui::IsItemDeactivatedAfterEdit() || (notesNeedSave && notesIdleFrames > kNotesMaxIdleFrames)) &&
            IsValidSaveFile()) {
            notesNeedSave = false;
            SaveManager::Instance->SaveSection(gSaveContext.fileNum, itemTrackerSectionId, true);
        }
    }
    ImGui::EndGroup();
}

void InitializeItemTrackerPersistence() {
    if (itemTrackerNotes.empty()) {
        itemTrackerNotes.push_back(0);
    }
    SaveManager::Instance->AddInitFunction(ItemTrackerInitFile);
    itemTrackerSectionId = SaveManager::Instance->AddSaveFunction("itemTrackerData", 1, ItemTrackerSaveFile, true, -1);
    SaveManager::Instance->AddLoadFunction("itemTrackerData", 1, ItemTrackerLoadFile);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(ItemTrackerOnFrame);
}
