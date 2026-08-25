#pragma once

#include <sstream>

namespace HarnessSohRuntime {

bool IsBooted();
void Boot();
void AdvanceFrame(const char* watchdogContext);
void HandleBoot(std::istringstream& arguments);
void HandleStep(std::istringstream& arguments);

} // namespace HarnessSohRuntime
