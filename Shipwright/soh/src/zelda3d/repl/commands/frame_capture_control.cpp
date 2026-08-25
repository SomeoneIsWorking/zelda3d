#include "frame_capture_control.h"

#include "../zelda3d_repl.h"

#include <fast/frame_capture.h>

#include <cstdio>
#include <cstring>

bool Zelda3D_FrameCaptureControlReplCommand(const char* command, const char* line, const char* outPath) {
    if (std::strcmp(command, "dump") != 0) {
        return false;
    }

    char path[1024];
    if (std::sscanf(line, "%*s %1023s", path) != 1) {
        return true;
    }
    std::strncpy(gSoh3dDumpPath, path, sizeof(gSoh3dDumpPath) - 1);
    gSoh3dDumpPath[sizeof(gSoh3dDumpPath) - 1] = '\0';
    gSoh3dDumpPending = 1;
    Zelda3D_ReplReply(outPath, "dump -> %s (pending)", gSoh3dDumpPath);
    return true;
}
