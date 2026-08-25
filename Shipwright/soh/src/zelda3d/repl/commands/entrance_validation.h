#ifndef ZELDA3D_REPL_COMMANDS_ENTRANCE_VALIDATION_H
#define ZELDA3D_REPL_COMMANDS_ENTRANCE_VALIDATION_H

bool Zelda3D_EntranceIndexIsValid(int entrance);
bool Zelda3D_ValidateEntrance(const char* command, int entrance, const char* outPath);

#endif
