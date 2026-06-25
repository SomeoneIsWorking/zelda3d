#include "csab.h"
#include "cmb.h"
#include "mat4.h"
#include <cstring>
#include <cmath>
#include <functional>

namespace SoH3D {

static uint16_t u16(const uint8_t* b, size_t o) { uint16_t v; memcpy(&v, b + o, 2); return v; }
static int16_t  s16(const uint8_t* b, size_t o) { int16_t v; memcpy(&v, b + o, 2); return v; }
static uint32_t u32(const uint8_t* b, size_t o) { uint32_t v; memcpy(&v, b + o, 4); return v; }
static float    f32(const uint8_t* b, size_t o) { float v; memcpy(&v, b + o, 4); return v; }

Csab::Track Csab::parseTrack(uint32_t o, bool isRotInt16) const {
    const uint8_t* b = mData.data();
    Track t;
    t.type = (int)u32(b, o);
    uint32_t nkf = u32(b, o + 4);
    t.timeStart = (int)u32(b, o + 8);
    t.timeEnd = (int)u32(b, o + 0xC) + 1;
    uint32_t p = o + 0x10;
    t.present = true;
    t.frames.reserve(nkf);
    if (t.type == LINEAR) {
        for (uint32_t i = 0; i < nkf; i++) {
            Keyframe k{ (float)u32(b, p), f32(b, p + 4), 0, 0 };
            t.frames.push_back(k);
            p += 8;
        }
    } else if (t.type == HERMITE) {
        if (isRotInt16) {
            // Quantized rotation keyframes (8 bytes): u16 time, then value/in-tangent/out-tangent as
            // s16 fixed-point ANGLES — full circle = 0x10000, so radians = s16 * 2pi/0x10000 = s16 *
            // pi/0x8000. Tangents are angle/frame, same scale. (ge1 uses float HERMITE so this path
            // was previously untested; using the s16s raw as radians exploded the link mesh.)
            const float kAngle = 3.14159265358979f / 32768.0f;
            for (uint32_t i = 0; i < nkf; i++) {
                Keyframe k{ (float)u16(b, p), s16(b, p + 2) * kAngle, s16(b, p + 4) * kAngle,
                            s16(b, p + 6) * kAngle };
                t.frames.push_back(k);
                p += 8;
            }
        } else {
            for (uint32_t i = 0; i < nkf; i++) {
                Keyframe k{ (float)u32(b, p), f32(b, p + 4), f32(b, p + 8), f32(b, p + 0xC) };
                t.frames.push_back(k);
                p += 0x10;
            }
        }
    } else {
        t.present = false; // CONSTANT / unknown -> no track (fall back to rest TRS)
    }
    // Flag a present-but-static track: all keyframe values equal => not real animation, just a
    // baked constant. For translation this distinguishes a redundant bone-OFFSET bake (which must
    // defer to the target rig's rest, see animatedBoneWorld) from genuine motion (e.g. idle bob).
    if (t.present && !t.frames.empty()) {
        t.constant = true;
        for (const auto& k : t.frames)
            if (std::fabs(k.value - t.frames[0].value) > 1e-4f) { t.constant = false; break; }
    }
    return t;
}

Csab::AnimNode Csab::parseAnod(uint32_t o) const {
    const uint8_t* b = mData.data();
    AnimNode n;
    n.boneIndex = u16(b, o + 4);
    n.isRotInt16 = u16(b, o + 6) != 0;
    for (int i = 0; i < 9; i++) {
        uint16_t off = u16(b, o + 8 + 2 * i);
        // isRotInt16 applies only to the rotation slots (3,4,5 = rX/rY/rZ); translation/scale tracks
        // stay float even in an int16 anod. (Translation is usually LINEAR float anyway.)
        if (off) n.tracks[i] = parseTrack(o + off, n.isRotInt16 && (i >= 3 && i <= 5));
    }
    return n;
}

Csab::Csab(std::vector<uint8_t> data) : mData(std::move(data)) {
    const uint8_t* b = mData.data();
    if (mData.size() < 0x38 || memcmp(b, "csab", 4) != 0) { mErr = "not a CSAB"; return; }
    uint32_t subver = u32(b, 0x08);
    if (subver != 0x03) { mErr = "only Ocarina subversion 3 supported"; return; }
    uint32_t anodBase = u32(b, 0x14); // = 0x18
    mDuration = (int)u32(b, 0x28) + 1;
    uint32_t anodCount = u32(b, 0x30);
    mBoneCount = (int)u32(b, 0x34);
    mBoneToAnim.resize(mBoneCount);
    for (int i = 0; i < mBoneCount; i++) mBoneToAnim[i] = s16(b, 0x38 + 2 * i);
    uint32_t idx = 0x38 + 2 * (uint32_t)mBoneCount;
    idx = (idx + 3) & ~3u;
    mNodes.reserve(anodCount);
    for (uint32_t i = 0; i < anodCount; i++) {
        uint32_t off = u32(b, idx + 4 * i);
        if (memcmp(b + anodBase + off, "anod", 4) != 0) { mErr = "bad anod"; return; }
        mNodes.push_back(parseAnod(anodBase + off));
    }
    mOk = true;
}

const Csab::AnimNode* Csab::nodeForBone(int boneId) const {
    if (boneId >= 0 && boneId < (int)mBoneToAnim.size()) {
        int ai = mBoneToAnim[boneId];
        if (ai >= 0 && ai < (int)mNodes.size()) return &mNodes[ai];
    }
    return nullptr;
}

float Csab::animFrame(float frame) const {
    float last = (float)mDuration;
    while (frame > last) frame -= last; // REPEAT
    return frame;
}

// ---- sampling (matches tools/csab.py / noclip) ----
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float lerpAngle(float v0, float v1, float t) {
    const float TAU = 6.283185307179586f;
    float da = fmodf(v1 - v0, TAU);
    float dist = fmodf(2 * da, TAU) - da;
    return v0 + dist * t;
}

// ---- rotation blend for the morph cross-fade (quaternion slerp) ----
// Component-wise euler lerp does NOT follow the shortest rotation arc (the composed Rz·Ry·Rx of
// per-axis-shortest angles can bulge to ~180deg mid-blend — seen live as a head-snap on an
// idle->idle morph). Blending the rotation MATRICES via quaternion slerp follows the true geodesic,
// giving the smooth shortest-arc cross-fade the N64 morph intends (docs/anim_system.md "THE MORPH").
typedef std::array<float, 4> Quat; // (x,y,z,w)
static Quat matToQuat(const Mat4& R) {
    // R is row-major (M*v); read the 3x3 rotation block.
    float m00 = R[0], m01 = R[1], m02 = R[2];
    float m10 = R[4], m11 = R[5], m12 = R[6];
    float m20 = R[8], m21 = R[9], m22 = R[10];
    float tr = m00 + m11 + m22;
    Quat q;
    if (tr > 0.0f) {
        float s = std::sqrt(tr + 1.0f) * 2.0f; // s = 4w
        q[3] = 0.25f * s;
        q[0] = (m21 - m12) / s;
        q[1] = (m02 - m20) / s;
        q[2] = (m10 - m01) / s;
    } else if (m00 > m11 && m00 > m22) {
        float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f; // s = 4x
        q[3] = (m21 - m12) / s;
        q[0] = 0.25f * s;
        q[1] = (m01 + m10) / s;
        q[2] = (m02 + m20) / s;
    } else if (m11 > m22) {
        float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f; // s = 4y
        q[3] = (m02 - m20) / s;
        q[0] = (m01 + m10) / s;
        q[1] = 0.25f * s;
        q[2] = (m12 + m21) / s;
    } else {
        float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f; // s = 4z
        q[3] = (m10 - m01) / s;
        q[0] = (m02 + m20) / s;
        q[1] = (m12 + m21) / s;
        q[2] = 0.25f * s;
    }
    return q;
}
static Mat4 quatToMat(const Quat& q) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    Mat4 m = matId();
    m[0] = 1 - 2 * (y * y + z * z); m[1] = 2 * (x * y - w * z);     m[2] = 2 * (x * z + w * y);
    m[4] = 2 * (x * y + w * z);     m[5] = 1 - 2 * (x * x + z * z); m[6] = 2 * (y * z - w * x);
    m[8] = 2 * (x * z - w * y);     m[9] = 2 * (y * z + w * x);     m[10] = 1 - 2 * (x * x + y * y);
    return m;
}
static Quat quatSlerp(Quat a, Quat b, float t) {
    float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];
    if (dot < 0.0f) { for (int i = 0; i < 4; i++) b[i] = -b[i]; dot = -dot; } // shortest arc
    Quat q;
    if (dot > 0.9995f) { // near-parallel -> linear (avoids sin(0) blow-up), then renormalize
        for (int i = 0; i < 4; i++) q[i] = a[i] + (b[i] - a[i]) * t;
    } else {
        float th = std::acos(dot), s = std::sin(th);
        float wa = std::sin((1 - t) * th) / s, wb = std::sin(t * th) / s;
        for (int i = 0; i < 4; i++) q[i] = a[i] * wa + b[i] * wb;
    }
    float n = std::sqrt(q[0]*q[0] + q[1]*q[1] + q[2]*q[2] + q[3]*q[3]);
    if (n > 1e-8f) for (int i = 0; i < 4; i++) q[i] /= n;
    return q;
}
static Mat4 eulerMat(const float r[3]) { // Rz·Ry·Rx, matching the local-compose order
    return matMul(matMul(matRz(r[2]), matRy(r[1])), matRx(r[0]));
}
// Embed a row-major 3x3 (9 floats) into a Mat4. Identity translation/w-row.
static Mat4 mat3to4(const float* m9) {
    Mat4 m = matId();
    m[0] = m9[0]; m[1] = m9[1]; m[2] = m9[2];
    m[4] = m9[3]; m[5] = m9[4]; m[6] = m9[5];
    m[8] = m9[6]; m[9] = m9[7]; m[10] = m9[8];
    return m;
}
// Post-multiply the per-bone OoT3D-native override rotation onto a bone's local rotation matrix,
// replicating the actor OverrideLimbDraw's MTXMODE_APPLY (rotation applied AFTER the bone's animated
// rotation, in the bone's local frame). bonePostRot is 9 floats per bone id (row-major 3x3), or null.
static Mat4 applyPostRot(Mat4 R, const float* bonePostRot, int postCount, int id) {
    if (bonePostRot && id >= 0 && id < postCount) {
        return matMul(R, mat3to4(bonePostRot + id * 9));
    }
    return R;
}
static float pointCubic(const float cf[4], float t) {
    return ((cf[0] * t + cf[1]) * t + cf[2]) * t + cf[3];
}

