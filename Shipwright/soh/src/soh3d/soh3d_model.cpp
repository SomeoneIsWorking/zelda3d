// SoH3D model bridge: connects the runtime C++ asset loader (asset/) to the
// libultraship direct-GL renderer (SoH3D_GL_*). Owns the model registry (actor id
// -> 3DS asset + world scale), lazily parses+decodes a model from the decrypted
// .3ds the first time it's drawn (on the render thread, GL current), and serves the
// renderer's provider callback with the CPU data to upload. No baked-in C arrays;
// the .3ds path comes from env SOH3D_3DS_ROM (never hardcoded — repo rule).
#include "asset/ctr_rom.h"
#include "asset/zar.h"
#include "asset/zsi.h"
#include "asset/zcol.h"
#include "asset/cmb.h"
#include "asset/ctxb.h"
#include "asset/csab.h"
#include "asset/mat4.h"
#include "asset/pica_texture.h"
#include "asset/cityhash.h"
#include "asset/texpack.h"
#include "fast/soh3d_gl.h"
#include "ship/Context.h"                              // #20 keyboard-inject verification shim
#include "ship/controller/controldeck/ControlDeck.h"   // #20 ProcessKeyboardEvent path
#include <stb_image.h>
#include "stairs_stone_png.h" // embedded PNG of assets/soh3d/stairs_stone.svg (custom stair texture)
#include "xbox_glyphs_png.h"  // embedded PNGs of assets/soh3d/xbox_{a,b,x,y}.svg (HUD button glyphs, #32)
#include "heart_tex_png.h"    // embedded PNGs of the crisp HUD heart textures (#31, gen_hud_tex.sh)
#include "digit_tex_png.h"    // embedded PNGs of the crisp HUD counter font (#31, gen_digit_tex.sh)
#include "button_tex_png.h"   // embedded PNG of the crisp HUD button-background disc (#31, gen_button_tex.sh)
#include "counter_icon_png.h" // embedded PNGs of the crisp HUD counter icons (#31, gen_counter_icons.sh)

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <set>
#include <functional>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Matches the typedef in soh3d.h (which this pure-C++ TU does not include). The N64
// floor-height callback used by the terrain warp; soh3d.c supplies the implementation.
typedef float (*SoH3D_FloorFn)(float x, float z);

// Matches the struct in soh3d.h (C ABI). Per OoT3D bone: which N64 jointRots index drives it
// and HOW. mode 0=rest (keep CMB rest rot), 1=replace (local rot := N64 rot), 2=left (C·R_n64),
// 3=right (R_n64·C). C is a row-major 3x3 constant rest-frame correction for bones whose OoT3D
// rest frame diverges from the N64 limb's (Grezzo re-rigged Link's spine/arms). See
// tools/soh3d_link_retarget_derive.py and [[soh3d-n64anim-retarget]].
typedef struct {
    signed char limb;
    unsigned char mode; // 0 rest, 1 replace, 2 left C·R, 3 right R·C, 4 two-sided C·R·C2
    float C[9];
    float C2[9];        // right factor for mode 4 (identity otherwise)
} SoH3dBoneCorr;

