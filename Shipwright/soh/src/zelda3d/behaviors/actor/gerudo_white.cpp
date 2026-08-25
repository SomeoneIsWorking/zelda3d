// Zelda3D En_Ge1 animation bridge. The N64 and OoT3D white-Gerudo rigs have the same 15-limb
// order, so this owner resolves the current CSAB and exposes the live joint rotations together.
#include "gerudo_white.h"

#include "../../anim/skeleton_draw_bridge.h"
#include "objects/object_ge1/object_ge1.h"
#include "overlays/actors/ovl_En_Ge1/z_en_ge1.h"

#include <cstdio>
#include <cstring>

extern "C" const char* Zelda3D_ResolveAnim_EnGe1(Actor* actor) {
    EnGe1* gerudo = reinterpret_cast<EnGe1*>(actor);
    const char* n64Animation = reinterpret_cast<const char*>(gerudo->animation);
    const char* csab = "ge1_s_wait";

    if (n64Animation != nullptr) {
        if (std::strcmp(n64Animation, dgGerudoWhiteClapAnim) == 0) {
            csab = "ge1_mon_akeru";
        } else if (std::strcmp(n64Animation, dgGerudoWhiteDismissiveAnim) == 0) {
            csab = "ge1_hanasi";
        }
    }

    if (gZelda3dAnimDebug) {
        static int debugCounter = 0;
        if ((debugCounter++ % 20) == 0) {
            std::fprintf(stderr, "SOH3D anim: csab=%s curFrame=%.2f animLength=%.2f n64=%s\n", csab,
                         gerudo->skelAnime.curFrame, gerudo->skelAnime.animLength,
                         n64Animation != nullptr ? n64Animation : "(null)");
            std::fflush(stdout);
        }
    }
    return csab;
}

extern "C" int Zelda3D_Joints_EnGe1(Actor* actor, const s16** outJointRots, int* outLimbCount) {
    EnGe1* gerudo = reinterpret_cast<EnGe1*>(actor);
    if (gerudo->skelAnime.jointTable == nullptr || gerudo->skelAnime.limbCount <= 0) {
        return 0;
    }

    // jointTable[0] is root translation; OoT3D bone zero begins at N64 limb one.
    *outJointRots = reinterpret_cast<const s16*>(&gerudo->skelAnime.jointTable[1]);
    *outLimbCount = gerudo->skelAnime.limbCount;
    return 1;
}
