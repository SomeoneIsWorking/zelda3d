#pragma once

#include <string>

namespace HarnessFrontend {

// Owns the frontend session's filesystem roots and shutdown state.
void ConfigureDirectories();
const std::string& SystemDirectory();
const std::string& SaveDirectory();

bool QuitRequested();
void RequestQuit();

} // namespace HarnessFrontend
