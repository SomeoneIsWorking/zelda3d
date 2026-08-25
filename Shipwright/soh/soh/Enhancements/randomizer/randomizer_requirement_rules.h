#pragma once

#include <cstdint>
#include "soh/Enhancements/randomizer/randomizerTypes.h"

RandomizerCheck GetRandomizerCheckFromFlag(int16_t flagType, int16_t flag);
RandomizerCheck GetRandomizerCheckFromSceneFlag(int16_t sceneNum, int16_t flagType, int16_t flag);
bool MeetsLACSRequirements();
bool CompletedAllTrials();
bool MeetsRainbowBridgeRequirements();
