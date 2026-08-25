#pragma once

#include <sstream>
#include <string>

namespace HarnessFrontend {

void SetupTexPack(const std::string& romPath);
void HandleTexPack(std::istringstream& arguments);
bool TexPackEnabled();
const std::string& TexPackRoot();

} // namespace HarnessFrontend
