#include "title_light_slots.h"

#include "../cutscene/zelda3d_cutscene.h"

namespace {

constexpr int kMaximumTitleLightSlots = 32;
Zelda3dLightSlot sTitleLightSlots[kMaximumTitleLightSlots];
int sTitleLightSlotCount = -1;

void ConvertTitleLightSlots() {
    const unsigned char* raw;
    int count;
    sTitleLightSlotCount = 0;
    if (!Zelda3D_TitleCsLightSlotsRaw(&raw, &count)) {
        return;
    }
    if (count > kMaximumTitleLightSlots) {
        count = kMaximumTitleLightSlots;
    }

    for (int i = 0; i < count; i++) {
        const unsigned char* entry = raw + i * 28;
        Zelda3dLightSlot* slot = &sTitleLightSlots[i];
        // Offsets per oot3d-decomp/docs/title_env_lighting.md §6 (decompiled
        // Environment_Update consumer, on-disk cmd-0x0F layout): direction
        // BEFORE color within each light group, matching N64's EnvLightSettings
        // field order byte-for-byte. The prior offsets read l0col/l1col before
        // their dir (swapped, off-by-one) and produced degenerate (0,0,0) or
        // constant (-72,-72,-72) directions for ~15/17 spot99 slots.
        // fogCol (the gameplay palette's +0x19 field, added 2026-07-22) is deliberately left at
        // zero here: the title drives its own fog window through Zelda3D_TitleCsBlendedFog and
        // never reads this field, and the title record's layout past the colour block has not
        // been byte-verified. Do not "fill it in" without checking against the oracle.
        for (int component = 0; component < 3; component++) {
            slot->amb[component] = entry[0x00 + component];
            slot->l0dir[component] = static_cast<signed char>(entry[0x03 + component]);
            slot->l0col[component] = entry[0x06 + component];
            slot->l1dir[component] = static_cast<signed char>(entry[0x09 + component]);
            slot->l1col[component] = entry[0x0c + component];
        }
    }
    sTitleLightSlotCount = count;
}

void EnsureTitleLightSlotsConverted() {
    if (sTitleLightSlotCount < 0) {
        ConvertTitleLightSlots();
    }
}

} // namespace

const Zelda3dLightSlot* Zelda3D_TitleLightSlots(void) {
    EnsureTitleLightSlotsConverted();
    return sTitleLightSlots;
}

int Zelda3D_TitleLightSlotCount(void) {
    EnsureTitleLightSlotsConverted();
    return sTitleLightSlotCount;
}
