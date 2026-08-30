// Zelda3D model bridge: connects the runtime C++ asset loader (asset/) to the
// libultraship direct-GL renderer (Zelda3D_GL_*). Owns the model registry (actor id
// -> 3DS asset + world scale), lazily parses+decodes a model from the decrypted
// .3ds the first time it's drawn (on the render thread, GL current), and serves the
// renderer's provider callback with the CPU data to upload. No baked-in C arrays;
// the .3ds path comes from env ZELDA3D_OOT3D_ROM (never hardcoded — repo rule).
#include "asset/ctr_rom.h"
#include "asset/zar.h"
#include "asset/zsi.h"
#include "asset/zcol.h"
#include "asset/cmb.h"
#include "asset/ctxb.h"
#include "asset/csab.h"
#include "asset/faceb.h"
#include "asset/mat4.h"
#include "asset/cmb_glgroups.h" // shared CMB -> renderer GlGroup/texture converter
#include "fast/zelda3d_material_overrides.h"
#include "fast/zelda3d_model_provider.h"
#include "fast/zelda3d_submission.h"
#include "zelda3d_asset_source.h"
#include "zelda3d_facial_assets.h"
#include "zelda3d_model_geometry.h"
#include "zelda3d_model_id_ranges.h"
#include "zelda3d_model_internal.h" // LoadedModel + loadModel (shared with zelda3d_anim.cpp)
#include "../core/zelda3d_log.h"
#include "../scene/zelda3d_stairs.h" // procedural stair geometry (gZelda3dStairs, generateStairsGroup, ...)

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

// Matches the typedef in zelda3d.h (which this pure-C++ TU does not include). The N64
// floor-height callback used by the terrain warp; zelda3d.c supplies the implementation.
typedef float (*Zelda3D_FloorFn)(float x, float z);

namespace {

struct ModelSpec {
    const char* zarPath;
    float worldScale;
    const char* cmbName; // substring to select the .cmb inside the ZAR (nullptr = first one).
                         // Needed when a ZAR holds several CMBs (e.g. a main model + a debris
                         // "hahen" variant) and firstWithSuffix would grab the wrong one.
};

// Registry keyed by modelId (the index). The actor->modelId mapping lives in
// zelda3d.c (which has the ACTOR_* ids); this stays pure-C++ / engine-agnostic.
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
// (Zelda3D_RoomModelId) keyed by the room's ZSI path. See zelda3d.c's room-draw hook.
constexpr int kSceneModelBase = Zelda3DModelIds::kSceneBase;

// Auto-replaced actor models live in a THIRD id range (above scene rooms) so the
// ZELDA3D_AUTO path can allocate ids for arbitrary actor ZARs (discovered at runtime
// from the object id -> ZAR table) without colliding with the hand-listed actor
// models (0..N) or scene rooms (1000..). Keyed by ZAR path; main CMB picked by the
// "largest non-debris" heuristic. See Zelda3D_AutoModelId / loadAutoModel.
constexpr int kAutoModelBase = Zelda3DModelIds::kAutomaticBase;

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
    // (slashing "spawns signs"). So kanban stays on N64 (skipped in Zelda3D_TryAuto) until the
    // break pieces are handled. Add an entry here only for a static prop with no break/spawn
    // behaviour. See scratch/evidence/multicmb_finding.md.
    { nullptr, {} },
};

// LoadedModel moved to zelda3d_model_internal.h (shared with zelda3d_anim.cpp).
std::unordered_map<int, std::unique_ptr<LoadedModel>> g_loaded;
std::unique_ptr<Zelda3D::CtrRom> g_rom;

// Scene-room id allocation: ZSI path -> model id (>= kSceneModelBase), and the reverse
// list so loadModel can recover the path from the id.
std::unordered_map<std::string, int> g_sceneRoomIds;
std::vector<std::string> g_sceneRoomPaths; // index = modelId - kSceneModelBase

// Auto-replaced actor id allocation: ZAR path -> model id (>= kAutoModelBase), and the
// reverse list so loadAutoModel can recover the path from the id.
std::unordered_map<std::string, int> g_autoModelIds;
std::vector<std::string> g_autoModelPaths; // index = modelId - kAutoModelBase

