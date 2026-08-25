#pragma once

#include <sstream>
#include <string>

namespace HarnessOracleStorage {
bool LoadStateFile(const std::string& path);
bool HandleLoad(std::istringstream& arguments);
void HandleSave(std::istringstream& arguments);
} // namespace HarnessOracleStorage
