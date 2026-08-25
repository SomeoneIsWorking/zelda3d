#pragma once

// C-compatible framing shared by every Zelda3D FIFO command endpoint.

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZELDA3D_FIFO_RPC_ID_LENGTH 16

static inline int Zelda3DFifoRpc_IsLowerHex(char value) {
    return ((value >= '0') && (value <= '9')) || ((value >= 'a') && (value <= 'f'));
}

// Returns 1 for a valid tagged request, 0 for a legacy request, and -1 for a
// malformed line beginning with '@'. `command` always points into `line`.
static inline int Zelda3DFifoRpc_ParseRequest(const char* line, char id[ZELDA3D_FIFO_RPC_ID_LENGTH + 1],
                                              const char** command) {
    size_t index;
    id[0] = '\0';
    *command = line;
    if (line[0] != '@') {
        return 0;
    }
    for (index = 0; index < ZELDA3D_FIFO_RPC_ID_LENGTH; index++) {
        char value = line[index + 1];
        if (!Zelda3DFifoRpc_IsLowerHex(value)) {
            id[0] = '\0';
            return -1;
        }
        id[index] = value;
    }
    if (line[ZELDA3D_FIFO_RPC_ID_LENGTH + 1] != ' ') {
        id[0] = '\0';
        return -1;
    }
    id[ZELDA3D_FIFO_RPC_ID_LENGTH] = '\0';
    *command = line + ZELDA3D_FIFO_RPC_ID_LENGTH + 2;
    return 1;
}

static inline int Zelda3DFifoRpc_FormatData(char* output, size_t outputSize, const char* id, const char* payload) {
    return snprintf(output, outputSize, "@%s D %s", id, payload);
}

static inline int Zelda3DFifoRpc_FormatEnd(char* output, size_t outputSize, const char* id, size_t lineCount) {
    return snprintf(output, outputSize, "@%s E %zu", id, lineCount);
}

#ifdef __cplusplus
}
#endif
