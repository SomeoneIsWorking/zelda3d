#include "repl_runtime.h"

#include "repl_camera_overrides.h"
#include "repl_fps.h"
#include "repl_game_camera.h"
#include "repl_menu_sync.h"
#include "repl_screen_diagnostics.h"
#include "repl_session_defaults.h"
#include "repl_time_override.h"
#include "repl_transport.h"

extern "C" {

int gZelda3dReplPolledThisFrame = 0;

void Zelda3D_ReplResetRunState(void) {
    Zelda3D::Repl::ResetTransport();
    Zelda3D::Repl::ResetFps();
    Zelda3D::Repl::ResetSessionDefaults();
    Zelda3D::Repl::ResetMenuState();
    gZelda3dReplPolledThisFrame = 0;
}

void Zelda3D_ReplPoll(PlayState* play) {
    if (play != nullptr) {
        gZelda3dReplPolledThisFrame = 1;
    }
    Zelda3D::Repl::TickFps();
    Zelda3D::Repl::ApplyTimeOverride();
    Zelda3D::Repl::ApplyGameCamera(play);
    Zelda3D::Repl::UpdateScreenDiagnostics(play);
    Zelda3D::Repl::ApplySessionDefaults();
    Zelda3D::Repl::ApplyMenuState(play);
    Zelda3D::Repl::PollTransport(play);
    Zelda3D::Repl::ApplyCameraOverrides(play);
}

void Zelda3D_ReplPollNoPlay(void) {
    if (gZelda3dReplPolledThisFrame) {
        gZelda3dReplPolledThisFrame = 0;
        return;
    }
    Zelda3D_ReplPoll(nullptr);
}

} // extern "C"
