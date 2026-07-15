// Parser for OoT3D CSAB skeletal animations (CTR Skeletal Animation Binary).
// Port of tools/csab.py (itself a port of noclip OcarinaOfTime3D/csab.ts, Ocarina
// subversion-3 path). Pure C++ (no SoH/LUS deps). Produces per-bone skinning
// matrices for a frame, which Cmb::buildDrawGroupsSkinned() applies. The matrix
// convention (T.Rz.Ry.Rx.S) is identical to Cmb's bind-pose computeBoneMatrices,
// so with anim==rest the skin matrices are identity and the bind-pose render is
// unchanged (the property the renderer relies on). Verified against ge1_s_wait.
#pragma once
#include <cstdint>
#include <vector>
#include <array>
#include <string>

namespace Zelda3D {

class Cmb;

class Csab {
  public:
    explicit Csab(std::vector<uint8_t> data);
    bool ok() const { return mOk; }
    const std::string& error() const { return mErr; }

    int duration() const { return mDuration; }     // frame count (raw+1)
    int boneCount() const { return mBoneCount; }
    int animNodeCount() const { return (int)mNodes.size(); }

    // VERIFICATION helper (REPL `boneinfo`, debug_journal 2026-07-15-epona-title-animation):
    // per-bone ANIMATED LOCAL TRS at `frame`, in the SAME sampling the renderer uses
    // (sampleLocalTRS -> the exact rest-fallback + static-translation rules skinMatrices applies).
    // Lets a bone-for-bone quantitative diff against the OoT3D oracle's own live limb-pose table
    // (harness `titleactors a`, TITLE_POSE_TABLE_VA) without going through world matrices — so a
    // divergence localizes to a specific bone's local rotation, not a propagated parent transform.
    struct BoneLocal {
        int id = 0;
        int parent = -1;
        float t[3] = { 0, 0, 0 };
        float r[3] = { 0, 0, 0 }; // radians, local euler (Rz.Ry.Rx order per this class' convention)
        float s[3] = { 1, 1, 1 };
    };
    void localTransforms(const Cmb& model, float frame, std::vector<BoneLocal>& out) const;

    // Per-bone-id skinning matrix at `frame` = animWorld(bone) . inverse(bindWorld).
    // out is sized to match model.boneMatrices(); identity for any bone without anim.
    // boneRotDelta (optional): 3 radians per bone id (rX,rY,rZ) ADDED to that bone's animated
    // LOCAL rotation before world-composing — used to replay a procedural per-limb rotation the
    // N64 actor applies in an OverrideLimbDraw (e.g. the cucco wing-flap) onto the OoT3D bones.
    // nullptr / deltaCount 0 = no delta (the static-pose property is preserved).
    // bonePostRot (optional): 9 floats per bone id (row-major 3x3) POST-MULTIPLIED onto that bone's
    // animated local rotation (R·Rpost), replicating an actor OverrideLimbDraw's MTXMODE_APPLY (e.g.
    // En_Ko/En_Sa head/torso tracking) on the OoT3D rig. Propagates to children via the hierarchy.
    void skinMatrices(const Cmb& model, float frame, std::vector<std::array<float, 16>>& out,
                      const float* boneRotDelta = nullptr, int deltaCount = 0,
                      const float* bonePostRot = nullptr, int postCount = 0) const;

    // Per-bone-id animated world matrix at `frame` (rest TRS overridden by tracks).
    void animatedBoneWorld(const Cmb& model, float frame, std::vector<std::array<float, 16>>& out,
                           const float* boneRotDelta = nullptr, int deltaCount = 0,
                           const float* bonePostRot = nullptr, int postCount = 0) const;

    // MORPH (anim-transition cross-fade, the N64 SkelAnime model — see docs/anim_system.md "THE
    // MORPH"). Blends this (INCOMING) clip at `frameIn` toward the OUTGOING clip's frozen pose at
    // `frameOut`, per-bone in LOCAL space: pose = lerp(incoming, outgoing, weight). `weight` is the
    // N64 `skelAnime->morphWeight` (1.0 = fully outgoing on the transition frame, ramping linearly
    // to 0.0 = fully incoming). Both clips drive the SAME model (same Cmb). boneRotDelta applies on
    // top of the blended local rotation, identically to skinMatrices.
    void skinMatricesMorph(const Cmb& model, float frameIn, const Csab& outgoing, float frameOut,
                           float weight, std::vector<std::array<float, 16>>& out,
                           const float* boneRotDelta = nullptr, int deltaCount = 0,
                           const float* bonePostRot = nullptr, int postCount = 0) const;
    void animatedBoneWorldMorph(const Cmb& model, float frameIn, const Csab& outgoing, float frameOut,
                                float weight, std::vector<std::array<float, 16>>& out,
                                const float* boneRotDelta, int deltaCount,
                                const float* bonePostRot, int postCount) const;

