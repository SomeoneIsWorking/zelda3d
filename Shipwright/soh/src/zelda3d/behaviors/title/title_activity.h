#ifndef ZELDA3D_BEHAVIORS_TITLE_TITLE_ACTIVITY_H
#define ZELDA3D_BEHAVIORS_TITLE_TITLE_ACTIVITY_H

#include "global.h"

#ifdef __cplusplus
namespace Zelda3D {

// Owns only the title-demo activation predicate and its process-lifetime run state.
class TitleActivity {
  public:
    static TitleActivity& Instance();

    bool shouldBeActive(const PlayState* play) const;
    bool activate();
    bool deactivate();
    bool resetRunState();
    bool isActive() const;

  private:
    bool mActive = false;
    bool mEntered = false;
};

} // namespace Zelda3D

extern "C" {
#endif

int Zelda3D_Title_IsActive(void);
int Zelda3D_TitleCamEnabled(void);
extern int gZelda3dTitleCam;

#ifdef __cplusplus
}
#endif

#endif // ZELDA3D_BEHAVIORS_TITLE_TITLE_ACTIVITY_H
