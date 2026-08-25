// Zelda3D REPL command dispatcher. FIFO lifecycle and command implementations live in their
// cohesive owners; this translation unit only parses a line and composes those handlers.
#include "../player/zelda3d_link.h"
#include "commands/actor_behavior_diagnostics.h"
#include "commands/actor_camera.h"
#include "commands/actor_control.h"
#include "commands/actor_diagnostics.h"
#include "commands/actor_scan_diagnostics.h"
#include "commands/animation_control.h"
#include "commands/archive_diagnostics.h"
#include "commands/camera_control.h"
#include "commands/collision_probe.h"
#include "commands/cutscene_title.h"
#include "commands/help.h"
#include "commands/hud_atlas.h"
#include "commands/input_injection.h"
#include "commands/instrumentation.h"
#include "commands/item_equipment.h"
#include "commands/menu_launcher.h"
#include "commands/model_control.h"
#include "commands/player_control.h"
#include "commands/process_control.h"
#include "commands/render_environment.h"
#include "commands/render_isolation.h"
#include "commands/room_environment.h"
#include "commands/save_player_state.h"
#include "commands/scene_transitions.h"
#include "commands/simulation.h"
#include "commands/time_control.h"
#include "zelda3d_repl.h"

#include <stdio.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

void Zelda3D_ReplDispatchCommand(PlayState* play, char* line, const char* outPath) {
    char command[32];
    while (*line == ' ' || *line == '\t' || *line == '\r') {
        line++;
    }
    if (*line == '\0' || *line == '#') {
        return;
    }
    if (sscanf(line, "%31s", command) != 1) {
        return;
    }

    if (play == NULL) {
        // These owners operate on process, launcher, input, or transport state and deliberately
        // remain usable before a PlayState exists.
        static const char* kPlayFreeCommands[] = {
            "key",       "log",  "fps",      "dump",    "inputdev", "keycap",     "menu",
            "menuclick", "help", "launcher", "menuhit", "menurow",  "switchgame",
        };
        bool playFree = false;
        for (const char* candidate : kPlayFreeCommands) {
            if (strcmp(command, candidate) == 0) {
                playFree = true;
                break;
            }
        }
        if (!playFree) {
            Zelda3D_ReplReply(outPath, "%s: no playstate (non-Play gamestate; play-gated command)", command);
            return;
        }
    }

    if (Zelda3D_HelpReplCommand(command, outPath)) {
    } else if (Zelda3D_LinkRepl(play, command, line, outPath)) {
    } else if (Zelda3D_MenuLauncherReplCommand(command, line, outPath)) {
    } else if (Zelda3D_SceneTransitionsReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_SavePlayerStateReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_RoomEnvironmentReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_TimeControlReplCommand(command, line, outPath)) {
    } else if (Zelda3D_PlayerControlReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_SimulationReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_InstrumentationReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_ActorDiagnosticsReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_ActorScanDiagnosticsReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_ActorCameraReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_ArchiveDiagnosticsReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_CollisionProbeReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_ModelControlReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_AnimationControlReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_ItemEquipmentReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_RenderEnvironmentReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_HudAtlasReplCommand(command, line, outPath)) {
    } else if (Zelda3D_InputInjectionReplCommand(command, line, outPath)) {
    } else if (Zelda3D_CameraControlReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_CutsceneTitleReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_ActorControlReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_ActorBehaviorDiagnosticsReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_RenderIsolationReplCommand(play, command, line, outPath)) {
    } else if (Zelda3D_ProcessControlReplCommand(play, command, line, outPath)) {
    } else {
        Zelda3D_ReplReply(outPath, "? '%s' (run `help` for the command catalog)", line);
    }
}

#ifdef __cplusplus
} // extern "C"
#endif