namespace {

struct ModelSpec {
    const char* zarPath;
    float worldScale;
    const char* cmbName; // substring to select the .cmb inside the ZAR (nullptr = first one).
                         // Needed when a ZAR holds several CMBs (e.g. a main model + a debris
                         // "hahen" variant) and firstWithSuffix would grab the wrong one.
};

// Registry keyed by modelId (the index). The actor->modelId mapping lives in
// soh3d.c (which has the ACTOR_* ids); this stays pure-C++ / engine-agnostic.
//   0 = geldwoman (white Gerudo, En_Ge1)
//   1 = large wooden crate (Obj_Kibako2) — pick the intact box, not the debris CMB
//   2 = bush (En_Kusa) — the intact bush, not the smaller obj_kusa03 variant
//   3 = pot (Obj_Tsubo) — the intact pot, not the tubo2_hahen debris CMB
//   4 = small liftable rock (En_Ishi type 0) — field-keep stone
//   5 = large/silver rock (En_Ishi type 1) — field-keep silver rock (obj_ginbure)
//   6 = field flower (Obj_Hana params&3==0) — field-keep flower
const ModelSpec kModels[] = {
    { "/actor/zelda_ge1.zar", 0.011f, nullptr },
    { "/actor/zelda_kibako2.zar", 0.10f, "CIkibako_model" },
    { "/actor/zelda_kusa.zar", 0.5f, "obj_kusa01_model" },
    { "/actor/zelda_tsubo.zar", 0.12f, "tubo2_model" },
    { "/actor/zelda_field_keep.zar", 0.4f, "obj_isi01_model" },
    { "/actor/zelda_field_keep.zar", 0.4f, "obj_ginbure_model" },
    { "/actor/zelda_field_keep.zar", 0.4f, "flower1_model" },
};

// Scene-room models live in a SEPARATE id range so they never collide with the actor
// table above. A room's geometry is a single embedded CMB inside a ZSI (no skeleton,
// no animation), drawn at the world origin. Ids are allocated on demand by the game
// (SoH3D_RoomModelId) keyed by the room's ZSI path. See soh3d.c's room-draw hook.
const int kSceneModelBase = 1000;

// Auto-replaced actor models live in a THIRD id range (above scene rooms) so the
// SOH3D_AUTO path can allocate ids for arbitrary actor ZARs (discovered at runtime
// from the object id -> ZAR table) without colliding with the hand-listed actor
// models (0..N) or scene rooms (1000..). Keyed by ZAR path; main CMB picked by the
// "largest non-debris" heuristic. See SoH3D_AutoModelId / loadAutoModel.
const int kAutoModelBase = 2000;

// Hand-curated multi-part assemblies: ZARs that hold ONE object split across several CMBs
// authored in a shared local space (so they assemble at the actor's single transform). The
// auto path merges exactly the listed CMBs (in order) into one model instead of the
// single-CMB "largest" pick, which would grab one floating sub-piece.
//   A GENERIC "merge all CMBs" is unsound: a survey of all 289 mapped object ZARs found 112
//   with >=2 "real" CMBs, but they are overwhelmingly COLLECTIONS (one ZAR shared by many
//   actor types, e.g. zelda_ec = 23 NPCs), ALTERNATE VARIANTS (cow/cow2, koume/kotake), or
//   BREAK-STATE/EFFECT pieces (kanban's L_*/R_* shattered halves). Genuine single-objects-
//   split-into-parts are rare, so each entry here is hand-verified. See
//   scratch/evidence/multicmb_finding.md.
struct AssemblySpec {
    const char* zarSuffix;             // matched against the ZAR path tail
    std::vector<std::string> cmbNames; // CMB name substrings to merge, in draw order
};
const AssemblySpec kAssemblies[] = {
    // (No active entries.) The merge mechanism is kept for genuine multi-part static props.
    // KANBAN was the first candidate but is EXCLUDED: although the merge renders the intact
    // sign (post bo_* + the 8 board segments L_*/R_*), En_Kanban's cut behaviour spawns more
    // En_Kanban actors for the broken pieces and the auto path re-replaces them as whole signs
    // (slashing "spawns signs"). So kanban stays on N64 (skipped in SoH3D_TryAuto) until the
    // break pieces are handled. Add an entry here only for a static prop with no break/spawn
    // behaviour. See scratch/evidence/multicmb_finding.md.
    { nullptr, {} },
};

// Loaded CPU data for a model, kept alive so the renderer can upload from it and
// so the provider can hand back stable pointers. The Zar + Cmb stay resident so the
// animation layer can load CSABs and recompute skin matrices per frame on demand.
struct LoadedModel {
    std::vector<SoH3D::CmbDrawGroup> groups;       // interleaved verts (CmbVertex == SoH3DGlVtx layout)
    std::vector<std::vector<uint8_t>> texRgba;     // decoded RGBA8 per CMB texture
    std::vector<SoH3DGlGroup> cGroups;             // C-API view
    std::vector<SoH3DGlTex> cTexs;                 // C-API view
    std::unique_ptr<SoH3D::Zar> zar;               // resident archive (for CSAB lookup)
    std::unique_ptr<SoH3D::Cmb> cmb;               // resident model (skeleton + bind matrices)
    std::unordered_map<std::string, std::unique_ptr<SoH3D::Csab>> anims; // cached by full name
    std::string defaultAnim; // chosen default (idle) CSAB base name, "" = none; computed lazily
    int defaultAnimDone = 0; // 0 = not yet scanned
    bool ok = false;
    bool skinned = false; // auto models: CMB has an articulated skeleton (>1 bone) -> the
                          // auto path skips it (no anim => T-pose), leaving it to N64.
    // Per-XZ ground-delta field D(x,z) = N64_floor - OoT3D_floor for a scene room, computed
    // once (deltaReady). The render mesh is LEFT UNTOUCHED (pixel-faithful OoT3D); instead
    // actors are offset by -D so they stand on the visible OoT3D ground (inverse of the old
    // render warp, which smeared at N64 collision steps). minx/minz/nx/nz/step describe the grid.
    bool deltaReady = false;
    std::vector<float> delta;
    float dMinX = 0, dMinZ = 0, dStep = 100.0f;
    int dNx = 0, dNz = 0;
    // Facial material-anim (keystone #3): per eye/mouth material slot, the texture indices of its
    // decoded .cmab frame sprites (appended to texRgba/cTexs at load). materialIndex -> [texIndex
    // per frame]. The override driver reads the live N64 eye/mouth index and binds frame N's texture
    // via SoH3D_GL_SetMatTexOverride. Empty for non-facial models.
    std::unordered_map<int, std::vector<int>> facialFrames;
};

std::unordered_map<int, std::unique_ptr<LoadedModel>> g_loaded;
std::unique_ptr<SoH3D::CtrRom> g_rom;

// Scene-room id allocation: ZSI path -> model id (>= kSceneModelBase), and the reverse
// list so loadModel can recover the path from the id.
std::unordered_map<std::string, int> g_sceneRoomIds;
std::vector<std::string> g_sceneRoomPaths; // index = modelId - kSceneModelBase

// Auto-replaced actor id allocation: ZAR path -> model id (>= kAutoModelBase), and the
// reverse list so loadAutoModel can recover the path from the id.
std::unordered_map<std::string, int> g_autoModelIds;
std::vector<std::string> g_autoModelPaths; // index = modelId - kAutoModelBase

SoH3D::CtrRom* rom() {
    if (!g_rom) {
        const char* path = getenv("SOH3D_3DS_ROM");
        if (!path || !*path) {
            fprintf(stderr, "[SoH3D] SOH3D_3DS_ROM not set — cannot load OoT3D assets\n");
            return nullptr;
        }
        g_rom = std::make_unique<SoH3D::CtrRom>(path);
        if (!g_rom->ok()) {
            fprintf(stderr, "[SoH3D] CtrRom(%s): %s\n", path, g_rom->error().c_str());
            g_rom.reset();
            return nullptr;
        }
    }
    return g_rom.get();
}

// Decode an already-parsed CMB (out->cmb) into the renderer's CPU views: bind-pose
// draw groups (model-space verts + bone bindings; GPU skinning applies the pose, or
// identity = bind pose for skeleton-less scene rooms), decoded RGBA8 textures, and the
// C-API group/texture views. Shared by the actor (ZAR) and scene-room (ZSI) paths.
// bakedVertexColor: keep the CMB's per-vertex color (OoT3D baked scene lighting). Only
// SCENE ROOMS use it; characters/props are lit dynamically (scene ambient tint), and their
// CMB color attribute is unused/garbage (e.g. geldwoman reads ~0 -> would render black),
// so for those we force white (the verified-correct behavior).
// Build one C-API group view from a CMB draw group. texBase is added to the material's
// texture index so several CMBs' textures can share one concatenated array (multi-CMB
// merge). verts is pointed at `srcVerts` (which must outlive the view — for merged models
// that is a slot in out->groups, so cGroups is built only after out->groups is final).
static SoH3DGlGroup makeCgroup(const SoH3D::Cmb& cmb, const SoH3D::CmbDrawGroup& g,
                               const SoH3D::CmbVertex* srcVerts, int texBase) {
    const SoH3D::CmbMaterial* mat =
        (g.material_index >= 0 && g.material_index < (int)cmb.materials().size()) ? &cmb.materials()[g.material_index]
                                                                                  : nullptr;
    SoH3DGlGroup cg{};
    cg.verts = reinterpret_cast<const SoH3DGlVtx*>(srcVerts);
    cg.vertCount = (int)g.verts.size();
    cg.texIndex = cmb.materialTexture(g.material_index) + texBase;
    cg.alphaTest = mat && mat->alpha_test ? 1 : 0;
    cg.alphaRef = mat ? mat->alpha_ref : 0.0f;
    cg.wrapS = mat ? mat->wrap_s : 0x2901;
    cg.wrapT = mat ? mat->wrap_t : 0x2901;
    cg.blendEnable = mat && mat->blend_enable ? 1 : 0;
    cg.blendSrcRGB = mat ? mat->blend_src_rgb : 0x0302;
    cg.blendDstRGB = mat ? mat->blend_dst_rgb : 0x0303;
    cg.blendEqRGB = mat ? mat->blend_eq_rgb : 0x8006;
    cg.blendSrcA = mat ? mat->blend_src_a : 0x0001;
    cg.blendDstA = mat ? mat->blend_dst_a : 0x0000;
    cg.blendEqA = mat ? mat->blend_eq_a : 0x8006;
    cg.depthWrite = mat ? (mat->depth_write ? 1 : 0) : 1;
    cg.polygonOffset = mat ? mat->polygon_offset : 0.0f;
    // OoT3D backface culling: cull byte 1 = single-sided (cull back), 3 = double-sided.
    // Honor it so the renderer matches N64 G_CULL_BACK (don't show terrain undersides /
    // mesh interiors). Only value 1 culls; everything else (3, none) draws both sides.
    cg.faceCull = (mat && mat->cull == 1) ? 1 : 0;
    cg.meshId = g.mesh_id;
    cg.materialIndex = g.material_index; // key for the facial eye/mouth texture-override channel
    for (int k = 0; k < 4; k++) cg.blendColor[k] = mat ? mat->blend_color[k] : (k == 3 ? 1.0f : 0.0f);
    return cg;
}

// Decode a CMB's textures and append them to the model's texture arrays, returning the
// base index they were appended at (so a group's material texture index can be rebased).
// Per-CMB-texture dimensions as uploaded, after any hi-res pack substitution. Parallel to
// out->texRgba; the caller uses these (not the CMB's) so cTexs gets the replacement's size.
// Texture UVs are normalized, so a larger pack texture is a drop-in for the original.
static int appendTextures(LoadedModel* out, const SoH3D::Cmb& cmb, std::vector<std::pair<int,int>>* dims = nullptr) {
    int base = (int)out->texRgba.size();
    const auto& texs = cmb.textures();
    for (const auto& t : texs) {
        auto raw = cmb.textureRaw(t);
        int w = t.width, h = t.height;
        std::vector<uint8_t> rgba;
        // Look up a hi-res replacement by the texture's Citra legacy hash.
        auto lb = SoH3D::PicaLegacyHashBytes(t.glFormat(), t.width, t.height, raw);
        uint64_t hash = lb.empty() ? 0 : SoH3D::CityHash64(reinterpret_cast<const char*>(lb.data()), lb.size());
        if (hash == 0 || !SoH3D::TexPackLookup(hash, w, h, rgba)) {
            w = t.width; h = t.height;
            rgba = SoH3D::PicaDecode(t.glFormat(), t.width, t.height, raw);
        }
        out->texRgba.push_back(std::move(rgba));
        if (dims) dims->push_back({ w, h });
    }
    return base;
}

// --- Facial material-anim frame textures (keystone #3) -------------------------------------------
// OoT3D animates an NPC's eye/mouth by swapping which texture a single eye/mouth MATERIAL samples
// each frame. The alternate frame sprites are NOT in the model CMB (it holds only the base frame) —
// they live in a sibling `.cmab` material-anim file whose embedded texture-data block is just the
// frame sprites concatenated, each the SAME size/format as the CMB's base eye/mouth texture. The
// cmab header points at a `strt` string-table of frame names (count) and the raw frame data.
//   header+0x18 -> strt offset (u32 count at strt+4); header+0x1c -> texDataOffset.
// We decode each frame (reusing the base texture's glFormat/dims) and append it to the model's
// texture array, recording materialIndex -> [texIndex per frame] so the override driver can bind
// frame N for the live eye/mouth index. Verified layout: tools/cmab.py + tools/face_cmb_dump.py.
struct FacialCmab { const char* cmabSuffix; int materialIndex; };
struct FacialAsset { const char* zarSuffix; FacialCmab cmabs[3]; };
// Material slots + cmab filenames are CONSTANTS per ZAR (resolved by dumping each face CMB/cmab;
// see docs/material_facial_channel_spec.md "Resolved unknowns"). kw1 bakes TWO eye materials (Fado
// mat1 / girl mat2) into one shared body — both cmabs are loaded; the driver picks by ENKO_TYPE.
static const FacialAsset kFacialAssets[] = {
    { "zelda_sa.zar",  { { "saria_eye.cmab", 2 }, { "saria_mouth.cmab", 3 }, { nullptr, -1 } } },
    { "zelda_km1.zar", { { "kokirimaster_eye.cmab", 1 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_kw1.zar", { { "kokiripeople_a_eye.cmab", 1 }, { "kokiripeople_b_eye.cmab", 2 }, { nullptr, -1 } } },
};

static bool strEndsWith(const std::string& s, const char* suf) {
    size_t n = std::strlen(suf);
    return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
}

// Decode a face ZAR's cmab eye/mouth frame textures and append them to the model's texture arrays,
// filling out->facialFrames. Must run AFTER buildFromCmb (cTexs built) and BEFORE GL upload (first
// draw). No-op for non-facial ZARs.
static void appendFacialFrames(LoadedModel* out, const std::string& zarPath) {
    if (!out->ok || !out->cmb || !out->zar) return;
    const FacialAsset* fa = nullptr;
    for (const auto& a : kFacialAssets)
        if (strEndsWith(zarPath, a.zarSuffix)) { fa = &a; break; }
    if (!fa) return;
    const SoH3D::Cmb& cmb = *out->cmb;
    // Rebuild a dims list parallel to the existing cTexs (w,h) so cTexs can be re-pointed after
    // texRgba grows (push_back may reallocate, invalidating earlier .data() pointers).
    std::vector<std::pair<int, int>> dims;
    dims.reserve(out->cTexs.size());
    for (const auto& t : out->cTexs) dims.push_back({ t.w, t.h });

    for (const auto& fc : fa->cmabs) {
        if (!fc.cmabSuffix) break;
        int mat = fc.materialIndex;
        if (mat < 0 || mat >= (int)cmb.materials().size()) continue;
        int baseTexIdx = cmb.materials()[mat].tex0_idx;
        if (baseTexIdx < 0 || baseTexIdx >= (int)cmb.textures().size()) continue;
        const SoH3D::CmbTexture& bt = cmb.textures()[baseTexIdx];
        const SoH3D::ZarFile* cf = nullptr;
        for (const auto& f : out->zar->files())
            if (strEndsWith(f.name, fc.cmabSuffix)) { cf = &f; break; }
        if (!cf) { fprintf(stderr, "[SoH3D] facial %s: cmab '%s' not in zar\n", zarPath.c_str(), fc.cmabSuffix); continue; }
        std::vector<uint8_t> buf = out->zar->read(*cf);
        if (buf.size() < 0x20) continue;
        auto rd32 = [&](size_t o) -> uint32_t {
            return (uint32_t)buf[o] | ((uint32_t)buf[o + 1] << 8) | ((uint32_t)buf[o + 2] << 16) | ((uint32_t)buf[o + 3] << 24);
        };
        uint32_t strtOff = rd32(0x18), texDataOff = rd32(0x1c);
        if (strtOff + 8 > buf.size()) continue;
        uint32_t n = rd32(strtOff + 4);
        uint32_t dataLen = bt.data_len;
        if (dataLen == 0 || n == 0 || (uint64_t)texDataOff + (uint64_t)n * dataLen > buf.size()) {
            fprintf(stderr, "[SoH3D] facial %s/%s: bad cmab layout (n=%u dataLen=%u texOff=%u len=%zu)\n",
                    zarPath.c_str(), fc.cmabSuffix, n, dataLen, texDataOff, buf.size());
            continue;
        }
        std::vector<int> frameTex;
        frameTex.reserve(n);
        for (uint32_t f = 0; f < n; f++) {
            std::vector<uint8_t> raw(buf.begin() + texDataOff + (size_t)f * dataLen,
                                     buf.begin() + texDataOff + (size_t)(f + 1) * dataLen);
            std::vector<uint8_t> rgba = SoH3D::PicaDecode(bt.glFormat(), bt.width, bt.height, raw);
            frameTex.push_back((int)out->texRgba.size());
            out->texRgba.push_back(std::move(rgba));
            dims.push_back({ bt.width, bt.height });
        }
        out->facialFrames[mat] = std::move(frameTex);
        fprintf(stderr, "[SoH3D] facial %s: loaded %u frames for mat %d from %s\n",
                zarPath.c_str(), n, mat, fc.cmabSuffix);
    }
    // Re-point cTexs at (possibly reallocated) texRgba storage, including the new facial frames.
    out->cTexs.resize(out->texRgba.size());
    for (size_t i = 0; i < out->texRgba.size(); i++)
        out->cTexs[i] = { out->texRgba[i].data(), dims[i].first, dims[i].second };
}

// ============================================================================
// #5 — Real stepped-polygon stairs from the OoT3D fake-flat "kaidan" ramps.
//
// OoT3D (like the N64 original) renders staircases as a single FLAT textured ramp:
// the slope is one planar quad and the step lines are painted into its texture. The
// texture is the game's own label for stairs — its name contains "kaidan" (Japanese
// 階段, "staircase"; e.g. spot01's `s01_kaidan_01`). We use that name as the detection
// signal (grounded in the asset, not a per-scene magic region): every kaidan ramp in
// every scene is replaced by ACTUAL 3D step geometry — horizontal treads + vertical
// risers — covering the exact same footprint, kept on the SAME kaidan material so the
// texture/UV/lighting/cull all match. The original flat ramp triangles are dropped.
//
// Step rise (world-units per generated step). Originally asset-derived to match the painted
// kaidan steps (~7.8u: the texture paints ~11 steps per 128px V-tile and the ramp UV maps
// ~86 world-Y per V-tile). Now that the steps wear our own tiled stone texture (not the
// painted kaidan ramp), the rise is no longer pinned to the asset — it's a runtime tunable
// (RmlUi "Stair Step Size" / SoH3D_SetStairRiserY), so the player can pick larger/smaller
// steps. N = round(rampRiseY / gSoH3dStairRiserY). Default is chunkier than the old 7.8.
float gSoH3dStairRiserY = 14.0f;
int gSoH3dStairs = 1; // env SOH3D_STAIRS / REPL `stairs` gate (default on)

// Decode the embedded custom stair texture (PNG -> RGBA8) once. Returns the cached pixel
// buffer + dims; w/h = 0 on failure (then the steps fall back to no texture / vertex color).
static const std::vector<uint8_t>& stairStoneTex(int& w, int& h) {
    static std::vector<uint8_t> rgba;
    static int sw = 0, sh = 0, tried = 0;
    if (!tried) {
        tried = 1;
        int n = 0;
        stbi_uc* px = stbi_load_from_memory(kStairStonePng, (int)kStairStonePngLen, &sw, &sh, &n, 4);
        if (px) {
            rgba.assign(px, px + (size_t)sw * sh * 4);
            stbi_image_free(px);
        } else {
            fprintf(stderr, "[SoH3D] stairs: failed to decode embedded stone texture\n");
            sw = sh = 0;
        }
    }
    w = sw; h = sh;
    return rgba;
}

static bool texNameIsKaidan(const SoH3D::Cmb& cmb, int matIndex) {
    if (matIndex < 0 || matIndex >= (int)cmb.materials().size()) return false;
    int ti = cmb.materials()[matIndex].tex0_idx;
    if (ti < 0 || ti >= (int)cmb.textures().size()) return false;
    return cmb.textures()[ti].name.find("kaidan") != std::string::npos;
}

// Solve a 3x3 linear system A x = b (Gaussian elimination, partial pivot). Returns false
// if singular. Used to fit the ramp's affine UV(a,c) map so generated step verts inherit
// the original texture coordinates.
static bool solve3(double A[3][3], double b[3], double x[3]) {
    for (int col = 0; col < 3; col++) {
        int piv = col;
        for (int r = col + 1; r < 3; r++)
            if (std::fabs(A[r][col]) > std::fabs(A[piv][col])) piv = r;
        if (std::fabs(A[piv][col]) < 1e-12) return false;
        if (piv != col) {
            for (int k = 0; k < 3; k++) std::swap(A[piv][k], A[col][k]);
            std::swap(b[piv], b[col]);
        }
        for (int r = col + 1; r < 3; r++) {
            double f = A[r][col] / A[col][col];
            for (int k = col; k < 3; k++) A[r][k] -= f * A[col][k];
            b[r] -= f * b[col];
        }
    }
    for (int r = 2; r >= 0; r--) {
        double s = b[r];
        for (int k = r + 1; k < 3; k++) s -= A[r][k] * x[k];
        x[r] = s / A[r][r];
    }
    return true;
}

// ---- shared kaidan-patch analysis (used by BOTH the render-side step geometry and the
// collision-side stepped floor in soh3d.c, so the two never diverge). A kaidan group's flat
// triangles are split into connected, coplanar ramp patches; each sloped patch yields a step
// frame (ascend/across axes, footprint bbox, step count). ----

// Per-triangle outward normal (CCW winding -> outward, matches the CMB convention).
static std::vector<std::array<float, 3>> stairTriNormals(const SoH3D::CmbDrawGroup& g) {
    size_t ntri = g.verts.size() / 3;
    std::vector<std::array<float, 3>> nrm(ntri);
    for (size_t t = 0; t < ntri; t++) {
        const float* p0 = g.verts[3 * t + 0].pos;
        const float* p1 = g.verts[3 * t + 1].pos;
        const float* p2 = g.verts[3 * t + 2].pos;
        float e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
        float e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
        float* n = nrm[t].data();
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        float l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (l > 1e-9f) { n[0] /= l; n[1] /= l; n[2] /= l; }
    }
    return nrm;
}

// Union-find: connect triangles that SHARE a vertex AND are ~coplanar (normal dot > 0.98).
// Separate staircases (no shared verts) and touching ramps of different orientation stay
// distinct patches. Returns the patches as lists of triangle indices.
static std::vector<std::vector<int>> stairPatches(const SoH3D::CmbDrawGroup& g,
                                                  const std::vector<std::array<float, 3>>& nrm) {
    size_t ntri = g.verts.size() / 3;
    std::vector<int> parent(ntri);
    for (size_t i = 0; i < ntri; i++) parent[i] = (int)i;
    std::function<int(int)> find = [&](int a) {
        while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; }
        return a;
    };
    auto coplanar = [&](size_t a, size_t b) {
        return nrm[a][0] * nrm[b][0] + nrm[a][1] * nrm[b][1] + nrm[a][2] * nrm[b][2] > 0.98f;
    };
    std::unordered_map<uint64_t, int> vmap; // quantized vertex -> a triangle that touches it
    auto vkey = [](const float* p) -> uint64_t {
        auto q = [](float v) -> uint64_t { return (uint64_t)(int64_t)std::llround(v / 2.0f) & 0x1FFFFF; };
        return (q(p[0]) << 42) | (q(p[1]) << 21) | q(p[2]);
    };
    for (size_t t = 0; t < ntri; t++) {
        for (int k = 0; k < 3; k++) {
            uint64_t key = vkey(g.verts[3 * t + k].pos);
            auto it = vmap.find(key);
            if (it != vmap.end() && coplanar(t, (size_t)it->second)) {
                parent[find((int)t)] = find(it->second);
            }
            vmap[key] = (int)t; // last writer; union above stitches the chain
        }
    }
    std::unordered_map<int, std::vector<int>> byroot;
    for (size_t t = 0; t < ntri; t++) byroot[find((int)t)].push_back((int)t);
    std::vector<std::vector<int>> out;
    out.reserve(byroot.size());
    for (auto& kv : byroot) out.push_back(std::move(kv.second));
    return out;
}

// A single staircase patch's stepping frame, derived purely from geometry (identical between
// render + collision). aDir = horizontal uphill, cDir = across; the footprint is the (a,c) bbox
// and the patch rises ymin..ymax in N steps of (da,dy).
struct StairFrame {
    float aDir[3], cDir[3];
    float amin, amax, cmin, cmax, ymin, ymax;
    int N;
    float da, dy;
};

// Compute the step frame for one patch. Returns false (caller keeps the flat tris) when the
// patch is not a walkable slope (flat floor / near-vertical wall) or is degenerate.
static bool stairFrameOf(const SoH3D::CmbDrawGroup& g, const std::vector<int>& tris,
                         const std::vector<std::array<float, 3>>& nrm, StairFrame& f) {
    float n[3] = { 0, 0, 0 };
    for (int t : tris) { n[0] += nrm[t][0]; n[1] += nrm[t][1]; n[2] += nrm[t][2]; }
    float nl = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
    if (nl < 1e-6f) return false;
    n[0] /= nl; n[1] /= nl; n[2] /= nl;

    // Only stair-able FLOORS: upward-facing and actually sloped (a flat floor or a near-vertical
    // wall is not a ramp).
    if (!(n[1] > 0.5f && (n[0] * n[0] + n[2] * n[2]) > 0.02f)) return false;

    float ah = std::sqrt(n[0] * n[0] + n[2] * n[2]);
    f.aDir[0] = -n[0] / ah; f.aDir[1] = 0.0f; f.aDir[2] = -n[2] / ah; // uphill gradient
    f.cDir[0] = f.aDir[2]; f.cDir[1] = 0.0f; f.cDir[2] = -f.aDir[0];  // across, in XZ

    f.amin = f.cmin = f.ymin = 1e30f;
    f.amax = f.cmax = f.ymax = -1e30f;
    for (int t : tris) {
        for (int k = 0; k < 3; k++) {
            const float* p = g.verts[3 * t + k].pos;
            float a = f.aDir[0] * p[0] + f.aDir[2] * p[2];
            float c = f.cDir[0] * p[0] + f.cDir[2] * p[2];
            f.amin = std::min(f.amin, a); f.amax = std::max(f.amax, a);
            f.cmin = std::min(f.cmin, c); f.cmax = std::max(f.cmax, c);
            f.ymin = std::min(f.ymin, p[1]); f.ymax = std::max(f.ymax, p[1]);
        }
    }
    if (f.amax - f.amin < 1.0f || f.cmax - f.cmin < 1.0f || f.ymax - f.ymin < 1.0f) return false;

    f.N = (int)std::lround((f.ymax - f.ymin) / (gSoH3dStairRiserY > 0.5f ? gSoH3dStairRiserY : 0.5f));
    if (f.N < 1) f.N = 1;
    if (f.N > 200) f.N = 200;
    f.da = (f.amax - f.amin) / f.N;
    f.dy = (f.ymax - f.ymin) / f.N;
    return true;
}

// Replace one kaidan draw group's flat-ramp triangles with stepped geometry. Each
// connected, coplanar patch (a single ramp; one group may hold several separate
// staircases) is rebuilt as treads+risers over its footprint.
static void generateStairsGroup(SoH3D::CmbDrawGroup& g) {
    size_t ntri = g.verts.size() / 3;
    if (ntri < 2) return;

    std::vector<std::array<float, 3>> nrm = stairTriNormals(g);
    std::vector<std::vector<int>> patches = stairPatches(g, nrm);

    std::vector<SoH3D::CmbVertex> outv;
    outv.reserve(g.verts.size() * 4);

    static int stairDbg = -1;
    if (stairDbg < 0) { const char* e = getenv("SOH3D_STAIRDBG"); stairDbg = (e && *e) ? atoi(e) : 0; }

    for (const std::vector<int>& tris : patches) {
        StairFrame f;
        bool ok = stairFrameOf(g, tris, nrm, f);
        if (stairDbg && ok) {
            float ac = (f.amin + f.amax) * 0.5f, cc = (f.cmin + f.cmax) * 0.5f;
            float wx = f.aDir[0] * ac + f.cDir[0] * cc, wz = f.aDir[2] * ac + f.cDir[2] * cc;
            printf("[SoH3D] stairdbg: patch world XZ=(%.0f,%.0f) y=[%.0f,%.0f] N=%d aSpan=%.0f cSpan=%.0f "
                   "aDir=(%.2f,%.2f) cDir=(%.2f,%.2f)\n",
                   wx, wz, f.ymin, f.ymax, f.N, f.amax - f.amin, f.cmax - f.cmin,
                   f.aDir[0], f.aDir[2], f.cDir[0], f.cDir[2]);
            fflush(stdout);
        }

        // Affine UV(a,c) fit + average color over the patch (render-only; the generated step
        // verts inherit the original ramp's texture coordinates and baked lighting).
        double M[3][3] = {}, bu[3] = {}, bv[3] = {};
        float col[4] = {}; int nc = 0;
        const SoH3D::CmbVertex& src = g.verts[3 * tris[0]];
        if (ok) {
            for (int t : tris) {
                for (int k = 0; k < 3; k++) {
                    const SoH3D::CmbVertex& v = g.verts[3 * t + k];
                    float a = f.aDir[0] * v.pos[0] + f.aDir[2] * v.pos[2];
                    float c = f.cDir[0] * v.pos[0] + f.cDir[2] * v.pos[2];
                    double r[3] = { a, c, 1.0 };
                    for (int i = 0; i < 3; i++) {
                        for (int j = 0; j < 3; j++) M[i][j] += r[i] * r[j];
                        bu[i] += r[i] * v.uv[0];
                        bv[i] += r[i] * v.uv[1];
                    }
                    for (int e = 0; e < 4; e++) col[e] += v.color[e];
                    nc++;
                }
            }
        }
        double mu[3], mv[3];
        if (ok) {
            for (int e = 0; e < 4; e++) col[e] /= (float)nc;
            double Mu[3][3], Mv[3][3];
            std::memcpy(Mu, M, sizeof(M)); std::memcpy(Mv, M, sizeof(M));
            ok = solve3(Mu, bu, mu) && solve3(Mv, bv, mv);
        }
        if (!ok) { // not a slope (or degenerate UV) -> keep the original tris verbatim
            for (int t : tris)
                for (int k = 0; k < 3; k++) outv.push_back(g.verts[3 * t + k]);
            continue;
        }

        // Emit a vertex: geometry at (aGeom, y, c) with explicit UV into the CUSTOM stair
        // texture (stairs_stone.svg). The texture is a single STEP tile: V in [0,Vnose) is
        // the tread (top surface), V == Vnose is the lit nosing line, V in (Vnose,1] is the
        // riser (front face). One geometry step maps to one tile in V; U tiles horizontally
        // across the staircase width (REPEAT) at kStairTileW world-units per tile. Vertex
        // color is forced white so the authored stone shows true (the kaidan baked color is
        // irrelevant now that we no longer use its texture).
        // Step color = the original kaidan ramp's averaged baked vertex color (col[], averaged over
        // the patch above) so the steps sit in the SAME scene lighting/tone as the ramp they replace
        // — NOT flat white (which read pale/unlit). emit multiplies in a per-face shade (stepShade)
        // for 3D readout: treads catch the most light, risers less, the side caps least.
        float baseCol[4] = { col[0], col[1], col[2], col[3] };
        float stepShade = 1.0f; // set per face below
        auto emit = [&](float aGeom, float y, float c, float u, float vtex, const float nrmv[3]) {
            SoH3D::CmbVertex v = src;
            v.pos[0] = f.aDir[0] * aGeom + f.cDir[0] * c;
            v.pos[1] = y;
            v.pos[2] = f.aDir[2] * aGeom + f.cDir[2] * c;
            v.nrm[0] = nrmv[0]; v.nrm[1] = nrmv[1]; v.nrm[2] = nrmv[2];
            v.uv[0] = u;
            v.uv[1] = vtex;
            v.color[0] = baseCol[0] * stepShade;
            v.color[1] = baseCol[1] * stepShade;
            v.color[2] = baseCol[2] * stepShade;
            v.color[3] = baseCol[3];
            outv.push_back(v);
        };
        (void)mu; (void)mv; // affine fit kept only as the degeneracy gate above
        const float SH_TREAD = 1.00f, SH_RISER = 0.72f, SH_SIDE = 0.55f; // per-face shade
        const float nUp[3] = { 0, 1, 0 };
        const float nDn[3] = { -f.aDir[0], 0, -f.aDir[2] }; // riser faces downhill (toward the climber)
        const float nCmin[3] = { -f.cDir[0], 0, -f.cDir[2] }; // side wall at cmin faces -c
        const float nCmax[3] = {  f.cDir[0], 0,  f.cDir[2] }; // side wall at cmax faces +c
        const float kTileW = 44.0f;   // world units per horizontal texture tile
        const float Vnose = 0.62f;    // tread/riser split in the texture (matches the SVG)
        const float uMin = f.cmin / kTileW, uMax = f.cmax / kTileW;
        // #1: raise the whole flight by a FULL step (user, 2026-06-20: "move elevation from d/2 to d")
        // so the treads sit a step above the original ramp diagonal and the top tread reaches the
        // upper ground. yr=dy => the tread top yk = ymin+(k+1)*dy lands exactly on the ORIGINAL ramp
        // diagonal at a1 (yRamp(a1) = ymin + (a1-amin)*dy/da = ymin+(k+1)*dy). So the stepped upper
        // surface and the ramp diagonal coincide at every step's back edge and diverge by at most one
        // step in front of it — the solid occupies ONLY the thin wedge between the steps and the ramp
        // they replace, exactly the envelope of the old flat ramp. The side caps are per-step wedge
        // TRIANGLES bounded above by the tread and below by the ramp diagonal — NOT rectangles down to
        // ymin (those buried the flanking brick wall, #1 follow-up). The closed stepped-vs-ramp wedge
        // still admits no sky bleed (#1 cyan halo) from normal viewing angles, while leaving the wall
        // behind fully visible.
        const float yr = f.dy;
        for (int k = 0; k < f.N; k++) {
            float a0 = f.amin + k * f.da, a1 = f.amin + (k + 1) * f.da;
            // yk/yk1 = the treads/risers, raised a full step above the original ramp diagonal. The
            // riser climbs at the BACK of the tread (a1) up to the next tread's height.
            float yk = f.ymin + k * f.dy + yr, yk1 = f.ymin + (k + 1) * f.dy + yr;
            // Tread (top face, +Y) at yk: front edge a0 = nosing (V=Vnose) -> back a1 = V=0.
            stepShade = SH_TREAD;
            emit(a0, yk, f.cmin, uMin, Vnose, nUp); emit(a1, yk, f.cmin, uMin, 0.0f, nUp); emit(a1, yk, f.cmax, uMax, 0.0f, nUp);
            emit(a0, yk, f.cmin, uMin, Vnose, nUp); emit(a1, yk, f.cmax, uMax, 0.0f, nUp); emit(a0, yk, f.cmax, uMax, Vnose, nUp);
            // Riser (front face, -aDir) at a1, yk -> yk1: top yk1 = nosing (V=Vnose), bottom yk = V=1.
            // (Skip the last step's riser; the top tread meets the upper ground at amax, no face past it.)
            if (k + 1 < f.N) {
                stepShade = SH_RISER;
                emit(a1, yk, f.cmin, uMin, 1.0f, nDn); emit(a1, yk1, f.cmin, uMin, Vnose, nDn); emit(a1, yk1, f.cmax, uMax, Vnose, nDn);
                emit(a1, yk, f.cmin, uMin, 1.0f, nDn); emit(a1, yk1, f.cmax, uMax, Vnose, nDn); emit(a1, yk, f.cmax, uMax, 1.0f, nDn);
            }
            // Side cap: the thin WEDGE between this step and the original ramp diagonal, on each c
            // edge — a single TRIANGLE per step (NOT a rectangle down to ymin, which buried the
            // flanking brick wall, #1 follow-up). The ramp diagonal is yRamp(a)=ymin+(a-amin)*dy/da,
            // so yRamp(a0)=ymin+k*dy and yRamp(a1)=ymin+(k+1)*dy=yk (since yr=dy). The wedge corners:
            //   P1 (a0, yRamp(a0)=ymin+k*dy) — front-bottom, on the ramp / base of this riser front
            //   P2 (a0, yk)                  — front-top, top of the riser / front edge of the tread
            //   P3 (a1, yk)                  — back, where the tread rejoins the ramp (yk=yRamp(a1))
            // Consecutive steps share P1 with the previous step's P3 along the ramp, so the triangles
            // tile the steps-vs-ramp region with no gap and no overhang below the ramp surface — the
            // staircase occupies exactly the old flat ramp's envelope, leaving the brick wall behind
            // fully visible. The ramp diagonal closes the underside, so no separate bottom/back plane
            // is needed and no sky bleeds through from normal viewing angles (#1 cyan halo).
            stepShade = SH_SIDE;
            float uA0 = a0 / kTileW, uA1 = a1 / kTileW;
            float yRamp0 = f.ymin + (float)k * f.dy; // ramp surface at a0 (one step below the tread)
            // cmin side faces -c (outward). CCW seen from -c: P1 -> P3 -> P2.
            emit(a0, yRamp0, f.cmin, uA0, 1.0f, nCmin); emit(a1, yk, f.cmin, uA1, Vnose, nCmin); emit(a0, yk, f.cmin, uA0, Vnose, nCmin);
            // cmax side faces +c (outward, opposite winding): P1 -> P2 -> P3.
            emit(a0, yRamp0, f.cmax, uA0, 1.0f, nCmax); emit(a0, yk, f.cmax, uA0, Vnose, nCmax); emit(a1, yk, f.cmax, uA1, Vnose, nCmax);

            // BOTTOM-FRONT SEAL (first step only). Each step's riser is emitted at the BACK of its
            // tread (a1), so the very FIRST step has no downhill face at its front (a=amin): the
            // volume between the first tread (ymin+dy) and the ramp base (ymin) is left OPEN there,
            // and the OoT3D sky bleeds through that gap at the bottom of the flight (user: "first
            // step missing its Y vertex" — a cyan wedge under the bottom step). Close it with one
            // downhill-facing front wall at a=amin spanning the full width, from the first tread top
            // (yk) down to the ramp base (ymin) — one step tall, flush with the ground, so nothing
            // below ymin is added and the flanking wall stays visible.
            if (k == 0) {
                stepShade = SH_RISER;
                emit(a0, f.ymin, f.cmin, uMin, 1.0f, nDn); emit(a0, yk, f.cmin, uMin, Vnose, nDn); emit(a0, yk, f.cmax, uMax, Vnose, nDn);
                emit(a0, f.ymin, f.cmin, uMin, 1.0f, nDn); emit(a0, yk, f.cmax, uMax, Vnose, nDn); emit(a0, f.ymin, f.cmax, uMax, 1.0f, nDn);
            }
        }
    }
    g.verts.swap(outv);
}

// SOH3D_STAIRS env -> gSoH3dStairs, parsed once. Called from both the render path
// (generateRoomStairs) and the collision collector, since either can run first.
static void ensureStairsEnv() {
    static int envChecked = 0;
    if (!envChecked) {
        envChecked = 1;
        const char* e = getenv("SOH3D_STAIRS");
        if (e && *e) gSoH3dStairs = atoi(e);
    }
}

// Replace every kaidan ramp group in a freshly-built scene-room model with stepped
// geometry. cGroups must be (re)built AFTER this — it mutates group vert vectors.
static void generateRoomStairs(LoadedModel* out) {
    ensureStairsEnv();
    if (!gSoH3dStairs || !out->cmb) return;
    for (auto& g : out->groups)
        if (texNameIsKaidan(*out->cmb, g.material_index)) generateStairsGroup(g);
}

static void buildFromCmb(LoadedModel* out, bool bakedVertexColor,
                         const std::vector<uint8_t>& skipMesh = {}, bool stairs = false) {
    SoH3D::Cmb& cmb = *out->cmb;
    out->groups = cmb.buildDrawGroups(skipMesh);
    if (!bakedVertexColor) {
        for (auto& g : out->groups)
            for (auto& v : g.verts) { v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f; }
    }
    if (stairs) generateRoomStairs(out);

    std::vector<std::pair<int,int>> dims;
    appendTextures(out, cmb, &dims);

    // Custom stair texture: if this room has kaidan (stair) groups, append the embedded stone
    // texture (assets/soh3d/stairs_stone.svg) and point the generated step groups at it,
    // REPEAT-tiled, instead of the stretched low-res kaidan texture.
    int stairTexIdx = -1;
    if (stairs) {
        int tw = 0, th = 0;
        const std::vector<uint8_t>& stex = stairStoneTex(tw, th);
        if (tw > 0 && th > 0) {
            stairTexIdx = (int)out->texRgba.size();
            out->texRgba.push_back(stex); // copy into the model's owned storage
            dims.push_back({ tw, th });
        }
    }

    out->cTexs.resize(out->texRgba.size());
    for (size_t i = 0; i < out->texRgba.size(); i++)
        out->cTexs[i] = { out->texRgba[i].data(), dims[i].first, dims[i].second };

    out->cGroups.reserve(out->groups.size());
    for (const auto& g : out->groups) {
        SoH3DGlGroup cg = makeCgroup(cmb, g, g.verts.data(), 0);
        if (stairTexIdx >= 0 && texNameIsKaidan(cmb, g.material_index)) {
            cg.texIndex = stairTexIdx;
            cg.wrapS = cg.wrapT = 0x2901; // GL_REPEAT — tile the stone across width & length
            cg.blendEnable = 0; cg.alphaTest = 0; cg.depthWrite = 1; cg.polygonOffset = 0.0f;
        }
        out->cGroups.push_back(cg);
    }
    out->ok = true;
}

// Build a model by MERGING several CMBs (a hand-curated multi-part assembly) into one set
// of draw groups + a concatenated texture array. Each CMB's verts are authored in the same
// ZAR-local space (verified for the assemblies in kAssemblies), so the parts assemble at the
// actor's single transform with no per-part offset. Characters/props are dynamically lit, so
// vertex color is forced white (like buildFromCmb). out->cmb holds the first (main) CMB so
// the resident-archive invariants hold; merged assemblies are static (no skinning).
//   NOTE: a GENERIC "merge every CMB" is unsound — most multi-CMB ZARs are collections /
//   variants / break-states, not assemblies (see scratch/evidence/multicmb_finding.md). Only
//   the explicit, verified kAssemblies entries use this path.
static void buildFromCmbs(LoadedModel* out, std::vector<std::unique_ptr<SoH3D::Cmb>>& cmbs) {
    struct Src { const SoH3D::Cmb* cmb; size_t gi; int texBase; };
    std::vector<Src> srcs;
    std::vector<std::pair<int,int>> dims;
    for (auto& up : cmbs) {
        SoH3D::Cmb& cmb = *up;
        int texBase = appendTextures(out, cmb, &dims);
        auto groups = cmb.buildDrawGroups();
        for (auto& g : groups) {
            for (auto& v : g.verts) { v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f; }
            srcs.push_back({ &cmb, out->groups.size(), texBase });
            out->groups.push_back(std::move(g));
        }
    }
    // out->groups is now final (no further reallocation) -> safe to point cGroups into it.
    out->cTexs.reserve(out->texRgba.size());
    // dims were captured per-append (post hi-res substitution), parallel to texRgba.
    for (size_t ti = 0; ti < out->texRgba.size(); ti++)
        out->cTexs.push_back({ out->texRgba[ti].data(), dims[ti].first, dims[ti].second });
    out->cGroups.reserve(out->groups.size());
    for (const auto& s : srcs)
        out->cGroups.push_back(makeCgroup(*s.cmb, out->groups[s.gi], out->groups[s.gi].verts.data(), s.texBase));
    out->ok = true;
}

// Load a scene-room model: read its ZSI, extract the single embedded room CMB, and
// build draw groups (no skeleton/animation — drawn at the world origin).
static void loadSceneRoom(int modelId, LoadedModel* out) {
    int idx = modelId - kSceneModelBase;
    if (idx < 0 || idx >= (int)g_sceneRoomPaths.size()) return;
    const std::string& path = g_sceneRoomPaths[idx];
    SoH3D::CtrRom* r = rom();
    if (!r) return;
    auto bytes = r->read(path);
    if (bytes.empty()) { fprintf(stderr, "[SoH3D] zsi not found: %s\n", path.c_str()); return; }
    SoH3D::Zsi zsi(std::move(bytes));
    if (!zsi.ok()) { fprintf(stderr, "[SoH3D] Zsi %s: %s\n", path.c_str(), zsi.error().c_str()); return; }
    if (!zsi.hasGeometry()) { fprintf(stderr, "[SoH3D] no room geometry in %s\n", path.c_str()); return; }
    out->cmb = std::make_unique<SoH3D::Cmb>(zsi.cmbBytes());
    if (!out->cmb->ok()) { fprintf(stderr, "[SoH3D] Cmb %s: %s\n", path.c_str(), out->cmb->error().c_str()); return; }
    // scene rooms carry OoT3D baked vertex lighting; #5 turns fake-flat kaidan ramps into real steps
    buildFromCmb(out, /*bakedVertexColor=*/true, /*skipMesh=*/{}, /*stairs=*/true);
    printf("[SoH3D] loaded scene-room model %d (%s): %zu groups, %zu textures\n", modelId, path.c_str(),
           out->cGroups.size(), out->cTexs.size());
    // #29 diagnostic: dump per-group material/texture + per-group bbox so the "untextured dome"
    // group can be identified by index (pair with SOH3D_SOLOGROUP to isolate it visually).
    if (getenv("SOH3D_DBG_ROOM")) {
        const auto& texs = out->cmb->textures();
        for (size_t i = 0; i < out->cGroups.size(); i++) {
            const auto& g = out->groups[i];
            int ti = out->cGroups[i].texIndex;
            const char* tn = (ti >= 0 && ti < (int)texs.size()) ? texs[ti].name.c_str() : "<none/stair>";
            float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
            for (const auto& v : g.verts)
                for (int k = 0; k < 3; k++) { mn[k] = std::min(mn[k], v.pos[k]); mx[k] = std::max(mx[k], v.pos[k]); }
            printf("[SoH3D_DBG_ROOM] grp%2zu mat%d tex%d %-18s verts%5zu mesh_id%d "
                   "x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]\n",
                   i, g.material_index, ti, tn, g.verts.size(), g.mesh_id,
                   mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]);
        }
    }
}

// Load an actor model: read its ZAR, find the .cmb, build groups (+ keep the ZAR/CMB
// resident so the animation layer can load CSABs and recompute skin matrices).
static void loadActorModel(int modelId, LoadedModel* out) {
    SoH3D::CtrRom* r = rom();
    if (!r) return;
    auto zarBytes = r->read(kModels[modelId].zarPath);
    if (zarBytes.empty()) { fprintf(stderr, "[SoH3D] zar not found: %s\n", kModels[modelId].zarPath); return; }
    out->zar = std::make_unique<SoH3D::Zar>(std::move(zarBytes));
    if (!out->zar->ok()) { fprintf(stderr, "[SoH3D] Zar: %s\n", out->zar->error().c_str()); return; }
    const SoH3D::ZarFile* cmbf = nullptr;
    const char* want = kModels[modelId].cmbName;
    if (want) {
        for (const auto& f : out->zar->files())
            if (f.name.find(want) != std::string::npos && f.name.size() >= 4 &&
                f.name.compare(f.name.size() - 4, 4, ".cmb") == 0) {
                cmbf = &f;
                break;
            }
    }
    if (!cmbf) cmbf = out->zar->firstWithSuffix(".cmb"); // fallback: single-CMB ZARs
    if (!cmbf) { fprintf(stderr, "[SoH3D] no .cmb in %s\n", kModels[modelId].zarPath); return; }
    out->cmb = std::make_unique<SoH3D::Cmb>(out->zar->read(*cmbf));
    if (!out->cmb->ok()) { fprintf(stderr, "[SoH3D] Cmb: %s\n", out->cmb->error().c_str()); return; }
    buildFromCmb(out, /*bakedVertexColor=*/false); // characters/props: dynamic lighting, color attr unused
    appendFacialFrames(out, kModels[modelId].zarPath); // eye/mouth .cmab frames (keystone #3)
    printf("[SoH3D] loaded model %d (%s): %zu groups, %zu textures\n", modelId, kModels[modelId].zarPath,
           out->cGroups.size(), out->cTexs.size());
}

// Geometric bounding-box diagonal of a model's draw groups, in the model's own
// local space. Used by the auto-scale path as a rotation-invariant size measure: the
// world scale for an auto-replaced actor = (measured N64 world bbox diagonal) / (this
// OoT3D model diagonal). Returns 0 if the model has no geometry.
static float bboxDiag(const std::vector<SoH3D::CmbDrawGroup>& groups) {
    float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
    bool any = false;
    for (const auto& g : groups)
        for (const auto& v : g.verts) {
            any = true;
            for (int k = 0; k < 3; k++) {
                mn[k] = std::min(mn[k], v.pos[k]);
                mx[k] = std::max(mx[k], v.pos[k]);
            }
        }
    if (!any) return 0.0f;
    float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Local-space Y (height) extent of a model's draw groups. The auto-scale path matches
// this against the N64 actor's measured world height (the dimension the manual scales
// were calibrated on), so worldScale = N64_world_height / model_height. Returns 0 if no
// geometry. OoT3D actor/prop models are authored Y-up.
static float bboxHeight(const std::vector<SoH3D::CmbDrawGroup>& groups) {
    float mn = 1e30f, mx = -1e30f;
    for (const auto& g : groups)
        for (const auto& v : g.verts) {
            mn = std::min(mn, v.pos[1]);
            mx = std::max(mx, v.pos[1]);
        }
    return (mn <= mx) ? (mx - mn) : 0.0f;
}

// Heuristic to pick a ZAR's MAIN model CMB when it holds several. OoT3D actor ZARs
// often bundle debris/effect variants (a crate's "hahen" shards, a "modelT" effect
// mesh) alongside the intact model. We skip those by name and, among the remainder,
// pick the CMB with the largest geometry (the main body). Returns nullptr if none.
static bool isDebrisCmbName(const std::string& n) {
    static const char* kSkip[] = { "hahen", "broke", "_bf", "kakera", "fragment" };
    std::string lo = n;
    for (auto& c : lo) c = (char)std::tolower((unsigned char)c);
    for (const char* s : kSkip)
        if (lo.find(s) != std::string::npos) return true;
    return false;
}

// A CMB whose geometry is FLAT (one bbox dimension ~0) is a billboard/sprite/decal quad
// (e.g. wood02's wd_model [800,655,0], the *_modelT transparency sprites), not a real 3D
// model. The auto path skips these so it picks the actual mesh (a 3D tree, not a flat white
// quad on the ground). True if the smallest extent is a tiny fraction of the largest.
static bool isFlatGroups(const std::vector<SoH3D::CmbDrawGroup>& groups) {
    float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
    bool any = false;
    for (const auto& g : groups)
        for (const auto& v : g.verts) {
            any = true;
            for (int k = 0; k < 3; k++) {
                mn[k] = std::min(mn[k], v.pos[k]);
                mx[k] = std::max(mx[k], v.pos[k]);
            }
        }
    if (!any) return true;
    float e0 = mx[0] - mn[0], e1 = mx[1] - mn[1], e2 = mx[2] - mn[2];
    float emax = std::max(e0, std::max(e1, e2));
    float emin = std::min(e0, std::min(e1, e2));
    return emax <= 1e-3f || emin < 0.02f * emax;
}

static size_t vertCountGroups(const std::vector<SoH3D::CmbDrawGroup>& groups) {
    size_t n = 0;
    for (const auto& g : groups) n += g.verts.size();
    return n;
}

// Load an auto-replaced actor model: read the ZAR at its registered path, pick the main
// CMB (largest non-debris), build draw groups. No hand-tuned cmbName — the heuristic
// generalizes the manual selection used by the explicit kModels[] table. Characters/props
// are dynamically lit, so vertex color is forced white like loadActorModel.
// The *_new Link body bakes ALL hand-pose + held-equipment variants into one CMB, each on a
// distinct mesh_id; the player path selects the live subset per frame via SoH3D_GL_SetMidMask
// (draw groups are split by mesh_id). So nothing Link-specific is culled at load anymore — the
// old build-time hand-variant/equipment cull was replaced by that per-frame mesh_id mask.

// #28e — build a synthetic textured BILLBOARD quad as a LoadedModel from a standalone CTXB
// sprite (no CMB). Used for the OoT3D sun/moon discs (tex/fine_sun.ctxb, tex/fine_moon0.ctxb in
// /kankyo/BlueSky.zar), which the engine billboards itself — there is no CMB to hang the texture
// on. The quad geometry matches the N64 sun/moon billboard exactly (VTX -31..32 in the XY plane),
// so with the same translate*billboard*scale transform (set in SoH3D_TryDrawSunMoon) it renders
// pixel-identically to the N64 sprite, just with the OoT3D texture. The caller pins it to the far
// plane (handle bit 30) and faces it to the camera via play->billboardMtxF. `additive` selects the
// blend: the sun/lens-flare discs are a glow on black (src_alpha,ONE = add over the sky), the moon
// is an alpha-masked disc (src_alpha,1-src_alpha = normal alpha blend).
static void loadBillboard(LoadedModel* out, const std::string& zarPath, const std::string& ctxbName,
                          bool additive) {
    SoH3D::CtrRom* r = rom();
    if (!r) return;
    auto zarBytes = r->read(zarPath);
    if (zarBytes.empty()) { fprintf(stderr, "[SoH3D] billboard: zar not found: %s\n", zarPath.c_str()); return; }
    out->zar = std::make_unique<SoH3D::Zar>(std::move(zarBytes));
    if (!out->zar->ok()) { fprintf(stderr, "[SoH3D] billboard Zar %s: %s\n", zarPath.c_str(), out->zar->error().c_str()); return; }
    const SoH3D::ZarFile* zf = nullptr;
    for (const auto& f : out->zar->files())
        if (f.name.find(ctxbName) != std::string::npos) { zf = &f; break; }
    if (!zf) { fprintf(stderr, "[SoH3D] billboard %s: no '%s'\n", zarPath.c_str(), ctxbName.c_str()); return; }
    SoH3D::Ctxb ctxb(out->zar->read(*zf));
    if (!ctxb.ok() || ctxb.textures().empty()) {
        fprintf(stderr, "[SoH3D] billboard ctxb %s: %s\n", ctxbName.c_str(), ctxb.error().c_str());
        return;
    }
    int tw = 0, th = 0;
    auto rgba = ctxb.decodeRGBA(0, &tw, &th);
    if (rgba.empty()) { fprintf(stderr, "[SoH3D] billboard %s: decode failed\n", ctxbName.c_str()); return; }
    out->texRgba.push_back(std::move(rgba));
    out->cTexs.push_back({ out->texRgba[0].data(), tw, th });

    // One quad (two triangles) in the XY plane, matching the N64 sun/moon billboard vertices.
    // weights[0]=1, boneIds[0]=0 so with identity uBones the GPU-skin pass is a no-op (pos == model
    // pos) — a non-skinned sprite. Per-vertex colour white; the draw's tint/alpha multiplies it.
    auto mkv = [](float x, float y, float u, float v) {
        SoH3D::CmbVertex vtx{};
        vtx.pos[0] = x; vtx.pos[1] = y; vtx.pos[2] = 0.0f;
        vtx.nrm[2] = 1.0f;
        vtx.uv[0] = u; vtx.uv[1] = v;
        vtx.weights[0] = 1.0f;
        vtx.color[0] = vtx.color[1] = vtx.color[2] = vtx.color[3] = 1.0f;
        return vtx;
    };
    SoH3D::CmbVertex bl = mkv(-31, -31, 0, 0), br = mkv(32, -31, 1, 0), tl = mkv(-31, 32, 0, 1),
                     tr = mkv(32, 32, 1, 1);
    SoH3D::CmbDrawGroup g;
    g.material_index = -1;
    g.mesh_id = -1;
    // N64 tris: gSP2Triangles(0,1,2, 0, 2,1,3, 0) over verts {bl,br,tl,tr}.
    g.verts = { bl, br, tl, tl, br, tr };
    out->groups.push_back(std::move(g));

    SoH3DGlGroup cg{};
    cg.verts = reinterpret_cast<const SoH3DGlVtx*>(out->groups[0].verts.data());
    cg.vertCount = (int)out->groups[0].verts.size();
    cg.texIndex = 0;
    cg.alphaTest = 0;
    cg.alphaRef = 0.0f;
    cg.wrapS = cg.wrapT = 0x2900; // GL_CLAMP (disc is centred; edges fade to black/transparent)
    cg.blendEnable = 1;
    cg.blendSrcRGB = 0x0302; // GL_SRC_ALPHA
    cg.blendDstRGB = additive ? 0x0001 : 0x0303; // GL_ONE (add) : GL_ONE_MINUS_SRC_ALPHA
    cg.blendEqRGB = 0x8006;  // GL_FUNC_ADD
    cg.blendSrcA = 0x0001;   // GL_ONE
    cg.blendDstA = additive ? 0x0001 : 0x0303;
    cg.blendEqA = 0x8006;
    for (int k = 0; k < 4; k++) cg.blendColor[k] = (k == 3) ? 1.0f : 0.0f;
    cg.depthWrite = 0; // sky element: never occlude the world
    cg.polygonOffset = 0.0f;
    cg.cull = 0;
    cg.faceCull = 0; // camera-facing billboard quad: always double-sided
    cg.meshId = -1;
    out->cGroups.push_back(cg);
    out->skinned = false;
    out->ok = true;
    printf("[SoH3D] billboard %s|%s%s: %dx%d tex\n", zarPath.c_str(), ctxbName.c_str(),
           additive ? " [add]" : "", tw, th);
}

static void loadAutoModel(int modelId, LoadedModel* out) {
    int idx = modelId - kAutoModelBase;
    if (idx < 0 || idx >= (int)g_autoModelPaths.size()) return;
    // A key may carry a forced-CMB selector: "<zar>|<cmbSubstr>". Used when one shared ZAR holds
    // several distinct objects, each needed by a DIFFERENT actor (e.g. zelda_spot01_objects.zar =
    // windmill c_s01fusya + well pillar c_s01idohashira + well water c_s01idomizu). The default
    // "largest CMB" heuristic would give every such actor the same (biggest) CMB. With a selector
    // we pick the named CMB instead; scale still auto-derives (per-actor N64 height / this CMB).
    std::string key = g_autoModelPaths[idx];
    // "BILLBOARD:" / "BILLBOARDADD:" prefix marks a standalone CTXB sprite (no CMB) drawn as a
    // camera-facing quad — the OoT3D sun/moon discs (#28e). Key = "<prefix><zar>|<ctxbName>".
    {
        bool add = false;
        const char* pfx = nullptr;
        if (key.rfind("BILLBOARDADD:", 0) == 0) { add = true; pfx = "BILLBOARDADD:"; }
        else if (key.rfind("BILLBOARD:", 0) == 0) { pfx = "BILLBOARD:"; }
        if (pfx) {
            std::string rest = key.substr(std::strlen(pfx));
            auto bar = rest.find('|');
            std::string zp = (bar == std::string::npos) ? rest : rest.substr(0, bar);
            std::string ctxb = (bar == std::string::npos) ? std::string() : rest.substr(bar + 1);
            loadBillboard(out, zp, ctxb, add);
            return;
        }
    }
    // "SKY:" prefix marks the skybox dome (a vertex-coloured, untextured CMB). It must keep its baked
    // per-vertex colour (the day/night gradient) and write NO depth (drawn behind all world geometry).
    // The renderer pins it to the far plane via the per-draw sky flag; see SoH3D_GL_Submit.
    bool sky = false;
    if (key.rfind("SKY:", 0) == 0) {
        sky = true;
        key = key.substr(4);
    }
    std::string zarPath = key;
    std::string forcedCmb;
    if (auto bar = key.find('|'); bar != std::string::npos) {
        zarPath = key.substr(0, bar);
        forcedCmb = key.substr(bar + 1);
    }
    SoH3D::CtrRom* r = rom();
    if (!r) return;
    auto zarBytes = r->read(zarPath);
    if (zarBytes.empty()) { fprintf(stderr, "[SoH3D] auto: zar not found: %s\n", zarPath.c_str()); return; }
    out->zar = std::make_unique<SoH3D::Zar>(std::move(zarBytes));
    if (!out->zar->ok()) { fprintf(stderr, "[SoH3D] auto Zar %s: %s\n", zarPath.c_str(), out->zar->error().c_str()); return; }

    // Forced-CMB selection: load exactly the named CMB (first match) and skip the heuristic.
    if (!forcedCmb.empty()) {
        for (const auto& f : out->zar->files()) {
            if (f.name.size() < 4 || f.name.compare(f.name.size() - 4, 4, ".cmb") != 0) continue;
            if (f.name.find(forcedCmb) == std::string::npos) continue;
            auto cmb = std::make_unique<SoH3D::Cmb>(out->zar->read(f));
            if (!cmb->ok()) { fprintf(stderr, "[SoH3D] auto forced-cmb %s '%s': %s\n", zarPath.c_str(), f.name.c_str(), cmb->error().c_str()); return; }
            out->cmb = std::move(cmb);
            out->skinned = out->cmb->bones().size() > 1;
            buildFromCmb(out, /*bakedVertexColor=*/sky);
            if (sky) {
                for (auto& grp : out->cGroups) grp.depthWrite = 0; // never occlude the world
            }
            printf("[SoH3D] auto-loaded model %d (%s | %s)%s: cmb '%s', height=%.1f, %zu groups, %zu textures\n",
                   modelId, zarPath.c_str(), forcedCmb.c_str(), sky ? " [sky]" : "", f.name.c_str(),
                   bboxHeight(out->groups), out->cGroups.size(), out->cTexs.size());
            return;
        }
        fprintf(stderr, "[SoH3D] auto forced-cmb %s: no cmb matches '%s' -> heuristic pick\n", zarPath.c_str(), forcedCmb.c_str());
    }

    // Hand-curated multi-part assembly? Merge exactly the named CMBs (in order) instead of
    // single-picking one (which would render one detached sub-piece). See kAssemblies.
    const AssemblySpec* asmSpec = nullptr;
    for (const auto& a : kAssemblies) {
        if (!a.zarSuffix) continue; // sentinel / empty table
        size_t n = std::strlen(a.zarSuffix);
        if (zarPath.size() >= n && zarPath.compare(zarPath.size() - n, n, a.zarSuffix) == 0) { asmSpec = &a; break; }
    }
    if (asmSpec) {
        std::vector<std::unique_ptr<SoH3D::Cmb>> cmbs;
        for (const auto& want : asmSpec->cmbNames) {
            // Each substring merges EVERY matching .cmb (in archive order), so one prefix can
            // pull a whole subassembly (e.g. "kanban_L_" = all 4 left board segments).
            int matched = 0;
            for (const auto& zf : out->zar->files()) {
                if (zf.name.size() < 4 || zf.name.compare(zf.name.size() - 4, 4, ".cmb") != 0) continue;
                if (zf.name.find(want) == std::string::npos) continue;
                auto c = std::make_unique<SoH3D::Cmb>(out->zar->read(zf));
                if (!c->ok()) { fprintf(stderr, "[SoH3D] assembly %s: '%s': %s\n", zarPath.c_str(), zf.name.c_str(), c->error().c_str()); continue; }
                cmbs.push_back(std::move(c));
                matched++;
            }
            if (!matched) fprintf(stderr, "[SoH3D] assembly %s: no cmb matches '%s'\n", zarPath.c_str(), want.c_str());
        }
        if (!cmbs.empty()) {
            size_t nMerged = cmbs.size();
            out->skinned = false; // hand-listed assemblies are static props (no skinning)
            buildFromCmbs(out, cmbs);
            out->cmb = std::move(cmbs[0]); // keep a resident CMB (the main part)
            printf("[SoH3D] auto-loaded ASSEMBLY model %d (%s): %zu cmbs merged, height=%.1f, %zu groups, %zu textures\n",
                   modelId, zarPath.c_str(), nMerged, bboxHeight(out->groups), out->cGroups.size(), out->cTexs.size());
            return;
        }
        fprintf(stderr, "[SoH3D] assembly %s: no cmbs merged -> single-pick fallback\n", zarPath.c_str());
    }

    // Pick the MAIN model CMB. Parse each candidate once (one-time per object). Prefer the
    // most-detailed real mesh: skip debris (by name) and flat billboard/sprite quads (e.g.
    // wood02's wd_model is a flat [800,655,0] decal that, picked by raw size, rendered as a
    // white quad on the ground). Among the rest, the CMB with the most vertices is the main
    // body (a 3D tree, not its sprite LOD). Fall back progressively so a ZAR with only
    // flat/debris CMBs still yields something rather than nothing.
    const SoH3D::ZarFile* best = nullptr;
    std::unique_ptr<SoH3D::Cmb> bestCmb;
    size_t bestVerts = 0;
    const SoH3D::ZarFile* fbFile = nullptr; // best non-debris (incl. flat), by diagonal
    std::unique_ptr<SoH3D::Cmb> fbCmb;
    float fbDiag = -1.0f;
    int nCmb = 0;
    for (const auto& f : out->zar->files()) {
        if (f.name.size() < 4 || f.name.compare(f.name.size() - 4, 4, ".cmb") != 0) continue;
        nCmb++;
        if (isDebrisCmbName(f.name)) continue;
        auto cmb = std::make_unique<SoH3D::Cmb>(out->zar->read(f));
        if (!cmb->ok()) continue;
        auto groups = cmb->buildDrawGroups();
        float d = bboxDiag(groups);
        if (d > fbDiag) { fbDiag = d; fbFile = &f; fbCmb = std::make_unique<SoH3D::Cmb>(out->zar->read(f)); }
        if (isFlatGroups(groups)) continue; // billboard/sprite/decal -> not the main mesh
        size_t nv = vertCountGroups(groups);
        if (nv > bestVerts) { bestVerts = nv; best = &f; bestCmb = std::move(cmb); }
    }
    if (!bestCmb) { best = fbFile; bestCmb = std::move(fbCmb); } // all flat? take largest non-debris
    // Last resort: if every CMB looked like debris (or none parsed), take the first .cmb.
    if (!bestCmb) {
        const SoH3D::ZarFile* f = out->zar->firstWithSuffix(".cmb");
        if (f) { bestCmb = std::make_unique<SoH3D::Cmb>(out->zar->read(*f)); best = f; }
    }
    if (!bestCmb || !bestCmb->ok()) { fprintf(stderr, "[SoH3D] auto: no usable .cmb in %s\n", zarPath.c_str()); return; }
    out->cmb = std::move(bestCmb);
    // Articulated (>1 bone) => skinned character. With no animation it would render in a
    // frozen bind/T-pose, so the auto path skips it and leaves the N64 model. Calibrated,
    // animated characters go through the explicit sModelTable (with an anim resolver).
    out->skinned = out->cmb->bones().size() > 1;
    // The *_new Link body bakes ALL hand-pose + held-equipment variants into one mesh, each on a
    // distinct CMB mesh_id; the game shows a state-dependent subset. We keep every variant as its
    // own draw group (buildDrawGroups now splits by mesh_id) and let the player path pick the
    // visible subset per frame via SoH3D_GL_SetMidMask. So NO build-time cull here.
    buildFromCmb(out, /*bakedVertexColor=*/false);
    appendFacialFrames(out, zarPath); // eye/mouth .cmab frames (keystone #3)
    printf("[SoH3D] auto-loaded model %d (%s): cmb '%s' of %d, height=%.1f, bones=%zu%s, %zu groups, %zu textures\n",
           modelId, zarPath.c_str(), best ? best->name.c_str() : "?", nCmb, bboxHeight(out->groups),
           out->cmb->bones().size(), out->skinned ? " (skinned->skip)" : "", out->cGroups.size(), out->cTexs.size());
}

LoadedModel* loadModel(int modelId) {
    auto it = g_loaded.find(modelId);
    if (it != g_loaded.end()) return it->second.get();

    auto lm = std::make_unique<LoadedModel>();
    LoadedModel* out = lm.get();
    g_loaded[modelId] = std::move(lm);

    if (modelId >= kAutoModelBase) {
        loadAutoModel(modelId, out);
    } else if (modelId >= kSceneModelBase) {
        loadSceneRoom(modelId, out);
    } else if (modelId >= 0 && modelId < (int)(sizeof(kModels) / sizeof(kModels[0]))) {
        loadActorModel(modelId, out);
    }
    return out;
}

// Renderer provider: hand back the CPU data for a model id (loads lazily).
int provider(int modelId, const SoH3DGlGroup** groups, int* groupCount, const SoH3DGlTex** texs, int* texCount) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || lm->cGroups.empty()) return 0;
    *groups = lm->cGroups.data();
    *groupCount = (int)lm->cGroups.size();
    *texs = lm->cTexs.data();
    *texCount = (int)lm->cTexs.size();
    return 1;
}

bool g_registered = false;

// --- Terrain warp: re-level the OoT3D room render mesh's walkable ground to the N64
// collision floor (so Link, who walks on N64 collision, stands on the visible ground)
// while preserving OoT3D cliff/mountain relief. We build a per-XZ displacement field
// D(x,z) = N64_floor - OoT3D_floor on a grid (structure outliers rejected, hole-filled
// from nearby ground), then shift every vertex Y by the bilinear sample. A whole column
// (ground + any building/cliff above it) shifts by the same local ground correction, so
// relief is preserved. Mirrors tools/soh3d_warp.py (the offline-verified oracle). ---

constexpr float kWarpStep = 100.0f;   // grid spacing (world units)
constexpr float kWarpReject = 120.0f; // |D| above this = structure, not ground -> hole-fill
constexpr float kNoFloor = -31000.0f; // floorFn returns <= this when there is no floor

// Topmost-or-nearest upward-facing (floor) triangle Y at (x,z) over a room's draw groups;
// returns false if no floor covers the point. If hasTarget, picks the floor hit closest
// to target (isolates the same surface across datasets, avoiding roof-vs-ground mixups).
static bool meshFloor(const std::vector<SoH3D::CmbDrawGroup>& groups, float x, float z, bool hasTarget,
                      float target, float* outY, bool lowest = false) {
    bool found = false;
    float best = 0.0f;
    for (const auto& g : groups) {
        const auto& v = g.verts;
        for (size_t i = 0; i + 2 < v.size(); i += 3) {
            const float* p0 = v[i].pos;
            const float* p1 = v[i + 1].pos;
            const float* p2 = v[i + 2].pos;
            float ax = p0[0], az = p0[2], bx = p1[0], bz = p1[2], cx = p2[0], cz = p2[2];
            // Cheap XZ-bbox reject first (this runs per-actor per-frame for grounding).
            if (x < ax && x < bx && x < cx) continue;
            if (x > ax && x > bx && x > cx) continue;
            if (z < az && z < bz && z < cz) continue;
            if (z > az && z > bz && z > cz) continue;
            float d = (bz - cz) * (ax - cx) + (cx - bx) * (az - cz);
            if (d > -1e-6f && d < 1e-6f) continue;
            float u = ((bz - cz) * (x - cx) + (cx - bx) * (z - cz)) / d;
            float w = ((cz - az) * (x - cx) + (ax - cx) * (z - cz)) / d;
            float t = 1.0f - u - w;
            if (u < -1e-4f || w < -1e-4f || t < -1e-4f) continue;
            // floor test: world normal.y > 0
            float ux = p1[0] - p0[0], uy = p1[1] - p0[1], uz = p1[2] - p0[2];
            float vx = p2[0] - p0[0], vy = p2[1] - p0[1], vz = p2[2] - p0[2];
            float ny = uz * vx - ux * vz;
            float nl = std::sqrt((uy * vz - uz * vy) * (uy * vz - uz * vy) + ny * ny +
                                 (ux * vy - uy * vx) * (ux * vy - uy * vx));
            if (nl < 1e-9f || ny / nl <= 0.5f) continue;
            float y = u * p0[1] + w * p1[1] + t * p2[1];
            if (!found) {
                best = y;
                found = true;
            } else if (lowest ? (y < best) : (hasTarget ? (std::fabs(y - target) < std::fabs(best - target)) : (y > best))) {
                best = y;
            }
        }
    }
    if (found) *outY = best;
    return found;
}

// Compute the per-XZ ground-delta field D(x,z) = N64_floor - OoT3D_floor for a scene room and
// store it on the model. The render mesh is NOT modified — actors are later offset by -D (so
// they stand on the visible OoT3D ground) via SoH3D_RoomGroundDeltaAt. This is the inverse of
// the old render-mesh warp, which had to smooth D and so smeared corrections across N64
// collision steps (ledges), wrongly lifting already-correct ground and floating fences/posts.
static void computeRoomGroundDelta(LoadedModel* lm, SoH3D_FloorFn floorFn) {
    if (lm->deltaReady || lm->groups.empty() || !floorFn) return;
    lm->deltaReady = true; // mark up front: a failed/no-op compute must not retry every frame

    float minx = 1e30f, maxx = -1e30f, minz = 1e30f, maxz = -1e30f;
    for (const auto& g : lm->groups)
        for (const auto& v : g.verts) {
            minx = std::min(minx, v.pos[0]); maxx = std::max(maxx, v.pos[0]);
            minz = std::min(minz, v.pos[2]); maxz = std::max(maxz, v.pos[2]);
        }
    if (minx > maxx) return;
    int nx = (int)((maxx - minx) / kWarpStep) + 2;
    int nz = (int)((maxz - minz) / kWarpStep) + 2;
    if ((long)nx * nz > 2000000) return; // sanity guard against pathological extents

    std::vector<float> D((size_t)nx * nz, 0.0f);
    std::vector<char> valid((size_t)nx * nz, 0);
    int nValid = 0;
    for (int j = 0; j < nz; j++) {
        for (int i = 0; i < nx; i++) {
            float x = minx + i * kWarpStep, z = minz + j * kWarpStep;
            float n64 = floorFn(x, z);
            if (n64 <= kNoFloor) continue;
            float oot;
            if (!meshFloor(lm->groups, x, z, true, n64, &oot)) continue;
            float d = n64 - oot;
            if (std::fabs(d) <= kWarpReject) {
                D[(size_t)j * nx + i] = d;
                valid[(size_t)j * nx + i] = 1;
                nValid++;
            }
        }
    }
    if (nValid == 0) return;

    // Hole-fill: BFS so structure / off-mesh cells inherit the nearest valid ground D (so an
    // actor over a spot with no OoT3D floor sample still gets a sane offset from nearby ground).
    std::vector<char> filled = valid;
    std::vector<int> q;
    q.reserve((size_t)nx * nz);
    for (int k = 0; k < nx * nz; k++)
        if (valid[k]) q.push_back(k);
    for (size_t head = 0; head < q.size(); head++) {
        int k = q[head], i = k % nx, j = k / nx;
        const int di[4] = { 1, -1, 0, 0 }, dj[4] = { 0, 0, 1, -1 };
        for (int e = 0; e < 4; e++) {
            int ni = i + di[e], nj = j + dj[e];
            if (ni < 0 || ni >= nx || nj < 0 || nj >= nz) continue;
            int nk = nj * nx + ni;
            if (!filled[nk]) {
                D[nk] = D[k];
                filled[nk] = 1;
                q.push_back(nk);
            }
        }
    }

    lm->delta = std::move(D);
    lm->dMinX = minx; lm->dMinZ = minz; lm->dNx = nx; lm->dNz = nz; lm->dStep = kWarpStep;
    fprintf(stderr, "[SoH3D] ground-delta field: %dx%d grid, %d ground cells (actors offset to OoT3D ground)\n",
            nx, nz, nValid);
}

} // namespace

