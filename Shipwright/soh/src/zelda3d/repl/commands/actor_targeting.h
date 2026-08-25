// Generic actor selection, visibility, and player targeting commands.
#ifndef ZELDA3D_REPL_COMMANDS_ACTOR_TARGETING_H
#define ZELDA3D_REPL_COMMANDS_ACTOR_TARGETING_H

#include "global.h"

#ifdef __cplusplus
extern "C" {
#endif

extern s32 gZelda3dSelId;
extern Actor* gZelda3dZTargetActor;

#ifdef __cplusplus
}
#endif

bool Zelda3D_ActorTargetingReplCommand(PlayState* play, const char* command, const char* line, const char* outPath);

#endif // ZELDA3D_REPL_COMMANDS_ACTOR_TARGETING_H
