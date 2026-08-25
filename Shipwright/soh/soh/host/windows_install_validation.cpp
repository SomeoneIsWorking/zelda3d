#include "windows_install_validation.h"

#include "soh/SohGui/SohGui.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

#ifdef _WIN32
#include <Windows.h>
#endif

namespace {

bool IsSubpath(const std::filesystem::path& path, const std::filesystem::path& base) {
    const auto relative = std::filesystem::relative(path, base);
    return !relative.empty() && relative.native()[0] != '.';
}

bool CleanupPermissionProbe() {
    try {
        if (std::filesystem::exists("./text.txt")) {
            std::filesystem::remove("./text.txt");
        }
        if (std::filesystem::exists("./test/")) {
            std::filesystem::remove("./test/");
        }
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
    return true;
}

} // namespace

bool WindowsInstallValidation::Advance() {
#ifndef _WIN32
    return true;
#else
    switch (phase_) {
        case Phase::TempDirectory: {
            std::filesystem::path tempPath;
            try {
                tempPath = std::filesystem::canonical(std::getenv("TEMP"));
            } catch (const std::filesystem::filesystem_error&) {
                std::string userPath = std::getenv("USERPROFILE");
                tempPath = std::filesystem::canonical(userPath + "\\AppData\\Local\\Temp");
            }

            wchar_t buffer[MAX_PATH];
            GetModuleFileName(nullptr, buffer, _countof(buffer));
            const std::filesystem::path ownPath = std::filesystem::canonical(buffer).parent_path();
            installPath_ = ownPath.string();
            if (IsSubpath(ownPath, tempPath)) {
                SohGui::RegisterPopup("SoH Path Error", "SoH is running in a temp folder.\nExtract the .zip and run again.",
                                      "OK", "", []() { std::exit(0); });
            } else {
                phase_ = Phase::Permissions;
            }
            return false;
        }
        case Phase::Permissions: {
            FILE* file = std::fopen("./text.txt", "w");
            bool directoryError = false;
            try {
                std::filesystem::create_directories("./test/");
            } catch (const std::filesystem::filesystem_error&) {
                directoryError = true;
            }

            if (file == nullptr || directoryError) {
                SohGui::RegisterPopup("SoH Permissions Error",
                                      "SoH does not have proper file permissions.\nPlease move it to a folder that "
                                      "does and run again.",
                                      "OK", "", [file]() {
                                          if (file != nullptr) {
                                              std::fclose(file);
                                          }
                                          CleanupPermissionProbe();
                                          std::exit(0);
                                      });
                return false;
            }

            std::fclose(file);
            if (!CleanupPermissionProbe()) {
                SohGui::RegisterPopup("SoH Permissions Error",
                                      "SoH does not have proper file permissions.\nPlease move it to a folder that "
                                      "does and run again.",
                                      "OK", "", []() { std::exit(0); });
                return false;
            }
            phase_ = Phase::OneDrive;
            return false;
        }
        case Phase::OneDrive:
            if (installPath_.find("OneDrive") != std::string::npos) {
                SohGui::RegisterPopup("SoH Path Error",
                                      "SoH appears to be in a OneDrive folder, which will cause issues.\nPlease move "
                                      "it to a folder outside of OneDrive, like the root of a\ndrive (e.g. "
                                      "\"C:\\Games\\SoH\").",
                                      "OK", "", []() { std::exit(0); });
                return false;
            }
            phase_ = Phase::Done;
            return true;
        case Phase::Done:
            return true;
    }
    return false;
#endif
}
