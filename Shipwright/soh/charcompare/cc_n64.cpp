// charcompare — N64 (OoT) viewport. See cc_n64.h.
#include "cc_n64.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <ship/Context.h>
#include <ship/resource/ResourceManager.h>
#include <ship/resource/ResourceLoader.h>
#include <ship/resource/File.h> // RESOURCE_FORMAT_BINARY
#include <ship/resource/archive/ArchiveManager.h> // ListFiles (skeleton discovery)

#include <fast/resource/ResourceType.h>
#include <fast/resource/factory/DisplayListFactory.h>
#include <fast/resource/factory/VertexFactory.h>
#include <fast/resource/factory/TextureFactory.h>
#include <fast/resource/factory/MatrixFactory.h>

#include "soh/resource/type/SohResourceType.h"
#include "soh/resource/type/Skeleton.h"
#include "soh/resource/type/SkeletonLimb.h"
#include "soh/resource/type/Animation.h"
#include "soh/resource/importer/SkeletonFactory.h"
#include "soh/resource/importer/SkeletonLimbFactory.h"
#include "soh/resource/importer/AnimationFactory.h"
#include "soh/resource/importer/ArrayFactory.h"

#include <fast/interpreter.h> // SCREEN_WIDTH/HEIGHT

namespace cc {

// --- N64 MtxF math (column-major mf[col][row], M*v convention, as OoT sys_matrix) -------------
static float sinS(int16_t a) { return sinf((float)a * (3.14159265358979f / 32768.0f)); }
static float cosS(int16_t a) { return cosf((float)a * (3.14159265358979f / 32768.0f)); }

static void matIdentity(MtxF& m) {
    memset(m.mf, 0, sizeof(m.mf));
    m.mf[0][0] = m.mf[1][1] = m.mf[2][2] = m.mf[3][3] = 1.0f;
}

// Verbatim port of Matrix_TranslateRotateZYX (sys_matrix.c): cmf = cmf * T(t) * Rz * Ry * Rx.
static void matTranslateRotateZYX(MtxF* cmf, const float t[3], const int16_t r[3]) {
    float sin = sinS(r[2]), cos = cosS(r[2]);
    float temp1, temp2;
    temp1 = cmf->xx; temp2 = cmf->xy;
    cmf->xw += temp1 * t[0] + temp2 * t[1] + cmf->xz * t[2];
    cmf->xx = temp1 * cos + temp2 * sin; cmf->xy = temp2 * cos - temp1 * sin;
    temp1 = cmf->yx; temp2 = cmf->yy;
    cmf->yw += temp1 * t[0] + temp2 * t[1] + cmf->yz * t[2];
    cmf->yx = temp1 * cos + temp2 * sin; cmf->yy = temp2 * cos - temp1 * sin;
    temp1 = cmf->zx; temp2 = cmf->zy;
    cmf->zw += temp1 * t[0] + temp2 * t[1] + cmf->zz * t[2];
    cmf->zx = temp1 * cos + temp2 * sin; cmf->zy = temp2 * cos - temp1 * sin;
    temp1 = cmf->wx; temp2 = cmf->wy;
    cmf->ww += temp1 * t[0] + temp2 * t[1] + cmf->wz * t[2];
    cmf->wx = temp1 * cos + temp2 * sin; cmf->wy = temp2 * cos - temp1 * sin;
    if (r[1] != 0) {
        sin = sinS(r[1]); cos = cosS(r[1]);
        temp1 = cmf->xx; temp2 = cmf->xz; cmf->xx = temp1 * cos - temp2 * sin; cmf->xz = temp1 * sin + temp2 * cos;
        temp1 = cmf->yx; temp2 = cmf->yz; cmf->yx = temp1 * cos - temp2 * sin; cmf->yz = temp1 * sin + temp2 * cos;
        temp1 = cmf->zx; temp2 = cmf->zz; cmf->zx = temp1 * cos - temp2 * sin; cmf->zz = temp1 * sin + temp2 * cos;
        temp1 = cmf->wx; temp2 = cmf->wz; cmf->wx = temp1 * cos - temp2 * sin; cmf->wz = temp1 * sin + temp2 * cos;
    }
    if (r[0] != 0) {
        sin = sinS(r[0]); cos = cosS(r[0]);
        temp1 = cmf->xy; temp2 = cmf->xz; cmf->xy = temp1 * cos + temp2 * sin; cmf->xz = temp2 * cos - temp1 * sin;
        temp1 = cmf->yy; temp2 = cmf->yz; cmf->yy = temp1 * cos + temp2 * sin; cmf->yz = temp2 * cos - temp1 * sin;
        temp1 = cmf->zy; temp2 = cmf->zz; cmf->zy = temp1 * cos + temp2 * sin; cmf->zz = temp2 * cos - temp1 * sin;
        temp1 = cmf->wy; temp2 = cmf->wz; cmf->wy = temp1 * cos + temp2 * sin; cmf->wz = temp2 * cos - temp1 * sin;
    }
}

// Transform a model-space point by an MtxF (column-major, M*v) — for FK bbox.
static void matApply(const MtxF& m, const float v[3], float out[3]) {
    for (int row = 0; row < 3; row++)
        out[row] = m.mf[0][row] * v[0] + m.mf[1][row] * v[1] + m.mf[2][row] * v[2] + m.mf[3][row];
}

// --- Factory registration --------------------------------------------------------------------
static bool g_registered = false;
void RegisterN64Factories() {
    if (g_registered) return;
    auto loader = Ship::Context::GetRawInstance()->GetResourceManager()->GetResourceLoader();
    using namespace Fast;
    loader->RegisterResourceFactory(std::make_shared<ResourceFactoryBinaryTextureV0>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(ResourceType::Texture), 0);
    loader->RegisterResourceFactory(std::make_shared<ResourceFactoryBinaryTextureV1>(), RESOURCE_FORMAT_BINARY,
                                    "Texture", static_cast<uint32_t>(ResourceType::Texture), 1);
    loader->RegisterResourceFactory(std::make_shared<ResourceFactoryBinaryVertexV0>(), RESOURCE_FORMAT_BINARY, "Vertex",
                                    static_cast<uint32_t>(ResourceType::Vertex), 0);
    loader->RegisterResourceFactory(std::make_shared<ResourceFactoryBinaryDisplayListV0>(), RESOURCE_FORMAT_BINARY,
                                    "DisplayList", static_cast<uint32_t>(ResourceType::DisplayList), 0);
    loader->RegisterResourceFactory(std::make_shared<ResourceFactoryBinaryMatrixV0>(), RESOURCE_FORMAT_BINARY, "Matrix",
                                    static_cast<uint32_t>(ResourceType::Matrix), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySkeletonV0>(), RESOURCE_FORMAT_BINARY,
                                    "Skeleton", static_cast<uint32_t>(SOH::ResourceType::SOH_Skeleton), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinarySkeletonLimbV0>(), RESOURCE_FORMAT_BINARY,
                                    "SkeletonLimb", static_cast<uint32_t>(SOH::ResourceType::SOH_SkeletonLimb), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryAnimationV0>(), RESOURCE_FORMAT_BINARY,
                                    "Animation", static_cast<uint32_t>(SOH::ResourceType::SOH_Animation), 0);
    loader->RegisterResourceFactory(std::make_shared<SOH::ResourceFactoryBinaryArrayV0>(), RESOURCE_FORMAT_BINARY,
                                    "Array", static_cast<uint32_t>(SOH::ResourceType::SOH_Array), 0);
    g_registered = true;
    fprintf(stderr, "[cc_n64] registered resource factories\n");
}

static Ship::ResourceManager* rm() { return Ship::Context::GetRawInstance()->GetResourceManager().get(); }

// Discover the N64 skeleton symbol(s) inside an object's OTR folder. The animmap gives the
// object name but not the skeleton symbol; SkeletonHeader resources are named "...Skel" (the
// per-limb resources are "...SkelLimbsLimb_...DL_..."), so we list the folder and keep the
// basenames that END in "Skel". Returned sorted; the caller tries them until one loads.
std::vector<std::string> FindN64Skeletons(const std::string& objectPath) {
    RegisterN64Factories();
    std::vector<std::string> out;
    auto am = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    auto list = am->ListFiles(objectPath + "/*");
    for (const auto& p : *list) {
        size_t slash = p.find_last_of('/');
        std::string name = (slash == std::string::npos) ? p : p.substr(slash + 1);
        // A SkeletonHeader resource contains "Skel" but is not a limb ("...SkelLimbsLimb...") or a
        // display list ("...DL..."). Names vary: gGerudoRedSkel (suffix) or object_daiku_Skel_007958
        // (ZAPD auto-name), so match the substring, not just the suffix.
        if (name.find("Skel") == std::string::npos) continue;
        if (name.find("Limb") != std::string::npos || name.find("DL") != std::string::npos) continue;
        out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// Scan an object's textures for the actor-animated EYE/MOUTH textures and bind a neutral default
// per face segment. OoT actors that animate a face (En_Zl4, En_Md, Malon, ...) draw eye/mouth via
// raw N64 segments the ACTOR sets each frame: by overwhelming convention segment 0x08 = right eye,
// 0x09 = left eye, 0x0A = mouth (see z_en_zl4.c:1305 — eyeTex on 0x08/0x09, mouthTex on 0x0A). The
// limb DLs G_SETTIMG those segments; unbound, the face is a VOID (the eye/mouth textures live in the
// object but are never reached). We pick a neutral expression (open eyes, neutral mouth) and bind it.
// NOTE: this assumes the canonical two-eyes+mouth layout. Outlier actors using a different segment
// for the mouth would mis-map, but those slots were void before, so this never regresses a face.
static void scanFaceTextures(ModelN64& m) {
    auto am = Ship::Context::GetRawInstance()->GetResourceManager()->GetArchiveManager();
    auto list = am->ListFiles(m.objectPath + "/*");
    auto lower = [](std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    };
    // Collect candidate eye / mouth texture basenames (skip the palette/TLUT companions "TLUT"/"Pal").
    std::vector<std::string> eyes, mouths;
    for (const auto& p : *list) {
        size_t slash = p.find_last_of('/');
        std::string name = (slash == std::string::npos) ? p : p.substr(slash + 1);
        std::string ln = lower(name);
        if (ln.find("tex") == std::string::npos) continue; // textures only
        if (ln.find("tlut") != std::string::npos || ln.find("pal") != std::string::npos) continue;
        if (ln.find("eye") != std::string::npos) eyes.push_back(name);
        else if (ln.find("mouth") != std::string::npos) mouths.push_back(name);
    }
    // Pick the most "neutral" candidate by name preference, else the first.
    auto pick = [&](const std::vector<std::string>& v, std::initializer_list<const char*> prefer) -> std::string {
        for (const char* key : prefer)
            for (const auto& s : v)
                if (lower(s).find(key) != std::string::npos) return s;
        return v.empty() ? std::string() : v.front();
    };
    std::string eye = pick(eyes, { "open", "normal", "default" });
    std::string mouth = pick(mouths, { "neutral", "normal", "close", "default" });

    m.faceSegs.clear();
    if (!eye.empty()) {
        std::string otr = "__OTR__" + m.objectPath + "/" + eye;
        m.faceSegs.emplace_back(0x08, otr); // right eye
        m.faceSegs.emplace_back(0x09, otr); // left eye
    }
    if (!mouth.empty())
        m.faceSegs.emplace_back(0x0A, "__OTR__" + m.objectPath + "/" + mouth);
    if (!m.faceSegs.empty())
        fprintf(stderr, "[cc_n64] face textures: eye='%s' mouth='%s'\n", eye.c_str(), mouth.c_str());
}

// Curated table of actor-drawn extra geometry: DLs that an actor emits in a PostLimbDraw callback,
// attached to a limb's matrix, that are NOT part of the skeleton (so the skeleton walk never reaches
// them). These exist only in actor C code and cannot be derived from the object data, so they are
// hand-listed here, keyed by skeleton symbol. limbIndex is 0-based (= SkelAnime limb index - 1).
//   - Gerudo (gGerudoWhiteSkel, object_ge1): EnGe1_PostLimbDraw draws sHairstyleDLists[hairstyle] on
//     GE1_LIMB_HEAD (SkelAnime 15 -> 0-based 14). The generic zelda_ge1.zar is the spiky red hair.
static void scanExtraLimbDLs(ModelN64& m) {
    struct Extra { const char* skel; int limb0; const char* dlObject; const char* dl; };
    static const Extra kExtras[] = {
        { "gGerudoWhiteSkel", 14, "objects/object_ge1", "gGerudoWhiteHairstyleSpikyDL" },
    };
    for (const auto& e : kExtras) {
        if (m.skelName != e.skel) continue;
        if (e.limb0 < 0 || e.limb0 >= m.limbCount) continue; // skeleton shape changed — skip safely
        m.extraLimbDLs.emplace_back(e.limb0, std::string(e.dlObject) + "/" + e.dl);
        fprintf(stderr, "[cc_n64] extra limb DL: limb %d -> %s/%s\n", e.limb0, e.dlObject, e.dl);
    }
}

void SetAnimN64(ModelN64& m, const std::string& animName) {
    if (animName.empty()) return;
    std::string path = m.objectPath + "/" + animName;
    auto res = rm()->LoadResource(path);
    if (!res) { fprintf(stderr, "[cc_n64] anim not found: %s\n", path.c_str()); return; }
    m.animRes = res;
    m.anim = res->GetRawPointer();
    m.animName = animName;
    auto* ah = (SOH::AnimationHeader*)m.anim;
    m.animFrameCount = ah->common.frameCount;
    // Actual array sizes from the resource — used to bound sampling. animmap can pair an anim with a
    // skeleton of a DIFFERENT limb count (multi-skeleton bosses like Volvagia: body skel + arm anim),
    // so jointIndices may be shorter than limbCount+1; reading past it would crash.
    m.animJointCount = m.limbCount + 1;
    m.animFrameDataCount = 0;
    if (auto anim = std::dynamic_pointer_cast<SOH::Animation>(res)) {
        m.animJointCount = (int)anim->rotationIndices.size();
        m.animFrameDataCount = (int)anim->rotationValues.size();
    }
}

ModelN64 LoadN64(const std::string& objectPath, const std::string& skelName,
                 const std::vector<std::string>& animNames) {
    RegisterN64Factories();
    ModelN64 m;
    m.objectPath = objectPath;
    m.skelName = skelName;
    m.anims = animNames;

    std::string skelPath = objectPath + "/" + skelName;
    auto res = rm()->LoadResource(skelPath);
    if (!res) { m.error = "skeleton not found: " + skelPath; return m; }
    m.skelRes = res;
    m.skelHeader = res->GetRawPointer();
    auto* fsh = (SOH::FlexSkeletonHeader*)m.skelHeader;
    m.limbCount = fsh->sh.limbCount;
    m.ok = (m.limbCount > 0 && fsh->sh.segment != nullptr);
    if (!m.ok) { m.error = "skeleton has no limbs"; return m; }

    // This renderer only handles RIGID (Standard/LOD) limbs. Skin limbs (animated skin meshes —
    // e.g. Epona, the player) have an incompatible limb struct that reads as garbage here, and
    // Curve skeletons use a different draw path entirely. Refuse them up front (the 3DS side
    // likewise skips skinned models) instead of dereferencing garbage limb pointers later.
    if (auto skel = std::dynamic_pointer_cast<SOH::Skeleton>(res)) {
        if (skel->type == SOH::SkeletonType::Curve || skel->limbType == SOH::LimbType::Skin ||
            skel->limbTableType == SOH::LimbType::Skin || skel->limbType == SOH::LimbType::Curve) {
            m.ok = false;
            m.error = "skinned/curve skeleton (N64 rigid renderer unsupported)";
            fprintf(stderr, "[cc_n64] %s: %s\n", skelPath.c_str(), m.error.c_str());
            return m;
        }
    }

    scanFaceTextures(m); // bind neutral eye/mouth textures to the actor face segments (0x08-0x0A)
    scanExtraLimbDLs(m); // actor PostLimbDraw extras (e.g. Gerudo hair) — see ModelN64::extraLimbDLs

    if (!animNames.empty()) SetAnimN64(m, animNames[0]);

    fprintf(stderr, "[cc_n64] loaded %s: %d limbs, skeletonType=%d, %zu anims, anim '%s' (%d frames)\n",
            skelPath.c_str(), m.limbCount, (int)fsh->sh.skeletonType, animNames.size(), m.animName.c_str(),
            m.animFrameCount);
    return m;
}

// Load an N64 character given only its object folder: discover the skeleton symbol(s) and try
// each until one loads with limbs (most objects have exactly one). animNames are the N64 anim
// symbols to select from (from the charcompare index).
ModelN64 LoadN64Auto(const std::string& objectPath, const std::vector<std::string>& animNames) {
    auto skels = FindN64Skeletons(objectPath);
    if (skels.empty()) {
        ModelN64 m;
        m.objectPath = objectPath;
        m.error = "no skeleton found in " + objectPath;
        fprintf(stderr, "[cc_n64] %s\n", m.error.c_str());
        return m;
    }
    ModelN64 last;
    for (const auto& s : skels) {
        ModelN64 m = LoadN64(objectPath, s, animNames);
        if (m.ok) return m;
        last = m;
    }
    return last; // carries the last attempt's error
}

// Sample the animation into jointTable[limbCount+1] (out[0]=root pos, out[1..]=rotations).
static void sampleAnim(const ModelN64& m, float frame, std::vector<std::array<int16_t, 3>>& out) {
    out.assign(m.limbCount + 1, { 0, 0, 0 });
    if (!m.anim || m.animFrameCount <= 0) return;
    auto* ah = (SOH::AnimationHeader*)m.anim;
    const int16_t* fd = ah->frameData;
    const SOH::JointIndex* ji = ah->jointIndices;
    uint16_t smax = ah->staticIndexMax;
    int f = ((int)frame % m.animFrameCount + m.animFrameCount) % m.animFrameCount;
    // Bound every read: the anim may have FEWER joints/frameData than this skeleton needs (a
    // mismatched animmap pairing on a multi-skeleton boss). Limbs past the anim's joint count keep
    // rotation 0; a frameData index past the array yields 0 instead of an OOB read.
    int njoints = std::min(m.limbCount + 1, m.animJointCount);
    int nfd = m.animFrameDataCount;
    auto sample = [&](uint16_t idx) -> int16_t {
        int e = (idx >= smax) ? (idx + f) : idx;
        return (nfd <= 0 || e < 0 || e >= nfd) ? 0 : fd[e];
    };
    for (int i = 0; i < njoints; i++) {
        out[i][0] = sample(ji[i].x);
        out[i][1] = sample(ji[i].y);
        out[i][2] = sample(ji[i].z);
    }
    if (getenv("CC_ANIMDBG")) {
        fprintf(stderr, "[animdbg] frame=%d smax=%d nfd=%d njoints=%d\n", f, smax, nfd, njoints);
        for (int i = 0; i < njoints; i++)
            fprintf(stderr, "   joint %2d  idx(%5u,%5u,%5u) -> rot(%6d,%6d,%6d)\n", i, ji[i].x, ji[i].y, ji[i].z,
                    out[i][0], out[i][1], out[i][2]);
    }
    // Diagnostic: zero all limb rotations (keep root position) to render the rest skeleton —
    // isolates the matrix/jointPos composition from the animation sampling.
    if (getenv("CC_N64_NOANIM"))
        for (int i = 1; i < m.limbCount + 1; i++) out[i] = { 0, 0, 0 };
}

// Convert an MtxF to the N64 fixed-point Mtx layout the interpreter reads for a segmented matrix
// (16 int32: [0..7] integer parts, [8..15] frac parts). Inverse of GfxSpMatrix's fixed-point read,
// so the interpreter recovers mf.mf[i][j] exactly.
static void guMtxF2L(const MtxF& mf, int32_t* out) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 2; j++) {
            int e1 = (int)(mf.mf[i][2 * j] * 65536.0f);
            int e2 = (int)(mf.mf[i][2 * j + 1] * 65536.0f);
            out[i * 2 + j] = (int)((e1 & 0xffff0000u) | (((unsigned)e2 >> 16) & 0xffffu));
            out[8 + i * 2 + j] = (int)((((unsigned)e1 << 16) & 0xffff0000u) | ((unsigned)e2 & 0xffffu));
        }
    }
}

