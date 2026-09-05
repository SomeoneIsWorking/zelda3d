#include "platform/rom_install.h"

#include "platform/rom_archive.h"
#include "platform/rom_validation.h"
#include "platform/rom_paths.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Zelda3D::Platform {
namespace {

std::optional<std::filesystem::path> EnvironmentSelection(const std::string& name) {
#ifdef _WIN32
    const std::wstring wideName(name.begin(), name.end());
    const wchar_t* value = _wgetenv(wideName.c_str());
#else
    const char* value = std::getenv(name.c_str());
#endif
    if (value == nullptr || value[0] == 0) {
        return std::nullopt;
    }
    return std::filesystem::path(value);
}

bool SetEnvironment(const std::string& name, const std::optional<std::filesystem::path>& value) {
#ifdef _WIN32
    const std::wstring wideName(name.begin(), name.end());
    return _wputenv_s(wideName.c_str(), value ? value->c_str() : L"") == 0;
#else
    return value ? setenv(name.c_str(), value->c_str(), 1) == 0 : unsetenv(name.c_str()) == 0;
#endif
}

bool ReplaceFile(const std::filesystem::path& source, const std::filesystem::path& destination) {
#ifdef _WIN32
    return MoveFileExW(source.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(source.c_str(), destination.c_str()) == 0;
#endif
}

bool LoadConfig(const std::filesystem::path& path, nlohmann::json& config, std::string& error) {
    std::ifstream stream(path);
    if (!stream) {
        std::error_code filesystemError;
        if (std::filesystem::exists(path, filesystemError) || filesystemError) {
            error = "The existing ROM selection file could not be read.";
            return false;
        }
        config = nlohmann::json::object();
        return true;
    }
    try {
        stream >> config;
        if (!config.is_object()) {
            error = "The existing ROM selection file does not contain a JSON object.";
            return false;
        }
        return true;
    } catch (const nlohmann::json::exception&) {
        error = "The existing ROM selection file contains invalid JSON.";
        return false;
    }
}

bool IsManagedCachePath(const std::filesystem::path& path, const std::filesystem::path& dataRoot) {
    std::error_code error;
    const std::filesystem::path cacheRoot = std::filesystem::weakly_canonical(dataRoot / "roms", error);
    if (error) {
        return false;
    }
    const std::filesystem::path candidate = std::filesystem::weakly_canonical(path, error);
    if (error) {
        return false;
    }
    const std::filesystem::path relative = candidate.lexically_relative(cacheRoot);
    return !relative.empty() && relative != "." && *relative.begin() != "..";
}

} // namespace

RomSelectionStore::RomSelectionStore(std::filesystem::path dataRoot)
    : mDataRoot(std::move(dataRoot)), mConfigPath(mDataRoot / "rom-selection.json") {}

std::optional<std::filesystem::path> RomSelectionStore::ActiveSelection(RomKind kind) {
    return EnvironmentSelection(std::string(GetRomSpec(kind).environmentVariable));
}

std::optional<std::filesystem::path> RomSelectionStore::ConfiguredSelection(RomKind kind) const {
    const RomSpec& spec = GetRomSpec(kind);
    if (const auto environment = ActiveSelection(kind)) {
        return environment;
    }
    nlohmann::json config;
    std::string error;
    if (!LoadConfig(mConfigPath, config, error)) {
        return std::nullopt;
    }
    const auto value = config.find(std::string(spec.id));
    if (value == config.end() || !value->is_object()) {
        return std::nullopt;
    }
    const auto path = value->find("path");
    if (path == value->end() || !path->is_string()) {
        return std::nullopt;
    }
    return RomPathFromUtf8(path->get<std::string>());
}

std::vector<RomKind> RomSelectionStore::ActivateConfiguredSelections() {
    std::vector<RomKind> missing;
    for (const RomSpec& spec : RomSpecs()) {
        const auto path = ConfiguredSelection(spec.kind);
        std::string validationError;
        if (!path.has_value() || !ValidateRomSelection(spec.kind, *path, validationError) ||
            !SetEnvironment(std::string(spec.environmentVariable), *path)) {
            missing.push_back(spec.kind);
        }
    }
    return missing;
}

RomImportResult RomSelectionStore::ImportSelection(RomKind kind, const std::filesystem::path& source) {
    RomImportResult result;
    bool managed = false;
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(source, error);
    if (error) {
        return { false, {}, "The selected file path could not be resolved." };
    }
    const auto identified = IdentifyRomFile(absolute);
    if (identified == kind) {
        if (!ValidateRomSelection(kind, absolute, result.error)) {
            return result;
        }
        result = { true, absolute, {} };
    } else if (identified.has_value()) {
        return { false, {}, "The selected file is a supported ROM, but not the requested game." };
    } else {
        result = ImportRomArchive(kind, absolute, mDataRoot);
        managed = result.accepted;
    }
    if (!result.accepted) {
        return result;
    }

    std::string persistError;
    if (Persist(kind, result.installedPath, managed, persistError)) {
        return result;
    }
    if (managed) {
        std::filesystem::remove(result.installedPath, error);
    }
    return { false, {}, persistError };
}

bool RomSelectionStore::Persist(RomKind kind, const std::filesystem::path& path, bool managed, std::string& error) {
    std::error_code filesystemError;
    std::filesystem::create_directories(mDataRoot, filesystemError);
    if (filesystemError) {
        error = "The setup directory could not be created: " + filesystemError.message();
        return false;
    }
    nlohmann::json config;
    if (!LoadConfig(mConfigPath, config, error)) {
        return false;
    }
    const std::string key(GetRomSpec(kind).id);
    std::optional<std::filesystem::path> previousPath;
    bool previousManaged = false;
    if (const auto value = config.find(key); value != config.end() && value->is_object()) {
        const auto savedPath = value->find("path");
        const auto savedManaged = value->find("managed");
        if (savedPath != value->end() && savedPath->is_string()) {
            previousPath = RomPathFromUtf8(savedPath->get<std::string>());
        }
        previousManaged = savedManaged != value->end() && savedManaged->is_boolean() && savedManaged->get<bool>();
    }
    std::string serialized;
    try {
        config[key] = { { "path", RomPathToUtf8(path) }, { "managed", managed } };
        serialized = config.dump(2);
    } catch (const nlohmann::json::exception& exception) {
        error = "ROM selection is not valid UTF-8: " + std::string(exception.what());
        return false;
    } catch (const std::filesystem::filesystem_error& exception) {
        error = "ROM selection cannot be encoded losslessly: " + std::string(exception.what());
        return false;
    }
    std::filesystem::path staging = mConfigPath;
    staging += ".part";
    {
        std::ofstream output(staging, std::ios::trunc);
        output << serialized << '\n';
        // A buffered write may fail only when close flushes it.
        output.close();
        if (!output) {
            std::filesystem::remove(staging, filesystemError);
            error = "The ROM selection could not be written to user storage.";
            return false;
        }
    }
    // All potentially failing encoding and buffered writes precede process activation.
    const std::string name(GetRomSpec(kind).environmentVariable);
    const auto previousEnvironment = EnvironmentSelection(name);
    if (!SetEnvironment(name, path)) {
        std::filesystem::remove(staging, filesystemError);
        error = "The validated ROM could not be activated for this process.";
        return false;
    }
    if (!ReplaceFile(staging, mConfigPath)) {
        if (!SetEnvironment(name, previousEnvironment)) {
            throw std::runtime_error("ROM selection failed and its previous environment could not be restored.");
        }
        std::filesystem::remove(staging, filesystemError);
        error = "The ROM selection could not be committed to user storage.";
        return false;
    }
    if (previousManaged && previousPath.has_value() && *previousPath != path &&
        IsManagedCachePath(*previousPath, mDataRoot)) {
        std::filesystem::remove(*previousPath, filesystemError);
    }
    return true;
}

const std::filesystem::path& RomSelectionStore::DataRoot() const {
    return mDataRoot;
}

} // namespace Zelda3D::Platform
