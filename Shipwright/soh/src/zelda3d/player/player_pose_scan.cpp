#include "player_pose_scan.h"

#include "../anim/pose_tracking.h"
#include "player_behavior.h"

#include <cmath>
#include <cstring>

static inline Zelda3D::PlayerBehavior& P() {
    return Zelda3D::PlayerBehavior::instance();
}

// sLinkModelId moved to PlayerBehavior::modelId (the player's current draw model id).
extern "C" int Zelda3D_LinkModelId(void) {
    return P().modelId;
}

// LinkPoseScan LOGGER. The pose (lastSkin) is recomputed in the DRAW path, not in Play_Update, so the
// per-frame discontinuity must be sampled there — once per rendered frame — to be meaningful (the
// frame-step `step` advances logic without drawing). When active, each drawn player frame records the
// max per-bone rotation jump + the bone + the resolved csab + its frame. The REPL reads the log back.
void Zelda3D::LinkPoseScan::setActive(int on) {
    mActive = on ? 1 : 0;
    if (on) {
        mCount = 0; // clear the log only when STARTING a scan; `off` keeps it for `dump`
        int mid = Zelda3D::PlayerBehavior::instance().modelId;
        if (mid >= 0)
            Zelda3D_PoseScanReset(mid); // baseline = next frame
    }
}
float Zelda3D::LinkPoseScan::get(int i, int* bone, float* frame, const char** csab) {
    if (i < 0 || i >= mCount) {
        if (bone)
            *bone = -1;
        if (frame)
            *frame = 0;
        if (csab)
            *csab = "";
        return 0;
    }
    if (bone)
        *bone = mLog[i].bone;
    if (frame)
        *frame = mLog[i].frame;
    if (csab)
        *csab = mLog[i].csab;
    return mLog[i].deg;
}
// Called from the draw path right after the pose is set, when the scan is active.
void Zelda3D::LinkPoseScan::record(int modelId, const char* csab, float frame) {
    if (!mActive || mCount >= (int)(sizeof(mLog) / sizeof(mLog[0])))
        return;
    int bone = -1;
    float deg = Zelda3D_PoseDiscontinuity(modelId, &bone);
    Rec& r = mLog[mCount++];
    r.deg = deg;
    r.bone = bone;
    r.frame = frame;
    const char* c = csab ? csab : "(n64-retarget)";
    int k = 0;
    for (; c[k] && k < 27; k++)
        r.csab[k] = c[k];
    r.csab[k] = '\0';
}
extern "C" void Zelda3D_PoseScanSetActive(int on) {
    P().poseScan.setActive(on);
}
extern "C" int Zelda3D_PoseScanCount(void) {
    return P().poseScan.count();
}
extern "C" float Zelda3D_PoseScanGet(int i, int* bone, float* frame, const char** csab) {
    return P().poseScan.get(i, bone, frame, csab);
}