extern "C" {

// Register the renderer's model provider once. Safe to call repeatedly.
void SoH3D_EnsureModelProvider(void) {
    if (!g_registered) {
        SoH3D_GL_SetModelProvider(provider);
        g_registered = true;
    }
}

float SoH3D_ModelScaleById(int modelId) {
    if (modelId < 0 || modelId >= (int)(sizeof(kModels) / sizeof(kModels[0]))) return 1.0f;
    return kModels[modelId].worldScale;
}

// Get-or-allocate a stable model id for a scene room, keyed by its ZSI path
// (/scene/<name>_<R>_info.zsi). The geometry loads lazily on first draw via the
// provider. Returns -1 if sceneName is null/empty. The game calls this from its
// room-draw hook with the OoT3D scene name (kSoH3dSceneNames) + room number.
int SoH3D_RoomModelId(const char* sceneName, int roomNum) {
    if (!sceneName || !*sceneName || roomNum < 0) return -1;
    std::string path = "/scene/" + std::string(sceneName) + "_" + std::to_string(roomNum) + "_info.zsi";
    auto it = g_sceneRoomIds.find(path);
    if (it != g_sceneRoomIds.end()) return it->second;
    int id = kSceneModelBase + (int)g_sceneRoomPaths.size();
    g_sceneRoomPaths.push_back(path);
    g_sceneRoomIds[path] = id;
    return id;
}

// #5 — toggle real stepped stairs. Sets the gate and evicts every cached scene-room
// model so the next draw rebuilds (with or without generated steps), for live A/B.
void SoH3D_SetStairs(int on) {
    gSoH3dStairs = on ? 1 : 0;
    for (auto it = g_loaded.begin(); it != g_loaded.end();) {
        if (it->first >= kSceneModelBase && it->first < kAutoModelBase) it = g_loaded.erase(it);
        else ++it;
    }
}
int SoH3D_GetStairs(void) { return gSoH3dStairs; }

// #5 — set the generated step rise (world-units/step). Larger = bigger steps. Drops the cached
// scene-room CPU models so the provider rebuilds their stair geometry with the new rise, and asks
// the GL layer to evict the matching uploads so the change shows live (next render pass). Collision
// keeps the previous rise until the next scene load (render is what the user is tuning here).
void SoH3D_SetStairRiserY(float v) {
    if (v < 1.0f) v = 1.0f;
    if (v == gSoH3dStairRiserY) return;
    gSoH3dStairRiserY = v;
    for (auto it = g_loaded.begin(); it != g_loaded.end();) {
        if (it->first >= kSceneModelBase && it->first < kAutoModelBase) it = g_loaded.erase(it);
        else ++it;
    }
    SoH3D_GL_RequestEvictRange(kSceneModelBase, kAutoModelBase);
}
float SoH3D_GetStairRiserY(void) { return gSoH3dStairRiserY; }

// #18 — crop a rectangle out of a top-down RGBA32 atlas (e.g. one returned by TexPackLookup) and
// box-downsample it (area-averaging, incl. alpha-weighted RGB so transparent edge pixels don't
// bleed dark color into the silhouette) to dst x dst. Returns the dst*dst*4 RGBA buffer. The HUD
// texrect path has NO mipmaps, so a large atlas crop must be pre-shrunk to a modest size or it
// shatters when minified on-screen (the #18 digit work hit this exact aliasing).
static std::vector<uint8_t> cropAndBoxDownsample(const std::vector<uint8_t>& atlas, int aw, int ah,
                                                 int cx, int cy, int cw, int ch, int dst) {
    std::vector<uint8_t> out((size_t)dst * dst * 4, 0);
    if (cw <= 0 || ch <= 0 || dst <= 0) return out;
    for (int dy = 0; dy < dst; dy++) {
        // Source row span [sy0, sy1) for this destination row.
        int sy0 = cy + dy * ch / dst;
        int sy1 = cy + (dy + 1) * ch / dst;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int dx = 0; dx < dst; dx++) {
            int sx0 = cx + dx * cw / dst;
            int sx1 = cx + (dx + 1) * cw / dst;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            double ar = 0, ag = 0, ab = 0, aa = 0, wsum = 0, asum = 0;
            for (int sy = sy0; sy < sy1; sy++) {
                if (sy < 0 || sy >= ah) continue;
                for (int sx = sx0; sx < sx1; sx++) {
                    if (sx < 0 || sx >= aw) continue;
                    const uint8_t* p = &atlas[((size_t)sy * aw + sx) * 4];
                    double a = p[3];
                    ar += p[0] * a; ag += p[1] * a; ab += p[2] * a; // alpha-weighted RGB
                    aa += a; asum += a; wsum += 1.0;
                }
            }
            uint8_t* d = &out[((size_t)dy * dst + dx) * 4];
            if (wsum > 0) {
                d[3] = (uint8_t)std::lround(aa / wsum);                  // mean alpha (coverage)
                double wa = asum > 0 ? asum : 1.0;
                d[0] = (uint8_t)std::lround(std::min(255.0, ar / wa));   // alpha-weighted mean RGB
                d[1] = (uint8_t)std::lround(std::min(255.0, ag / wa));
                d[2] = (uint8_t)std::lround(std::min(255.0, ab / wa));
            }
        }
    }
    return out;
}

