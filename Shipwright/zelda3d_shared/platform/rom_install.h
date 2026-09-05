#pragma once

#include "platform/rom_identity.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Zelda3D::Platform {

struct RomImportResult {
    bool accepted = false;
    std::filesystem::path installedPath;
    std::string error;
};

class RomSelectionStore {
  public:
    explicit RomSelectionStore(std::filesystem::path dataRoot);

    static std::optional<std::filesystem::path> ActiveSelection(RomKind kind);
    std::optional<std::filesystem::path> ConfiguredSelection(RomKind kind) const;
    std::vector<RomKind> ActivateConfiguredSelections();
    RomImportResult ImportSelection(RomKind kind, const std::filesystem::path& source);
    const std::filesystem::path& DataRoot() const;

  private:
    bool Persist(RomKind kind, const std::filesystem::path& path, bool managed, std::string& error);

    std::filesystem::path mDataRoot;
    std::filesystem::path mConfigPath;
};

} // namespace Zelda3D::Platform