float Csab::sampleTrack(const Track& t, float frame, bool rotation) {
    const auto& f = t.frames;
    if (f.empty()) return 0;
    int i1 = -1;
    for (int i = 0; i < (int)f.size(); i++) { if (frame < f[i].time) { i1 = i; break; } }
    if (t.type == LINEAR) {
        if (i1 == 0) return f[0].value;
        if (i1 < 0) return f.back().value;
        const Keyframe& k0 = f[i1 - 1]; const Keyframe& k1 = f[i1];
        float tt = (frame - k0.time) / (k1.time - k0.time);
        return rotation ? lerpAngle(k0.value, k1.value, tt) : lerpf(k0.value, k1.value, tt);
    }
    // HERMITE
    const Keyframe* k0; const Keyframe* k1;
    float length, tt;
    if (i1 <= 0) {
        // WRAP segment: the query frame is past the last keyframe (or before the first), so the
        // active segment spans last_keyframe -> first_keyframe ACROSS the loop seam. The forward
        // distance must wrap by timeEnd. The old `fmodf(k1->time - k0->time, timeEnd)` produced a
        // NEGATIVE length here (k1=first < k0=last => fmodf(-16,17) = -16), making the cubic
        // EXTRAPOLATE garbage — seen live as nml_carryB_free bone 20 (R_HAND) snapping ~160deg once
        // per loop (the clip's own bone-20 range is only 56deg, so the pop was pure extrapolation).
        k0 = &f.back(); k1 = &f.front();
        length = (k1->time - k0->time) + (float)t.timeEnd;       // forward distance across the seam
        if (length == 0) return k0->value;
        float num = frame - k0->time;
        if (num < 0) num += (float)t.timeEnd;                    // frame sits in the [0, first_kf) tail
        tt = num / length;
    } else {
        k0 = &f[i1 - 1]; k1 = &f[i1];
        length = k1->time - k0->time;
        if (length == 0) return k0->value;
        tt = (frame - k0->time) / length;
    }
    float p0 = k0->value, p1 = k1->value;
    if (rotation) {
        // int16 rotation values are wrapped to [-pi,pi); the cubic must interpolate the
        // CONTINUOUS curve, so unwrap p1 to the branch nearest p0 (take the short way).
        // Without this, a keyframe pair straddling +-pi (e.g. +176deg -> -168deg, a +16deg
        // move) sweeps the long way (~-344deg) and the bone spins all the way around — seen
        // live as Link's head/back-shield/arms "spinning weird". Tangents are slopes
        // (angle/frame), invariant under adding 2*pi, so they stay valid.
        const float PI = 3.14159265358979f, TAU = 6.283185307179586f;
        p1 = p0 + (fmodf(p1 - p0 + PI + TAU, TAU) - PI);
    }
    float s0 = k0->tangentOut * length, s1 = k1->tangentIn * length;
    float cf[4] = { 2 * p0 - 2 * p1 + s0 + s1, -3 * p0 + 3 * p1 - 2 * s0 - s1, s0, p0 };
    return pointCubic(cf, tt);
}