// #18 — Xbox face-button HUD glyphs, now sourced as 3DS-style GRAY STONE buttons from the OoT3D
// texture pack (user approved 2026-06-20, overriding the #32 Xbox style). The pack's UI glyph atlas
// (hash 439913BD09FA2671, 4096x2048) carries a row of pre-composited circular gray buttons — a gray
// stone disc with the black letter already centered (A/B/X/Y, at atlas x=1165/1288/1411/1534, y=1280,
// 124px pitch). These are drawn UNTINTED (SoH3D_DrawXboxBtn: out.rgb=TEXEL0, out.a=TEXEL0.a*PRIM.a),
// so we crop+box-downsample each disc to 64x64 full-colour RGBA and hand it over directly — no
// compositing needed (the letter is baked into the atlas). Falls back to the embedded Xbox SVG PNGs
// when the pack is absent. Same 64x64 dims as the SVG glyphs, so the HUD layout is unchanged.
// #21 FIX — port the HUD texture identity from the N64 model to PC reality.
//
// Fast3D's texture cache (libultraship interpreter.cpp) keys textures by their raw SOURCE ADDRESS
// and is invalidated MANUALLY (Gfx_TextureCacheDelete is only called by the few actors that reuse a
// texture's memory, e.g. Boss Dodongo's animated lava). That is the N64 model: a texture lives at a
// stable, engine-managed DRAM/segment address that uniquely identifies it for the session.
//
// These SoH3D HUD textures (heart row, rupee/counter icons, button disc, digits, glyphs) are PC heap
// buffers (std::vector) decoded at runtime. Their address is NOT an engine-managed identity — malloc
// can hand us an address that a PRIOR texture occupied, was cached under, then freed WITHOUT a
// Gfx_TextureCacheDelete (most textures never call it). When that happens the very first HUD draw's
// cache lookup HITS the stale prior-tenant entry and renders its GPU texture instead of ours — garbled
// HUD that persists the whole session and clears only on restart (#21; nondeterministic because the
// heap address, and thus the collision, varies per launch).
//
// Port: a PC-allocated texture cannot trust the N64 "address == fresh identity" assumption, so when we
// first take ownership of a buffer we explicitly evict any stale cache entry left at that address. The
// buffer is allocated once and lives for the session, so a single purge at first use is sufficient and
// the next draw uploads our real pixels. (Purge can only remove a stale/wrong entry or nothing — the
// HUD buffer's own entry does not exist yet on first draw — so it never harms correct rendering.)
extern "C" void Gfx_TextureCacheDelete(const uint8_t* texAddr);
static inline void SoH3D_HudTexClaim(const void* addr) {
    if (addr != nullptr) {
        Gfx_TextureCacheDelete((const uint8_t*)addr);
    }
}