// Limb-space transform helper shared by the two passes: cur = parent ∘ (T(pos) · R(rot)).
static MtxF limbMatrix(ModelN64& m, int limbIndex, const MtxF& parent,
                       const std::vector<std::array<int16_t, 3>>& joints) {
    auto** seg = (SOH::StandardLimb**)((SOH::FlexSkeletonHeader*)m.skelHeader)->sh.segment;
    SOH::StandardLimb* limb = seg[limbIndex];
    float pos[3] = { (float)limb->jointPos.x, (float)limb->jointPos.y, (float)limb->jointPos.z };
    if (limbIndex == 0) { pos[0] = joints[0][0]; pos[1] = joints[0][1]; pos[2] = joints[0][2]; }
    const auto& r = joints[limbIndex + 1];
    int16_t rot[3] = { r[0], r[1], r[2] };
    MtxF cur = parent;
    matTranslateRotateZYX(&cur, pos, rot);
    return cur;
}

// Pass 1: FK — fill world[limbIndex] for every limb (so segment-0x0D refs to ANY limb resolve).
static void computeWorld(ModelN64& m, int limbIndex, const MtxF& parent,
                         const std::vector<std::array<int16_t, 3>>& joints, std::vector<MtxF>& world) {
    if (limbIndex < 0 || limbIndex >= m.limbCount) return; // malformed/unsupported skeleton: don't deref OOB
    auto** seg = (SOH::StandardLimb**)((SOH::FlexSkeletonHeader*)m.skelHeader)->sh.segment;
    SOH::StandardLimb* limb = seg[limbIndex];
    MtxF cur = limbMatrix(m, limbIndex, parent, joints);
    world[limbIndex] = cur;
    if (limb->child != LIMB_DONE) computeWorld(m, limb->child, cur, joints, world);
    if (limb->sibling != LIMB_DONE) computeWorld(m, limb->sibling, parent, joints, world);
}