void Csab::sampleLocalTRS(int boneId, bool nonRoot, const float restT[3], const float restR[3],
                         const float restS[3], float fr, float t[3], float r[3], float s[3]) const {
    s[0] = restS[0]; s[1] = restS[1]; s[2] = restS[2];
    r[0] = restR[0]; r[1] = restR[1]; r[2] = restR[2];
    t[0] = restT[0]; t[1] = restT[1]; t[2] = restT[2];
    const AnimNode* node = nodeForBone(boneId);
    if (!node) return;
    // track slots: 0..2 tX/Y/Z, 3..5 rX/Y/Z, 6..8 sX/Y/Z
    if (node->tracks[6].present) s[0] = sampleTrack(node->tracks[6], fr, false);
    if (node->tracks[7].present) s[1] = sampleTrack(node->tracks[7], fr, false);
    if (node->tracks[8].present) s[2] = sampleTrack(node->tracks[8], fr, false);
    if (node->tracks[3].present) r[0] = sampleTrack(node->tracks[3], fr, true);
    if (node->tracks[4].present) r[1] = sampleTrack(node->tracks[4], fr, true);
    if (node->tracks[5].present) r[2] = sampleTrack(node->tracks[5], fr, true);
    // Bone OFFSETS belong to the SKELETON, not the clip. CSABs authored for one rig (the
    // boy/adult Link clips) bake that rig's bone translations into per-bone translation tracks
    // as STATIC (constant-valued) tracks. When such a clip drives a DIFFERENT-proportioned rig
    // — child Link has no own idle/most clips and reuses the boy ones — those longer baked
    // offsets override the child's shorter rest offsets and STRETCH the limb (the long/
    // stretched right arm, #7/#8: child R-arm 283/593/548 -> boy 442/927/856). So for a
    // non-root bone, IGNORE a static translation track and keep the rig's rest offset (a no-op
    // for same-rig clips, where rest == the baked value). Genuinely ANIMATED translation
    // (varying track, e.g. the idle pelvis bob) is still applied — it is real motion, not a
    // skeletal offset. Root (parent<0) always keeps its translation (root placement/motion).
    if (node->tracks[0].present && !(nonRoot && node->tracks[0].constant))
        t[0] = sampleTrack(node->tracks[0], fr, false);
    if (node->tracks[1].present && !(nonRoot && node->tracks[1].constant))
        t[1] = sampleTrack(node->tracks[1], fr, false);
    if (node->tracks[2].present && !(nonRoot && node->tracks[2].constant))
        t[2] = sampleTrack(node->tracks[2], fr, false);
}

