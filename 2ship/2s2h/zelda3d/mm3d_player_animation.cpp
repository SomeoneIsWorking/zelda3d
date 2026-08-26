#include "mm3d_player_animation.h"

#include <cstdio>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "asset/lzs.h"
#include "mm3d_model_store.h"
#include "mm3d_player_animation_policy.h"

namespace Zelda3D::MM3D {
namespace {

constexpr const char* kPlayerAnimationArchive = "/actors/zelda2_link_new.gar.lzs";

struct PlayerAnimationRoute {
    PlayerModelForm form;
    std::unordered_map<std::string, std::string> resolvedPaths;
};

std::unordered_map<int, PlayerAnimationRoute> g_playerModels;
std::unique_ptr<Gar> g_playerAnimations;
std::unordered_set<std::string> g_playerAnimationMembers;
bool g_archiveLoadAttempted = false;

const Gar* LoadPlayerAnimationArchive() {
    if (g_playerAnimations != nullptr || g_archiveLoadAttempted) {
        return g_playerAnimations.get();
    }

    CtrRom* rom = AssetRom();
    if (rom == nullptr) {
        return nullptr;
    }
    g_archiveLoadAttempted = true;
    std::vector<uint8_t> bytes = rom->read(kPlayerAnimationArchive);
    if (bytes.empty()) {
        fprintf(stderr, "[MM3D-PLAYER-ANIM] archive not found: %s\n", kPlayerAnimationArchive);
        return nullptr;
    }
    if (LzsIsCompressed(bytes)) {
        std::string error;
        bytes = LzsDecompress(bytes, &error);
        if (bytes.empty()) {
            fprintf(stderr, "[MM3D-PLAYER-ANIM] LzS inflate %s: %s\n", kPlayerAnimationArchive, error.c_str());
            return nullptr;
        }
    }
    auto archive = std::make_unique<Gar>(std::move(bytes));
    if (!archive->ok()) {
        fprintf(stderr, "[MM3D-PLAYER-ANIM] GAR parse %s: %s\n", kPlayerAnimationArchive, archive->error().c_str());
        return nullptr;
    }
    g_playerAnimations = std::move(archive);
    for (const GarFile& file : g_playerAnimations->files()) {
        if (file.type == "csab" || file.path.ends_with(".csab")) {
            g_playerAnimationMembers.insert(file.path);
        }
    }
    return g_playerAnimations.get();
}

} // namespace

void RegisterPlayerAnimationModel(int modelId, PlayerModelForm form) {
    if (modelId < 0) {
        return;
    }
    const auto route = g_playerModels.find(modelId);
    if (route == g_playerModels.end()) {
        g_playerModels.emplace(modelId, PlayerAnimationRoute{ form, {} });
    } else if (route->second.form != form) {
        route->second = PlayerAnimationRoute{ form, {} };
    }
}

bool IsPlayerAnimationModel(int modelId) {
    return g_playerModels.contains(modelId);
}

const char* ResolvePlayerAnimationPath(int modelId, const char* animationOtr) {
    auto route = g_playerModels.find(modelId);
    if (route == g_playerModels.end() || animationOtr == nullptr) {
        return nullptr;
    }
    const std::string resource(animationOtr);
    if (const auto resolved = route->second.resolvedPaths.find(resource);
        resolved != route->second.resolvedPaths.end()) {
        return resolved->second.empty() ? nullptr : resolved->second.c_str();
    }
    if (LoadPlayerAnimationArchive() == nullptr) {
        return nullptr;
    }

    const PlayerAnimationPathCandidates candidates = PlayerAnimationPathsForForm(route->second.form, animationOtr);
    std::string selected;
    for (std::size_t candidateIndex = 0; candidateIndex < candidates.count; ++candidateIndex) {
        const std::string& candidate = candidates.paths[candidateIndex];
        if (g_playerAnimationMembers.contains(candidate)) {
            selected = candidate;
            break;
        }
    }
    const auto inserted = route->second.resolvedPaths.emplace(resource, std::move(selected)).first;
    return inserted->second.empty() ? nullptr : inserted->second.c_str();
}

const Gar* PlayerAnimationArchive(int modelId) {
    return IsPlayerAnimationModel(modelId) ? LoadPlayerAnimationArchive() : nullptr;
}

} // namespace Zelda3D::MM3D