Zelda3D::CtrRom* rom() {
    if (!g_rom) {
        const char* path = getenv("ZELDA3D_OOT3D_ROM");
        if (!path || !*path) {
            fprintf(stderr, "[Zelda3D] ZELDA3D_OOT3D_ROM not set — cannot load OoT3D assets\n");
            return nullptr;
        }
        g_rom = std::make_unique<Zelda3D::CtrRom>(path);
        if (!g_rom->ok()) {
            fprintf(stderr, "[Zelda3D] CtrRom(%s): %s\n", path, g_rom->error().c_str());
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
// Thin alias for the shared, game-agnostic CMB->GlGroup converter (cmb3d/asset/
// cmb_glgroups). Kept as a local name so the many call sites below read unchanged.
static inline Zelda3DGlGroup makeCgroup(const Zelda3D::Cmb& cmb, const Zelda3D::CmbDrawGroup& g,
                                        const Zelda3D::CmbVertex* srcVerts, int texBase) {
    return Zelda3D::MakeGlGroup(cmb, g, srcVerts, texBase);
}

// Decode a CMB's textures and append them to the model's texture arrays, returning the
// base index they were appended at (so a group's material texture index can be rebased).
// Per-CMB-texture dimensions as uploaded, after any hi-res pack substitution. Parallel to
// out->texRgba; the caller uses these (not the CMB's) so cTexs gets the replacement's size.
// Texture UVs are normalized, so a larger pack texture is a drop-in for the original.
// Decode a CMB's textures (hi-res pack applied) into the model's texture arrays.
// The load-bearing decode lives in the shared cmb3d converter; this wrapper keeps
// the LoadedModel-owned `dims` scratch that the caller may pass as nullptr.
static int appendTextures(LoadedModel* out, const Zelda3D::Cmb& cmb, std::vector<std::pair<int, int>>* dims = nullptr) {
    if (dims) {
        return Zelda3D::AppendCmbTextures(cmb, out->texRgba, *dims, out->texLevels);
    }
    std::vector<std::pair<int, int>> scratch;
    return Zelda3D::AppendCmbTextures(cmb, out->texRgba, scratch, out->texLevels);
}

static bool strEndsWith(const std::string& value, const char* suffix) {
    const size_t length = std::strlen(suffix);
    return value.size() >= length && value.compare(value.size() - length, length, suffix) == 0;
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
// (RmlUi "Stair Step Size" / Zelda3D_SetStairRiserY), so the player can pick larger/smaller
// steps. N = round(rampRiseY / gZelda3dStairRiserY). Default is chunkier than the old 7.8.

// Replace every kaidan ramp group in a freshly-built scene-room model with stepped
// geometry. cGroups must be (re)built AFTER this — it mutates group vert vectors.
static void generateRoomStairs(LoadedModel* out) {
    ensureStairsEnv();
    if (!gZelda3dStairs || !out->cmb) {
        return;
    }
    for (auto& g : out->groups) {
        if (texNameIsKaidan(*out->cmb, g.material_index)) {
            generateStairsGroup(g);
        }
    }
}

static void buildFromCmb(LoadedModel* out, bool bakedVertexColor, const std::vector<uint8_t>& skipMesh = {},
                         bool stairs = false) {
    Zelda3D::Cmb& cmb = *out->cmb;
    out->groups = cmb.buildDrawGroups(skipMesh);
    if (!bakedVertexColor) {
        for (auto& g : out->groups) {
            for (auto& v : g.verts) {
                v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f;
            }
        }
    } else {
        // Even in bakedVertexColor mode, some sky-dome CMBs (fine_star being the flagship
        // case, task #16) declare a vertex COLOR attribute in the SEPD but the stream data
        // isn't valid vColor — the CMB parser reads it and produces NaN / huge values, which
        // then propagate through `texture * vColor * shade` and NaN-out to black, hiding the
        // stars entirely. Sanitize: anything non-finite or wildly outside [0,1] falls back to
        // 1.0. This is port-faithful (the 3DS treats an un-authored attribute as identity too).
        auto sane = [](float c) { return std::isfinite(c) && c >= 0.0f && c <= 2.0f; };
        for (auto& g : out->groups) {
            for (auto& v : g.verts) {
                for (int k = 0; k < 4; k++) {
                    if (!sane(v.color[k])) {
                        v.color[k] = 1.0f;
                    }
                }
            }
        }
    }
    if (stairs) {
        generateRoomStairs(out);
    }

    std::vector<std::pair<int, int>> dims;
    appendTextures(out, cmb, &dims);

    // Custom stair texture: if this room has kaidan (stair) groups, append the embedded stone
    // texture (assets/zelda3d/stairs_stone.svg) and point the generated step groups at it,
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
    for (size_t i = 0; i < out->texRgba.size(); i++) {
        out->cTexs[i] = { out->texRgba[i].data(), dims[i].first, dims[i].second,
                          i < out->texLevels.size() ? out->texLevels[i] : 1 };
    }

    out->cGroups.reserve(out->groups.size());
    for (const auto& g : out->groups) {
        Zelda3DGlGroup cg = makeCgroup(cmb, g, g.verts.data(), 0);
        if (stairTexIdx >= 0 && texNameIsKaidan(cmb, g.material_index)) {
            cg.texIndex = stairTexIdx;
            cg.wrapS = cg.wrapT = 0x2901; // GL_REPEAT — tile the stone across width & length
            cg.blendEnable = 0;
            cg.alphaTest = 0;
            cg.depthWrite = 1;
            cg.polygonOffset = 0.0f;
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
static void buildFromCmbs(LoadedModel* out, std::vector<std::unique_ptr<Zelda3D::Cmb>>& cmbs) {
    struct Src {
        const Zelda3D::Cmb* cmb;
        size_t gi;
        int texBase;
    };
    std::vector<Src> srcs;
    std::vector<std::pair<int, int>> dims;
    for (auto& up : cmbs) {
        Zelda3D::Cmb& cmb = *up;
        int texBase = appendTextures(out, cmb, &dims);
        auto groups = cmb.buildDrawGroups();
        for (auto& g : groups) {
            for (auto& v : g.verts) {
                v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f;
            }
            srcs.push_back({ &cmb, out->groups.size(), texBase });
            out->groups.push_back(std::move(g));
        }
    }
    // out->groups is now final (no further reallocation) -> safe to point cGroups into it.
    out->cTexs.reserve(out->texRgba.size());
    // dims were captured per-append (post hi-res substitution), parallel to texRgba.
    for (size_t ti = 0; ti < out->texRgba.size(); ti++) {
        out->cTexs.push_back({ out->texRgba[ti].data(), dims[ti].first, dims[ti].second,
                               ti < out->texLevels.size() ? out->texLevels[ti] : 1 });
    }
    out->cGroups.reserve(out->groups.size());
    for (const auto& s : srcs) {
        out->cGroups.push_back(makeCgroup(*s.cmb, out->groups[s.gi], out->groups[s.gi].verts.data(), s.texBase));
    }
    out->ok = true;
}

// Load a scene-room model: read its ZSI, extract the single embedded room CMB, and
// build draw groups (no skeleton/animation — drawn at the world origin).
static void loadSceneRoom(int modelId, LoadedModel* out) {
    int idx = modelId - kSceneModelBase;
    if (idx < 0 || idx >= (int)g_sceneRoomPaths.size()) {
        return;
    }
    const std::string& path = g_sceneRoomPaths[idx];
    Zelda3D::CtrRom* r = rom();
    if (!r) {
        return;
    }
    auto bytes = r->read(path);
    if (bytes.empty()) {
        fprintf(stderr, "[Zelda3D] zsi not found: %s\n", path.c_str());
        return;
    }
    Zelda3D::Zsi zsi(std::move(bytes));
    if (!zsi.ok()) {
        fprintf(stderr, "[Zelda3D] Zsi %s: %s\n", path.c_str(), zsi.error().c_str());
        return;
    }
    if (!zsi.hasGeometry()) {
        fprintf(stderr, "[Zelda3D] no room geometry in %s\n", path.c_str());
        return;
    }
    out->cmb = std::make_unique<Zelda3D::Cmb>(zsi.cmbBytes());
    if (!out->cmb->ok()) {
        fprintf(stderr, "[Zelda3D] Cmb %s: %s\n", path.c_str(), out->cmb->error().c_str());
        return;
    }
    // scene rooms carry OoT3D baked vertex lighting; #5 turns fake-flat kaidan ramps into real steps
    buildFromCmb(out, /*bakedVertexColor=*/true, /*skipMesh=*/{}, /*stairs=*/true);
    fprintf(stderr, "[Zelda3D] loaded scene-room model %d (%s): %zu groups, %zu textures\n", modelId, path.c_str(),
            out->cGroups.size(), out->cTexs.size());
    // #29 diagnostic (`log room 1`): dump per-group material/texture + per-group bbox so the
    // "untextured dome" group can be identified by index (pair with ZELDA3D_SOLOGROUP to isolate
    // it visually).
    if (Zelda3D_LogEnabled(Z3D_LOG_ROOM)) {
        const auto& texs = out->cmb->textures();
        for (size_t i = 0; i < out->cGroups.size(); i++) {
            const auto& g = out->groups[i];
            int ti = out->cGroups[i].texIndex;
            const char* tn = (ti >= 0 && ti < (int)texs.size()) ? texs[ti].name.c_str() : "<none/stair>";
            float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
            for (const auto& v : g.verts) {
                for (int k = 0; k < 3; k++) {
                    mn[k] = std::min(mn[k], v.pos[k]);
                    mx[k] = std::max(mx[k], v.pos[k]);
                }
            }
            Z3D_LOG(ROOM,
                    "grp%2zu mat%d tex%d %-18s verts%5zu mesh_id%d "
                    "x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]\n",
                    i, g.material_index, ti, tn, g.verts.size(), g.mesh_id, mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]);
        }
    }
}

// Load an actor model: read its ZAR, find the .cmb, build groups (+ keep the ZAR/CMB
// resident so the animation layer can load CSABs and recompute skin matrices).
static void loadActorModel(int modelId, LoadedModel* out) {
    Zelda3D::CtrRom* r = rom();
    if (!r) {
        return;
    }
    auto zarBytes = r->read(kModels[modelId].zarPath);
    if (zarBytes.empty()) {
        fprintf(stderr, "[Zelda3D] zar not found: %s\n", kModels[modelId].zarPath);
        return;
    }
    out->zar = std::make_unique<Zelda3D::Zar>(std::move(zarBytes));
    if (!out->zar->ok()) {
        fprintf(stderr, "[Zelda3D] Zar: %s\n", out->zar->error().c_str());
        return;
    }
    const Zelda3D::ZarFile* cmbf = nullptr;
    const char* want = kModels[modelId].cmbName;
    if (want) {
        for (const auto& f : out->zar->files()) {
            if (f.name.find(want) != std::string::npos && f.name.size() >= 4 &&
                f.name.compare(f.name.size() - 4, 4, ".cmb") == 0) {
                cmbf = &f;
                break;
            }
        }
    }
    if (!cmbf) {
        cmbf = out->zar->firstWithSuffix(".cmb"); // fallback: single-CMB ZARs
    }
    if (!cmbf) {
        fprintf(stderr, "[Zelda3D] no .cmb in %s\n", kModels[modelId].zarPath);
        return;
    }
    out->cmb = std::make_unique<Zelda3D::Cmb>(out->zar->read(*cmbf));
    if (!out->cmb->ok()) {
        fprintf(stderr, "[Zelda3D] Cmb: %s\n", out->cmb->error().c_str());
        return;
    }
    buildFromCmb(out, /*bakedVertexColor=*/false);             // characters/props: dynamic lighting, color attr unused
    Zelda3D_AppendFacialFrames(out, kModels[modelId].zarPath); // eye/mouth .cmab frames (keystone #3)
    fprintf(stderr, "[Zelda3D] loaded model %d (%s): %zu groups, %zu textures\n", modelId, kModels[modelId].zarPath,
            out->cGroups.size(), out->cTexs.size());
}

// Geometric bounding-box diagonal of a model's draw groups, in the model's own
// local space. Used by the auto-scale path as a rotation-invariant size measure: the
// world scale for an auto-replaced actor = (measured N64 world bbox diagonal) / (this
// OoT3D model diagonal). Returns 0 if the model has no geometry.
static float bboxDiag(const std::vector<Zelda3D::CmbDrawGroup>& groups) {
    float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
    bool any = false;
    for (const auto& g : groups) {
        for (const auto& v : g.verts) {
            any = true;
            for (int k = 0; k < 3; k++) {
                mn[k] = std::min(mn[k], v.pos[k]);
                mx[k] = std::max(mx[k], v.pos[k]);
            }
        }
    }
    if (!any) {
        return 0.0f;
    }
    float dx = mx[0] - mn[0], dy = mx[1] - mn[1], dz = mx[2] - mn[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

static bool isDebrisCmbName(const std::string& n) {
    static const char* kSkip[] = { "hahen", "broke", "_bf", "kakera", "fragment" };
    std::string lo = n;
    for (auto& c : lo) {
        c = (char)std::tolower((unsigned char)c);
    }
    for (const char* s : kSkip) {
        if (lo.find(s) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// A CMB whose geometry is FLAT (one bbox dimension ~0) is a billboard/sprite/decal quad
// (e.g. wood02's wd_model [800,655,0], the *_modelT transparency sprites), not a real 3D
// model. The auto path skips these so it picks the actual mesh (a 3D tree, not a flat white
// quad on the ground). True if the smallest extent is a tiny fraction of the largest.
static bool isFlatGroups(const std::vector<Zelda3D::CmbDrawGroup>& groups) {
    float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
    bool any = false;
    for (const auto& g : groups) {
        for (const auto& v : g.verts) {
            any = true;
            for (int k = 0; k < 3; k++) {
                mn[k] = std::min(mn[k], v.pos[k]);
                mx[k] = std::max(mx[k], v.pos[k]);
            }
        }
    }
    if (!any) {
        return true;
    }
    float e0 = mx[0] - mn[0], e1 = mx[1] - mn[1], e2 = mx[2] - mn[2];
    float emax = std::max(e0, std::max(e1, e2));
    float emin = std::min(e0, std::min(e1, e2));
    return emax <= 1e-3f || emin < 0.02f * emax;
}

static size_t vertCountGroups(const std::vector<Zelda3D::CmbDrawGroup>& groups) {
    size_t n = 0;
    for (const auto& g : groups) {
        n += g.verts.size();
    }
    return n;
}

// Load an auto-replaced actor model: read the ZAR at its registered path, pick the main
// CMB (largest non-debris), build draw groups. No hand-tuned cmbName — the heuristic
// generalizes the manual selection used by the explicit kModels[] table. Characters/props
// are dynamically lit, so vertex color is forced white like loadActorModel.
// The *_new Link body bakes ALL hand-pose + held-equipment variants into one CMB, each on a
// distinct mesh_id; the player path selects the live subset per frame via Zelda3D_GL_SetMidMask
// (draw groups are split by mesh_id). So nothing Link-specific is culled at load anymore — the
// old build-time hand-variant/equipment cull was replaced by that per-frame mesh_id mask.

// #28e — build a synthetic textured BILLBOARD quad as a LoadedModel from a standalone CTXB
// sprite (no CMB). Used for the OoT3D sun/moon discs (tex/fine_sun.ctxb, tex/fine_moon0.ctxb in
// /kankyo/BlueSky.zar), which the engine billboards itself — there is no CMB to hang the texture
// on. The quad geometry matches the N64 sun/moon billboard exactly (VTX -31..32 in the XY plane),
// so with the same translate*billboard*scale transform (set in Zelda3D_TryDrawSunMoon) it renders
// pixel-identically to the N64 sprite, just with the OoT3D texture. The caller pins it to the far
// plane (handle bit 30) and faces it to the camera via play->billboardMtxF. `additive` selects the
// blend: the sun/lens-flare discs are a glow on black (src_alpha,ONE = add over the sky), the moon
// is an alpha-masked disc (src_alpha,1-src_alpha = normal alpha blend).
// Load a standalone CTXB texture and build a quad. If zarPath ends in ".ctxb" the file is read
// DIRECTLY from the romfs (no ZAR wrap — used for /menu/ and /misc/ atmospheric textures at title,
// task #16); otherwise zarPath is the ZAR archive and ctxbName picks the file inside it.
// mirrorQuadrant: some standalone billboard textures (the moon's fine_moon1/fine_moon2 halo
// glows) are authored as a SINGLE QUADRANT of a symmetric radial gradient, with the brightest
// texel at one corner and the opposite corner/edges at zero — meant to be reconstructed by
// mirroring that quadrant across both axes into a full glow, centred on the quadrant's bright
// corner. The CTXB container for these standalone sprites carries no material/sampler chunk (RE'd
// via scratch/ctxb_hdr_dump.cpp: chunkCount=1, only a "tex " chunk — no wrap-mode byte anywhere in
// the asset), so there's no declared wrap mode to honor; this bakes the mirrored expansion
// explicitly at load time instead of relying on GPU mirrored-repeat addressing (whose mirror axis
// falls at a texel boundary, not at the quadrant's bright corner, so it doesn't reconstruct this
// asset's specific corner-centred layout without an extra UV pre-flip). Verified against the
// measured source texture (bright corner isolated, all others near-zero) — see
// debug_journal/2026-07-10-moon-mirror-and-fade-attenuation.md.
static std::vector<uint8_t> mirrorExpandQuadrant(const std::vector<uint8_t>& src, int q, int* outW, int* outH) {
    int n = 2 * q;
    std::vector<uint8_t> out(n * n * 4);
    auto srcPx = [&](int x, int y) -> const uint8_t* { return &src[(y * q + x) * 4]; };
    for (int Y = 0; Y < n; Y++) {
        int sy = (Y < q) ? (q - 1 - Y) : (Y - q);
        for (int X = 0; X < n; X++) {
            int sx = (X < q) ? X : (n - 1 - X);
            const uint8_t* s = srcPx(sx, sy);
            uint8_t* d = &out[(Y * n + X) * 4];
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
    }
    *outW = n;
    *outH = n;
    return out;
}

static void loadBillboard(LoadedModel* out, const std::string& zarPath, const std::string& ctxbName, bool additive,
                          float u0 = 0.0f, float v0 = 0.0f, float u1 = 1.0f, float v1 = 1.0f,
                          bool mirrorQuadrant = false) {
    Zelda3D::CtrRom* r = rom();
    if (!r) {
        return;
    }
    std::vector<uint8_t> ctxbBytes;
    bool directCtxb = (zarPath.size() >= 5 && zarPath.compare(zarPath.size() - 5, 5, ".ctxb") == 0);
    if (directCtxb) {
        ctxbBytes = r->read(zarPath);
        if (ctxbBytes.empty()) {
            fprintf(stderr, "[Zelda3D] billboard: ctxb not found: %s\n", zarPath.c_str());
            return;
        }
    } else {
        auto zarBytes = r->read(zarPath);
        if (zarBytes.empty()) {
            fprintf(stderr, "[Zelda3D] billboard: zar not found: %s\n", zarPath.c_str());
            return;
        }
        out->zar = std::make_unique<Zelda3D::Zar>(std::move(zarBytes));
        if (!out->zar->ok()) {
            fprintf(stderr, "[Zelda3D] billboard Zar %s: %s\n", zarPath.c_str(), out->zar->error().c_str());
            return;
        }
        const Zelda3D::ZarFile* zf = nullptr;
        for (const auto& f : out->zar->files()) {
            if (f.name.find(ctxbName) != std::string::npos) {
                zf = &f;
                break;
            }
        }
        if (!zf) {
            fprintf(stderr, "[Zelda3D] billboard %s: no '%s'\n", zarPath.c_str(), ctxbName.c_str());
            return;
        }
        ctxbBytes = out->zar->read(*zf);
    }
    Zelda3D::Ctxb ctxb(std::move(ctxbBytes));
    if (!ctxb.ok() || ctxb.textures().empty()) {
        fprintf(stderr, "[Zelda3D] billboard ctxb %s: %s\n", ctxbName.c_str(), ctxb.error().c_str());
        return;
    }
    int tw = 0, th = 0;
    auto rgba = ctxb.decodeRGBA(0, &tw, &th);
    if (rgba.empty()) {
        fprintf(stderr, "[Zelda3D] billboard %s: decode failed\n", ctxbName.c_str());
        return;
    }
    if (mirrorQuadrant) {
        int mw = 0, mh = 0;
        rgba = mirrorExpandQuadrant(rgba, tw, &mw, &mh);
        tw = mw;
        th = mh;
    }
    out->texRgba.push_back(std::move(rgba));
    out->cTexs.push_back({ out->texRgba[0].data(), tw, th, 1 });

    // One quad (two triangles) in the XY plane, matching the N64 sun/moon billboard vertices.
    // weights[0]=1, boneIds[0]=0 so with identity uBones the GPU-skin pass is a no-op (pos == model
    // pos) — a non-skinned sprite. Per-vertex colour white; the draw's tint/alpha multiplies it.
    auto mkv = [](float x, float y, float u, float v) {
        Zelda3D::CmbVertex vtx{};
        vtx.pos[0] = x;
        vtx.pos[1] = y;
        vtx.pos[2] = 0.0f;
        vtx.nrm[2] = 1.0f;
        vtx.uv[0] = u;
        vtx.uv[1] = v;
        vtx.weights[0] = 1.0f;
        vtx.color[0] = vtx.color[1] = vtx.color[2] = vtx.color[3] = 1.0f;
        return vtx;
    };
    Zelda3D::CmbVertex bl = mkv(-31, -31, u0, v0), br = mkv(32, -31, u1, v0), tl = mkv(-31, 32, u0, v1),
                       tr = mkv(32, 32, u1, v1);
    Zelda3D::CmbDrawGroup g;
    g.material_index = -1;
    g.mesh_id = -1;
    // N64 tris: gSP2Triangles(0,1,2, 0, 2,1,3, 0) over verts {bl,br,tl,tr}.
    g.verts = { bl, br, tl, tl, br, tr };
    out->groups.push_back(std::move(g));

    Zelda3DGlGroup cg{};
    cg.verts = reinterpret_cast<const Zelda3DGlVtx*>(out->groups[0].verts.data());
    cg.vertCount = (int)out->groups[0].verts.size();
    cg.texIndex = 0;
    cg.uv0Scale[0] = cg.uv0Scale[1] = 1.0f;
    cg.coord0Mapping = 1;
    cg.alphaTest = 0;
    cg.alphaRef = 0.0f;
    cg.wrapS = cg.wrapT = 0x2900; // GL_CLAMP (disc is centred; edges fade to black/transparent)
    cg.blendEnable = 1;
    cg.blendSrcRGB = 0x0302;                     // GL_SRC_ALPHA
    cg.blendDstRGB = additive ? 0x0001 : 0x0303; // GL_ONE (add) : GL_ONE_MINUS_SRC_ALPHA
    cg.blendEqRGB = 0x8006;                      // GL_FUNC_ADD
    cg.blendSrcA = 0x0001;                       // GL_ONE
    cg.blendDstA = additive ? 0x0001 : 0x0303;
    cg.blendEqA = 0x8006;
    for (int k = 0; k < 4; k++) {
        cg.blendColor[k] = (k == 3) ? 1.0f : 0.0f;
    }
    cg.depthWrite = 0; // sky element: never occlude the world
    cg.polygonOffset = 0.0f;
    cg.cull = 0;
    cg.faceCull = 0; // camera-facing billboard quad: always double-sided
    cg.meshId = -1;
    out->cGroups.push_back(cg);
    out->skinned = false;
    out->ok = true;
    fprintf(stderr, "[Zelda3D] billboard %s|%s%s: %dx%d tex\n", zarPath.c_str(), ctxbName.c_str(),
            additive ? " [add]" : "", tw, th);
}

static void loadAutoModel(int modelId, LoadedModel* out) {
    int idx = modelId - kAutoModelBase;
    if (idx < 0 || idx >= (int)g_autoModelPaths.size()) {
        return;
    }
    // A key may carry a forced-CMB selector: "<zar>|<cmbSubstr>". Used when one shared ZAR holds
    // several distinct objects, each needed by a DIFFERENT actor (e.g. zelda_spot01_objects.zar =
    // windmill c_s01fusya + well pillar c_s01idohashira + well water c_s01idomizu). The default
    // "largest CMB" heuristic would give every such actor the same (biggest) CMB. With a selector
    // we pick the named CMB instead; scale still auto-derives (per-actor N64 height / this CMB).
    std::string key = g_autoModelPaths[idx];
    // "BILLBOARD:" / "BILLBOARDADD:" prefix marks a standalone CTXB sprite (no CMB) drawn as a
    // camera-facing quad — the OoT3D sun/moon discs (#28e). Key = "<prefix><zar>|<ctxbName>".
    // Optional trailing "#u0,v0,u1,v1" clamps the sampled texture region to that UV subrect —
    // e.g. "#0,0.4,0.5,1" samples only the upper-left quadrant. Used for the fine_lensflare
    // atlas: its rainbow-ring halo lives in the upper-left quadrant of the texture, and the
    // sun-flare orbs live in the right half. To render just the halo behind the moon we
    // sample only the ring region.
    // Optional trailing "~MIRROR" (before any "#uv") marks the ctxb as a single QUADRANT of a
    // symmetric radial glow that must be mirror-expanded 2x2 at load — see
    // mirrorExpandQuadrant()'s comment. Used for the moon's fine_moon1/fine_moon2 halo sprites.
    {
        bool add = false;
        const char* pfx = nullptr;
        if (key.rfind("BILLBOARDADD:", 0) == 0) {
            add = true;
            pfx = "BILLBOARDADD:";
        } else if (key.rfind("BILLBOARD:", 0) == 0) {
            pfx = "BILLBOARD:";
        }
        if (pfx) {
            std::string rest = key.substr(std::strlen(pfx));
            auto bar = rest.find('|');
            std::string zp = (bar == std::string::npos) ? rest : rest.substr(0, bar);
            std::string tail = (bar == std::string::npos) ? std::string() : rest.substr(bar + 1);
            std::string ctxb = tail;
            float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
            auto hash = tail.find('#');
            if (hash != std::string::npos) {
                ctxb = tail.substr(0, hash);
                std::string uv = tail.substr(hash + 1);
                if (std::sscanf(uv.c_str(), "%f,%f,%f,%f", &u0, &v0, &u1, &v1) != 4) {
                    u0 = 0.0f;
                    v0 = 0.0f;
                    u1 = 1.0f;
                    v1 = 1.0f;
                }
            }
            bool mirror = false;
            const std::string mirrorTag = "~MIRROR";
            if (ctxb.size() >= mirrorTag.size() &&
                ctxb.compare(ctxb.size() - mirrorTag.size(), mirrorTag.size(), mirrorTag) == 0) {
                mirror = true;
                ctxb = ctxb.substr(0, ctxb.size() - mirrorTag.size());
            }
            loadBillboard(out, zp, ctxb, add, u0, v0, u1, v1, mirror);
            return;
        }
    }
    // "SKY:" prefix marks the skybox dome (a vertex-coloured, untextured CMB). It must keep its baked
    // per-vertex colour (the day/night gradient) and write NO depth (drawn behind all world geometry).
    // The renderer pins it to the far plane via the per-draw sky flag; see Zelda3D_GL_Submit.
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
    Zelda3D::CtrRom* r = rom();
    if (!r) {
        return;
    }
    auto zarBytes = r->read(zarPath);
    if (zarBytes.empty()) {
        fprintf(stderr, "[Zelda3D] auto: zar not found: %s\n", zarPath.c_str());
        return;
    }
    out->zar = std::make_unique<Zelda3D::Zar>(std::move(zarBytes));
    if (!out->zar->ok()) {
        fprintf(stderr, "[Zelda3D] auto Zar %s: %s\n", zarPath.c_str(), out->zar->error().c_str());
        return;
    }

    // Forced-CMB selection: load exactly the named CMB (first match) and skip the heuristic.
    if (!forcedCmb.empty()) {
        for (const auto& f : out->zar->files()) {
            if (f.name.size() < 4 || f.name.compare(f.name.size() - 4, 4, ".cmb") != 0) {
                continue;
            }
            if (f.name.find(forcedCmb) == std::string::npos) {
                continue;
            }
            auto cmb = std::make_unique<Zelda3D::Cmb>(out->zar->read(f));
            if (!cmb->ok()) {
                fprintf(stderr, "[Zelda3D] auto forced-cmb %s '%s': %s\n", zarPath.c_str(), f.name.c_str(),
                        cmb->error().c_str());
                return;
            }
            out->cmb = std::move(cmb);
            out->skinned = out->cmb->bones().size() > 1;
            buildFromCmb(out, /*bakedVertexColor=*/sky);
            Zelda3D_AppendFacialFrames(out, key);
            if (sky) {
                for (auto& grp : out->cGroups) {
                    grp.depthWrite = 0; // never occlude the world
                }
            }
            fprintf(stderr,
                    "[Zelda3D] auto-loaded model %d (%s | %s)%s: cmb '%s', height=%.1f, %zu groups, %zu textures\n",
                    modelId, zarPath.c_str(), forcedCmb.c_str(), sky ? " [sky]" : "", f.name.c_str(),
                    Zelda3D_ModelGeometryHeight(*out), out->cGroups.size(), out->cTexs.size());
            return;
        }
        fprintf(stderr, "[Zelda3D] auto forced-cmb %s: no cmb matches '%s' -> heuristic pick\n", zarPath.c_str(),
                forcedCmb.c_str());
    }

    // Hand-curated multi-part assembly? Merge exactly the named CMBs (in order) instead of
    // single-picking one (which would render one detached sub-piece). See kAssemblies.
    const AssemblySpec* asmSpec = nullptr;
    for (const auto& a : kAssemblies) {
        if (!a.zarSuffix) {
            continue; // sentinel / empty table
        }
        size_t n = std::strlen(a.zarSuffix);
        if (zarPath.size() >= n && zarPath.compare(zarPath.size() - n, n, a.zarSuffix) == 0) {
            asmSpec = &a;
            break;
        }
    }
    if (asmSpec) {
        std::vector<std::unique_ptr<Zelda3D::Cmb>> cmbs;
        for (const auto& want : asmSpec->cmbNames) {
            // Each substring merges EVERY matching .cmb (in archive order), so one prefix can
            // pull a whole subassembly (e.g. "kanban_L_" = all 4 left board segments).
            int matched = 0;
            for (const auto& zf : out->zar->files()) {
                if (zf.name.size() < 4 || zf.name.compare(zf.name.size() - 4, 4, ".cmb") != 0) {
                    continue;
                }
                if (zf.name.find(want) == std::string::npos) {
                    continue;
                }
                auto c = std::make_unique<Zelda3D::Cmb>(out->zar->read(zf));
                if (!c->ok()) {
                    fprintf(stderr, "[Zelda3D] assembly %s: '%s': %s\n", zarPath.c_str(), zf.name.c_str(),
                            c->error().c_str());
                    continue;
                }
                cmbs.push_back(std::move(c));
                matched++;
            }
            if (!matched) {
                fprintf(stderr, "[Zelda3D] assembly %s: no cmb matches '%s'\n", zarPath.c_str(), want.c_str());
            }
        }
        if (!cmbs.empty()) {
            size_t nMerged = cmbs.size();
            out->skinned = false; // hand-listed assemblies are static props (no skinning)
            buildFromCmbs(out, cmbs);
            out->cmb = std::move(cmbs[0]); // keep a resident CMB (the main part)
            fprintf(stderr,
                    "[Zelda3D] auto-loaded ASSEMBLY model %d (%s): %zu cmbs merged, height=%.1f, %zu groups, %zu "
                    "textures\n",
                    modelId, zarPath.c_str(), nMerged, Zelda3D_ModelGeometryHeight(*out), out->cGroups.size(),
                    out->cTexs.size());
            return;
        }
        fprintf(stderr, "[Zelda3D] assembly %s: no cmbs merged -> single-pick fallback\n", zarPath.c_str());
    }

    // Pick the MAIN model CMB. Parse each candidate once (one-time per object). Prefer the
    // most-detailed real mesh: skip debris (by name) and flat billboard/sprite quads (e.g.
    // wood02's wd_model is a flat [800,655,0] decal that, picked by raw size, rendered as a
    // white quad on the ground). Among the rest, the CMB with the most vertices is the main
    // body (a 3D tree, not its sprite LOD). Fall back progressively so a ZAR with only
    // flat/debris CMBs still yields something rather than nothing.
    const Zelda3D::ZarFile* best = nullptr;
    std::unique_ptr<Zelda3D::Cmb> bestCmb;
    size_t bestVerts = 0;
    const Zelda3D::ZarFile* fbFile = nullptr; // best non-debris (incl. flat), by diagonal
    std::unique_ptr<Zelda3D::Cmb> fbCmb;
    float fbDiag = -1.0f;
    int nCmb = 0;
    for (const auto& f : out->zar->files()) {
        if (f.name.size() < 4 || f.name.compare(f.name.size() - 4, 4, ".cmb") != 0) {
            continue;
        }
        nCmb++;
        if (isDebrisCmbName(f.name)) {
            continue;
        }
        auto cmb = std::make_unique<Zelda3D::Cmb>(out->zar->read(f));
        if (!cmb->ok()) {
            continue;
        }
        auto groups = cmb->buildDrawGroups();
        float d = bboxDiag(groups);
        if (d > fbDiag) {
            fbDiag = d;
            fbFile = &f;
            fbCmb = std::make_unique<Zelda3D::Cmb>(out->zar->read(f));
        }
        if (isFlatGroups(groups)) {
            continue; // billboard/sprite/decal -> not the main mesh
        }
        size_t nv = vertCountGroups(groups);
        if (nv > bestVerts) {
            bestVerts = nv;
            best = &f;
            bestCmb = std::move(cmb);
        }
    }
    if (!bestCmb) {
        best = fbFile;
        bestCmb = std::move(fbCmb);
    } // all flat? take largest non-debris
    // Last resort: if every CMB looked like debris (or none parsed), take the first .cmb.
    if (!bestCmb) {
        const Zelda3D::ZarFile* f = out->zar->firstWithSuffix(".cmb");
        if (f) {
            bestCmb = std::make_unique<Zelda3D::Cmb>(out->zar->read(*f));
            best = f;
        }
    }
    if (!bestCmb || !bestCmb->ok()) {
        fprintf(stderr, "[Zelda3D] auto: no usable .cmb in %s\n", zarPath.c_str());
        return;
    }
    out->cmb = std::move(bestCmb);
    // Articulated (>1 bone) => skinned character. With no animation it would render in a
    // frozen bind/T-pose, so the auto path skips it and leaves the N64 model. Calibrated,
    // animated characters go through the explicit sModelTable (with an anim resolver).
    out->skinned = out->cmb->bones().size() > 1;
    // The *_new Link body bakes ALL hand-pose + held-equipment variants into one mesh, each on a
    // distinct CMB mesh_id; the game shows a state-dependent subset. We keep every variant as its
    // own draw group (buildDrawGroups now splits by mesh_id) and let the player path pick the
    // visible subset per frame via Zelda3D_GL_SetMidMask. So NO build-time cull here.
    buildFromCmb(out, /*bakedVertexColor=*/false);
    Zelda3D_AppendFacialFrames(out, key); // eye/mouth .cmab frames (keystone #3); key preserves forced-CMB selector
    // Note: `skinned` on its own doesn't mean the model is skipped — with the
    // default N64ANIM path (ZELDA3D_N64ANIM=1, gZelda3dAnimLive=1) skinned
    // characters render as their OoT3D model driven by the live N64
    // SkelAnime joint table (see Zelda3D_AutoModelSkinned callers). The
    // old " (skinned->skip)" tag was misleading; state=3 (real skip) only
    // fires when the retarget path is unavailable.
    fprintf(stderr,
            "[Zelda3D] auto-loaded model %d (%s): cmb '%s' of %d, height=%.1f, bones=%zu%s, %zu groups, %zu textures\n",
            modelId, zarPath.c_str(), best ? best->name.c_str() : "?", nCmb, Zelda3D_ModelGeometryHeight(*out),
            out->cmb->bones().size(), out->skinned ? " (skinned=SkelAnime retarget)" : "", out->cGroups.size(),
            out->cTexs.size());
}

} // namespace
  // so zelda3d_anim.cpp can resolve a model). It still sees the internal-linkage loaders above
  // (anonymous-namespace members are visible throughout this TU).

Zelda3D::CtrRom* Zelda3D_ModelRom() {
    return rom();
}

LoadedModel* loadModel(int modelId) {
    auto it = g_loaded.find(modelId);
    if (it != g_loaded.end()) {
        return it->second.get();
    }

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

namespace { // resume internal-linkage model-core helpers

// Renderer provider: hand back the CPU data for a model id (loads lazily).
int provider(int modelId, const Zelda3DGlGroup** groups, int* groupCount, const Zelda3DGlTex** texs, int* texCount) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || lm->cGroups.empty()) {
        return 0;
    }
    *groups = lm->cGroups.data();
    *groupCount = (int)lm->cGroups.size();
    *texs = lm->cTexs.data();
    *texCount = (int)lm->cTexs.size();
    return 1;
}

} // namespace

extern "C" {

// Register the renderer's model provider once. Safe to call repeatedly.
// The provider slot lives in libultraship (`Zelda3D_GL_SetModelProvider` -> one `g_provider`), which
// both game cores SHARE, while this latch is a file static inside ONE core. So the latch was making
// a claim it had no standing to make: "the provider is already mine" was only ever "I set it once".
//
// On `mm -> oot -> mm` that is wrong by the third run. MM registers on run 1; OoT overwrites the
// shared slot on run 2; MM's latch is still set on run 3, so it never re-registers and every MM
// model lookup resolves through OoT's provider and OoT's id space. The failure is silent -- wrong
// or missing geometry, not a crash -- which is the kind this codebase has learned to distrust most.
//
// Fixed by deleting the latch rather than making it run-scoped: a run stamp would also work today,
// but only because exactly one core runs at a time, and the store is two pointer writes. "Ensure"
// now means ensure.
void Zelda3D_EnsureModelProvider(void) {
    Zelda3D_GL_SetModelProvider(provider);
}

float Zelda3D_ModelScaleById(int modelId) {
    if (modelId < 0 || modelId >= (int)(sizeof(kModels) / sizeof(kModels[0]))) {
        return 1.0f;
    }
    return kModels[modelId].worldScale;
}

// Master Quest room/collision selection. OoT3D ships a COMPLETE parallel MQ asset set: 232
// `*_dd_info.zsi` files, and all 232 differ byte-for-byte from their vanilla twin (md5 compared over
// the whole ROM). zelda3d had no MQ branch at all, so in an MQ dungeon it rendered AND collided the
// VANILLA 3DS geometry while the N64 side ran MQ rooms and actors — wrong walls, wrong floors, and
// silent: MQ room counts equal vanilla's in 232/232 cases, so nothing errored or logged.
//
// This PROBES for the `_dd` twin and falls back. Only the 12 MQ dungeons have one, so applying the
// suffix unconditionally under MQ would break every overworld scene with a file-not-found.
// Get-or-allocate a stable model id for a scene room, keyed by its ZSI path
// (/scene/<name>_<R>_info.zsi). The geometry loads lazily on first draw via the
// provider. Returns -1 if sceneName is null/empty. The game calls this from its
// room-draw hook with the OoT3D scene name (kZelda3dSceneNames) + room number.
int Zelda3D_RoomModelId(const char* sceneName, int roomNum) {
    if (!sceneName || !*sceneName || roomNum < 0) {
        return -1;
    }
    std::string path = Zelda3D_ResolveSceneZsiPath(sceneName, roomNum);
    auto it = g_sceneRoomIds.find(path);
    if (it != g_sceneRoomIds.end()) {
        return it->second;
    }
    int id = kSceneModelBase + (int)g_sceneRoomPaths.size();
    g_sceneRoomPaths.push_back(path);
    g_sceneRoomIds[path] = id;
    return id;
}

// #5 — toggle real stepped stairs. Sets the gate and evicts every cached scene-room
// model so the next draw rebuilds (with or without generated steps), for live A/B.
void Zelda3D_SetStairs(int on) {
    gZelda3dStairs = on ? 1 : 0;
    for (auto it = g_loaded.begin(); it != g_loaded.end();) {
        if (it->first >= kSceneModelBase && it->first < kAutoModelBase) {
            it = g_loaded.erase(it);
        } else {
            ++it;
        }
    }
}
int Zelda3D_GetStairs(void) {
    return gZelda3dStairs;
}

// #5 — set the generated step rise (world-units/step). Larger = bigger steps. Drops the cached
// scene-room CPU models so the provider rebuilds their stair geometry with the new rise, and asks
// the GL layer to evict the matching uploads so the change shows live (next render pass). Collision
// keeps the previous rise until the next scene load (render is what the user is tuning here).
void Zelda3D_SetStairRiserY(float v) {
    if (v < 1.0f) {
        v = 1.0f;
    }
    if (v == gZelda3dStairRiserY) {
        return;
    }
    gZelda3dStairRiserY = v;
    for (auto it = g_loaded.begin(); it != g_loaded.end();) {
        if (it->first >= kSceneModelBase && it->first < kAutoModelBase) {
            it = g_loaded.erase(it);
        } else {
            ++it;
        }
    }
    Zelda3D_GL_RequestEvictRange(kSceneModelBase, kAutoModelBase);
}
float Zelda3D_GetStairRiserY(void) {
    return gZelda3dStairRiserY;
}

// Get-or-allocate a stable model id for an auto-replaced actor model, keyed by its ZAR
// path (e.g. "/actor/zelda_box.zar"). The geometry loads lazily on first draw via the
// provider. Returns -1 if zarPath is null/empty. The game calls this from the ZELDA3D_AUTO
// actor path with the ZAR resolved from the actor's object id (kZelda3dObjectZars).
int Zelda3D_AutoModelId(const char* zarPath) {
    if (!zarPath || !*zarPath) {
        return -1;
    }
    std::string path(zarPath);
    auto it = g_autoModelIds.find(path);
    if (it != g_autoModelIds.end()) {
        return it->second;
    }
    int id = kAutoModelBase + (int)g_autoModelPaths.size();
    g_autoModelPaths.push_back(path);
    g_autoModelIds[path] = id;
    return id;
}

// Failed or articulated models are skipped by the automatic static-model replacement path.
int Zelda3D_AutoModelSkinned(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) {
        return 1;
    }
    return lm->skinned ? 1 : 0;
}

// The ZAR path an auto model was allocated from (e.g. "/actor/zelda_kw1.zar"), or NULL. Lets the
// actor draw path identify WHICH model is loaded by archive name (stable), since the numeric model
// id is allocation-order dependent. Used to pick a shared-CMB variant subset (e.g. En_Ko Kokiri
// kids: kokiripeople/kokirimaster bake multiple head variants on distinct mesh_ids).
const char* Zelda3D_AutoModelZar(int modelId) {
    int idx = modelId - kAutoModelBase;
    if (idx < 0 || idx >= (int)g_autoModelPaths.size()) {
        return nullptr;
    }
    return g_autoModelPaths[idx].c_str();
}

} // extern "C"