const void* SoH3D_XboxGlyphTex(char which, int* w, int* h) {
    struct Glyph { std::vector<uint8_t> rgba; int w = 0, hh = 0; };
    static Glyph g[4];
    static int tried = 0;
    if (!tried) {
        tried = 1;
        // Pack disc crop boxes in the glyph atlas (x, y, w, h), order A,B,X,Y. Square 124px discs.
        static const int kDiscX[4] = { 1165, 1288, 1411, 1534 };
        const int discY = 1280, discW = 124, discH = 124, kDst = 64;
        std::vector<uint8_t> atlas;
        int aw = 0, ah = 0;
        bool havePack = SoH3D::TexPackLookup(0x439913BD09FA2671ULL, aw, ah, atlas) && aw > 0 && ah > 0;
        const unsigned char* png[4] = { kXboxGlyphAPng, kXboxGlyphBPng, kXboxGlyphXPng, kXboxGlyphYPng };
        unsigned int len[4] = { kXboxGlyphAPngLen, kXboxGlyphBPngLen, kXboxGlyphXPngLen, kXboxGlyphYPngLen };
        for (int i = 0; i < 4; i++) {
            if (havePack) {
                g[i].rgba = cropAndBoxDownsample(atlas, aw, ah, kDiscX[i], discY, discW, discH, kDst);
                g[i].w = kDst; g[i].hh = kDst;
                continue;
            }
            int sw = 0, sh = 0, n = 0;
            stbi_uc* px = stbi_load_from_memory(png[i], (int)len[i], &sw, &sh, &n, 4);
            if (px) {
                g[i].rgba.assign(px, px + (size_t)sw * sh * 4);
                g[i].w = sw; g[i].hh = sh;
                stbi_image_free(px);
            } else {
                fprintf(stderr, "[SoH3D] xbox glyph %d: PNG decode failed\n", i);
            }
        }
    }
    int idx;
    switch (which) {
        case 'A': case 'a': idx = 0; break;
        case 'B': case 'b': idx = 1; break;
        case 'X': case 'x': idx = 2; break;
        case 'Y': case 'y': idx = 3; break;
        default: if (w) *w = 0; if (h) *h = 0; return nullptr;
    }
    if (g[idx].rgba.empty()) { if (w) *w = 0; if (h) *h = 0; return nullptr; }
    static bool reg = false;
    if (!reg) {
        reg = true; // #21: evict any stale prior-tenant cache entry at each glyph buffer's address
        for (int k = 0; k < 4; k++) {
            if (!g[k].rgba.empty()) SoH3D_HudTexClaim(g[k].rgba.data());
        }
    }
    if (w) *w = g[idx].w;
    if (h) *h = g[idx].hh;
    return g[idx].rgba.data();
}

// #18 — derive the FULL / EMPTY HUD heart from the OoT3D item atlas (user approved 2026-06-20).
// The clean red heart icon lives in the pack item atlas (hash CF461E58E637A97A, 4096x4096) at
// x=2018..2338, y=3020..3352 (a red heart with a light rim). The lifemeter combine is
// out=(PRIM-ENV)*TEXEL0+ENV on rgb and reads TEXEL0.rgb as the PRIM<->ENV lerp factor, alpha as the
// silhouette (see z_lifemeter.c HealthMeter_Draw). So the heart's rgb must be an INTENSITY (bright
// body -> tints to PRIM red, dark -> ENV). The saturated-red core has low luminance but high VALUE,
// so we use value = max(r,g,b) as the intensity (luminance would make the red body dark -> wrong);
// alpha stays the silhouette. EMPTY uses the same silhouette with rgb pinned low (0x20, the SVG
// empty-heart level) so it lerps toward ENV (dark). Both box-downsampled to 64x64 (same as the SVG
// hearts) so they align in the row and render crisp under HUD minification.
static void heartPackVariant(const std::vector<uint8_t>& atlas, int aw, int ah, bool empty,
                             std::vector<uint8_t>& outRgba, int& ow, int& oh) {
    const int hx = 2018, hy = 3020, hw = 320, hh = 332, kDst = 64;
    std::vector<uint8_t> crop = cropAndBoxDownsample(atlas, aw, ah, hx, hy, hw, hh, kDst);
    for (size_t i = 0; i < (size_t)kDst * kDst; i++) {
        uint8_t* p = &crop[i * 4];
        if (empty) {
            p[0] = p[1] = p[2] = 0x20; // dark -> lerps toward ENV, matching the SVG empty heart
        } else {
            uint8_t v = std::max(p[0], std::max(p[1], p[2])); // value = bright body & rim highlight
            p[0] = p[1] = p[2] = v;
        }
    }
    outRgba.swap(crop);
    ow = kDst; oh = kDst;
}

// #31/#18 — crisp higher-res HUD heart textures. Kinds 0 (full) and 4 (empty) are derived from the
// OoT3D item-atlas heart in the texture pack (grayscale-value rgb=intensity, a=silhouette; see
// heartPackVariant); kinds 1/2/3 (3-4, 1/2, 1-4) stay the embedded SVG hearts (the pack has no
// fractional hearts). Falls back entirely to the embedded SVG PNGs when the pack is absent. `kind`
// is SOH3D_HEART_* (0..4). Returns the buffer + dims, or NULL on failure. The N64 heart combine
// reads TEXEL0.rgb as the PRIM<->ENV lerp factor, so the grayscale heart tints exactly like IA8.
const void* SoH3D_HeartTex(int kind, int* w, int* h) {
    struct Tex { std::vector<uint8_t> rgba; int w = 0, hh = 0; };
    static Tex t[5];
    static int tried = 0;
    if (!tried) {
        tried = 1;
        const unsigned char* png[5] = { kHeartFullPng, kHeartThreeQuarterPng, kHeartHalfPng,
                                        kHeartQuarterPng, kHeartEmptyPng };
        unsigned int len[5] = { kHeartFullPngLen, kHeartThreeQuarterPngLen, kHeartHalfPngLen,
                                kHeartQuarterPngLen, kHeartEmptyPngLen };
        std::vector<uint8_t> atlas;
        int aw = 0, ah = 0;
        bool havePack = SoH3D::TexPackLookup(0xCF461E58E637A97AULL, aw, ah, atlas) && aw > 0 && ah > 0;
        for (int i = 0; i < 5; i++) {
            if (havePack && (i == 0 || i == 4)) {
                heartPackVariant(atlas, aw, ah, /*empty=*/i == 4, t[i].rgba, t[i].w, t[i].hh);
                continue;
            }
            int sw = 0, sh = 0, n = 0;
            stbi_uc* px = stbi_load_from_memory(png[i], (int)len[i], &sw, &sh, &n, 4);
            if (px) {
                t[i].rgba.assign(px, px + (size_t)sw * sh * 4);
                t[i].w = sw; t[i].hh = sh;
                stbi_image_free(px);
            } else {
                fprintf(stderr, "[SoH3D] heart tex %d: PNG decode failed\n", i);
            }
        }
    }
    if (kind < 0 || kind >= 5 || t[kind].rgba.empty()) {
        if (w) *w = 0; if (h) *h = 0; return nullptr;
    }
    static bool reg = false;
    if (!reg) {
        reg = true; // #21: evict any stale prior-tenant cache entry at each heart buffer's address
        for (int k = 0; k < 5; k++) {
            if (!t[k].rgba.empty()) SoH3D_HudTexClaim(t[k].rgba.data());
        }
    }
    if (w) *w = t[kind].w;
    if (h) *h = t[kind].hh;
    return t[kind].rgba.data();
}

// #31 — crisp HUD button-background disc (round beveled circle behind the B / C / A buttons).
// Decode the embedded PNG once into persistent RGBA32 (grayscale, a=coverage). Returns the buffer
// + dims, or NULL on failure. The button combine is G_CC_MODULATEIA_PRIM, so the grayscale disc
// tints to each button's PRIM colour exactly like the original 32x32 IA8 gButtonBackgroundTex.
const void* SoH3D_ButtonBgTex(int* w, int* h) {
    static std::vector<uint8_t> rgba;
    static int bw = 0, bh = 0;
    static int tried = 0;
    if (!tried) {
        tried = 1;
        // #18 — prefer the OoT3D texture pack's HD button-bg disc (filename hash 08F40E3D6D548398),
        // a grayscale white beveled disc on transparency that tints via MODULATEIA_PRIM exactly like
        // the SVG (white center -> PRIM colour, dark rim stays dark). Loaded at runtime from the
        // gitignored pack (never embedded/committed — it is a game asset). Falls back to the embedded
        // SVG disc when the pack isn't present. (Pack returns top-down RGBA32; this pack is flip=0.)
        std::vector<uint8_t> pk;
        int pw = 0, ph = 0;
        if (SoH3D::TexPackLookup(0x08F40E3D6D548398ULL, pw, ph, pk) && pw > 0 && ph > 0) {
            rgba.swap(pk);
            bw = pw; bh = ph;
        } else {
            int sw = 0, sh = 0, n = 0;
            stbi_uc* px = stbi_load_from_memory(kButtonBgPng, (int)kButtonBgPngLen, &sw, &sh, &n, 4);
            if (px) {
                rgba.assign(px, px + (size_t)sw * sh * 4);
                bw = sw; bh = sh;
                stbi_image_free(px);
            } else {
                fprintf(stderr, "[SoH3D] button bg tex: PNG decode failed\n");
            }
        }
    }
    if (rgba.empty()) { if (w) *w = 0; if (h) *h = 0; return nullptr; }
    static bool reg = false;
    if (!reg) { reg = true; SoH3D_HudTexClaim(rgba.data()); } // #21: evict stale cache entry at this addr
    if (w) *w = bw;
    if (h) *h = bh;
    return rgba.data();
}

// #31 — crisp HUD counter icons (0=rupee gem, 1=small key, 2=clock). Decode the embedded PNGs once
// into persistent RGBA32 (grayscale, a=coverage). Returns the buffer + dims, or NULL on failure.
// Rupee/key draw MODULATEIA_PRIM (PRIM tints the grayscale facet shading); the clock draws
// MODULATERGBA_PRIM with PRIM white (grayscale shown directly). All three are 16x16 IA8 in N64.
const void* SoH3D_CounterIconTex(int kind, int* w, int* h) {
    struct Tex { std::vector<uint8_t> rgba; int w = 0, hh = 0; };
    static Tex t[3];
    static int tried = 0;
    if (!tried) {
        tried = 1;
        const unsigned char* png[3] = { kRupeeIconPng, kSmallKeyIconPng, kClockIconPng };
        unsigned int len[3] = { kRupeeIconPngLen, kSmallKeyIconPngLen, kClockIconPngLen };
        for (int i = 0; i < 3; i++) {
            int sw = 0, sh = 0, n = 0;
            stbi_uc* px = stbi_load_from_memory(png[i], (int)len[i], &sw, &sh, &n, 4);
            if (px) {
                t[i].rgba.assign(px, px + (size_t)sw * sh * 4);
                t[i].w = sw; t[i].hh = sh;
                stbi_image_free(px);
            } else {
                fprintf(stderr, "[SoH3D] counter icon %d: PNG decode failed\n", i);
            }
        }
    }
    if (kind < 0 || kind >= 3 || t[kind].rgba.empty()) {
        if (w) *w = 0; if (h) *h = 0; return nullptr;
    }
    static bool reg = false;
    if (!reg) {
        reg = true; // #21: evict any stale prior-tenant cache entry at each counter-icon buffer's address
        for (int k = 0; k < 3; k++) {
            if (!t[k].rgba.empty()) SoH3D_HudTexClaim(t[k].rgba.data());
        }
    }
    if (w) *w = t[kind].w;
    if (h) *h = t[kind].hh;
    return t[kind].rgba.data();
}

// #31 — crisp HUD counter font (0..9 = digit, 10 = ':'). Decode the embedded PNGs once into
// persistent RGBA32 (grayscale, a=coverage). Returns the buffer + dims, or NULL on failure.
const void* SoH3D_DigitTex(int glyph, int* w, int* h) {
    struct Tex { std::vector<uint8_t> rgba; int w = 0, hh = 0; };
    static Tex t[11];
    static int tried = 0;
    if (!tried) {
        tried = 1;
        const unsigned char* png[11] = { kDigit0Png, kDigit1Png, kDigit2Png, kDigit3Png, kDigit4Png,
                                         kDigit5Png, kDigit6Png, kDigit7Png, kDigit8Png, kDigit9Png,
                                         kDigitColonPng };
        unsigned int len[11] = { kDigit0PngLen, kDigit1PngLen, kDigit2PngLen, kDigit3PngLen, kDigit4PngLen,
                                 kDigit5PngLen, kDigit6PngLen, kDigit7PngLen, kDigit8PngLen, kDigit9PngLen,
                                 kDigitColonPngLen };
        for (int i = 0; i < 11; i++) {
            int sw = 0, sh = 0, n = 0;
            stbi_uc* px = stbi_load_from_memory(png[i], (int)len[i], &sw, &sh, &n, 4);
            if (px) {
                t[i].rgba.assign(px, px + (size_t)sw * sh * 4);
                t[i].w = sw; t[i].hh = sh;
                stbi_image_free(px);
            } else {
                fprintf(stderr, "[SoH3D] digit tex %d: PNG decode failed\n", i);
            }
        }
    }
    if (glyph < 0 || glyph >= 11 || t[glyph].rgba.empty()) {
        if (w) *w = 0; if (h) *h = 0; return nullptr;
    }
    static bool reg = false;
    if (!reg) {
        reg = true; // #21: evict any stale prior-tenant cache entry at each digit buffer's address
        for (int k = 0; k < 11; k++) {
            if (!t[k].rgba.empty()) SoH3D_HudTexClaim(t[k].rgba.data());
        }
    }
    if (w) *w = t[glyph].w;
    if (h) *h = t[glyph].hh;
    return t[glyph].rgba.data();
}

