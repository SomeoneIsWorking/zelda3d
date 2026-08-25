#include "player_retarget.h"

#include <cmath>
#include <cstring>

// childlink_v2 per-bone retarget correction (kLinkChildBoneCorr). Legs/neck stay on pure replace
// (no regression); the divergent spine/upper-arms are HAND-WOVEN (#7/#31) — a constant fitted C
// provably can't reconcile the re-authored OoT3D arm motion, so the arm corrections are tuned by
// hand against the OoT3D CSAB ground truth (linksrc 3ds), NOT auto-fit.
#include "../tables/zelda3d_link_bonecorr.inc"

// HAND-WEAVE live-tune scaffold (#7 long arm). A mutable runtime copy of the table that the player
// path actually passes, so each upper-body bone's correction can be adjusted LIVE over the REPL
// (`linkcorr`) and seen in-game against the 3ds ground truth, instead of rebuild-per-guess. The
// final hand-tuned values get baked back into zelda3d_link_bonecorr.inc (`linkcorr bake <path>`).
// Shorthand for the single PlayerBehavior instance (its composed subsystems hold the former
// file-static Link state). Used by the extern "C" draw body + the per-frame hooks below.
// LinkRetarget: lazy-init the runtime correction table from the baked child table (kLinkChildBoneCorr).
void Zelda3D::LinkRetarget::ensure() {
    if (inited)
        return;
    memcpy(table, kLinkChildBoneCorr, sizeof(table));
    inited = true;
}

void Zelda3D::LinkRetarget::reset() {
    memcpy(table, kLinkChildBoneCorr, sizeof(table));
    inited = true;
}

// Build a row-major 3x3 rotation C = Rz(cz)·Ry(cy)·Rx(cx) (the live/csab ZYX convention) from
// Euler degrees, written into out[9]. Identity when all zero.
void Zelda3D_EulerToMat3(float cxDeg, float cyDeg, float czDeg, float* out) {
    float rx = cxDeg * (3.14159265358979f / 180.0f);
    float ry = cyDeg * (3.14159265358979f / 180.0f);
    float rz = czDeg * (3.14159265358979f / 180.0f);
    float cx = cosf(rx), sx = sinf(rx), cy = cosf(ry), sy = sinf(ry), cz = cosf(rz), sz = sinf(rz);
    // Rz*Ry*Rx, row-major.
    out[0] = cz * cy;
    out[1] = cz * sy * sx - sz * cx;
    out[2] = cz * sy * cx + sz * sx;
    out[3] = sz * cy;
    out[4] = sz * sy * sx + cz * cx;
    out[5] = sz * sy * cx - cz * sx;
    out[6] = -sy;
    out[7] = cy * sx;
    out[8] = cy * cx;
}
// Inverse: recover (cx,cy,cz) degrees from a row-major 3x3 such that M = Rz·Ry·Rx. For display.
void Zelda3D_Mat3ToEuler(const float* m, float* outDeg) {
    float sy = -m[6];
    if (sy > 1.0f)
        sy = 1.0f;
    else if (sy < -1.0f)
        sy = -1.0f;
    float ry = asinf(sy), rx, rz;
    if (fabsf(m[6]) < 0.99999f) {
        rx = atan2f(m[7], m[8]);
        rz = atan2f(m[3], m[0]);
    } else {
        rx = atan2f(-m[5], m[4]);
        rz = 0.0f;
    }
    outDeg[0] = rx * (180.0f / 3.14159265358979f);
    outDeg[1] = ry * (180.0f / 3.14159265358979f);
    outDeg[2] = rz * (180.0f / 3.14159265358979f);
}