void Csab::animatedBoneWorld(const Cmb& model, float frame, std::vector<std::array<float, 16>>& out,
                             const float* boneRotDelta, int deltaCount, const float* bonePostRot,
                             int postCount) const {
    const auto& bones = model.bones();
    const auto& bind = model.boneMatrices();
    out.assign(bind.size(), matId());
    std::vector<char> done(bind.size(), 0);
    std::vector<const CmbBone*> byId(bind.size(), nullptr);
    for (const auto& bn : bones) if (bn.id >= 0 && (size_t)bn.id < byId.size()) byId[bn.id] = &bn;
    float fr = animFrame(frame);

    // resolve a bone's animated world matrix, recursing through parents.
    std::function<Mat4(int)> world = [&](int id) -> Mat4 {
        if (id < 0 || (size_t)id >= out.size() || !byId[id]) return matId();
        if (done[id]) return out[id];
        const CmbBone* bn = byId[id];
        float s[3], r[3], t[3];
        sampleLocalTRS(id, bn->parent >= 0, bn->trans, bn->rot, bn->scale, fr, t, r, s);
        // Procedural OverrideLimbDraw delta: add the extra LOCAL rotation (radians) for this bone
        // on top of the animated pose, in the SAME T·Rz·Ry·Rx·S frame the tracks use.
        if (boneRotDelta && id >= 0 && id < deltaCount) {
            r[0] += boneRotDelta[id * 3 + 0];
            r[1] += boneRotDelta[id * 3 + 1];
            r[2] += boneRotDelta[id * 3 + 2];
        }
        Mat4 R = applyPostRot(eulerMat(r), bonePostRot, postCount, id);
        Mat4 L = matMul(matT(t[0], t[1], t[2]), matMul(R, matS(s[0], s[1], s[2])));
        Mat4 W = (bn->parent < 0) ? L : matMul(world(bn->parent), L);
        out[id] = W;
        done[id] = 1;
        return W;
    };
    for (const auto& bn : bones) world(bn.id);
}