// Get-or-allocate a stable model id for an auto-replaced actor model, keyed by its ZAR
// path (e.g. "/actor/zelda_box.zar"). The geometry loads lazily on first draw via the
// provider. Returns -1 if zarPath is null/empty. The game calls this from the SOH3D_AUTO
// actor path with the ZAR resolved from the actor's object id (kSoH3dObjectZars).
int SoH3D_AutoModelId(const char* zarPath) {
    if (!zarPath || !*zarPath) return -1;
    std::string path(zarPath);
    auto it = g_autoModelIds.find(path);
    if (it != g_autoModelIds.end()) return it->second;
    int id = kAutoModelBase + (int)g_autoModelPaths.size();
    g_autoModelPaths.push_back(path);
    g_autoModelIds[path] = id;
    return id;
}

// Local-space Y (height) extent of a loaded model. The auto-scale path uses it as the
// OoT3D-side size when deriving worldScale = N64_world_height / model_height. Loads the
// model lazily; returns 0 if it failed to load or has no geometry.
float SoH3D_AutoModelHeight(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return 0.0f;
    return bboxHeight(lm->groups);
}

// Bind-pose local-space minimum Y of a model (its lowest vertex, i.e. the feet). The N64-anim
// auto path uses groundOffset = -minY so the model's feet land on the actor's world Y (ground)
// after scaling. Returns 0 if no geometry.
float SoH3D_AutoModelMinY(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return 0.0f;
    float mn = 1e30f;
    for (const auto& g : lm->groups)
        for (const auto& v : g.verts) mn = std::min(mn, v.pos[1]);
    return (mn < 1e29f) ? mn : 0.0f;
}

// Local-space XZ extents (full X span, full Z span) of a loaded model's bind-pose geometry.
// Used to size a FLAT prop (e.g. the well-water plane, #2) to a world target rectangle: a
// flat plane has ~zero height, so the height/diagonal auto-scale can't size it — its footprint
// must be matched to the XZ target instead. Returns 1 with the spans, or 0 if no geometry.
int SoH3D_AutoModelExtentXZ(int modelId, float* outX, float* outZ) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return 0;
    float minx = 1e30f, maxx = -1e30f, minz = 1e30f, maxz = -1e30f;
    for (const auto& g : lm->groups)
        for (const auto& v : g.verts) {
            minx = std::min(minx, v.pos[0]); maxx = std::max(maxx, v.pos[0]);
            minz = std::min(minz, v.pos[2]); maxz = std::max(maxz, v.pos[2]);
        }
    if (maxx < minx || maxz < minz) return 0;
    if (outX) *outX = maxx - minx;
    if (outZ) *outZ = maxz - minz;
    return 1;
}

// 1 if a loaded auto model is skinned (articulated skeleton -> the auto path leaves it to
// N64 to avoid a frozen T-pose), else 0. Loads the model lazily; treats a load failure as
// "skinned" (==skip) so a bad model never auto-replaces.
int SoH3D_AutoModelSkinned(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return 1;
    return lm->skinned ? 1 : 0;
}

// The ZAR path an auto model was allocated from (e.g. "/actor/zelda_kw1.zar"), or NULL. Lets the
// actor draw path identify WHICH model is loaded by archive name (stable), since the numeric model
// id is allocation-order dependent. Used to pick a shared-CMB variant subset (e.g. En_Ko Kokiri
// kids: kokiripeople/kokirimaster bake multiple head variants on distinct mesh_ids).
const char* SoH3D_AutoModelZar(int modelId) {
    int idx = modelId - kAutoModelBase;
    if (idx < 0 || idx >= (int)g_autoModelPaths.size()) return nullptr;
    return g_autoModelPaths[idx].c_str();
}

// Number of bones in a loaded model's OoT3D skeleton (0 if none/failed). The N64-anim retarget
// maps N64 jointTable[i+1] -> OoT3D bone i, so a correct retarget needs the OoT3D bone count to
// match the actor's N64 limb count; the auto path uses this to refuse mismatched rigs (which
// would pose giant/malformed) and fall back to N64.
int SoH3D_AutoModelBoneCount(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) return 0;
    return (int)lm->cmb->bones().size();
}

// Facial material-anim (keystone #3): the GL texture index of the eye/mouth material's frame-N
// sprite (decoded from the sibling .cmab at load; see appendFacialFrames / kFacialAssets). Returns
// -1 if this model has no facial frames for `materialIndex` or `frame` is out of range. The override
// driver (soh3d_anim_override.cpp) reads the live N64 eye/mouth index and binds this via
// SoH3D_GL_SetMatTexOverride. Returns the frame count for this material when frame < 0 (query).
int SoH3D_FacialFrameTex(int modelId, int materialIndex, int frame) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return -1;
    auto it = lm->facialFrames.find(materialIndex);
    if (it == lm->facialFrames.end()) return -1;
    if (frame < 0) return (int)it->second.size();
    if (frame >= (int)it->second.size()) return -1;
    return it->second[frame];
}

// Sum of OoT3D bone lengths (|local translation| of every non-root bone) for a loaded model.
// Rotation-invariant skeleton "size". The N64 actor and the OoT3D model are the SAME character
// (Grezzo port), so (Σ N64 jointPos lengths × actor->scale) / (Σ OoT3D bone-trans lengths) is
// the correct OoT3D->world scale — independent of pose and free of the bbox-measure overshoot
// that made skinned auto-actors giant. Returns 0 if no skeleton.
float SoH3D_AutoModelBoneLenSum(int modelId, int boneCap) {
    (void)boneCap; // see #13: capping at limbCount REGRESSED working actors (ratio 1.0 -> 1.2-1.4),
                   // so bones with id>=limbCount are needed for the sum to match on normal rigs.
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) return 0.0f;
    float sum = 0.0f;
    for (const auto& bn : lm->cmb->bones()) {
        if (bn.parent < 0) continue; // root translation is a placement, not a bone length
        sum += std::sqrt(bn.trans[0] * bn.trans[0] + bn.trans[1] * bn.trans[1] + bn.trans[2] * bn.trans[2]);
    }
    return sum;
}

// Default (idle) animation for an auto-replaced model: the OoT3D model plays its OWN authored
// CSAB instead of retargeting N64 joints (which explodes on divergent rigs). Scans the ZAR's
// Anim/*.csab and prefers an idle-looking one ("wait"/"stand"/"matsu"/"_w"), else the first.
// Returns the base name (no "Anim/"/".csab"), or NULL if the model has no animations. Cached.
const char* SoH3D_AutoModelDefaultAnim(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->zar) return nullptr;
    if (!lm->defaultAnimDone) {
        lm->defaultAnimDone = 1;
        std::string first, idle;
        for (const auto& f : lm->zar->files()) {
            const std::string& n = f.name;
            if (n.size() < 5 || n.compare(n.size() - 5, 5, ".csab") != 0) continue;
            std::string base = n;
            if (base.rfind("Anim/", 0) == 0) base = base.substr(5);
            if (base.size() > 5) base = base.substr(0, base.size() - 5); // strip .csab
            if (first.empty()) first = base;
            std::string low = base;
            for (char& c : low) if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
            if (idle.empty() && (low.find("wait") != std::string::npos || low.find("stand") != std::string::npos ||
                                 low.find("matsu") != std::string::npos || low.find("_w") != std::string::npos)) {
                idle = base;
            }
        }
        lm->defaultAnim = !idle.empty() ? idle : first;
        fprintf(stderr, "[SoH3D] model %d default anim = '%s'\n", modelId,
                lm->defaultAnim.empty() ? "(none)" : lm->defaultAnim.c_str());
    }
    return lm->defaultAnim.empty() ? nullptr : lm->defaultAnim.c_str();
}

// Does this model's OWN resident ZAR contain the CSAB `base` (base name, "Anim/x.csab", or a
// verbatim zar path)? Mirrors getCsab's resolution: exact "Anim/<base>.csab", or — for a bare base —
// any file ending "/<base>.csab" (the link-style age-split dirs). Used to reject a generic N64->CSAB
// map hit whose CSAB lives in a DIFFERENT skeleton's zar (e.g. the Kokiri-kid os_anime entries that
// also match En_Hy adults but resolve to km1/kw1 CSABs absent from the boj/ahg body zars -> #73
// T-pose). Returns 0 (no model / no zar / not present) so the caller can fall back to the default idle.
extern "C" int SoH3D_AutoModelHasCsab(int modelId, const char* base) {
    if (base == nullptr || *base == '\0') return 0;
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->zar) return 0;
    std::string nm(base);
    bool verbatim = nm.rfind("Anim/", 0) == 0 || (nm.size() > 5 && nm.compare(nm.size() - 5, 5, ".csab") == 0);
    std::string full = verbatim ? nm : ("Anim/" + nm + ".csab");
    for (const auto& f : lm->zar->files()) if (f.name == full) return 1;
    if (!verbatim) {
        std::string suffix = "/" + nm + ".csab";
        for (const auto& f : lm->zar->files())
            if (f.name.size() >= suffix.size() &&
                f.name.compare(f.name.size() - suffix.size(), suffix.size(), suffix) == 0)
                return 1;
    }
    return 0;
}

// LIVE anim-compare tooling: list a model's CSAB base names (+ duration) into `out`, space-separated
// as "base(duration)". For REPL `animlist` so the live comparer knows which CSABs to `animforce`.
extern "C" void SoH3D_AutoModelCsabList(int modelId, char* out, int outsz) {
    if (out == nullptr || outsz <= 0) return;
    out[0] = '\0';
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->zar) return;
    int pos = 0;
    for (const auto& f : lm->zar->files()) {
        const std::string& n = f.name;
        if (n.size() < 5 || n.compare(n.size() - 5, 5, ".csab") != 0) continue;
        std::string base = n;
        if (base.rfind("Anim/", 0) == 0) base = base.substr(5);
        base = base.substr(0, base.size() - 5); // strip .csab
        int dur = -1;
        auto cmb = lm->cmb.get();
        if (cmb) { SoH3D::Csab c(lm->zar->read(f)); if (c.ok()) dur = (int)c.duration(); }
        int w = snprintf(out + pos, (size_t)(outsz - pos), "%s%s(%d)", pos ? " " : "", base.c_str(), dur);
        if (w <= 0 || w >= outsz - pos) break;
        pos += w;
    }
}

// ORACLE DUMP (gated by the caller): print the OoT3D model's skeleton — per-bone rest
// translation/rotation/scale + parent + world-space rest position (FK), plus mesh height and
// the world rest extent (bone span). Used offline to design the programmatic N64<->OoT3D scale
// and bone-correspondence (see PROGRESS "replace ALL characters"). stderr, parseable.
void SoH3D_DumpModelBones(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) {
        fprintf(stderr, "[SKELDUMP] OOT3D model %d: no cmb\n", modelId);
        return;
    }
    const auto& bones = lm->cmb->bones();
    const auto& bm = lm->cmb->boneMatrices();
    float minY = 1e30f, maxY = -1e30f;
    for (const auto& bn : bones) {
        if (bn.id >= 0 && (size_t)bn.id < bm.size()) {
            float wy = bm[bn.id][7];
            minY = std::min(minY, wy);
            maxY = std::max(maxY, wy);
        }
    }
    float boneSpanY = (minY <= maxY) ? (maxY - minY) : 0.0f;
    fprintf(stderr, "[SKELDUMP] OOT3D model %d: %zu bones meshH=%.2f boneSpanY=%.2f\n", modelId, bones.size(),
            bboxHeight(lm->groups), boneSpanY);
    for (const auto& bn : bones) {
        float wx = 0, wy = 0, wz = 0;
        if (bn.id >= 0 && (size_t)bn.id < bm.size()) {
            wx = bm[bn.id][3];
            wy = bm[bn.id][7];
            wz = bm[bn.id][11];
        }
        fprintf(stderr,
                "[SKELDUMP] OOT3D b id=%d parent=%d trans=(%.3f,%.3f,%.3f) rot=(%.4f,%.4f,%.4f) "
                "scale=(%.3f,%.3f,%.3f) world=(%.2f,%.2f,%.2f)\n",
                bn.id, bn.parent, bn.trans[0], bn.trans[1], bn.trans[2], bn.rot[0], bn.rot[1], bn.rot[2],
                bn.scale[0], bn.scale[1], bn.scale[2], wx, wy, wz);
    }
    fflush(stderr);
}

// Render-mesh floor height at world (x,z) for a loaded scene-room model (the warped
// geometry, since the warp runs in-place). Returns 0 and leaves *outY untouched if no
// floor covers the point or the model is not a loaded scene room. For verifying that the
// warp made the drawn ground match N64 (compare to the REPL `floorat`).
int SoH3D_RoomMeshFloorAt(int modelId, float x, float z, float* outY) {
    if (modelId < kSceneModelBase) return 0;
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return 0;
    return meshFloor(lm->groups, x, z, false, 0.0f, outY) ? 1 : 0;
}

// OoT3D render-mesh floor Y at (x,z) for a scene room, picking the floor hit CLOSEST to
// `target` (the actor's N64 floor, so multi-level spots pick the right surface). Returns 1 +
// *outY on a hit. Used to ground actors exactly on the visible OoT3D ground (per-actor, so
// meshFloor's XZ-bbox reject keeps it cheap). Exact — no grid approximation.
int SoH3D_RoomOoT3DFloorAt(int modelId, float x, float z, float target, float* outY) {
    if (modelId < kSceneModelBase) return 0;
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return 0;
    return meshFloor(lm->groups, x, z, /*hasTarget=*/true, target, outY) ? 1 : 0;
}

// Compute & cache a scene-room model's ground-delta field (N64 - OoT3D per XZ), once.
// `floorFn` raycasts the N64 collision (provided by soh3d.c, which has the PlayState). The
// render mesh is NOT modified; actors are offset by -D via SoH3D_RoomGroundDeltaAt. Call
// before the room is first drawn.
void SoH3D_ComputeRoomGroundDelta(int modelId, SoH3D_FloorFn floorFn) {
    if (modelId < kSceneModelBase) return; // scene rooms only
    LoadedModel* lm = loadModel(modelId);
    if (lm && lm->ok) computeRoomGroundDelta(lm, floorFn);
}

// Sample the cached ground-delta field: *outD = N64_floor - OoT3D_floor at world (x,z) for a
// scene room (bilinear). Returns 1 on success, 0 if the model isn't a scene room or the field
// isn't ready. Actors add -(*outD) to their render Y to stand on the visible OoT3D ground.
int SoH3D_RoomGroundDeltaAt(int modelId, float x, float z, float* outD) {
    if (modelId < kSceneModelBase) return 0;
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->deltaReady || lm->delta.empty()) return 0;
    float fx = (x - lm->dMinX) / lm->dStep, fz = (z - lm->dMinZ) / lm->dStep;
    int ix = (int)std::floor(fx), iz = (int)std::floor(fz);
    float tx = fx - ix, tz = fz - iz;
    auto cell = [&](int i, int j) -> float {
        i = i < 0 ? 0 : (i >= lm->dNx ? lm->dNx - 1 : i);
        j = j < 0 ? 0 : (j >= lm->dNz ? lm->dNz - 1 : j);
        return lm->delta[(size_t)j * lm->dNx + i];
    };
    *outD = cell(ix, iz) * (1 - tx) * (1 - tz) + cell(ix + 1, iz) * tx * (1 - tz) +
            cell(ix, iz + 1) * (1 - tx) * tz + cell(ix + 1, iz + 1) * tx * tz;
    return 1;
}

} // extern "C"

// Get-or-load the parsed CSAB for `animName` (base name or full "Anim/<n>.csab"),
// caching it on the model. Returns nullptr if missing/unparseable (logged once via
// the cached null entry). Shared by the frame- and phase-based update entry points.
static SoH3D::Csab* getCsab(LoadedModel* lm, const char* animName) {
    std::string nm(animName);
    // Accept three forms: a bare base ("ge1_s_wait" -> "Anim/ge1_s_wait.csab", the common case),
    // an explicit "Anim/..." path, or any verbatim zar-relative .csab path. The link rig stores its
    // CSABs under "boy/anim/" / "child/anim/" (not "Anim/") AND splits them across those two dirs by
    // age, so for a bare base we also do a basename scan: match any file ending "/<base>.csab". Each
    // zar holds exactly one file per basename, so this resolves the age dir automatically.
    bool verbatim = nm.rfind("Anim/", 0) == 0 || (nm.size() > 5 && nm.compare(nm.size() - 5, 5, ".csab") == 0);
    std::string full = verbatim ? nm : ("Anim/" + nm + ".csab");
    auto it = lm->anims.find(full);
    if (it == lm->anims.end()) {
        const SoH3D::ZarFile* af = nullptr;
        for (const auto& f : lm->zar->files()) if (f.name == full) { af = &f; break; }
        if (!af && !verbatim) { // basename fallback: "<base>.csab" anywhere in the zar (link boy/child/anim)
            std::string suffix = "/" + nm + ".csab";
            for (const auto& f : lm->zar->files()) {
                if (f.name.size() >= suffix.size() && f.name.compare(f.name.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    af = &f; full = f.name; // cache under the real path so a re-resolve hits directly
                    break;
                }
            }
            auto it2 = lm->anims.find(full); // the resolved path may already be cached
            if (it2 != lm->anims.end()) return it2->second.get();
        }
        std::unique_ptr<SoH3D::Csab> csab;
        if (af) {
            csab = std::make_unique<SoH3D::Csab>(lm->zar->read(*af));
            if (!csab->ok()) { fprintf(stderr, "[SoH3D] Csab %s: %s\n", full.c_str(), csab->error().c_str()); csab.reset(); }
        } else {
            fprintf(stderr, "[SoH3D] anim not found: %s\n", full.c_str());
        }
        it = lm->anims.emplace(full, std::move(csab)).first;
    }
    return it->second.get();
}

// Retarget a live N64 SkelAnime pose onto the OoT3D skeleton. `jointRots` points to the
// actor's per-limb rotations (jointTable[1..limbCount], each a Vec3s of binang x,y,z; the
// caller skips jointTable[0] which is the root translation). OoT3D bone id i corresponds to
// N64 limb (i+1) for same-rig characters (Grezzo preserved the skeletons), so bone i takes
// jointRots[i]. The N64 jointTable already encodes each limb's FULL local orientation (the
// standing pose's big rotations included -- e.g. En_Ge1 limb1 = (-90,0,-90), matching OoT3D
// bone0's rest), exactly like a CSAB rotation track REPLACES the bone's rest rotation. So we
// use the N64 rotation as the local rotation directly (Rz*Ry*Rx, same order as csab.cpp) and
// do NOT compose it with the CMB rest rotation -- composing double-applies the orientation and
// contorts the pose. Convention derived QUANTITATIVELY (tools/soh3d_anim_derive.py: diff CSAB
// ge1_s_wait skin matrices vs N64-joint-driven ones -> struct=replace, euler order ZYX wins
// over every compose variant). L = T(rest)*Rz*Ry*Rx(n64)*S(rest); skin = animWorld*bindInverse.
extern "C" void SoH3D_UpdateAnimN64Mapped(int modelId, const int16_t* jointRots, int rotCount,
                                          const signed char* boneToLimb, int mapCount);

// Posed-feet grounding (#29b): cache this frame's skin matrices for a tracked model. Defined near
// SoH3D_UpdateAnim (inside the same extern "C" region); forward-declared here so the N64-retarget
// update paths below can call it too.
extern "C" {
static void cacheSkinForGround(int modelId, const std::vector<std::array<float, 16>>& sm);
}

extern "C" void SoH3D_UpdateAnimN64(int modelId, const int16_t* jointRots, int rotCount) {
    SoH3D_UpdateAnimN64Mapped(modelId, jointRots, rotCount, nullptr, 0);
}

// As SoH3D_UpdateAnimN64, but with an explicit OoT3D-bone -> N64-limb correspondence
// (`boneToLimb`, indexed by bone id; -1 = no live joint -> keep rest). NULL map = identity
// (bone i <- limb i), the same-rig assumption. The map is the precomputed correspondence
// (tools/soh3d_skel_match.py -> soh3d_bonemap.inc), needed for rigs whose topology differs
// from the N64 skeleton (OoT3D inserts root/reorient bones). See PROGRESS "replace ALL chars".
extern "C" void SoH3D_UpdateAnimN64Mapped(int modelId, const int16_t* jointRots, int rotCount,
                                          const signed char* boneToLimb, int mapCount) {
    using namespace SoH3D;
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }
    const auto& bones = lm->cmb->bones();
    const auto& bind = lm->cmb->boneMatrices();
    const float kBinangToRad = 3.14159265358979f / 32768.0f;

    std::vector<Mat4> aw(bind.size(), matId());
    std::vector<char> done(bind.size(), 0);
    std::vector<const CmbBone*> byId(bind.size(), nullptr);
    for (const auto& bn : bones)
        if (bn.id >= 0 && (size_t)bn.id < byId.size()) byId[bn.id] = &bn;

    std::function<Mat4(int)> world = [&](int id) -> Mat4 {
        if (id < 0 || (size_t)id >= aw.size() || !byId[id]) return matId();
        if (done[id]) return aw[id];
        const CmbBone* bn = byId[id];
        Mat4 L = matT(bn->trans[0], bn->trans[1], bn->trans[2]);
        // limb = the N64 limb whose rotation drives this OoT3D bone: the precomputed map if
        // present, else identity (bone id == limb index).
        int limb = boneToLimb ? (id < mapCount ? (int)boneToLimb[id] : -1) : id;
        if (limb >= 0 && limb < rotCount) {
            // Use the N64 joint rotation AS the bone's local rotation (replacing the CMB rest
            // rotation), in csab.cpp's Rz*Ry*Rx order. The jointTable already carries the full
            // limb orientation, so composing it with the rest rotation double-applies and
            // contorts (verified by tools/soh3d_anim_derive.py: replace beats compose).
            float rx = jointRots[limb * 3 + 0] * kBinangToRad;
            float ry = jointRots[limb * 3 + 1] * kBinangToRad;
            float rz = jointRots[limb * 3 + 2] * kBinangToRad;
            L = matMul(L, matMul(matMul(matRz(rz), matRy(ry)), matRx(rx)));
        } else {
            // No live joint for this bone: keep its CMB rest orientation (bind pose).
            L = matMul(L, matMul(matMul(matRz(bn->rot[2]), matRy(bn->rot[1])), matRx(bn->rot[0])));
        }
        L = matMul(L, matS(bn->scale[0], bn->scale[1], bn->scale[2]));
        Mat4 W = (bn->parent < 0) ? L : matMul(world(bn->parent), L);
        aw[id] = W;
        done[id] = 1;
        return W;
    };
    for (const auto& bn : bones) world(bn.id);

    std::vector<std::array<float, 16>> sm(bind.size());
    for (size_t id = 0; id < bind.size(); id++) sm[id] = matMul(aw[id], matInverse(bind[id]));
    cacheSkinForGround(modelId, sm); // posed-feet grounding for the player path (#29b)
    // Upload bind for correct rigid pose interpolation (see SoH3D_UpdateAnim / interpSkinPose).
    SoH3D_GL_SetBoneBind(modelId, bind.empty() ? nullptr : bind.front().data(), (int)bind.size());
    SoH3D_GL_SetBones(modelId, sm.empty() ? nullptr : sm.front().data(), (int)sm.size());
}

