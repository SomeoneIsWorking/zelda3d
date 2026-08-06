set(CVAR_PREFIX_RANDOMIZER_ENHANCEMENT "gRandoEnhancements")
set(CVAR_PREFIX_RANDOMIZER_SETTING "gRandoSettings")
set(CVAR_PREFIX_COSMETIC "gCosmetics")
set(CVAR_PREFIX_AUDIO "gAudioEditor")
set(CVAR_PREFIX_CHEAT "gCheats")
set(CVAR_PREFIX_ENHANCEMENT "gEnhancements")
set(CVAR_PREFIX_SETTING "gSettings")
set(CVAR_PREFIX_WINDOW "gOpenWindows")
set(CVAR_PREFIX_TRACKER "gTrackers")
set(CVAR_PREFIX_DEVELOPER_TOOLS "gDeveloperTools")
set(CVAR_PREFIX_GENERAL "gGeneral")
set(CVAR_PREFIX_REMOTE "gRemote")
set(CVAR_PREFIX_GAMEPLAY_STATS "gGameplayStats")
set(CVAR_PREFIX_TIME_DISPLAY "gTimeDisplay")
# These are OCARINA OF TIME's CVar namespaces, so they are handed to OoT's target rather than
# add_compile_definition'd at this (root) scope. They used to be global, which put -DCVAR_PREFIX_*
# on every target in the tree -- including MM, whose own namespaces are different. MM's
# `#define CVAR_PREFIX_COSMETIC "gCosmetic"` (BenGui/CosmeticEditor.h) then REDEFINED OoT's
# "gCosmetics" and the compiler said so on every TU that included it. Applied to soh_settings in
# Shipwright/soh/CMakeLists.txt; see claim C072.
#
# The `set()`s above stay at this scope on purpose: lus-cvars.cmake builds the ENGINE's key names
# out of ${CVAR_PREFIX_SETTING} and ${CVAR_PREFIX_WINDOW} (gSettings.VsyncEnabled,
# gOpenWindows.Console, ...), and those keys are shared by both games. It is the C++ MACROS that are
# per-game, not the strings.
set(SOH_CVAR_PREFIX_DEFINITIONS
	CVAR_PREFIX_RANDOMIZER_ENHANCEMENT="${CVAR_PREFIX_RANDOMIZER_ENHANCEMENT}"
	CVAR_PREFIX_RANDOMIZER_SETTING="${CVAR_PREFIX_RANDOMIZER_SETTING}"
	CVAR_PREFIX_COSMETIC="${CVAR_PREFIX_COSMETIC}"
	CVAR_PREFIX_AUDIO="${CVAR_PREFIX_AUDIO}"
	CVAR_PREFIX_CHEAT="${CVAR_PREFIX_CHEAT}"
	CVAR_PREFIX_ENHANCEMENT="${CVAR_PREFIX_ENHANCEMENT}"
	CVAR_PREFIX_SETTING="${CVAR_PREFIX_SETTING}"
	CVAR_PREFIX_WINDOW="${CVAR_PREFIX_WINDOW}"
	CVAR_PREFIX_TRACKER="${CVAR_PREFIX_TRACKER}"
	CVAR_PREFIX_DEVELOPER_TOOLS="${CVAR_PREFIX_DEVELOPER_TOOLS}"
	CVAR_PREFIX_GENERAL="${CVAR_PREFIX_GENERAL}"
	CVAR_PREFIX_REMOTE="${CVAR_PREFIX_REMOTE}"
	CVAR_PREFIX_GAMEPLAY_STATS="${CVAR_PREFIX_GAMEPLAY_STATS}"
	CVAR_PREFIX_TIME_DISPLAY="${CVAR_PREFIX_TIME_DISPLAY}"
)