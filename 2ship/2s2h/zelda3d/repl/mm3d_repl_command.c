#include "2s2h/zelda3d/repl/mm3d_repl_command.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char* Zelda3D_MmReplSkipWhitespace(const char* cursor) {
    while ((*cursor != '\0') && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    return cursor;
}

int Zelda3D_MmReplMatch(const char* command, const char* name, Zelda3DMmReplArgs* args) {
    if ((command == NULL) || (name == NULL) || (args == NULL)) {
        return 0;
    }
    command = Zelda3D_MmReplSkipWhitespace(command);
    size_t nameLength = strlen(name);
    if ((strncmp(command, name, nameLength) != 0) ||
        ((command[nameLength] != '\0') && !isspace((unsigned char)command[nameLength]))) {
        return 0;
    }
    args->cursor = Zelda3D_MmReplSkipWhitespace(command + nameLength);
    return 1;
}

int Zelda3D_MmReplArgsEnd(const Zelda3DMmReplArgs* args) {
    return (args == NULL) || (args->cursor == NULL) || (*Zelda3D_MmReplSkipWhitespace(args->cursor) == '\0');
}

int Zelda3D_MmReplNextToken(Zelda3DMmReplArgs* args, char* output, size_t outputSize) {
    if ((args == NULL) || (args->cursor == NULL) || (output == NULL) || (outputSize == 0)) {
        return 0;
    }
    const char* start = Zelda3D_MmReplSkipWhitespace(args->cursor);
    const char* end = start;
    while ((*end != '\0') && !isspace((unsigned char)*end)) {
        end++;
    }
    size_t length = (size_t)(end - start);
    if ((length == 0) || (length >= outputSize)) {
        return 0;
    }
    memcpy(output, start, length);
    output[length] = '\0';
    args->cursor = Zelda3D_MmReplSkipWhitespace(end);
    return 1;
}

int Zelda3D_MmReplParseI32(Zelda3DMmReplArgs* args, int base, int32_t* output) {
    if ((args == NULL) || (args->cursor == NULL) || (output == NULL)) {
        return 0;
    }
    const char* start = Zelda3D_MmReplSkipWhitespace(args->cursor);
    if (*start == '\0') {
        return 0;
    }
    errno = 0;
    char* end = NULL;
    long value = strtol(start, &end, base);
    if ((end == start) || (errno == ERANGE) || (value < INT32_MIN) || (value > INT32_MAX) ||
        ((*end != '\0') && !isspace((unsigned char)*end))) {
        return 0;
    }
    *output = (int32_t)value;
    args->cursor = Zelda3D_MmReplSkipWhitespace(end);
    return 1;
}

int Zelda3D_MmReplParseFloat(Zelda3DMmReplArgs* args, float* output) {
    if ((args == NULL) || (args->cursor == NULL) || (output == NULL)) {
        return 0;
    }
    const char* start = Zelda3D_MmReplSkipWhitespace(args->cursor);
    if (*start == '\0') {
        return 0;
    }
    errno = 0;
    char* end = NULL;
    float value = strtof(start, &end);
    if ((end == start) || (errno == ERANGE) || !isfinite(value) || ((*end != '\0') && !isspace((unsigned char)*end))) {
        return 0;
    }
    *output = value;
    args->cursor = Zelda3D_MmReplSkipWhitespace(end);
    return 1;
}
