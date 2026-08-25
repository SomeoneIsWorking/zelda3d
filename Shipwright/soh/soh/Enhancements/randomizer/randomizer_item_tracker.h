#pragma once

#include <string>
#include <vector>
#include <stdint.h>
#include <libultraship/libultraship.h>

void DrawItemAmmo(int itemId);

typedef struct ItemTrackerItem {
    uint32_t id;
    std::string name;
    std::string nameFaded;
    uint32_t data;
    void (*drawFunc)(ItemTrackerItem);
} ItemTrackerItem;

bool HasSong(ItemTrackerItem);
bool HasQuestItem(ItemTrackerItem);
bool HasEquipment(ItemTrackerItem);

#define ITEM_TRACKER_ITEM(id, data, drawFunc) { id, #id, #id "_Faded", data, drawFunc }

#define ITEM_TRACKER_ITEM_CUSTOM(id, name, nameFaded, data, drawFunc) { id, #name, #nameFaded "_Faded", data, drawFunc }

void ItemTracker_LoadFromPreset(nlohmann::json trackerInfo);

typedef struct ItemTrackerDungeon {
    uint32_t id;
    std::vector<uint32_t> items;
} ItemTrackerDungeon;

enum ItemTrackerNumberOption {
    ITEM_TRACKER_NUMBER_NONE,
    ITEM_TRACKER_NUMBER_CURRENT_CAPACITY_ONLY,
    ITEM_TRACKER_NUMBER_CURRENT_AMMO_ONLY,
    ITEM_TRACKER_NUMBER_CAPACITY,
    ITEM_TRACKER_NUMBER_AMMO,
};

enum ItemTrackerKeysNumberOption {
    KEYS_COLLECTED_MAX,
    KEYS_CURRENT_COLLECTED_MAX,
    KEYS_CURRENT_MAX,
};

enum ItemTrackerTriforcePieceNumberOption {
    TRIFORCE_PIECE_COLLECTED_REQUIRED,
    TRIFORCE_PIECE_COLLECTED_REQUIRED_MAX,
};

enum ItemTrackerDisplayType {
    SECTION_DISPLAY_HIDDEN,
    SECTION_DISPLAY_MAIN_WINDOW,
    SECTION_DISPLAY_SEPARATE,
};

enum ItemTrackerExtendedDisplayType {
    SECTION_DISPLAY_EXTENDED_HIDDEN,
    SECTION_DISPLAY_EXTENDED_MAIN_WINDOW,
    SECTION_DISPLAY_EXTENDED_MISC_WINDOW,
    SECTION_DISPLAY_EXTENDED_SEPARATE,
};

enum ItemTrackerMinimalDisplayType {
    SECTION_DISPLAY_MINIMAL_HIDDEN,
    SECTION_DISPLAY_MINIMAL_SEPARATE,
};

struct ItemTrackerNumbers {
    int currentCapacity;
    int maxCapacity;
    int currentAmmo;
};

class ItemTrackerSettingsWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;

  protected:
    void InitElement() override {};
    void DrawElement() override;
    void UpdateElement() override {};
};

class ItemTrackerWindow final : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void Draw() override;

  protected:
    void InitElement() override;
    void DrawElement() override;
    void UpdateElement() override {};
};
