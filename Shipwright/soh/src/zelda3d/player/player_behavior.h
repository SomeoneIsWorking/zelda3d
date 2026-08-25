// Composition root for Link's dedicated Zelda3D behavior path.
#ifndef ZELDA3D_PLAYER_BEHAVIOR_H
#define ZELDA3D_PLAYER_BEHAVIOR_H

#include "../behaviors/actor_behavior.h"
#include "player_grab_driver.h"
#include "player_joint_dump.h"
#include "player_midmask.h"
#include "player_pose_scan.h"
#include "player_retarget.h"
#include "player_transform_pin.h"

namespace Zelda3D {

class PlayerBehavior : public ActorBehavior {
  public:
    static PlayerBehavior& instance();

    s16 actorId() const override;
    bool tryDrawModel(PlayState* play, Actor* actor) override;

    int repl(PlayState* play, const char* cmd, const char* line, const char* outPath);
    void walkInject(PlayState* play) {
        grab.walkInject(play);
    }
    void applyPin(PlayState* play, Actor* actor) {
        transformPin.apply(play, actor);
    }
    float groundDiag(PlayState* play, const char** outCsab);

    int modelId = -1;
    LinkMidMask midmask;
    LinkRetarget retarget;
    LinkPoseScan poseScan;
    LinkJointDump jointDump;
    LinkGrabDriver grab;
    LinkTransformPin transformPin;
};

} // namespace Zelda3D

#endif // ZELDA3D_PLAYER_BEHAVIOR_H