    // TWO-SOURCE per-limb blend (the N64 sUpperBodyLimbCopyMap model, z_player.c:397 — used for
    // carry-WALK: lower body plays the locomotion clip, upper body plays the carry pose). `this` is
    // the LOWER clip (driving root/waist/legs), `upper` the UPPER clip (driving chest/head/arms);
    // for each bone id, upperMask[id] != 0 selects `upper`, else `this`. Both clips drive the SAME
    // rig (same Cmb). Each bone's LOCAL TRS comes from its selected source, then the normal
    // parent-multiply composes the hierarchy — so an upper bone hangs off the lower body's posed
    // pelvis exactly as the N64 copy-map result does. Frames are sampled independently (frameLower
    // on this, frameUpper on upper). boneRotDelta/bonePostRot apply to all bones as in skinMatrices.
    void skinMatricesTwoSource(const Cmb& model, float frameLower, const Csab& upper, float frameUpper,
                               const unsigned char* upperMask, int maskCount,
                               std::vector<std::array<float, 16>>& out,
                               const float* boneRotDelta = nullptr, int deltaCount = 0,
                               const float* bonePostRot = nullptr, int postCount = 0) const;
    void animatedBoneWorldTwoSource(const Cmb& model, float frameLower, const Csab& upper,
                                    float frameUpper, const unsigned char* upperMask, int maskCount,
                                    std::vector<std::array<float, 16>>& out,
                                    const float* boneRotDelta = nullptr, int deltaCount = 0,
                                    const float* bonePostRot = nullptr, int postCount = 0) const;

  private:
    // Sample a bone's animated LOCAL transform (TRS) at `fr` from this clip's tracks, falling back
    // to the rest TRS where a track is absent (or is a static non-root translation bake — see the
    // note in animatedBoneWorld). Shared by the single-clip and morph world builders.
    void sampleLocalTRS(int boneId, bool nonRoot, const float restT[3], const float restR[3],
                        const float restS[3], float fr, float t[3], float r[3], float s[3]) const;

    enum { CONSTANT = 0, LINEAR = 1, HERMITE = 2 };
    struct Keyframe { float time, value, tangentIn, tangentOut; };
    struct Track {
        int type = CONSTANT;
        int timeStart = 0, timeEnd = 1;
        bool present = false;
        bool constant = false; // present but all keyframe values equal -> a static (non-animated) bake
        std::vector<Keyframe> frames;
    };
    struct AnimNode {
        int boneIndex = 0;
        bool isRotInt16 = false;
        Track tracks[9]; // tX tY tZ rX rY rZ sX sY sZ
    };

    bool mOk = false;
    std::string mErr;
    std::vector<uint8_t> mData;

    int mDuration = 1;
    int mBoneCount = 0;
    std::vector<int16_t> mBoneToAnim;
    std::vector<AnimNode> mNodes;

    Track parseTrack(uint32_t o, bool isRotInt16) const;
    AnimNode parseAnod(uint32_t o) const;

    const AnimNode* nodeForBone(int boneId) const;
    static float sampleTrack(const Track& t, float frame, bool rotation);
    float animFrame(float frame) const;
};

// Rest-pose skin matrices with procedural per-bone overrides — NO CSAB clip. Composes each bone's
// world from its CMB REST TRS (no animation tracks) and converts to skinMatrix = animWorld·inverse(bind),
// applying the SAME boneRotDelta (local-euler add, radians) and bonePostRot (MTXMODE_APPLY 3x3) channels
// as Csab::skinMatrices. For procedurally-posed static models that have no CSAB (e.g. En_Door's panel
// swing, ported from N64 EnDoor_OverrideLimbDraw which rotates the panel limb by the live open angle).
void restPoseSkinMatrices(const Cmb& model, std::vector<std::array<float, 16>>& out,
                          const float* boneRotDelta = nullptr, int deltaCount = 0,
                          const float* bonePostRot = nullptr, int postCount = 0);

} // namespace Zelda3D