// As SoH3D_UpdateAnimN64Mapped, but each OoT3D bone carries a per-bone CORRECTION (SoH3dBoneCorr,
// indexed by bone id): mode 1 = pure "replace" (local rot := N64 rot, the same-rest case that works
// for Link's legs/head); mode 2/3 = apply a constant rest-frame correction C on the left (C·R_n64)
// or right (R_n64·C) for bones whose OoT3D rest diverges from the N64 limb's (Grezzo re-rigged
// Link's spine/upper arms — see tools/soh3d_link_retarget_derive.py). mode 0 / limb<0 = keep the
// CMB rest pose. Same FK + skin-matrix tail as the Mapped variant.
extern "C" void SoH3D_UpdateAnimN64Corr(int modelId, const int16_t* jointRots, int rotCount,
                                        const SoH3dBoneCorr* corr, int corrCount) {
    using namespace SoH3D;
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }
    const auto& bones = lm->cmb->bones();
    const auto& bind = lm->cmb->boneMatrices();
    const float kBinangToRad = 3.14159265358979f / 32768.0f;

    auto corrMat = [](const float* c) -> Mat4 {
        Mat4 m = matId();
        m[0] = c[0]; m[1] = c[1]; m[2] = c[2];
        m[4] = c[3]; m[5] = c[4]; m[6] = c[5];
        m[8] = c[6]; m[9] = c[7]; m[10] = c[8];
        return m;
    };

    std::vector<Mat4> aw(bind.size(), matId());
    std::vector<char> done(bind.size(), 0);
    std::vector<const CmbBone*> byId(bind.size(), nullptr);
    for (const auto& bn : bones)
        if (bn.id >= 0 && (size_t)bn.id < byId.size()) byId[bn.id] = &bn;

    std::function<Mat4(int)> world = [&](int id) -> Mat4 {
        if (id < 0 || (size_t)id >= aw.size() || !byId[id]) return matId();
        if (done[id]) return aw[id];
        const CmbBone* bn = byId[id];
        Mat4 L = matT(bn->trans[0], bn->trans[1], bn->trans[2]);
        const SoH3dBoneCorr* c = (corr && id < corrCount) ? &corr[id] : nullptr;
        int limb = c ? c->limb : -1;
        int mode = c ? c->mode : 0;
        if (mode >= 1 && limb >= 0 && limb < rotCount) {
            float rx = jointRots[limb * 3 + 0] * kBinangToRad;
            float ry = jointRots[limb * 3 + 1] * kBinangToRad;
            float rz = jointRots[limb * 3 + 2] * kBinangToRad;
            Mat4 R = matMul(matMul(matRz(rz), matRy(ry)), matRx(rx)); // N64 local rotation (Rz·Ry·Rx)
            if (mode == 2) R = matMul(corrMat(c->C), R);              // left:  C·R_n64
            else if (mode == 3) R = matMul(R, corrMat(c->C));         // right: R_n64·C
            else if (mode == 4) R = matMul(matMul(corrMat(c->C), R), corrMat(c->C2)); // C·R·C2
            else if (mode == 5) {
                // Conjugation C·R·C⁻¹ (C⁻¹ = Cᵀ for a rotation): a change of basis for the rest-frame
                // discrepancy. Unlike mode 2/3 (a one-sided constant that's only right near the tuned
                // pose), this transforms R itself, so a single hand-tuned C holds across the FULL pose
                // range — idle, walk AND the arms-overhead carry pose (#6). Tune just C.
                Mat4 C = corrMat(c->C);
                Mat4 Ci = matId();
                Ci[0] = C[0]; Ci[1] = C[4]; Ci[2] = C[8];
                Ci[4] = C[1]; Ci[5] = C[5]; Ci[6] = C[9];
                Ci[8] = C[2]; Ci[9] = C[6]; Ci[10] = C[10];
                R = matMul(matMul(C, R), Ci);
            }
            L = matMul(L, R);
        } else {
            // No live joint / rest mode: keep the CMB rest orientation (bind pose).
            L = matMul(L, matMul(matMul(matRz(bn->rot[2]), matRy(bn->rot[1])), matRx(bn->rot[0])));
        }
        L = matMul(L, matS(bn->scale[0], bn->scale[1], bn->scale[2]));
        Mat4 W = (bn->parent < 0) ? L : matMul(world(bn->parent), L);
        aw[id] = W;
        done[id] = 1;
        return W;
    };
    for (const auto& bn : bones) world(bn.id);

    std::vector<std::array<float, 16>> sm(bind.size());
    for (size_t id = 0; id < bind.size(); id++) sm[id] = matMul(aw[id], matInverse(bind[id]));
    cacheSkinForGround(modelId, sm); // posed-feet grounding for the player path (#29b)
    // Upload bind for correct rigid pose interpolation (see SoH3D_UpdateAnim / interpSkinPose).
    SoH3D_GL_SetBoneBind(modelId, bind.empty() ? nullptr : bind.front().data(), (int)bind.size());
    SoH3D_GL_SetBones(modelId, sm.empty() ? nullptr : sm.front().data(), (int)sm.size());
}

extern "C" {

// Set the model's GPU skinning pose to `animName` (CSAB base name, e.g. "ge1_s_wait")
// at `frame`. animName==NULL/"" resets to the bind pose. Loads the model + caches the
// parsed CSAB on first use; recomputes skin matrices each call (cheap: <=32 bones).
// Call once per game frame before the SoH3D draw. Safe to call repeatedly.
// Per-model procedural per-bone local-rotation deltas (radians, 3 per bone id), set by the auto
// retarget path from an N64 OverrideLimbDraw probe (e.g. the cucco wing-flap) and consumed by the
// next SoH3D_UpdateAnim for that model. Empty = no delta (the common case; static-pose unchanged).
static std::unordered_map<int, std::vector<float>>& boneRotDeltas() {
    static std::unordered_map<int, std::vector<float>> m;
    return m;
}
extern "C" void SoH3D_ClearBoneRotDeltas(int modelId) { boneRotDeltas().erase(modelId); }
extern "C" void SoH3D_SetBoneRotDelta(int modelId, int boneId, float rx, float ry, float rz) {
    if (boneId < 0) return;
    LoadedModel* lm = loadModel(modelId);
    int n = (lm && lm->ok && lm->cmb) ? (int)lm->cmb->boneMatrices().size() : 0;
    if (boneId >= n) return;
    auto& v = boneRotDeltas()[modelId];
    if ((int)v.size() != n * 3) v.assign(n * 3, 0.0f);
    v[boneId * 3 + 0] = rx;
    v[boneId * 3 + 1] = ry;
    v[boneId * 3 + 2] = rz;
}

// Per-model per-bone POST-rotation matrix (row-major 3x3, 9 floats/bone) post-multiplied onto the
// bone's animated local rotation by the CSAB skinner — the OoT3D actor OverrideLimbDraw MTXMODE_APPLY
// channel (En_Ko/En_Sa head/torso tracking). Distinct from boneRotDeltas (euler pre-add, cucco flap):
// a post-multiply in the bone's local frame matches OoT3D's matrix-apply and propagates to children.
static std::unordered_map<int, std::vector<float>>& bonePostRots() {
    static std::unordered_map<int, std::vector<float>> m;
    return m;
}
extern "C" void SoH3D_ClearBonePostRots(int modelId) { bonePostRots().erase(modelId); }
extern "C" void SoH3D_SetBonePostRot(int modelId, int boneId, const float* mat9) {
    if (boneId < 0 || !mat9) return;
    LoadedModel* lm = loadModel(modelId);
    int n = (lm && lm->ok && lm->cmb) ? (int)lm->cmb->boneMatrices().size() : 0;
    if (boneId >= n) return;
    auto& v = bonePostRots()[modelId];
    if ((int)v.size() != n * 9) {
        v.assign(n * 9, 0.0f);
        for (int b = 0; b < n; b++) { v[b*9+0] = v[b*9+4] = v[b*9+8] = 1.0f; } // identity per bone
    }
    for (int k = 0; k < 9; k++) v[boneId * 9 + k] = mat9[k];
}

// #5 debug: dump per-bone vert influence + spatial extent so the wing bones can be identified by
// geometry (the parsed CMB has no bone names). Prints, per bone: id, parent, #verts weighted to it,
// and the mean local position of those verts (a wing bone's verts sit far out on one side in Z/X).
extern "C" void SoH3D_DumpBoneStats(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) {
        fprintf(stderr, "[BONESTATS] model %d not loaded\n", modelId);
        return;
    }
    const auto& bones = lm->cmb->bones();
    int n = (int)lm->cmb->boneMatrices().size();
    std::vector<int> vc(n, 0);
    std::vector<double> mx(n, 0), my(n, 0), mz(n, 0);
    for (const auto& g : lm->groups) {
        for (const auto& v : g.verts) {
            for (int k = 0; k < 4; k++) {
                if (v.weights[k] <= 0.0f) continue;
                int b = (int)(v.boneIds[k] + 0.5f);
                if (b < 0 || b >= n) continue;
                vc[b]++;
                mx[b] += v.pos[0]; my[b] += v.pos[1]; mz[b] += v.pos[2];
            }
        }
    }
    fprintf(stderr, "[BONESTATS] model %d bones=%d\n", modelId, n);
    for (const auto& bn : bones) {
        int id = bn.id;
        if (id < 0 || id >= n) continue;
        int c = vc[id];
        fprintf(stderr, "[BONESTATS]  bone %2d parent %2d verts %5d meanPos(%.1f,%.1f,%.1f) trans(%.1f,%.1f,%.1f)\n",
                id, bn.parent, c,
                c ? mx[id] / c : 0.0, c ? my[id] / c : 0.0, c ? mz[id] / c : 0.0,
                bn.trans[0], bn.trans[1], bn.trans[2]);
    }
    fflush(stderr);
}

// --- Posed-feet grounding for the player path (#29b "Link floats") ---------------------------
// The OoT3D Link CSABs carry absolute hip (bone 1) TRANSLATION tracks authored for the BOY rig;
// applied to ANY Link rig they lift the whole skeleton off the floor (the child floats ~930 local
// units ~= 40px on screen). The working N64-retarget path grounds precisely because it applies the
// rest (bind) translation and only REPLACES rotations -- it never sees those hip translations. The
// own-CSAB (linksrc 3ds) path applies the full CSAB, so it floats. We can't just drop the
// translation (the BOY rig's own run NEEDS its hip bob), so instead we measure the posed model's
// lowest VISIBLE vertex (its feet) each frame and offset the draw so the feet land on the actor's
// world pos.y -- the per-frame analogue of the auto path's bind-pose groundOffset. Gated per-model
// (only the player turns it on) so the per-vertex cost isn't paid on every NPC; needs the live mesh_id
// visibility mask so a hidden/unposed equipment variant (which sits at its bind ~-1325) can't skew it.
static std::unordered_map<int, char>& trackMinYFlags() {
    static std::unordered_map<int, char> m;
    return m;
}
static std::unordered_map<int, std::vector<std::array<float, 16>>>& lastSkin() {
    static std::unordered_map<int, std::vector<std::array<float, 16>>> m;
    return m;
}
// Cache this frame's skin matrices for a tracked model so SoH3D_PosedGroundOffset can recompute the
// posed feet position against the (later-known) mesh_id mask. No-op unless tracking is enabled.
static void cacheSkinForGround(int modelId, const std::vector<std::array<float, 16>>& sm) {
    auto it = trackMinYFlags().find(modelId);
    if (it == trackMinYFlags().end() || !it->second) return;
    lastSkin()[modelId] = sm;
}
extern "C" void SoH3D_SetTrackPosedMinY(int modelId, int enable) {
    trackMinYFlags()[modelId] = enable ? 1 : 0;
    if (!enable) lastSkin().erase(modelId);
}
// Model-local Y translation to add (innermost, pre-scale) so the posed model's lowest VISIBLE
// vertex lands on the actor's ground. midMask selects the drawn equipment/hand variant subset
// (same bit convention as SoH3D_GL_SetMidMask: bit i = mesh_id i visible; mesh_id<0 or >=64 always
// shown). Returns 0 if tracking wasn't enabled / no pose cached.
extern "C" float SoH3D_PosedGroundOffset(int modelId, unsigned long long midMask) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return 0.0f;
    auto it = lastSkin().find(modelId);
    if (it == lastSkin().end() || it->second.empty()) return 0.0f;
    const auto& sm = it->second;
    const int n = (int)sm.size();
    float mn = 1e30f;
    for (const auto& g : lm->groups) {
        if (g.mesh_id >= 0 && g.mesh_id < 64 && !((midMask >> g.mesh_id) & 1ull)) continue;
        for (const auto& v : g.verts) {
            float y = 0.0f, wsum = 0.0f;
            for (int k = 0; k < 4; k++) {
                float w = v.weights[k];
                if (w <= 0.0f) continue;
                int b = (int)(v.boneIds[k] + 0.5f);
                if (b < 0 || b >= n) continue;
                const float* M = sm[b].data();
                y += w * (M[4] * v.pos[0] + M[5] * v.pos[1] + M[6] * v.pos[2] + M[7]);
                wsum += w;
            }
            if (wsum > 0.0f) { y /= wsum; if (y < mn) mn = y; }
        }
    }
    return (mn < 1e29f) ? -mn : 0.0f;
}

// Model-local position of a posed bone's ORIGIN this frame, recovered from the cached skin matrices
// (#6 held-actor attach). The animated bone-world matrix is aw[b] = skin[b]*bind[b], so the bone
// origin in model space is skin[b] applied to the bind-pose origin (bind[b]'s translation column).
// Returns 1 and writes outModelPos (3 floats) on success; 0 if no pose is cached / bone out of range.
// The caller lifts this through the actor world matrix (Matrix_MultVec3f) to get world space. Uses
// the SAME lastSkin cache the feet-grounding path already maintains, so any posing path (N64-retarget
// or CSAB) that ran cacheSkinForGround this frame exposes it. Requires SoH3D_SetTrackPosedMinY(1).
extern "C" int SoH3D_PosedBoneWorldPos(int modelId, int boneId, float* outModelPos) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb || !outModelPos) return 0;
    auto it = lastSkin().find(modelId);
    if (it == lastSkin().end() || it->second.empty()) return 0;
    const auto& sm = it->second;
    const auto& bind = lm->cmb->boneMatrices();
    if (boneId < 0 || (size_t)boneId >= sm.size() || (size_t)boneId >= bind.size()) return 0;
    const float* M = sm[boneId].data();
    const float* B = bind[boneId].data();
    const float bx = B[3], by = B[7], bz = B[11]; // bind-pose bone origin (row-major translation col)
    outModelPos[0] = M[0] * bx + M[1] * by + M[2] * bz + M[3];
    outModelPos[1] = M[4] * bx + M[5] * by + M[6] * bz + M[7];
    outModelPos[2] = M[8] * bx + M[9] * by + M[10] * bz + M[11];
    return 1;
}