// Assign each dList-bearing limb a matrix SLOT in SkelAnime_DrawFlex draw order (root, child,
// sibling) — matching the `mtx++` advance, so the segment-0x0D matrix array is indexed the way
// the limb DLs (and their cross-limb references) expect. slot[limbIndex] = -1 if no dList.
static void assignSlots(ModelN64& m, int limbIndex, int& counter, std::vector<int>& slot) {
    if (limbIndex < 0 || limbIndex >= m.limbCount) return;
    auto** seg = (SOH::StandardLimb**)((SOH::FlexSkeletonHeader*)m.skelHeader)->sh.segment;
    SOH::StandardLimb* limb = seg[limbIndex];
    slot[limbIndex] = (limb->dList != nullptr) ? counter++ : -1;
    if (limb->child != LIMB_DONE) assignSlots(m, limb->child, counter, slot);
    if (limb->sibling != LIMB_DONE) assignSlots(m, limb->sibling, counter, slot);
}

// Pass 2: emit a matrix-load (from the segment-0x0D array slot) + the limb's geometry DL per limb.
static void emitLimbs(ModelN64& m, int limbIndex, const std::vector<int>& slot, std::vector<Gfx>& dl) {
    if (limbIndex < 0 || limbIndex >= m.limbCount) return;
    auto** seg = (SOH::StandardLimb**)((SOH::FlexSkeletonHeader*)m.skelHeader)->sh.segment;
    SOH::StandardLimb* limb = seg[limbIndex];
    static const char* onlyEnv = getenv("CC_N64_ONLYLIMB");
    bool drawThis = !onlyEnv || atoi(onlyEnv) == limbIndex;
    // Guard: our renderer assumes Standard/LOD RIGID limbs, where dList is an "__OTR__" path string
    // (a valid user-space heap pointer). SKIN limbs (e.g. Epona / player-style animated skin meshes)
    // have an incompatible struct, so reading dList at the StandardLimb offset yields garbage — a
    // non-canonical pointer that crashes strlen. Skip those limbs (skin skeletons are unsupported,
    // mirroring the 3DS side skipping skinned models) instead of crashing.
    auto dListIsValidString = [](const void* p) {
        uintptr_t v = (uintptr_t)p;
        return v >= 0x1000 && v < 0x0000800000000000ULL; // within x86-64 user address space
    };
    if (drawThis && limb->dList != nullptr && slot[limbIndex] >= 0 && dListIsValidString(limb->dList)) {
        if (getenv("CC_N64_DBG"))
            fprintf(stderr, "[cc_n64] limb %d dListPtr=%p\n", limbIndex, (void*)limb->dList);
        // limb->dList is an OTR PATH string; resolve to the DisplayList resource's Gfx* (mirrors
        // the game's gSPDisplayList wrapper). The flex limb DLs load their matrices from segment
        // 0x0D themselves; we also load this limb's own matrix as the base (like SkelAnime_DrawFlex).
        std::string p = (const char*)limb->dList;
        if (p.rfind("__OTR__", 0) == 0) p = p.substr(7);
        auto res = rm()->LoadResource(p);
        Gfx* g = res ? (Gfx*)res->GetRawPointer() : nullptr;
        if (g) {
            m.limbRes.push_back(res);
            if (getenv("CC_N64_DUMPDL")) {
                fprintf(stderr, "[cc_n64] limb %d slot %d DL %s:\n", limbIndex, slot[limbIndex], p.c_str());
                for (Gfx* c = g; c < g + 2000; c++) {
                    uintptr_t w0 = (uintptr_t)c->words.w0, w1 = (uintptr_t)c->words.w1;
                    unsigned op = (unsigned)((w0 >> 24) & 0xFF);
                    fprintf(stderr, "      op=%02X w0=%016zx w1=%016zx\n", op, w0, w1);
                    if (op == (unsigned)(G_ENDDL & 0xFF) || op == 0xDF) break;
                }
            }
            uintptr_t segMtx = 0x0D000000u | ((uintptr_t)slot[limbIndex] * 0x40u) | 1u; // segmented
            { Gfx gm = gsSPMatrix((Mtx*)segMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW); dl.push_back(gm); }
            if (!getenv("CC_N64_NODL")) { Gfx gd = gsSPDisplayList(g); dl.push_back(gd); }
            // Actor PostLimbDraw extras (e.g. Gerudo hair): drawn with THIS limb's matrix, right after
            // the limb's own DL — exactly when the game's PostLimbDraw runs (matrix stack still at the
            // limb). Re-load the slot matrix (the limb DL may have loaded other 0x0D slots internally)
            // then emit the extra DL. See ModelN64::extraLimbDLs / scanExtraLimbDLs.
            if (!getenv("CC_N64_NODL") && !getenv("CC_N64_NOEXTRA")) {
                for (const auto& [exLimb, exPath] : m.extraLimbDLs) {
                    if (exLimb != limbIndex) continue;
                    auto exRes = rm()->LoadResource(exPath);
                    Gfx* exg = exRes ? (Gfx*)exRes->GetRawPointer() : nullptr;
                    if (!exg) { fprintf(stderr, "[cc_n64] extra DL load failed: %s\n", exPath.c_str()); continue; }
                    m.limbRes.push_back(exRes);
                    { Gfx gm = gsSPMatrix((Mtx*)segMtx, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW); dl.push_back(gm); }
                    { Gfx gd = gsSPDisplayList(exg); dl.push_back(gd); }
                }
            }
        }
    }
    if (limb->child != LIMB_DONE) emitLimbs(m, limb->child, slot, dl);
    if (limb->sibling != LIMB_DONE) emitLimbs(m, limb->sibling, slot, dl);
}

