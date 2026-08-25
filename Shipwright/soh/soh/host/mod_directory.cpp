#include "mod_directory.h"

#include "app_identity.h"

#include <ship/Context.h>

#include <filesystem>
#include <fstream>
#include <string>

void Zelda3D_EnsureModDirectory() {
    try {
        std::string modsPath = Ship::Context::LocateFileAcrossAppDirs("mods", kSohAppShortName);
        if (std::filesystem::exists(modsPath)) {
            return;
        }

        modsPath = Ship::Context::GetPathRelativeToAppDirectory("mods", kSohAppShortName);
        if (std::filesystem::create_directories(modsPath)) {
            std::ofstream(modsPath + "/custom_mod_files_go_here.txt").close();
        }
    } catch (const std::filesystem::filesystem_error&) {
        // Mods are optional; a read-only application directory must not block game startup.
    }
}