void Csab::animatedBoneWorldMorph(const Cmb& model, float frameIn, const Csab& outgoing,
                                  float frameOut, float weight,
                                  std::vector<std::array<float, 16>>& out, const float* boneRotDelta,
                                  int deltaCount, const float* bonePostRot, int postCount) const {
    const auto& bones = model.bones();
    const auto& bind = model.boneMatrices();
    out.assign(bind.size(), matId());
    std::vector<char> done(bind.size(), 0);
    std::vector<const CmbBone*> byId(bind.size(), nullptr);
    for (const auto& bn : bones) if (bn.id >= 0 && (size_t)bn.id < byId.size()) byId[bn.id] = &bn;
    float frIn = animFrame(frameIn);
    float frOut = outgoing.animFrame(frameOut);
    if (weight < 0.0f) weight = 0.0f; if (weight > 1.0f) weight = 1.0f;

    // resolve a bone's MORPHED world matrix: blend incoming/outgoing LOCAL TRS by `weight`, then
    // recurse through parents (parents are already morphed, so the blend stays local-rotation-true).
    std::function<Mat4(int)> world = [&](int id) -> Mat4 {
        if (id < 0 || (size_t)id >= out.size() || !byId[id]) return matId();
        if (done[id]) return out[id];
        const CmbBone* bn = byId[id];
        bool nonRoot = bn->parent >= 0;
        float si[3], ri[3], ti[3], so[3], ro[3], to[3];
        sampleLocalTRS(id, nonRoot, bn->trans, bn->rot, bn->scale, frIn, ti, ri, si);
        outgoing.sampleLocalTRS(id, nonRoot, bn->trans, bn->rot, bn->scale, frOut, to, ro, so);
        // pose = lerp(incoming, outgoing, weight). weight=1 -> outgoing (frozen), weight=0 ->
        // incoming. Rotation blends along the geodesic (quaternion slerp of the two local rotation
        // matrices); translation/scale lerp linearly.
        float s[3], t[3];
        for (int k = 0; k < 3; k++) {
            s[k] = lerpf(si[k], so[k], weight);
            t[k] = lerpf(ti[k], to[k], weight);
        }
        Mat4 R = quatToMat(quatSlerp(matToQuat(eulerMat(ri)), matToQuat(eulerMat(ro)), weight));
        // Procedural OverrideLimbDraw delta (head-track/cucco) post-multiplied onto the blended
        // rotation. Exact for the common no-concurrent-morph case; a small approximation only while a
        // tracking actor also happens to be mid-transition (deltas are small there).
        if (boneRotDelta && id >= 0 && id < deltaCount) {
            float d[3] = { boneRotDelta[id * 3 + 0], boneRotDelta[id * 3 + 1], boneRotDelta[id * 3 + 2] };
            R = matMul(R, eulerMat(d));
        }
        R = applyPostRot(R, bonePostRot, postCount, id);
        Mat4 L = matMul(matT(t[0], t[1], t[2]), matMul(R, matS(s[0], s[1], s[2])));
        Mat4 W = (bn->parent < 0) ? L : matMul(world(bn->parent), L);
        out[id] = W;
        done[id] = 1;
        return W;
    };
    for (const auto& bn : bones) world(bn.id);
}

