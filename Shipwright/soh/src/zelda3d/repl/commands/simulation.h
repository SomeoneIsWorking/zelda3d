// Deterministic gameplay-frame control for reproducible REPL captures and diagnostics.
#ifndef ZELDA3D_REPL_COMMANDS_SIMULATION_H
#define ZELDA3D_REPL_COMMANDS_SIMULATION_H

#include "global.h"

bool Zelda3D_SimulationReplCommand(PlayState* play, const char* command, const char* line, const char* outPath);

#endif // ZELDA3D_REPL_COMMANDS_SIMULATION_H
