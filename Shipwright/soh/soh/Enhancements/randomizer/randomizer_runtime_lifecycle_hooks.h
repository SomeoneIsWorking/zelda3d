#pragma once

#include <cstdint>

void RandomizerOnGameFrameUpdateHandler();
void RandomizerOnActorUpdateHandler(void* actorRef);
void RandomizerOnPlayerUpdateHandler();
void RandomizerOnSceneSpawnActorsHandler();
void RandomizerOnPlayDestroyHandler();
void RandomizerOnExitGameHandler(int32_t fileNum);
void RandomizerOnKaleidoscopeUpdateHandler(int16_t inDungeonScene);
void RandomizerOnCuccoOrChickenHatch();
