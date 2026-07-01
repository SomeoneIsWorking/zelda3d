// Zelda3D behavior: Link (the PLAYER) — OoT3D body replacement, as a structured ActorBehavior class
// composed of individual subsystems.
//
// Unlike the NPC/prop behaviors, the player's draw is invoked from Player_Draw via the DEDICATED
// Zelda3D_TryDrawPlayer hook (z_player.c), NOT the generic Zelda3D_TryActorModelDraw registry — the
// player's draw path is special (it runs inside the player overlay, not the generic actor draw loop).
// So PlayerBehavior is reached through that hook: the extern "C" shims at the bottom of zelda3d_link.cpp
// delegate to the singleton. It is intentionally NOT added to findActorBehavior's generic switch, to
// avoid a double draw.
//
// PlayerBehavior owns Link's draw policy through composed member SUBSYSTEMS — each encapsulates one
// cohesive piece of formerly-file-static state + behavior:
//   midmask   equipment / hand-pose mesh-id visibility (childlink_v2 bakes all variants on mesh_ids)
//   retarget  per-bone N64->OoT3D correction table + hand-weave pose freeze (linksrc n64 path)
//   poseScan  per-bone pose-discontinuity scanner log (anim QA tooling)
//   jointDump live player jointTable CSV capture (retarget-correction derivation)
//   grab      #6/#9 deterministic cucco-pickup driver
//   pin       #8 hand-weave transform pin (byte-deterministic side-profile view)
// The low-level CMB skeleton FK retarget primitives (Zelda3D_UpdateAnimN64*, Zelda3D_PosedGroundOffset,
// ...) live in zelda3d_model.cpp; these subsystems CALL them.
#ifndef ZELDA3D_BEHAVIORS_ACTOR_PLAYER_H
#define ZELDA3D_BEHAVIORS_ACTOR_PLAYER_H

#include "../actor_behavior.h"
#include "../../zelda3d_link.h" // Zelda3dBoneCorr (C-ABI retarget correction record)
#include <cstdio>             // FILE*

namespace Zelda3D {

// Equipment / hand-pose mesh-id visibility. The link CMB bakes every hand-pose + held-equipment
// variant on a distinct mesh_id; compute() picks the visible subset from Link's live state each frame.
class LinkMidMask {
public:
    unsigned long long compute(Player* player) const;       // visible mesh_id subset this frame
    unsigned long long overrideMask = ~0ull;                // REPL `linkmid`: ~0 = no override
    bool overrideSet = false;

private:
    unsigned long long boyMidMask(Player* player) const;    // adult/boy link_v2.cmb mesh_id layout
};

// Per-bone N64->OoT3D retarget correction table + the hand-weave pose freeze (linksrc n64; linkcorr).
class LinkRetarget {
public:
    void ensure();                  // lazy-init: copy kLinkChildBoneCorr -> table
    Zelda3dBoneCorr table[25];
    bool inited = false;
    // hand-weave pose freeze: latch the live jointTable so `linkcorr` tuning is the only variable.
    Vec3s frozenJoints[40];
    int frozenCount = 0;            // >0 = pose frozen at this limbCount
    int freezeReq = 0;              // 1 = latch the live pose on the next player draw
};

// Per-bone pose-discontinuity scanner log (anim QA): records the largest per-bone rotation jump
// between consecutive drawn frames, to flag hard-cut transitions. Driven from the player draw.
class LinkPoseScan {
public:
    void setActive(int on);
    int count() const { return mCount; }
    float get(int i, int* bone, float* frame, const char** csab);
    void record(int modelId, const char* csab, float frame);

private:
    struct Rec { float deg; int bone; float frame; char csab[28]; };
    Rec mLog[512];
    int mCount = 0;
    int mActive = 0;
};

// Live player jointTable CSV capture (REPL `linkjointdump`) — for the quantitative per-bone
// retarget-correction derivation (tools/zelda3d_link_retarget_derive.py).
class LinkJointDump {
public:
    FILE* file = nullptr;
    int remaining = 0;
    int cap = 0;
};

// #6/#9 deterministic cucco-pickup driver: holds the asel-selected actor in front of Link and injects
// fresh A edges until heldActor is set. Run from Zelda3D_WalkInject just before Play_Update.
class LinkGrabDriver {
public:
    void walkInject(PlayState* play);
    int frames = 0;                 // remaining drive frames (0 = idle)
};

// #8 hand-weave transform pin: pin the player's world pos + facing yaw each frame so the side-profile
// view is byte-deterministic across `linkcorr` tweaks.
class LinkPin {
public:
    void apply(PlayState* play, Actor* actor);
    int on = 0;                     // REPL `linkpin <0|1>`
    Vec3f pos;
    s16 yaw = 0;
};

class PlayerBehavior : public ActorBehavior {
public:
    static PlayerBehavior& instance();

    s16 actorId() const override;                              // ACTOR_PLAYER
    bool tryDrawModel(PlayState* play, Actor* actor) override; // draw the OoT3D Link body

    // Link-specific per-frame hooks + REPL, driven from zelda3d.c via the extern "C" shims.
    int repl(PlayState* play, const char* cmd, const char* line, const char* outPath);
    void walkInject(PlayState* play) { grab.walkInject(play); }
    void applyPin(PlayState* play, Actor* actor) { pin.apply(play, actor); }
    float groundDiag(PlayState* play, const char** outCsab);

    int modelId = -1; // last modelId the player body drew with (REPL pose scanner / Zelda3D_LinkModelId)

    // Composed subsystems (see header comment).
    LinkMidMask midmask;
    LinkRetarget retarget;
    LinkPoseScan poseScan;
    LinkJointDump jointDump;
    LinkGrabDriver grab;
    LinkPin pin;
};

} // namespace Zelda3D

#endif // ZELDA3D_BEHAVIORS_ACTOR_PLAYER_H
