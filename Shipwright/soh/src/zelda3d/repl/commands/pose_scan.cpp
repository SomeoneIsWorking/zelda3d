#include "pose_scan.h"

#include "../../player/player_pose_scan.h"
#include "../zelda3d_repl.h"

#include <stdio.h>
#include <string.h>

bool Zelda3D_PoseScanReplCommand(const char* command, const char* line, const char* outPath) {
    if (strcmp(command, "posescan") != 0) {
        return false;
    }

    // Samples drawn player frames so animation-transition spikes are observed at the shipping seam.
    char action[16] = { 0 };
    sscanf(line, "%*s %15s", action);
    if (strcmp(action, "on") == 0) {
        Zelda3D_PoseScanSetActive(1);
        Zelda3D_ReplReply(outPath, "posescan on (recording; modelId=%d)", Zelda3D_LinkModelId());
    } else if (strcmp(action, "off") == 0) {
        int count = Zelda3D_PoseScanCount();
        Zelda3D_PoseScanSetActive(0);
        Zelda3D_ReplReply(outPath, "posescan off (n=%d frames recorded)", count);
    } else if (strcmp(action, "dump") == 0) {
        int count = Zelda3D_PoseScanCount();
        char report[8192];
        int length = snprintf(report, sizeof(report), "posescan n=%d\n", count);
        for (int i = 0; i < count && length < static_cast<int>(sizeof(report)) - 64; i++) {
            int bone;
            float frame;
            const char* csab;
            float degrees = Zelda3D_PoseScanGet(i, &bone, &frame, &csab);
            length += snprintf(report + length, sizeof(report) - length, "%d,%.1f,%d,%.1f,%s\n", i, degrees, bone,
                               frame, csab);
        }
        Zelda3D_ReplReply(outPath, "%s", report);
    } else {
        Zelda3D_ReplReply(outPath, "usage: posescan <on|off|dump> (n=%d)", Zelda3D_PoseScanCount());
    }
    return true;
}