void Csab::animatedBoneWorldTwoSource(const Cmb& model, float frameLower, const Csab& upper,
                                      float frameUpper, const unsigned char* upperMask, int maskCount,
                                      std::vector<std::array<float, 16>>& out,
                                      const float* boneRotDelta, int deltaCount,
                                      const float* bonePostRot, int postCount) const {
    const auto& bones = model.bones();
    const auto& bind = model.boneMatrices();
    out.assign(bind.size(), matId());
    std::vector<char> done(bind.size(), 0);
    std::vector<const CmbBone*> byId(bind.size(), nullptr);
    for (const auto& bn : bones) if (bn.id >= 0 && (size_t)bn.id < byId.size()) byId[bn.id] = &bn;
    float frLo = animFrame(frameLower);
    float frUp = upper.animFrame(frameUpper);

    // resolve a bone's animated world matrix; its LOCAL TRS is sampled from whichever clip the
    // upper-body mask selects, then composed through its (already-resolved) parent. Mixing happens
    // purely at the local-transform level, so the hierarchy stays consistent (an upper-body arm
    // still hangs off the lower clip's posed torso/pelvis — the N64 copy-map result).
    std::function<Mat4(int)> world = [&](int id) -> Mat4 {
        if (id < 0 || (size_t)id >= out.size() || !byId[id]) return matId();
        if (done[id]) return out[id];
        const CmbBone* bn = byId[id];
        bool useUpper = (upperMask && id < maskCount && upperMask[id]);
        float s[3], r[3], t[3];
        if (useUpper)
            upper.sampleLocalTRS(id, bn->parent >= 0, bn->trans, bn->rot, bn->scale, frUp, t, r, s);
        else
            sampleLocalTRS(id, bn->parent >= 0, bn->trans, bn->rot, bn->scale, frLo, t, r, s);
        if (boneRotDelta && id >= 0 && id < deltaCount) {
            r[0] += boneRotDelta[id * 3 + 0];
            r[1] += boneRotDelta[id * 3 + 1];
            r[2] += boneRotDelta[id * 3 + 2];
        }
        Mat4 R = applyPostRot(eulerMat(r), bonePostRot, postCount, id);
        Mat4 L = matMul(matT(t[0], t[1], t[2]), matMul(R, matS(s[0], s[1], s[2])));
        Mat4 W = (bn->parent < 0) ? L : matMul(world(bn->parent), L);
        out[id] = W;
        done[id] = 1;
        return W;
    };
    for (const auto& bn : bones) world(bn.id);
}

void Csab::skinMatricesTwoSource(const Cmb& model, float frameLower, const Csab& upper,
                                 float frameUpper, const unsigned char* upperMask, int maskCount,
                                 std::vector<std::array<float, 16>>& out, const float* boneRotDelta,
                                 int deltaCount, const float* bonePostRot, int postCount) const {
    std::vector<std::array<float, 16>> aw;
    animatedBoneWorldTwoSource(model, frameLower, upper, frameUpper, upperMask, maskCount, aw,
                               boneRotDelta, deltaCount, bonePostRot, postCount);
    const auto& bind = model.boneMatrices();
    out.assign(bind.size(), matId());
    for (size_t id = 0; id < bind.size(); id++)
        out[id] = matMul(aw[id], matInverse(bind[id]));
}

void Csab::skinMatricesMorph(const Cmb& model, float frameIn, const Csab& outgoing, float frameOut,
                             float weight, std::vector<std::array<float, 16>>& out,
                             const float* boneRotDelta, int deltaCount, const float* bonePostRot,
                             int postCount) const {
    std::vector<std::array<float, 16>> aw;
    animatedBoneWorldMorph(model, frameIn, outgoing, frameOut, weight, aw, boneRotDelta, deltaCount,
                           bonePostRot, postCount);
    const auto& bind = model.boneMatrices();
    out.assign(bind.size(), matId());
    for (size_t id = 0; id < bind.size(); id++)
        out[id] = matMul(aw[id], matInverse(bind[id]));
}

void Csab::skinMatrices(const Cmb& model, float frame, std::vector<std::array<float, 16>>& out,
                        const float* boneRotDelta, int deltaCount, const float* bonePostRot,
                        int postCount) const {
    std::vector<std::array<float, 16>> aw;
    animatedBoneWorld(model, frame, aw, boneRotDelta, deltaCount, bonePostRot, postCount);
    const auto& bind = model.boneMatrices();
    out.assign(bind.size(), matId());
    for (size_t id = 0; id < bind.size(); id++)
        out[id] = matMul(aw[id], matInverse(bind[id]));
}

} // namespace SoH3D
