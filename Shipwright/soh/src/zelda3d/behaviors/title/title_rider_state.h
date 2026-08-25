#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_RIDER_STATE_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_RIDER_STATE_H

#include "global.h"

#ifdef __cplusplus
#include "title_rider.h"

namespace Zelda3D {

// Owns the title rider integrator and every raw actor pointer tied to one game-core run.
class TitleRiderState {
  public:
    static TitleRiderState& Instance();

    void update(PlayState* play);
    void apply(PlayState* play, Actor* actor);
    void release(PlayState* play);
    void resetRunState();
    const TitleRider& rider() const;

  private:
    TitleRider mRider;
};

} // namespace Zelda3D

extern "C" {
#endif

void Zelda3D_Title_RiderApply(PlayState* play, Actor* actor);
int Zelda3D_Title_RiderState(float* outPos, int* outComputedYaw, int* outHorseWorldYaw, int* outHorseShapeYaw);

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_RIDER_STATE_H
