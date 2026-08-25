#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*Zelda3DMmReplReply)(const char* line, void* user);

typedef struct Zelda3DMmReplArgs {
    const char* cursor;
} Zelda3DMmReplArgs;

// Match one exact command token. Prefixes such as "warpfoo" do not match
// "warp". On success, args starts at the first non-whitespace argument.
int Zelda3D_MmReplMatch(const char* command, const char* name, Zelda3DMmReplArgs* args);
int Zelda3D_MmReplArgsEnd(const Zelda3DMmReplArgs* args);
int Zelda3D_MmReplNextToken(Zelda3DMmReplArgs* args, char* output, size_t outputSize);
int Zelda3D_MmReplParseI32(Zelda3DMmReplArgs* args, int base, int32_t* output);
int Zelda3D_MmReplParseFloat(Zelda3DMmReplArgs* args, float* output);

#ifdef __cplusplus
}
#endif
