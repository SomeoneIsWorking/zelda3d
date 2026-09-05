#include "platform/rom_setup.h"

#include "platform/rom_install.h"

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace Zelda3D::Platform {
namespace {

constexpr int kQuitButton = 0;
constexpr int kBrowseButton = 1;

struct DialogResult {
    std::mutex mutex;
    bool finished = false;
    bool failed = false;
    std::optional<std::filesystem::path> selection;
};

void SDLCALL FileDialogCallback(void* userdata, const char* const* fileList, int) {
    auto& result = *static_cast<DialogResult*>(userdata);
    std::lock_guard lock(result.mutex);
    result.failed = fileList == nullptr;
    if (fileList != nullptr && fileList[0] != nullptr) {
        result.selection = std::filesystem::path(fileList[0]);
    }
    result.finished = true;
}

std::optional<std::filesystem::path> BrowseForRom(bool& failed) {
    static constexpr SDL_DialogFileFilter kFilters[] = {
        { "Supported ROM or ZIP", "z64;n64;v64;3ds;zip" },
        { "All files", "*" },
    };
    DialogResult result;
    SDL_ShowOpenFileDialog(FileDialogCallback, &result, nullptr, kFilters, std::size(kFilters), nullptr, false);
    for (;;) {
        {
            std::lock_guard lock(result.mutex);
            if (result.finished) {
                failed = result.failed;
                return result.selection;
            }
        }
        SDL_PumpEvents();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int ShowRequirementPrompt(const RomSpec& spec) {
    const SDL_MessageBoxButtonData buttons[] = {
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, kQuitButton, "Quit" },
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, kBrowseButton, "Browse..." },
    };
    const std::string message =
        "Zelda3D needs your legally obtained " + std::string(spec.label) +
        " ROM.\n\nSelect the ROM directly, or select a ZIP containing exactly one matching ROM at any folder depth. "
        "No game files are included in this port.";
    const SDL_MessageBoxData data = {
        SDL_MESSAGEBOX_INFORMATION,
        nullptr,
        "Zelda3D — Initial Setup",
        message.c_str(),
        static_cast<int>(std::size(buttons)),
        buttons,
        nullptr,
    };
    int button = kQuitButton;
    if (!SDL_ShowMessageBox(&data, &button)) {
        return kQuitButton;
    }
    return button;
}

void ShowError(const std::string& message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Zelda3D — Setup could not use that file", message.c_str(), nullptr);
}

} // namespace

bool EnsureRomSetup(const std::filesystem::path& dataRoot) {
    if (dataRoot.empty()) {
        SPDLOG_ERROR("Zelda3D setup cannot resolve the OS user configuration directory");
        return false;
    }

    RomSelectionStore store(dataRoot);
    const std::vector<RomKind> missing = store.ActivateConfiguredSelections();
    if (missing.empty()) {
        return true;
    }

    const bool initializedVideoHere = SDL_WasInit(SDL_INIT_VIDEO) == 0;
    if (initializedVideoHere && !SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        SPDLOG_ERROR("Zelda3D setup cannot initialize its UI: {}", SDL_GetError());
        return false;
    }

    bool ready = true;
    for (const RomKind kind : missing) {
        const RomSpec& spec = GetRomSpec(kind);
        for (;;) {
            if (ShowRequirementPrompt(spec) != kBrowseButton) {
                ready = false;
                break;
            }
            bool dialogFailed = false;
            const auto selection = BrowseForRom(dialogFailed);
            if (dialogFailed) {
                ShowError(std::string("The file picker failed: ") + SDL_GetError());
                continue;
            }
            if (!selection.has_value()) {
                continue;
            }
            const RomImportResult imported = store.ImportSelection(kind, *selection);
            if (imported.accepted) {
                break;
            }
            ShowError(imported.error);
        }
        if (!ready) {
            break;
        }
    }

    if (initializedVideoHere) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
    return ready;
}

} // namespace Zelda3D::Platform
