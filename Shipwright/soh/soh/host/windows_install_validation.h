#pragma once

#include <string>

class WindowsInstallValidation {
  public:
    // Advances one validation phase. Returns true once the install path is usable.
    bool Advance();

  private:
    enum class Phase { TempDirectory, Permissions, OneDrive, Done };

    Phase phase_ = Phase::TempDirectory;
    std::string installPath_;
};
