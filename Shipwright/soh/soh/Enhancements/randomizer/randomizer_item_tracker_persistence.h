#pragma once

#include <nlohmann/json.hpp>

void ItemTrackerOnFrame();
void ItemTracker_LoadFromPreset(nlohmann::json trackerInfo);
void ItemTracker_SaveToPreset(nlohmann::json& windows);
void ApplyItemTrackerPresetPlacement(const char* uniqueName);
void FinishItemTrackerPresetPlacement();
void DrawItemTrackerNotes(bool separateWindow = false);
void InitializeItemTrackerPersistence();
