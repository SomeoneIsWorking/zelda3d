#pragma once

#include <vector>

#include "randomizer_item_tracker.h"

void BeginFloatingWindows(const char* uniqueName, int flags = 0);
void EndFloatingWindows();
void DrawItemsInRows(const std::vector<ItemTrackerItem>& items, int itemsPerRow = 6);
void DrawItemsInACircle(const std::vector<ItemTrackerItem>& items);
