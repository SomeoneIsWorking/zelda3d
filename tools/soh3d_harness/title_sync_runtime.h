#pragma once

#include <cstdint>

namespace HarnessTitleSyncRuntime {

enum class ArmResult { Ready, Failed };

void MarkManualStateTouch();
ArmResult EnsureArmed();
bool IsActive();
void AdvanceAfterSohFrame();
const char* StatusTag();
void PrintStatus();
bool ReadOracleVblankCounter(uint32_t* output);

} // namespace HarnessTitleSyncRuntime
