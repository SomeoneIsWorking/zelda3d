// Stable player behavior composition and C ABI shims.
#include "player_draw.h"
#include "player_behavior.h"
#include "player_ground_diagnostics.h"
#include "zelda3d_link.h"

// --- PlayerBehavior class boilerplate + C-ABI shims --------------------------------------------
// Link's behavior is reached through the dedicated player draw, input, lifecycle, and command hooks,
// not the generic findActorBehavior registry — the
// player draw path is special. These shims keep the existing C entry points stable while routing them
// to the single PlayerBehavior instance, so the rest of the engine is unchanged.
Zelda3D::PlayerBehavior& Zelda3D::PlayerBehavior::instance() {
    static Zelda3D::PlayerBehavior sPlayer;
    return sPlayer;
}
s16 Zelda3D::PlayerBehavior::actorId() const {
    return ACTOR_PLAYER;
}
bool Zelda3D::PlayerBehavior::tryDrawModel(PlayState* play, Actor* actor) {
    return Zelda3D_PlayerDrawImpl(play, actor) != 0;
}

extern "C" int Zelda3D_TryDrawPlayer(PlayState* play, Actor* actor) {
    return Zelda3D::PlayerBehavior::instance().tryDrawModel(play, actor) ? 1 : 0;
}
extern "C" float Zelda3D_LinkGroundDiag(PlayState* play, const char** outCsab) {
    return Zelda3D::PlayerBehavior::instance().groundDiag(play, outCsab);
}
extern "C" void Zelda3D_LinkApplyPin(PlayState* play, Actor* actor) {
    Zelda3D::PlayerBehavior::instance().applyPin(play, actor);
}
extern "C" void Zelda3D_LinkWalkInject(PlayState* play) {
    Zelda3D::PlayerBehavior::instance().walkInject(play);
}
extern "C" int Zelda3D_LinkRepl(PlayState* play, const char* cmd, const char* line, const char* outPath) {
    return Zelda3D::PlayerBehavior::instance().repl(play, cmd, line, outPath);
}
