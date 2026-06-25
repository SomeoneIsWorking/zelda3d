// SoH3D behavior: Link (the PLAYER) — OoT3D body replacement, as a structured ActorBehavior class.
//
// Unlike the NPC/prop behaviors, the player's draw is invoked from Player_Draw via the DEDICATED
// SoH3D_TryDrawPlayer hook (z_player.c), NOT the generic SoH3D_TryActorModelDraw registry — the
// player's draw path is special (it runs inside the player overlay, not the generic actor draw loop).
// So PlayerBehavior is reached through that hook: the extern "C" shims at the bottom of player.cpp
// (formerly the free SoH3D_TryDrawPlayer / SoH3D_Link* functions) delegate to the singleton. It is
// intentionally NOT added to findActorBehavior's generic switch, to avoid a double draw.
//
// The class owns Link's full draw policy: the equipment mesh-id mask, the per-bone retarget
// correction table + hand-weave state, the pose-freeze, linkpin, the cucco-grab repro driver, and all
// the `link*` REPL commands. The low-level CMB skeleton FK retarget primitives (SoH3D_UpdateAnimN64*,
// SoH3D_PosedGroundOffset, ...) live in soh3d_model.cpp; this behavior CALLS them.
#ifndef SOH3D_BEHAVIORS_ACTOR_PLAYER_H
#define SOH3D_BEHAVIORS_ACTOR_PLAYER_H

#include "../actor_behavior.h"

namespace SoH3D {

class PlayerBehavior : public ActorBehavior {
public:
    static PlayerBehavior& instance();

    s16 actorId() const override;                              // ACTOR_PLAYER
    bool tryDrawModel(PlayState* play, Actor* actor) override; // draw the OoT3D Link body (was SoH3D_TryDrawPlayer)

    // Link-specific per-frame hooks + REPL, driven from soh3d.c via the extern "C" shims in player.cpp.
    int repl(PlayState* play, const char* cmd, const char* line, const char* outPath); // `link*` commands
    void walkInject(PlayState* play);                          // #6/#9 cucco-grab pickup driver
    void applyPin(PlayState* play, Actor* actor);              // #8 hand-weave transform pin
    float groundDiag(PlayState* play, const char** outCsab);   // #79 feet-grounding diagnostic
};

} // namespace SoH3D

#endif // SOH3D_BEHAVIORS_ACTOR_PLAYER_H
