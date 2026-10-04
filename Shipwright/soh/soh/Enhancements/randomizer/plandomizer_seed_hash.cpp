#include "plandomizer_seed_hash.h"

#include <iterator>
#include <limits>
#include <vector>

#include <libultraship/log/luslog.h>

#include "Plandomizer.h"
#include "rando_hash.h"
#include "soh/SohGui/ImGuiUtils.h"

#include <fast/Fast3dGui.h>

namespace {
// One entry per loaded `file_hash` element, in file order, each already bounded by
// plandoSeedIconIndex on the way in. This is the column's only storage: spoilerHash was a second
// vector fed the identical value on the identical iteration and read exactly once, to size a
// column wrap that this one sizes the same way.
std::vector<int32_t> plandoHash;

// Plandomizer's producer-side twin of Rando::SeedIconIndex (rando_hash.h), which cannot be reused
// as it stands: a `file_hash` entry is a raw JSON int32 and that function takes a uint8_t, so
// passing 300 would narrow it to 44 -- in range, and silently the wrong icon, which is the very
// defect this exists to stop. The half of the bound a uint8_t cannot express is therefore checked
// here and the rest, against gSeedTextures, is left to the shared validator. Widening that
// signature would delete this function; it lives in files this change does not own.
// Audited 2026-08-13, docs/issues/0023, follow-on to 5f3d7f6c.
uint8_t plandoSeedIconIndex(const int32_t value, const int32_t position) {
    if (value < 0 || value > std::numeric_limits<uint8_t>::max()) {
        LUSLOG_ERROR("Plandomizer: file_hash[%d] icon id %d is out of range (valid 0..%u) -- REFUSED, "
                     "icon left at 0",
                     position, value, static_cast<unsigned>(std::numeric_limits<uint8_t>::max()));
        return 0;
    }
    return Rando::SeedIconIndex(static_cast<uint8_t>(value), "file_hash", position);
}

} // namespace

void PlandomizerClearSeedHash() {
    plandoHash.clear();
}

void PlandomizerLoadSeedHash(const nlohmann::json& fileHash) {
    // Audited 2026-08-13, docs/issues/0023. This is the only producer of the seed-icon ids
    // the row below reads back with gSeedTextures[hash].tex, so the bound belongs here.
    // An in-range id is stored exactly as it was; a refused one becomes 0, the value an
    // absent entry produces anyway and the only sink available, since Sprite.tex is an
    // asset NAME and there is no blank icon to point a refusal at. is_number() is checked
    // because the json->int32_t conversion this loop used to do implicitly throws
    // nlohmann::type_error, which the catch below (parse_error only) does not take -- a
    // stringified hash terminated the process instead of reporting anything.
    int32_t position = 0;
    for (auto& load : fileHash) {
        int32_t iconId = 0;
        if (load.is_number()) {
            iconId = plandoSeedIconIndex(load.get<int32_t>(), position);
        } else {
            LUSLOG_ERROR("Plandomizer: file_hash[%d] is not a number -- REFUSED, icon left at 0", position);
        }
        plandoHash.push_back(iconId);
        position++;
    }
}

void PlandomizerWriteSeedHash(nlohmann::json& spoilerSave) {
    // Audited 2026-08-13, docs/issues/0023. plandoHash holds one entry per loaded `file_hash` element,
    // so it holds fewer than five whenever that file's hash is short or absent -- and `locations`
    // alone is what enables the Save button. A five-entry hash, all the randomizer ever writes, dumps
    // exactly as it did before.
    nlohmann::json fileHash = nlohmann::json::array();
    for (const int32_t iconId : plandoHash) {
        fileHash.push_back(iconId);
    }
    spoilerSave["file_hash"] = fileHash;
}

void PlandomizerDrawSeedHash(bool hasLoadedSpoilerLog) {
    ImGui::SeparatorText("Current Seed Hash");
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (ImGui::GetContentRegionAvail().x * 0.5f) - (34.0f * 5.0f));
    if (hasLoadedSpoilerLog) {
        if (ImGui::BeginTable("HashIcons", 5)) {
            for (int i = 0; i < 5; i++) {
                ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            }
            ImGui::TableNextColumn();

            int32_t index = 0;
            PlandoPushImageButtonStyle();
            for (auto& hash : plandoHash) {
                ImGui::PushID(index);
                void* textureID =
                    std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                        ->GetTextureByName(gSeedTextures[hash].tex);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
                auto upRet = ImGui::ImageButton(
                    "HASH_ARROW_UP",
                    std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                        ->GetTextureByName("HASH_ARROW_UP"),
                    ImVec2(35.0f, 18.0f), ImVec2(1, 1), ImVec2(0, 0), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1));
                ImGui::PopStyleVar();
                if (upRet) {
                    if (hash + 1 >= std::ssize(gSeedTextures)) {
                        hash = 0;
                    } else {
                        hash++;
                    }
                }
                ImGui::Image(textureID, ImVec2(35.0f, 35.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
                auto downRet = ImGui::ImageButton(
                    "HASH_ARROW_DWN",
                    std::dynamic_pointer_cast<Fast::Fast3dGui>(Ship::Context::GetRawInstance()->GetWindow()->GetGui())
                        ->GetTextureByName("HASH_ARROW_DWN"),
                    ImVec2(35.0f, 18.0f), ImVec2(0, 0), ImVec2(1, 1), ImVec4(0, 0, 0, 0), ImVec4(1, 1, 1, 1));
                ImGui::PopStyleVar();
                if (downRet) {
                    if (hash == 0) {
                        hash = static_cast<int32_t>(gSeedTextures.size()) - 1;
                    } else {
                        hash--;
                    }
                }
                if (index != std::ssize(plandoHash) - 1) {
                    ImGui::TableNextColumn();
                }
                ImGui::PopID();
                index++;
            }
            PlandoPopImageButtonStyle();
            ImGui::EndTable();
        }
    } else {
        ImGui::Text("No Spoiler Log Loaded");
    }
}
