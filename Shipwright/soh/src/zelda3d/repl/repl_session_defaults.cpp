#include "repl_session_defaults.h"

#include "soh/cvar_prefixes.h"
#include "../core/zelda3d_log.h"
#include <libultraship/bridge/consolevariablebridge.h>

#include <cstdlib>

namespace {

struct SessionDefaultsState {
    bool dropsConfigured = false;
    bool chordsConfigured = false;
    bool fpsConfigured = false;
};

SessionDefaultsState sDefaults;

int DefaultEnabledUnlessDisabled(const char* variable) {
    const char* value = std::getenv(variable);
    return value != nullptr && value[0] == '1' ? 0 : 1;
}

void ApplyDropDefault() {
    if (sDefaults.dropsConfigured) {
        return;
    }
    sDefaults.dropsConfigured = true;
    const int enabled = DefaultEnabledUnlessDisabled("ZELDA3D_NO3DDROPS");
    CVarSetInteger(CVAR_ENHANCEMENT("NewDrops"), enabled);
    Z3D_LOG(SOH_REPL, "NewDrops -> %d\n", CVarGetInteger(CVAR_ENHANCEMENT("NewDrops"), -1));
}

void ApplyChordDefault() {
    if (sDefaults.chordsConfigured) {
        return;
    }
    sDefaults.chordsConfigured = true;
    const int enabled = DefaultEnabledUnlessDisabled("ZELDA3D_NOCHORDS");
    CVarSetInteger("gControllerChords", enabled);
    CVarSetInteger(CVAR_ENHANCEMENT("DpadEquips"), enabled);
    Z3D_LOG(SOH_REPL, "chords -> %d, DpadEquips -> %d\n", enabled, CVarGetInteger(CVAR_ENHANCEMENT("DpadEquips"), -1));
}

void ApplyPresentationFpsDefault() {
    if (sDefaults.fpsConfigured) {
        return;
    }
    sDefaults.fpsConfigured = true;
    const int target = DefaultEnabledUnlessDisabled("ZELDA3D_NO60FPS") ? 60 : 20;
    CVarSetInteger(CVAR_SETTING("InterpolationFPS"), target);
    Z3D_LOG(SOH_REPL, "InterpolationFPS -> %d\n", CVarGetInteger(CVAR_SETTING("InterpolationFPS"), -1));
}

} // namespace

namespace Zelda3D::Repl {

void ApplySessionDefaults() {
    ApplyDropDefault();
    ApplyChordDefault();
    ApplyPresentationFpsDefault();
}

void ResetSessionDefaults() {
    sDefaults = {};
}

} // namespace Zelda3D::Repl
