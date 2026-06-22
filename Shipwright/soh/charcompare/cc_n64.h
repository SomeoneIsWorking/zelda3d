// charcompare — N64 (OoT) viewport.
//
// Renders an N64 OoT character through libultraship's Fast3D, the real engine path:
// load the skeleton + animation + limb display-list resources from oot.o2r, pose the
// skeleton (a reimplementation of the SkelAnime_DrawFlex walk), and emit a Fast3D
// display list of per-limb matrices + gSPDisplayList(limb DL). Side-by-side with the
// 3DS model (cc_3ds) for the N64<->3DS anim-map comparison.
#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <fast/types.h>                // MtxF, Mtx
#include <libultraship/libultra/gbi.h> // Gfx, Vp

#include "cc_3ds.h" // cc::DlistKeys (shared dlist-key storage)

namespace cc {

// One loaded N64 character: the resolved skeleton header, the current animation, and
// framing info. The skeleton/anim are kept alive (their resolved pointers / OTR-path
// strings must outlive every interp->Run).
struct ModelN64 {
    bool ok = false;
    std::string objectPath;  // e.g. "objects/object_geldb"
    std::string skelName;    // e.g. "gGerudoRedSkel"
    int limbCount = 0;
    void* skelHeader = nullptr; // FlexSkeletonHeader* (resolved limb pointers)
    void* anim = nullptr;       // AnimationHeader*
    std::string animName;
    int animFrameCount = 0;
    int animJointCount = 0;     // jointIndices entries in the anim (may differ from limbCount+1)
    int animFrameDataCount = 0; // frameData entries — bound for OOB-safe sampling
    std::vector<std::string> anims; // animation base names found for this object
    std::string error;
    // Auto-fit framing derived from the posed skeleton bbox (FK over joint positions).
    float fitScale = 1.0f;
    float center[3] = { 0, 0, 0 };
    // keep the resource shared_ptrs alive
    std::shared_ptr<void> skelRes, animRes;
    std::vector<std::shared_ptr<void>> limbRes;
    // Actor-provided face textures: the eye/mouth limb DLs reference raw N64 segments (0x08 eyes,
    // 0x0A mouth) that the actor normally binds each frame (En_Zl4 etc.). We're not the actor, so
    // we bind a neutral default per segment to a persistent "__OTR__<path>" string — exactly how the
    // game's gSPSegment(seg, SEGMENTED_TO_VIRTUAL(eyeTex)) works (eyeTex IS that string; the limb
    // DL's raw G_SETTIMG resolves the segment, sees the OTR signature, and loads the texture).
    // Without this the segment is unbound -> the face renders as a VOID.
    std::vector<std::pair<int, std::string>> faceSegs; // (segment number, "__OTR__<path>")
    // Extra display lists the ACTOR draws in a PostLimbDraw callback, attached to a limb's matrix —
    // geometry that is NOT part of the skeleton and so unreachable by the skeleton walk. Each entry is
    // (limbIndex, OTR DL path); after that limb's own DL we re-load its matrix and emit this DL,
    // mirroring e.g. EnGe1_PostLimbDraw drawing gGerudoWhiteHairstyleSpikyDL on GE1_LIMB_HEAD.
    // Curated per skeleton (actor C callbacks can't be derived generically). limbIndex is 0-based
    // (charcompare's limb numbering = SkelAnime limb index - 1, since SkelAnime limb 0 = LIMB_NONE).
    std::vector<std::pair<int, std::string>> extraLimbDLs;
    // Model-space (FK-transformed) geometry bbox, measured once via the interpreter (Cc_BboxMeasure*).
    // Framing scales the DEPTH axis to this true mesh extent — the joint bbox is near-flat in Z and
    // crushes depth precision, z-fighting the head/face. meshMeasured stays false until the first
    // measured frame; framing falls back to the joint bbox until then.
    bool meshMeasured = false;
    float meshMin[3] = { 0, 0, 0 }, meshMax[3] = { 0, 0, 0 };
    // Stable framing, computed ONCE from the geometry (mesh) bbox and reused for every animation
    // frame. Framing the model per-frame from the animated joint bbox made it rescale/recenter as
    // limbs moved ("models abruptly change size / move around during animation"); caching a fixed
    // geometry-based frame keeps the model put and constant-sized (and size-matched to the 3DS,
    // which also frames by its geometry bbox). frCtr/frHalf are in model space.
    bool framingCached = false;
    float frCtr[3] = { 0, 0, 0 }, frHalf[3] = { 0, 0, 0 };
};

// Register the resource factories the N64 path needs (Fast DisplayList/Vertex/Texture/
// Matrix + SOH Skeleton/SkeletonLimb/Animation/PlayerAnimation). Call once after the
// Context's window/resource-manager are initialized. Safe to call repeatedly.
void RegisterN64Factories();

// Load an N64 character: object resource folder + skeleton symbol name + the list of
// animation symbol names (from tools/skeldata/n64_anims.json). Loads + resolves the
// skeleton and enumerates anims; the first anim is selected.
ModelN64 LoadN64(const std::string& objectPath, const std::string& skelName,
                 const std::vector<std::string>& animNames);

// List the skeleton symbol(s) (basenames ending in "Skel") inside an object's OTR folder.
std::vector<std::string> FindN64Skeletons(const std::string& objectPath);

// Load an N64 character by object folder alone, discovering its skeleton (see FindN64Skeletons).
ModelN64 LoadN64Auto(const std::string& objectPath, const std::vector<std::string>& animNames);

// Select the current animation (by symbol name) and recompute frame count + framing.
void SetAnimN64(ModelN64& m, const std::string& animName);

// Append the posed skeleton's draw commands to `dl` for animation frame `frame`, into the
// given viewport (rx/ry/rz degrees orient the model like cc_3ds). keys holds the per-limb
// MtxF storage (must outlive interp->Run).
void EmitDlistN64(ModelN64& m, float frame, std::vector<Gfx>& dl, std::unordered_map<Mtx*, MtxF>& mtx,
                  DlistKeys& keys, float rx, float ry, float rz, const Rect& vp);

} // namespace cc
