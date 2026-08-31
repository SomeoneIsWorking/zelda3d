#pragma once

#include <sstream>

namespace HarnessMemory {

void HandleRead(std::istringstream& arguments, int bitWidth);
void HandleWrite(std::istringstream& arguments, int bitWidth);
void HandleMem(std::istringstream& arguments);
void HandleWriteBlockSelfTest(std::istringstream& arguments);

} // namespace HarnessMemory