// Pose-discontinuity scanner (anim QA tooling): the 3d3 named-CSAB path picks ONE csab at a phase and
// never blends morphs, so any transition that hard-cuts the pose shows as a per-bone rotation that JUMPS
// between consecutive frames far beyond what a continuous animation could produce. This compares the
// current cached pose (lastSkin) against the previous snapshot and returns the LARGEST per-bone rotation
// delta (degrees) plus that bone. Generic: works for ANY animation/transition driven through the model,
// needs no oracle. Orthonormalizes each bone's 3x3 (removing skin scale) then measures the relative
// rotation angle acos((tr(Ra^T Rb)-1)/2). First call after a reset returns 0 (no previous). Pair with
// the freeze/step harness + the action machine to sweep transitions and auto-flag pops. Uses lastSkin,
// so it requires SoH3D_SetTrackPosedMinY(1) on the model (the player path already enables it).
static std::unordered_map<int, std::vector<std::array<float, 16>>>& posePrev() {
    static std::unordered_map<int, std::vector<std::array<float, 16>>> m;
    return m;
}
static void orthoRows(const float* M, float r[3][3]) {
    // row-major 4x4 rotation rows (v' = M*v): r0..r2; Gram-Schmidt to a pure rotation.
    float a[3] = { M[0], M[1], M[2] }, b[3] = { M[4], M[5], M[6] }, c[3] = { M[8], M[9], M[10] };
    auto norm = [](float* v) { float n = std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); if (n>1e-8f){v[0]/=n;v[1]/=n;v[2]/=n;} };
    auto dot = [](const float* u, const float* v) { return u[0]*v[0]+u[1]*v[1]+u[2]*v[2]; };
    norm(a);
    float pb = dot(a, b); b[0]-=pb*a[0]; b[1]-=pb*a[1]; b[2]-=pb*a[2]; norm(b);
    c[0]=a[1]*b[2]-a[2]*b[1]; c[1]=a[2]*b[0]-a[0]*b[2]; c[2]=a[0]*b[1]-a[1]*b[0]; // c = a x b
    for (int k=0;k<3;k++){ r[0][k]=a[k]; r[1][k]=b[k]; r[2][k]=c[k]; }
}
extern "C" float SoH3D_PoseDiscontinuity(int modelId, int* outBone) {
    if (outBone) *outBone = -1;
    auto it = lastSkin().find(modelId);
    if (it == lastSkin().end() || it->second.empty()) return 0.0f;
    const auto& cur = it->second;
    auto& prev = posePrev()[modelId];
    float maxDeg = 0.0f; int maxBone = -1;
    if (prev.size() == cur.size()) {
        for (size_t b = 0; b < cur.size(); b++) {
            float Ra[3][3], Rb[3][3];
            orthoRows(prev[b].data(), Ra);
            orthoRows(cur[b].data(), Rb);
            // tr(Ra^T * Rb) = sum_ij Ra[i][j]*Rb[i][j]  (Ra rows are basis vectors)
            float tr = 0.0f;
            for (int i=0;i<3;i++) for (int j=0;j<3;j++) tr += Ra[i][j]*Rb[i][j];
            float cosA = (tr - 1.0f) * 0.5f;
            if (cosA > 1.0f) cosA = 1.0f; if (cosA < -1.0f) cosA = -1.0f;
            float deg = std::acos(cosA) * (180.0f / 3.14159265358979f);
            if (deg > maxDeg) { maxDeg = deg; maxBone = (int)b; }
        }
    }
    prev = cur; // snapshot for next call
    if (outBone) *outBone = maxBone;
    return maxDeg;
}
extern "C" void SoH3D_PoseScanReset(int modelId) { posePrev().erase(modelId); }

// Look up the per-model procedural bone-rotation deltas (cucco flap, euler pre-add), if any.
static void getBoneRotDeltas(int modelId, const float** outDrot, int* outDcount) {
    *outDrot = nullptr; *outDcount = 0;
    auto it = boneRotDeltas().find(modelId);
    if (it != boneRotDeltas().end() && !it->second.empty()) {
        *outDrot = it->second.data();
        *outDcount = (int)it->second.size() / 3;
    }
}
// Look up the per-model per-bone post-rotation matrices (head/torso track, MTXMODE_APPLY), if any.
static void getBonePostRots(int modelId, const float** outPost, int* outCount) {
    *outPost = nullptr; *outCount = 0;
    auto it = bonePostRots().find(modelId);
    if (it != bonePostRots().end() && !it->second.empty()) {
        *outPost = it->second.data();
        *outCount = (int)it->second.size() / 9;
    }
}

// Common tail for the CSAB sample paths: cache for grounding, then upload bind + skin to GL. The
// GL layer recovers the animated bone-world transform (skin*bind) and interpolates the pose RIGIDLY
// between logic frames — interpolating the skin matrices directly shatters large per-frame rotations.
static void uploadSkin(int modelId, LoadedModel* lm, std::vector<std::array<float, 16>>& sm) {
    cacheSkinForGround(modelId, sm); // posed-feet grounding for the player path (#29b)
    const auto& bind = lm->cmb->boneMatrices();
    SoH3D_GL_SetBoneBind(modelId, bind.empty() ? nullptr : bind.front().data(), (int)bind.size());
    // vector<array<float,16>> is contiguous -> hand the renderer a flat float buffer.
    SoH3D_GL_SetBones(modelId, sm.empty() ? nullptr : sm.front().data(), (int)sm.size());
}

void SoH3D_UpdateAnim(int modelId, const char* animName, float frame) {
    if (!animName || !*animName) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb || !lm->zar) return;

    SoH3D::Csab* anim = getCsab(lm, animName);
    if (!anim) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }

    std::vector<std::array<float, 16>> sm;
    const float* drot = nullptr; int dcount = 0;
    const float* post = nullptr; int pcount = 0;
    getBoneRotDeltas(modelId, &drot, &dcount);
    getBonePostRots(modelId, &post, &pcount);
    anim->skinMatrices(*lm->cmb, frame, sm, drot, dcount, post, pcount);
    uploadSkin(modelId, lm, sm);
}

// MORPH variant of SoH3D_UpdateAnim: cross-fade the INCOMING clip (inName@fIn) toward the frozen
// OUTGOING clip (outName@fOut) by `weight` (= N64 morphWeight, 1->0 over the transition). Same
// model, same upload tail. If the outgoing CSAB can't be resolved, falls back to a plain incoming
// sample (no morph) rather than dropping the pose.
static void SoH3D_UpdateAnimMorph(int modelId, const char* inName, float fIn, const char* outName,
                                  float fOut, float weight) {
    if (!inName || !*inName) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb || !lm->zar) return;
    SoH3D::Csab* in = getCsab(lm, inName);
    if (!in) { SoH3D_GL_SetBones(modelId, nullptr, 0); return; }
    SoH3D::Csab* out = (outName && *outName) ? getCsab(lm, outName) : nullptr;
    const float* drot = nullptr; int dcount = 0;
    const float* post = nullptr; int pcount = 0;
    getBoneRotDeltas(modelId, &drot, &dcount);
    getBonePostRots(modelId, &post, &pcount);
    std::vector<std::array<float, 16>> sm;
    if (out) {
        in->skinMatricesMorph(*lm->cmb, fIn, *out, fOut, weight, sm, drot, dcount, post, pcount);
    } else {
        in->skinMatrices(*lm->cmb, fIn, sm, drot, dcount, post, pcount); // outgoing unresolved -> no blend
    }
    uploadSkin(modelId, lm, sm);
}

// Drive an auto-replaced model by its OWN OoT3D CSAB (animName). Two playhead modes:
//   PHASE-LOCK (n64AnimLength>4): the OoT3D CSAB is driven at the SAME fractional progress as the
//     actor's live N64 animation (csab_frame = (n64CurFrame/n64AnimLength) * csab_duration). This
//     is the fix for "OoT3D anims too fast" — the CSAB free-ran at a fixed `rate` regardless of how
//     fast the N64 game logic was actually advancing the anim, so a slow N64 walk looked sped-up.
//     Locking to N64 progress makes the OoT3D motion match the N64 motion's tempo exactly.
//   FREE-RUN (stub idle, n64AnimLength<=4, or duration unknown): self-managed per-model playhead at
//     `rate` frames/draw. N64 idles are often 2-frame fidget stubs with no meaningful progress to
//     lock to (see memory n64-idle-stub-no-phaselock), so we free-run the full OoT3D idle instead.
// animName==NULL -> bind pose. The CSAB wraps the frame internally (Csab::animFrame REPEAT).
//
// MORPH (#8/#86, keystone fix #2): `morphWeight` is the live N64 skelAnime->morphWeight (1.0 on the
// transition frame, ramping linearly to 0). When a transition is detected (the resolved CSAB name
// changes while morphWeight>0) we FREEZE the outgoing clip+frame and, while morphWeight>0, cross-fade
// the new clip toward that frozen pose — exactly the N64 SkelAnime morph model (docs/anim_system.md
// "THE MORPH"). Without this the auto/CSAB path hard-cuts every transition (1-frame arm/limb pops).
// gSoH3dMorph (REPL `morph 0|1`, env SOH3D_MORPH default on) gates it for A/B verification.
int gSoH3dMorph = -1;
void SoH3D_UpdateAnimAuto(int modelId, const char* animName, float rate, float n64CurFrame,
                          float n64AnimLength, float morphWeight) {
    static std::unordered_map<int, float> frames;
    static std::unordered_map<int, std::string> lastCsab; // per-model: which CSAB the playhead is on
    static std::unordered_map<int, float> lastFrame;      // last incoming frame rendered (freeze src)
    static std::unordered_map<int, std::string> morphOut; // frozen outgoing CSAB during a morph
    static std::unordered_map<int, float> morphOutFrame;  // frozen outgoing frame
    if (!animName || !*animName) {
        frames.erase(modelId); lastCsab.erase(modelId); lastFrame.erase(modelId);
        morphOut.erase(modelId); morphOutFrame.erase(modelId);
        SoH3D_UpdateAnim(modelId, nullptr, 0); return;
    }
    if (gSoH3dMorph < 0) {
        const char* v = getenv("SOH3D_MORPH");
        gSoH3dMorph = (v != NULL && v[0] == '0') ? 0 : 1;
    }
    if (!gSoH3dMorph) morphWeight = 0.0f;

    // --- incoming frame f (phase-lock to the N64 anim's progress, else free-run) ---
    LoadedModel* lm = loadModel(modelId);
    float dur = 0.0f;
    if (lm && lm->ok && lm->cmb && lm->zar) {
        SoH3D::Csab* c = getCsab(lm, animName);
        if (c) dur = (float)c->duration();
    }
    bool locked = (n64AnimLength > 4.0f && n64CurFrame >= 0.0f && dur > 0.0f);
    auto lcIt = lastCsab.find(modelId);
    bool csabChanged = (lcIt == lastCsab.end()) || (lcIt->second != animName);
    float f;
    if (locked) {
        float phase = n64CurFrame / n64AnimLength;
        phase -= std::floor(phase); // wrap into [0,1)
        f = phase * dur;
        frames[modelId] = f; // keep the free-run playhead in sync for a later mode switch
    } else {
        // FREE-RUN. Restart from 0 whenever the CSAB changes, so a one-shot (a wave, a hand-off)
        // plays from its start instead of resuming at the previous anim's frame.
        float& pf = frames[modelId];
        if (csabChanged) pf = 0.0f;
        f = pf;
        pf += rate;
    }

    // --- morph bookkeeping: on a real transition, freeze the outgoing clip at its last frame ---
    if (csabChanged) {
        if (morphWeight > 0.0f && lcIt != lastCsab.end()) {
            morphOut[modelId] = lcIt->second; // the clip we're leaving
            auto lfIt = lastFrame.find(modelId);
            morphOutFrame[modelId] = (lfIt != lastFrame.end()) ? lfIt->second : 0.0f;
        } else {
            morphOut.erase(modelId); morphOutFrame.erase(modelId); // hard cut / first anim
        }
    }
    if (morphWeight <= 0.0f) { morphOut.erase(modelId); morphOutFrame.erase(modelId); }
    lastCsab[modelId] = animName;
    lastFrame[modelId] = f;

    auto moIt = morphOut.find(modelId);
    if (moIt != morphOut.end() && morphWeight > 0.0f) {
        SoH3D_UpdateAnimMorph(modelId, animName, f, moIt->second.c_str(), morphOutFrame[modelId],
                              morphWeight);
    } else {
        SoH3D_UpdateAnim(modelId, animName, f);
    }
}

} // extern "C"

#include "soh3d_collision.h"

extern "C" int SoH3D_LoadSceneCollisionRaw(const char* sceneName, SoH3D_RawCollision* out) {
    if (!sceneName || !*sceneName || !out) return 0;
    memset(out, 0, sizeof(*out));
    SoH3D::CtrRom* r = rom();
    if (!r) return 0;
    std::string path = "/scene/" + std::string(sceneName) + "_info.zsi";
    auto bytes = r->read(path);
    if (bytes.empty()) { fprintf(stderr, "[SoH3D] collision zsi not found: %s\n", path.c_str()); return 0; }
    SoH3D::OoT3DCollision col(bytes);
    if (!col.ok()) { fprintf(stderr, "[SoH3D] collision %s: %s\n", path.c_str(), col.error().c_str()); return 0; }
    const auto& verts = col.verts();
    const auto& polys = col.polys();
    const auto& surfs = col.surfaces();
    if (verts.empty() || polys.empty()) return 0;

    out->numVerts = (int)verts.size();
    out->numPolys = (int)polys.size();
    out->numSurf = (int)surfs.size();
    out->verts = (int16_t*)malloc(sizeof(int16_t) * 3 * verts.size());
    out->polyVtx = (uint16_t*)malloc(sizeof(uint16_t) * 3 * polys.size());
    out->polyNrm = (int16_t*)malloc(sizeof(int16_t) * 3 * polys.size());
    out->polyDist = (float*)malloc(sizeof(float) * polys.size());
    out->polyType = (uint16_t*)malloc(sizeof(uint16_t) * polys.size());
    out->surf0 = surfs.empty() ? nullptr : (uint32_t*)malloc(sizeof(uint32_t) * surfs.size());
    out->surf1 = surfs.empty() ? nullptr : (uint32_t*)malloc(sizeof(uint32_t) * surfs.size());
    if (!out->verts || !out->polyVtx || !out->polyNrm || !out->polyDist || !out->polyType ||
        (!surfs.empty() && (!out->surf0 || !out->surf1))) {
        SoH3D_FreeRawCollision(out);
        return 0;
    }
    for (size_t i = 0; i < verts.size(); i++) {
        out->verts[i * 3 + 0] = verts[i].x;
        out->verts[i * 3 + 1] = verts[i].y;
        out->verts[i * 3 + 2] = verts[i].z;
    }
    for (size_t k = 0; k < polys.size(); k++) {
        out->polyVtx[k * 3 + 0] = polys[k].vA;
        out->polyVtx[k * 3 + 1] = polys[k].vB;
        out->polyVtx[k * 3 + 2] = polys[k].vC;
        out->polyNrm[k * 3 + 0] = polys[k].nx;
        out->polyNrm[k * 3 + 1] = polys[k].ny;
        out->polyNrm[k * 3 + 2] = polys[k].nz;
        out->polyDist[k] = polys[k].dist;
        out->polyType[k] = polys[k].type;
    }
    for (size_t s = 0; s < surfs.size(); s++) {
        out->surf0[s] = surfs[s].data0;
        out->surf1[s] = surfs[s].data1;
    }
    printf("[SoH3D] loaded scene collision %s: %d verts, %d polys, %d surface types\n",
           path.c_str(), out->numVerts, out->numPolys, out->numSurf);
    return 1;
}

// #5 — collision-side stepped stairs. The render path turns each fake-flat kaidan ramp into
// real treads+risers (generateStairsGroup); this produces the matching collision floor so Link
// stands on the SAME visible steps instead of the smooth ramp underneath. Walks every room of
// the scene, finds kaidan groups, runs the IDENTICAL shared patch analysis (stairPatches/
// stairFrameOf), and emits each step's horizontal TREAD as a world-space quad (2 tris). Risers
// are intentionally omitted: the original OoT3D ramp collision stays in place underneath, so the
// treads (which sit on/above it) just become the higher walking surface BgCheck returns — no
// gaps, and Link auto-steps the small rise. Coordinates are world-space (rooms draw at identity,
// gSoH3dSceneScale=1/off=0), matching the collision frame. Returns 1 with malloc'd arrays (0
// verts/tris when the scene has no kaidan stairs); free with SoH3D_FreeStairTreads.
extern "C" int SoH3D_CollectSceneStairTreads(const char* sceneName,
                                             float** outVerts, int* outNVerts,
                                             int** outTris, int* outNTris) {
    if (outVerts) *outVerts = nullptr;
    if (outTris) *outTris = nullptr;
    if (outNVerts) *outNVerts = 0;
    if (outNTris) *outNTris = 0;
    if (!sceneName || !*sceneName || !outVerts || !outTris || !outNVerts || !outNTris) return 0;
    ensureStairsEnv();
    if (!gSoH3dStairs) return 1; // stairs disabled -> empty (smooth ramp collision)
    SoH3D::CtrRom* r = rom();
    if (!r) return 0;

    std::vector<float> verts; // 3 floats per vertex (world x,y,z)
    std::vector<int> tris;    // 3 vertex indices per triangle

    for (int room = 0; room < 64; room++) {
        std::string path = "/scene/" + std::string(sceneName) + "_" + std::to_string(room) + "_info.zsi";
        auto bytes = r->read(path);
        if (bytes.empty()) break; // rooms are contiguous 0..n-1
        SoH3D::Zsi zsi(std::move(bytes));
        if (!zsi.ok() || !zsi.hasGeometry()) continue;
        SoH3D::Cmb cmb(zsi.cmbBytes());
        if (!cmb.ok()) continue;
        std::vector<SoH3D::CmbDrawGroup> groups = cmb.buildDrawGroups({});
        for (const SoH3D::CmbDrawGroup& g : groups) {
            if (!texNameIsKaidan(cmb, g.material_index)) continue;
            if (g.verts.size() < 6) continue;
            std::vector<std::array<float, 3>> nrm = stairTriNormals(g);
            std::vector<std::vector<int>> patches = stairPatches(g, nrm);
            for (const std::vector<int>& pt : patches) {
                StairFrame f;
                if (!stairFrameOf(g, pt, nrm, f)) continue;
                const float yr = f.dy; // #1: same FULL-step raise as the render side
                for (int k = 0; k < f.N; k++) {
                    float a0 = f.amin + k * f.da, a1 = f.amin + (k + 1) * f.da;
                    float yk = f.ymin + k * f.dy + yr; // tread raised a full step, matching render
                    // Tread quad corners (world XZ from a,c; y at the lowered tread), CCW from above.
                    const float cc[4][2] = { { a0, f.cmin }, { a1, f.cmin }, { a1, f.cmax }, { a0, f.cmax } };
                    int base = (int)(verts.size() / 3);
                    for (int j = 0; j < 4; j++) {
                        float a = cc[j][0], c = cc[j][1];
                        verts.push_back(f.aDir[0] * a + f.cDir[0] * c);
                        verts.push_back(yk);
                        verts.push_back(f.aDir[2] * a + f.cDir[2] * c);
                    }
                    tris.push_back(base + 0); tris.push_back(base + 1); tris.push_back(base + 2);
                    tris.push_back(base + 0); tris.push_back(base + 2); tris.push_back(base + 3);
                }
            }
        }
    }

    if (verts.empty() || tris.empty()) return 1; // no stairs -> empty success
    float* vp = (float*)malloc(sizeof(float) * verts.size());
    int* tp = (int*)malloc(sizeof(int) * tris.size());
    if (!vp || !tp) { free(vp); free(tp); return 0; }
    std::memcpy(vp, verts.data(), sizeof(float) * verts.size());
    std::memcpy(tp, tris.data(), sizeof(int) * tris.size());
    *outVerts = vp; *outNVerts = (int)(verts.size() / 3);
    *outTris = tp; *outNTris = (int)(tris.size() / 3);
    return 1;
}

extern "C" void SoH3D_FreeStairTreads(float* verts, int* tris) {
    free(verts);
    free(tris);
}

extern "C" void SoH3D_FreeRawCollision(SoH3D_RawCollision* out) {
    if (!out) return;
    free(out->verts);
    free(out->polyVtx);
    free(out->polyNrm);
    free(out->polyDist);
    free(out->polyType);
    free(out->surf0);
    free(out->surf1);
    memset(out, 0, sizeof(*out));
}

// #20 — headless keyboard verification. Feed a raw KbScancode through the EXACT same path the SDL
// window handler uses (Fast3dWindow::KeyDown/KeyUp -> ControlDeck::ProcessKeyboardEvent), so the
// default keyboard->N64-button mapping (LUS::ControllerDefaultMappings) can be exercised and
// observed live with no physical keyboard. Only the SDL physical-key->scancode step is skipped
// (generic libultraship plumbing, not SoH3D-specific). Returns 1 if the deck consumed the event,
// 0 if not, -1 if no control deck. Used by the REPL `key` command.
extern "C" int SoH3D_InjectKey(int scancode, int down) {
    auto* ctx = Ship::Context::GetRawInstance();
    if (ctx == nullptr) {
        return -1;
    }
    auto controlDeck = ctx->GetControlDeck();
    if (controlDeck == nullptr) {
        return -1;
    }
    Ship::KbEventType ev = down ? Ship::KbEventType::LUS_KB_EVENT_KEY_DOWN
                                : Ship::KbEventType::LUS_KB_EVENT_KEY_UP;
    return controlDeck->ProcessKeyboardEvent(ev, static_cast<Ship::KbScancode>(scancode)) ? 1 : 0;
}