void EmitDlistN64(ModelN64& m, float frame, std::vector<Gfx>& dl, std::unordered_map<Mtx*, MtxF>& mtx,
                  DlistKeys& keys, float rx, float ry, float rz, const Rect& vp) {
    if (!m.ok) return;
    std::vector<std::array<int16_t, 3>> joints;
    sampleAnim(m, frame, joints);

    // Pass 1a (identity framing): joint world matrices -> bbox for the auto-fit.
    std::vector<MtxF> worldId(m.limbCount);
    MtxF id; matIdentity(id);
    computeWorld(m, 0, id, joints, worldId);
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for (int i = 0; i < m.limbCount; i++) {
        float o[3], zero[3] = { 0, 0, 0 };
        matApply(worldId[i], zero, o);
        for (int k = 0; k < 3; k++) { lo[k] = std::min(lo[k], o[k]); hi[k] = std::max(hi[k], o[k]); }
    }
    float ctr[3], ext[3];
    for (int k = 0; k < 3; k++) {
        ctr[k] = (lo[k] + hi[k]) * 0.5f;
        ext[k] = std::max((hi[k] - lo[k]) * 0.5f, 1.0f);
    }
    // STABLE framing: once the one-time interpreter mesh measure (Cc_Bbox*) has run, frame by the
    // model's fixed GEOMETRY bbox — cached so it never changes with the animated pose. This is what
    // stops the model rescaling/drifting as it animates, and matches how the 3DS side frames (by its
    // geometry bbox), so the two halves are the same size. Until the measure completes (frame 0), fall
    // back to this frame's joint bbox (one transient frame). worldId below still uses the live joints,
    // so the limbs animate normally — only the camera frame F is held constant.
    if (m.meshMeasured) {
        if (!m.framingCached) {
            for (int k = 0; k < 3; k++) {
                m.frCtr[k] = (m.meshMin[k] + m.meshMax[k]) * 0.5f;
                m.frHalf[k] = std::max((m.meshMax[k] - m.meshMin[k]) * 0.5f, 1.0f);
            }
            m.framingCached = true;
        }
        for (int k = 0; k < 3; k++) { ctr[k] = m.frCtr[k]; ext[k] = m.frHalf[k]; }
    }
    if (getenv("CC_N64_DBG")) {
        fprintf(stderr, "[cc_n64] joint bbox x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f] ctr(%.0f,%.0f,%.0f) ext(%.0f,%.0f,%.0f)\n",
                lo[0], hi[0], lo[1], hi[1], lo[2], hi[2], ctr[0], ctr[1], ctr[2], ext[0], ext[1], ext[2]);
        fprintf(stderr, "[cc_n64] animJointCount=%d animFrameDataCount=%d limbCount=%d\n", m.animJointCount,
                m.animFrameDataCount, m.limbCount);
        for (int ji : { 0, 1, 2, 10, 17 })
            if (ji < (int)joints.size())
                fprintf(stderr, "   joints[%2d] = (%d, %d, %d)\n", ji, joints[ji][0], joints[ji][1], joints[ji][2]);
        auto** seg = (SOH::StandardLimb**)((SOH::FlexSkeletonHeader*)m.skelHeader)->sh.segment;
        for (int i = 0; i < m.limbCount; i++) {
            float o[3], zero[3] = { 0, 0, 0 }, fz[3], uz[3] = { 0, 0, 100 }, fx[3], ux[3] = { 100, 0, 0 };
            matApply(worldId[i], zero, o);
            matApply(worldId[i], uz, fz);   // where local +Z points (facing)
            matApply(worldId[i], ux, fx);   // where local +X points (left/right)
            SOH::StandardLimb* lb = seg[i];
            fprintf(stderr,
                    "   limb %2d origin(%4.0f,%4.0f,%4.0f) +Zdir(%+.2f,%+.2f,%+.2f) +Xdir(%+.2f,%+.2f,%+.2f) "
                    "child=%d sib=%d dList=%s\n",
                    i, o[0], o[1], o[2], (fz[0] - o[0]) / 100, (fz[1] - o[1]) / 100, (fz[2] - o[2]) / 100,
                    (fx[0] - o[0]) / 100, (fx[1] - o[1]) / 100, (fx[2] - o[2]) / 100, lb->child, lb->sibling,
                    lb->dList ? "Y" : "-");
        }
    }

    // Framing matrix F (column-major M*v): scale + R(rx,ry,rz), center. xComp widens clip-X for a
    // narrow (split) viewport so the model isn't squashed (see cc_3ds). The fit target is 0.62 NDC
    // (vs cc_3ds's 0.8) because this bbox is over the JOINT positions, and the limb MESH extends
    // past the joints (head/hands), so a tighter joint-fit would clip the mesh. (A proper fix would
    // bbox the transformed geometry like cc_3ds; the margin keeps the whole model on screen for now.)
    const float xComp = (float)SCREEN_WIDTH / vp.w;
    // Fit the GEOMETRY bbox (ext is the cached mesh half-extent once measured) into ~0.55 of NDC —
    // the SAME target as the 3DS side (cc_3ds CC_FIT3DS), so the two halves render the same size.
    // (Before the mesh measure completes, ext is the joint bbox for one transient frame.) CC_FIT
    // overrides.
    static float fitTarget = [] { const char* e = getenv("CC_FIT"); return e ? (float)atof(e) : 0.55f; }();
    // Frame by HEIGHT (Y) — pose-stable and matches the 3DS side, so the halves size-match. Framing
    // by max(width,height) shrank wide rest poses (arms-out) vs the other side. ext is the cached
    // geometry half-extent (joint bbox for the one frame before the mesh measure completes).
    const float fit = fitTarget / std::max(ext[1], 1.0f);
    auto rad = [](float d) { return d * 3.14159265358979f / 180.0f; };
    float cx = cosf(rad(rx)), sx = sinf(rad(rx)), cyr = cosf(rad(ry)), syr = sinf(rad(ry)), cz = cosf(rad(rz)),
          sz = sinf(rad(rz));
    float Rx[3][3] = { { 1, 0, 0 }, { 0, cx, -sx }, { 0, sx, cx } };
    float Ry[3][3] = { { cyr, 0, syr }, { 0, 1, 0 }, { -syr, 0, cyr } };
    float Rz[3][3] = { { cz, -sz, 0 }, { sz, cz, 0 }, { 0, 0, 1 } };
    auto mul3 = [](const float A[3][3], const float B[3][3], float O[3][3]) {
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) { O[i][j] = 0; for (int k = 0; k < 3; k++) O[i][j] += A[i][k] * B[k][j]; }
    };
    float Rzy[3][3], R[3][3];
    mul3(Rz, Ry, Rzy);
    mul3(Rzy, Rx, R);
    // Depth (Z) scale: fill the z-buffer with the model's TRUE geometry depth. The JOINT bbox is
    // near-flat in Z (joints sit on the central sagittal plane) so a joint-based Z scale either clips
    // the protruding mesh (face/hair stick far out in Z) or, if shrunk to avoid that, crushes depth
    // precision so the head's BACK wimple z-fights OVER the face. Instead frame Z by the measured
    // MODEL-SPACE mesh bbox (m.meshMin/Max): rotate its 8 corners by R, take the Z half-extent about
    // ctr, and scale so the model fills |clip z| ~0.45 about the 0.5 bias — full precision, no clip.
    // X/Y still use the joint fit + margin (the mesh stays on-screen there). Falls back to the
    // isotropic X/Y scale until the one-time interpreter measure (Cc_Bbox*) completes.
    float zScale = fit;
    if (m.meshMeasured) {
        float zhalf = 1.0f;
        for (int c = 0; c < 8; c++) {
            float p[3] = { (c & 1) ? m.meshMax[0] : m.meshMin[0], (c & 2) ? m.meshMax[1] : m.meshMin[1],
                           (c & 4) ? m.meshMax[2] : m.meshMin[2] };
            float rzc = R[2][0] * (p[0] - ctr[0]) + R[2][1] * (p[1] - ctr[1]) + R[2][2] * (p[2] - ctr[2]);
            zhalf = std::max(zhalf, fabsf(rzc));
        }
        zScale = 0.45f / zhalf;
    }
    // X is NEGATED so F has a NEGATIVE determinant, matching a real N64 perspective projection.
    // Root cause of the old mirror bug (proven via the ZELDA3D_GFXDUMP draw dump, charcompare vs the
    // live game): the Fast3D path renders MP = FK·F and the OGL backend then applies its invertY.
    // The real game's projection (guPerspective) carries a handedness flip so that, AFTER the
    // backend invertY, on-screen winding is correct — its MP determinant is NEGATIVE (measured:
    // 79/80 of child Zelda's limbs). This hand-built F was a pure rotation+scale (det>0), so every
    // N64 model rendered MIRRORED vs the game (reversed rotation, "inverted faces", asymmetric parts
    // — e.g. the torso — reading wrong). Negating one output axis restores the game's handedness;
    // X is the screen-horizontal flip (matches the observed left/right mirror and is NOT Y, which
    // would render upside-down). After this, charcompare's N64 MP det is negative like the game's,
    // and the N64 half faces the same way as the (in-game-verified) zelda3d 3DS half.
    const float S[3] = { -fit * xComp, fit, zScale };
    // F maps point p: clip_j = S_j * sum_k R[j][k]*(p_k - ctr_k) + bias_j. In column-major MtxF
    // (mf[col][row], clip_row = sum_col mf[col][row]*p_col + mf[3][row]):
    MtxF F;
    memset(F.mf, 0, sizeof(F.mf));
    const float bias[3] = { 0, 0, 0.5f };
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) F.mf[col][row] = S[row] * R[row][col];
        float t = bias[row];
        for (int col = 0; col < 3; col++) t -= F.mf[col][row] * ctr[col];
        F.mf[3][row] = t;
    }
    F.mf[3][3] = 1.0f;

    // The per-limb matrices packed into segment 0x0D are the MODEL-SPACE FK (worldId, computed from
    // identity) — NOT the framed F*FK. The flex limb DLs read these as N64 16.16 FIXED-POINT, which
    // has ~4 fractional digits: fine for model-space matrices (3x3 ~1, translations in the hundreds,
    // exactly like the real game) but catastrophic if the tiny framing scale (~7e-4) is baked in
    // (entries quantize to a few ulps -> flex cross-referenced meshes tear). The framing F instead
    // goes in the PROJECTION matrix below (float replacement map, exact), so MP = FK * F frames the
    // model with full precision — mirroring how the game uses modelview(model) * projection.
    // Slot assignment (draw order) -> the segment-0x0D array is sized by dList-bearing limbs.
    std::vector<int> slot(m.limbCount, -1);
    int dlCount = 0;
    assignSlots(m, 0, dlCount, slot);
    if (getenv("CC_N64_DBG")) {
        fprintf(stderr, "[cc_n64] dlCount=%d  slot map:", dlCount);
        for (int i = 0; i < m.limbCount; i++) if (slot[i] >= 0) fprintf(stderr, " limb%d->slot%d", i, slot[i]);
        fprintf(stderr, "\n");
    }
    keys.blobs.push_back(std::make_unique<std::vector<int32_t>>(std::max(dlCount, 1) * 16));
    int32_t* mtxArray = keys.blobs.back()->data();
    for (int i = 0; i < m.limbCount; i++)
        if (slot[i] >= 0) guMtxF2L(worldId[i], &mtxArray[slot[i] * 16]);
    if (getenv("CC_N64_DBG")) {
        // Round-trip: read back slot N exactly as the interpreter's fixed-point reader does.
        auto readback = [&](int s, float out[4][4]) {
            const int32_t* addr = &mtxArray[s * 16];
            for (int i = 0; i < 4; i++)
                for (int j = 0; j < 4; j += 2) {
                    int32_t ip = addr[i * 2 + j / 2];
                    uint32_t fp = (uint32_t)addr[8 + i * 2 + j / 2];
                    out[i][j] = (int32_t)((ip & 0xffff0000) | (fp >> 16)) / 65536.0f;
                    out[i][j + 1] = (int32_t)((ip << 16) | (fp & 0xffff)) / 65536.0f;
                }
        };
        for (int li : { 1, 22 }) {
            if (slot[li] < 0) continue;
            float rb[4][4];
            readback(slot[li], rb);
            fprintf(stderr, "[cc_n64] limb%d slot%d  FK.trans(%.1f,%.1f,%.1f) readback.trans(%.1f,%.1f,%.1f)\n",
                    li, slot[li], worldId[li].mf[3][0], worldId[li].mf[3][1], worldId[li].mf[3][2], rb[3][0], rb[3][1],
                    rb[3][2]);
        }
    }

    // Viewport + scissor (the side-by-side split passes a half-width rect; full = whole screen).
    keys.vpStore.push_back(std::make_unique<Vp>());
    Vp* vpp = keys.vpStore.back().get();
    vpp->vp.vscale[0] = (vp.w / 2) * 4;            vpp->vp.vscale[1] = (vp.h / 2) * 4;
    vpp->vp.vscale[2] = G_MAXZ;                    vpp->vp.vscale[3] = 0;
    vpp->vp.vtrans[0] = (vp.x0 + vp.w / 2) * 4;    vpp->vp.vtrans[1] = (vp.y0 + vp.h / 2) * 4;
    vpp->vp.vtrans[2] = 0;                         vpp->vp.vtrans[3] = 0;
    { Gfx g = gsSPViewport(vpp); dl.push_back(g); }
    { Gfx g = gsDPSetScissor(G_SC_NON_INTERLACE, vp.x0, vp.y0, vp.x0 + vp.w, vp.y0 + vp.h); dl.push_back(g); }
    // Projection = the framing F (via the float replacement map, so it's exact). MP = MatrixMul(
    // modelview=FK, P=F) frames the model-space FK with full precision (the segment-0x0D FK stays
    // model-scale for good fixed-point precision). Mirrors the game's modelview(model)*projection.
    keys.mtxStore.push_back(std::make_unique<Mtx>());
    Mtx* projKey = keys.mtxStore.back().get();
    mtx[projKey] = F;
    { Gfx g = gsSPMatrix(projKey, G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_PROJECTION); dl.push_back(g); }

    // Persistent RDP/RSP render state the limb DLs assume the game already set
    // (Gfx_SetupDL_25Opa / SETUPDL_25 in z_rcp.c). The limb DLs set per-material combine/
    // texture/geometry but rely on this for cycle type, render mode, z-buffer, lighting —
    // without it the triangles don't rasterize (each limb drawn alone produced 0 pixels).
    { Gfx g = gsDPPipeSync(); dl.push_back(g); }
    { Gfx g = gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON); dl.push_back(g); }
    { Gfx g = gsDPSetCombineMode(G_CC_MODULATEIDECALA, G_CC_MODULATEIA_PRIM2); dl.push_back(g); }
    { Gfx g = gsDPSetOtherMode(G_AD_NOTPATTERN | G_CD_MAGICSQ | G_CK_NONE | G_TC_FILT | G_TF_BILERP | G_TT_NONE |
                                   G_TL_TILE | G_TD_CLAMP | G_TP_PERSP | G_CYC_2CYCLE | G_PM_NPRIMITIVE,
                               G_AC_NONE | G_ZS_PIXEL | G_RM_FOG_SHADE_A | G_RM_AA_ZB_OPA_SURF2);
      dl.push_back(g); }
    // Match SETUPDL_25 exactly, INCLUDING G_FOG. The limb DLs assume the game's persistent fog
    // state: G_FOG makes GfxSpVertex store the fog factor (z*winv*fog_mul + fog_offset) in the
    // shade alpha instead of the vertex alpha. With no fog setup the regs are 0, so the fog factor
    // is 0 -> the cycle-1 fog blend (G_RM_FOG_SHADE_A in the limb render mode) is a no-op and the
    // model renders normally. If G_FOG is OMITTED, limbs that don't re-set it keep vertex alpha 255,
    // which the fog blend composites to solid black against the unset (black) fog colour (invisible).
    uint32_t geoMode = G_ZBUFFER | G_SHADE | G_CULL_BACK | G_FOG | G_LIGHTING | G_SHADING_SMOOTH;
    if (getenv("CC_N64_NOCULL")) geoMode &= ~(uint32_t)G_CULL_BACK;
    { Gfx g = gsSPLoadGeometryMode(geoMode); dl.push_back(g); }
    // Blend colour = the ALPHA-COMPARE THRESHOLD (.a=8) for limbs using G_AC_THRESHOLD / CVG_X_ALPHA
    // (the interpreter alpha-tests against blend_color.a). The game sets this in its setup
    // (z_rcp.c gsDPSetBlendColor(0,0,0,8)); we'd omitted it, so the threshold was a stale value that
    // discarded opaque face/eye limbs (alpha 255) -> faces rendered as a VOID. With .a=8 only truly
    // transparent texels (alpha < 8) drop, so faces render. Combine alpha here = TEXEL0 alpha.
    { Gfx g = gsDPSetBlendColor(0, 0, 0, 8); dl.push_back(g); }

    // One directional + ambient light so the lit limbs shade correctly (without lights the
    // G_LIGHTING geometry mode reads garbage shade).
    //
    // HEADLIGHT: the light must follow the CAMERA, not the model. The interpreter lights a vertex as
    // dot(normal_obj, modelview^T * lightDir) = dot(normal_model, lightDir) — i.e. lightDir is in
    // MODEL space (the framing rotation R lives in the projection, not the modelview). A fixed
    // model-space dir therefore lights a fixed side of the MODEL, which drifts off-camera as the view
    // rotates (ry/rx/rz) — that is why Zelda's front read dark at ry=180 while her back was lit. Put
    // the light where the camera is: lightModel = R^T * lightClip, with lightClip pointing toward the
    // viewer (clip -z, slightly from above). Then surfaces facing the camera are lit at any view angle.
    const float lightClip[3] = { 0.0f, 0.45f, 1.0f }; // toward viewer, a touch from above
    float lightModel[3];
    for (int i = 0; i < 3; i++)
        lightModel[i] = R[0][i] * lightClip[0] + R[1][i] * lightClip[1] + R[2][i] * lightClip[2]; // R^T * lightClip
    float ll = sqrtf(lightModel[0] * lightModel[0] + lightModel[1] * lightModel[1] + lightModel[2] * lightModel[2]);
    if (ll < 1e-6f) ll = 1.0f;
    keys.lightStore.push_back(std::make_unique<Lights1>());
    Lights1* lights = keys.lightStore.back().get();
    memset(lights, 0, sizeof(*lights));
    static int kAmb = [] { const char* e = getenv("CC_AMB"); return e ? atoi(e) : 140; }();
    static int kDir = [] { const char* e = getenv("CC_DIR"); return e ? atoi(e) : 150; }();
    lights->a.l.col[0] = lights->a.l.col[1] = lights->a.l.col[2] = kAmb;
    lights->a.l.colc[0] = lights->a.l.colc[1] = lights->a.l.colc[2] = kAmb;
    lights->l[0].l.col[0] = lights->l[0].l.col[1] = lights->l[0].l.col[2] = kDir;
    lights->l[0].l.colc[0] = lights->l[0].l.colc[1] = lights->l[0].l.colc[2] = kDir;
    lights->l[0].l.dir[0] = (int8_t)(lightModel[0] / ll * 120.0f);
    lights->l[0].l.dir[1] = (int8_t)(lightModel[1] / ll * 120.0f);
    lights->l[0].l.dir[2] = (int8_t)(lightModel[2] / ll * 120.0f);
    { Gfx g = gsSPNumLights(NUMLIGHTS_1); dl.push_back(g); }
    { Gfx g = gsSPLight(&lights->l[0], 1); dl.push_back(g); }
    { Gfx g = gsSPLight(&lights->a, 2); dl.push_back(g); }

    // Bind the per-limb matrix array to segment 0x0D (what the flex limb DLs reference).
    { Gfx g = gsSPSegment(0x0D, (uintptr_t)mtxArray); dl.push_back(g); }
    // Safety net: some actors' limb DLs gsSPDisplayList()/reference a SCRATCH SEGMENT the game sets
    // up at draw time (e.g. Darunia's limbs call seg 0x0C; Bari stores geometry in seg 0x08/0x09;
    // Dinolfos' jaw gSPDisplayLists seg 0x09 at a MISALIGNED offset like 0x09000001). We can't
    // reproduce that actor-specific setup; left unbound, SegAddr returns the raw segmented address and
    // the interpreter executes/reads it -> SEGV. Point the common actor scratch segments at a buffer
    // filled with byte 0xDF: G_ENDDL is opcode 0xDF, so a branch/call to ANY offset — aligned OR
    // misaligned — reads op=(w0>>24)&0xFF == 0xDF and ends the DL IMMEDIATELY. (The old "ENDDL only at
    // offset 0, zeros after" failed when a DL branched to offset>0: the zeros decode as G_SPNOOP, so
    // execution walked through the whole buffer and ran off the end into unmapped memory -> the
    // nondeterministic Dinolfos/"zf" crash.) The model renders without that actor-provided geometry.
    static std::vector<Gfx> kScratchSeg = [] {
        std::vector<Gfx> v(0x4000, Gfx{}); // 0x4000 * sizeof(Gfx)
        memset(v.data(), 0xDF, v.size() * sizeof(Gfx)); // every byte 0xDF -> any (mis)aligned read = G_ENDDL
        return v;
    }();
    // Bind ALL actor scratch segments (0x01..0x0C) — limb DLs reference various ones for matrices
    // (e.g. Bari's nucleus loads a matrix from seg 0x01), display lists and vertices. We provide
    // none of them, so any reference resolves into this safe bounded buffer instead of crashing.
    // Segment 0x0D is OURS (the per-limb matrix array) and must not be overwritten.
    for (int seg = 0x01; seg <= 0x0C; seg++) {
        Gfx g = gsSPSegment(seg, (uintptr_t)kScratchSeg.data());
        dl.push_back(g);
    }
    // Segment 0x09: several actors (En_Zf/Dinolfos+Lizalfos, En_Bw) point seg 0x09 at the shared
    // render-mode setup DL `D_80116280` (z_actor.c) and their limb DLs gSPDisplayList into it to set
    // the XLU render mode + alpha-threshold, then return. Left as the 0xDF stub the branch just
    // ENDDLs (geometry then draws with the wrong/previous render mode); recreate D_80116280 here so
    // the branch does its real job. This is what lets the N64 Dinolfos/Lizalfos render correctly.
    static std::vector<Gfx> kSeg9DL = [] {
        std::vector<Gfx> v;
        Gfx a = gsDPSetRenderMode(G_RM_FOG_SHADE_A, AA_EN | Z_CMP | Z_UPD | IM_RD | CLR_ON_CVG | CVG_DST_WRAP |
                                                        ZMODE_XLU | FORCE_BL | GBL_c2(G_BL_CLR_IN, G_BL_A_IN,
                                                                                      G_BL_CLR_MEM, G_BL_1MA));
        Gfx b = gsDPSetAlphaCompare(G_AC_THRESHOLD);
        Gfx c = gsSPEndDisplayList();
        v.push_back(a); v.push_back(b); v.push_back(c);
        return v;
    }();
    { Gfx g = gsSPSegment(0x09, (uintptr_t)kSeg9DL.data()); dl.push_back(g); }
    // Override the actor face segments (0x08 eyes, 0x0A mouth) with the neutral default textures
    // scanned from the object. The path strings live in m.faceSegs (stable for this model's life);
    // gSPSegment binds the segment to the "__OTR__<path>" pointer, and the limb DL's raw G_SETTIMG
    // resolves it -> OTR signature -> texture loads. This is exactly what the actor does each frame.
    // (Must come AFTER the scratch-segment loop so it wins for 0x08/0x09/0x0A.)
    for (const auto& [seg, path] : m.faceSegs) {
        if (getenv("CC_N64_DBG"))
            fprintf(stderr, "[cc_n64] bind seg 0x%02X -> %p '%s'\n", seg, (void*)path.c_str(), path.c_str());
        Gfx g = gsSPSegment(seg, (uintptr_t)path.c_str());
        dl.push_back(g);
    }

    emitLimbs(m, 0, slot, dl);
}

} // namespace cc
