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
#include "asset/mat4.h"
#include "asset/pica_texture.h"
#include "asset/cityhash.h"
#include "asset/texpack.h"
#include "asset/cmb_glgroups.h"   // shared CMB -> renderer GlGroup/texture converter
#include "fast/zelda3d_gl.h"
#include "zelda3d_model_internal.h" // LoadedModel + loadModel (shared with zelda3d_anim.cpp)
#include "ship/Context.h"                              // #20 keyboard-inject verification shim
#include "ship/controller/controldeck/ControlDeck.h"   // #20 ProcessKeyboardEvent path
#include <stb_image.h>
#include "zelda3d_stairs.h" // procedural stair geometry (gZelda3dStairs, generateStairsGroup, ...)

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
const int kSceneModelBase = 1000;

// Auto-replaced actor models live in a THIRD id range (above scene rooms) so the
// ZELDA3D_AUTO path can allocate ids for arbitrary actor ZARs (discovered at runtime
// from the object id -> ZAR table) without colliding with the hand-listed actor
// models (0..N) or scene rooms (1000..). Keyed by ZAR path; main CMB picked by the
// "largest non-debris" heuristic. See Zelda3D_AutoModelId / loadAutoModel.
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

// PC HUD — decode an OoT3D standalone romfs .ctxb atlas (e.g. /menu/01_US_ENGLISH/hud_all00.ctxb,
// icon_item_menu00.ctxb) to RGBA8 once, cached by romfs path. These are the real 3DS HUD textures
// the native Vulkan HUD (zelda3d_hud_vk.cpp) draws sub-rects of, in place of the N64/SVG icons.
// Returns the RGBA buffer (top row first) + dims, or NULL. Texel index `texIdx` selects the entry
// (these menu files each carry a single atlas at index 0).
struct OoT3dAtlas {
    std::vector<uint8_t> rgba;
    int w = 0, h = 0;
};
extern "C" const void* Zelda3D_OoT3dAtlas(const char* romfsPath, int texIdx, int* w, int* h) {
    static std::unordered_map<std::string, OoT3dAtlas> cache;
    if (romfsPath == nullptr) {
        if (w) *w = 0; if (h) *h = 0; return nullptr;
    }
    std::string key = std::string(romfsPath) + "#" + std::to_string(texIdx);
    auto it = cache.find(key);
    if (it == cache.end()) {
        OoT3dAtlas a;
        Zelda3D::CtrRom* r = rom();
        if (r) {
            std::vector<uint8_t> bytes = r->read(romfsPath);
            if (!bytes.empty()) {
                Zelda3D::Ctxb ctxb(std::move(bytes));
                if (ctxb.ok() && texIdx >= 0 && texIdx < (int)ctxb.textures().size()) {
                    int tw = 0, th = 0;
                    a.rgba = ctxb.decodeRGBA((size_t)texIdx, &tw, &th);
                    a.w = tw; a.h = th;
                } else {
                    fprintf(stderr, "[Zelda3D] OoT3dAtlas %s: ctxb %s\n", romfsPath,
                            ctxb.ok() ? "texIdx OOR" : ctxb.error().c_str());
                }
            } else {
                fprintf(stderr, "[Zelda3D] OoT3dAtlas: romfs file not found: %s\n", romfsPath);
            }
        }
        // No Zelda3D_HudTexClaim here: this atlas feeds only the native Vulkan HUD (Zelda3D_Hud_Tex),
        // never the Fast3D pointer-keyed texture cache, so it needs no eviction guard.
        it = cache.emplace(std::move(key), std::move(a)).first;
    }
    if (it->second.rgba.empty()) {
        if (w) *w = 0; if (h) *h = 0; return nullptr;
    }
    if (w) *w = it->second.w;
    if (h) *h = it->second.h;
    return it->second.rgba.data();
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
static int appendTextures(LoadedModel* out, const Zelda3D::Cmb& cmb, std::vector<std::pair<int,int>>* dims = nullptr) {
    if (dims) return Zelda3D::AppendCmbTextures(cmb, out->texRgba, *dims);
    std::vector<std::pair<int,int>> scratch;
    return Zelda3D::AppendCmbTextures(cmb, out->texRgba, scratch);
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
    // Mido / Malon / En_Hy townsfolk (keystone #3 extension). Material slots + cmab names dumped per
    // ZAR (tools/face_cmb_dump.py + cmab strt names); cross-checked vs OoT3D draw decomp for the slot.
    { "zelda_md.zar",  { { "mido_eye.cmab", 1 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_ma1.zar", { { "childmalon_eye.cmab", 3 }, { "childmalon_mouth.cmab", 4 }, { nullptr, -1 } } },
    { "zelda_ma2.zar", { { "malon_eye.cmab", 4 }, { "malon_mouth.cmab", 5 }, { nullptr, -1 } } },
    { "zelda_boj.zar", { { "hyliaman1_eye.cmab", 3 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_ahg.zar", { { "hyliaman2_eye.cmab", 3 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_bji.zar", { { "hyliaoldman_eye.cmab", 3 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_aob.zar", { { "hyliawoman1_eye.cmab", 1 }, { nullptr, -1 }, { nullptr, -1 } } },
    { "zelda_bob.zar", { { "hyliawoman3_eye.cmab", 1 }, { nullptr, -1 }, { nullptr, -1 } } },
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
    const Zelda3D::Cmb& cmb = *out->cmb;
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
        const Zelda3D::CmbTexture& bt = cmb.textures()[baseTexIdx];
        const Zelda3D::ZarFile* cf = nullptr;
        for (const auto& f : out->zar->files())
            if (strEndsWith(f.name, fc.cmabSuffix)) { cf = &f; break; }
        if (!cf) { fprintf(stderr, "[Zelda3D] facial %s: cmab '%s' not in zar\n", zarPath.c_str(), fc.cmabSuffix); continue; }
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
            fprintf(stderr, "[Zelda3D] facial %s/%s: bad cmab layout (n=%u dataLen=%u texOff=%u len=%zu)\n",
                    zarPath.c_str(), fc.cmabSuffix, n, dataLen, texDataOff, buf.size());
            continue;
        }
        std::vector<int> frameTex;
        frameTex.reserve(n);
        for (uint32_t f = 0; f < n; f++) {
            std::vector<uint8_t> raw(buf.begin() + texDataOff + (size_t)f * dataLen,
                                     buf.begin() + texDataOff + (size_t)(f + 1) * dataLen);
            std::vector<uint8_t> rgba = Zelda3D::PicaDecode(bt.glFormat(), bt.width, bt.height, raw);
            frameTex.push_back((int)out->texRgba.size());
            out->texRgba.push_back(std::move(rgba));
            dims.push_back({ bt.width, bt.height });
        }
        out->facialFrames[mat] = std::move(frameTex);
        fprintf(stderr, "[Zelda3D] facial %s: loaded %u frames for mat %d from %s\n",
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
// (RmlUi "Stair Step Size" / Zelda3D_SetStairRiserY), so the player can pick larger/smaller
// steps. N = round(rampRiseY / gZelda3dStairRiserY). Default is chunkier than the old 7.8.

// Replace every kaidan ramp group in a freshly-built scene-room model with stepped
// geometry. cGroups must be (re)built AFTER this — it mutates group vert vectors.
static void generateRoomStairs(LoadedModel* out) {
    ensureStairsEnv();
    if (!gZelda3dStairs || !out->cmb) return;
    for (auto& g : out->groups)
        if (texNameIsKaidan(*out->cmb, g.material_index)) generateStairsGroup(g);
}

static void buildFromCmb(LoadedModel* out, bool bakedVertexColor,
                         const std::vector<uint8_t>& skipMesh = {}, bool stairs = false) {
    Zelda3D::Cmb& cmb = *out->cmb;
    out->groups = cmb.buildDrawGroups(skipMesh);
    if (!bakedVertexColor) {
        for (auto& g : out->groups)
            for (auto& v : g.verts) { v.color[0] = v.color[1] = v.color[2] = v.color[3] = 1.0f; }
    }
    if (stairs) generateRoomStairs(out);

    std::vector<std::pair<int,int>> dims;
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
    for (size_t i = 0; i < out->texRgba.size(); i++)
        out->cTexs[i] = { out->texRgba[i].data(), dims[i].first, dims[i].second };

    out->cGroups.reserve(out->groups.size());
    for (const auto& g : out->groups) {
        Zelda3DGlGroup cg = makeCgroup(cmb, g, g.verts.data(), 0);
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
static void buildFromCmbs(LoadedModel* out, std::vector<std::unique_ptr<Zelda3D::Cmb>>& cmbs) {
    struct Src { const Zelda3D::Cmb* cmb; size_t gi; int texBase; };
    std::vector<Src> srcs;
    std::vector<std::pair<int,int>> dims;
    for (auto& up : cmbs) {
        Zelda3D::Cmb& cmb = *up;
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
    Zelda3D::CtrRom* r = rom();
    if (!r) return;
    auto bytes = r->read(path);
    if (bytes.empty()) { fprintf(stderr, "[Zelda3D] zsi not found: %s\n", path.c_str()); return; }
    Zelda3D::Zsi zsi(std::move(bytes));
    if (!zsi.ok()) { fprintf(stderr, "[Zelda3D] Zsi %s: %s\n", path.c_str(), zsi.error().c_str()); return; }
    if (!zsi.hasGeometry()) { fprintf(stderr, "[Zelda3D] no room geometry in %s\n", path.c_str()); return; }
    out->cmb = std::make_unique<Zelda3D::Cmb>(zsi.cmbBytes());
    if (!out->cmb->ok()) { fprintf(stderr, "[Zelda3D] Cmb %s: %s\n", path.c_str(), out->cmb->error().c_str()); return; }
    // scene rooms carry OoT3D baked vertex lighting; #5 turns fake-flat kaidan ramps into real steps
    buildFromCmb(out, /*bakedVertexColor=*/true, /*skipMesh=*/{}, /*stairs=*/true);
    fprintf(stderr, "[Zelda3D] loaded scene-room model %d (%s): %zu groups, %zu textures\n", modelId, path.c_str(),
           out->cGroups.size(), out->cTexs.size());
    // #29 diagnostic: dump per-group material/texture + per-group bbox so the "untextured dome"
    // group can be identified by index (pair with ZELDA3D_SOLOGROUP to isolate it visually).
    if (getenv("ZELDA3D_DBG_ROOM")) {
        const auto& texs = out->cmb->textures();
        for (size_t i = 0; i < out->cGroups.size(); i++) {
            const auto& g = out->groups[i];
            int ti = out->cGroups[i].texIndex;
            const char* tn = (ti >= 0 && ti < (int)texs.size()) ? texs[ti].name.c_str() : "<none/stair>";
            float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
            for (const auto& v : g.verts)
                for (int k = 0; k < 3; k++) { mn[k] = std::min(mn[k], v.pos[k]); mx[k] = std::max(mx[k], v.pos[k]); }
            fprintf(stderr, "[Zelda3D_DBG_ROOM] grp%2zu mat%d tex%d %-18s verts%5zu mesh_id%d "
                   "x[%.0f,%.0f] y[%.0f,%.0f] z[%.0f,%.0f]\n",
                   i, g.material_index, ti, tn, g.verts.size(), g.mesh_id,
                   mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]);
        }
    }
}

// Load an actor model: read its ZAR, find the .cmb, build groups (+ keep the ZAR/CMB
// resident so the animation layer can load CSABs and recompute skin matrices).
static void loadActorModel(int modelId, LoadedModel* out) {
    Zelda3D::CtrRom* r = rom();
    if (!r) return;
    auto zarBytes = r->read(kModels[modelId].zarPath);
    if (zarBytes.empty()) { fprintf(stderr, "[Zelda3D] zar not found: %s\n", kModels[modelId].zarPath); return; }
    out->zar = std::make_unique<Zelda3D::Zar>(std::move(zarBytes));
    if (!out->zar->ok()) { fprintf(stderr, "[Zelda3D] Zar: %s\n", out->zar->error().c_str()); return; }
    const Zelda3D::ZarFile* cmbf = nullptr;
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
    if (!cmbf) { fprintf(stderr, "[Zelda3D] no .cmb in %s\n", kModels[modelId].zarPath); return; }
    out->cmb = std::make_unique<Zelda3D::Cmb>(out->zar->read(*cmbf));
    if (!out->cmb->ok()) { fprintf(stderr, "[Zelda3D] Cmb: %s\n", out->cmb->error().c_str()); return; }
    buildFromCmb(out, /*bakedVertexColor=*/false); // characters/props: dynamic lighting, color attr unused
    appendFacialFrames(out, kModels[modelId].zarPath); // eye/mouth .cmab frames (keystone #3)
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
static float bboxHeight(const std::vector<Zelda3D::CmbDrawGroup>& groups) {
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
static bool isFlatGroups(const std::vector<Zelda3D::CmbDrawGroup>& groups) {
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

static size_t vertCountGroups(const std::vector<Zelda3D::CmbDrawGroup>& groups) {
    size_t n = 0;
    for (const auto& g : groups) n += g.verts.size();
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
static void loadBillboard(LoadedModel* out, const std::string& zarPath, const std::string& ctxbName,
                          bool additive, float u0 = 0.0f, float v0 = 0.0f,
                          float u1 = 1.0f, float v1 = 1.0f) {
    Zelda3D::CtrRom* r = rom();
    if (!r) return;
    auto zarBytes = r->read(zarPath);
    if (zarBytes.empty()) { fprintf(stderr, "[Zelda3D] billboard: zar not found: %s\n", zarPath.c_str()); return; }
    out->zar = std::make_unique<Zelda3D::Zar>(std::move(zarBytes));
    if (!out->zar->ok()) { fprintf(stderr, "[Zelda3D] billboard Zar %s: %s\n", zarPath.c_str(), out->zar->error().c_str()); return; }
    const Zelda3D::ZarFile* zf = nullptr;
    for (const auto& f : out->zar->files())
        if (f.name.find(ctxbName) != std::string::npos) { zf = &f; break; }
    if (!zf) { fprintf(stderr, "[Zelda3D] billboard %s: no '%s'\n", zarPath.c_str(), ctxbName.c_str()); return; }
    Zelda3D::Ctxb ctxb(out->zar->read(*zf));
    if (!ctxb.ok() || ctxb.textures().empty()) {
        fprintf(stderr, "[Zelda3D] billboard ctxb %s: %s\n", ctxbName.c_str(), ctxb.error().c_str());
        return;
    }
    int tw = 0, th = 0;
    auto rgba = ctxb.decodeRGBA(0, &tw, &th);
    if (rgba.empty()) { fprintf(stderr, "[Zelda3D] billboard %s: decode failed\n", ctxbName.c_str()); return; }
    out->texRgba.push_back(std::move(rgba));
    out->cTexs.push_back({ out->texRgba[0].data(), tw, th });

    // One quad (two triangles) in the XY plane, matching the N64 sun/moon billboard vertices.
    // weights[0]=1, boneIds[0]=0 so with identity uBones the GPU-skin pass is a no-op (pos == model
    // pos) — a non-skinned sprite. Per-vertex colour white; the draw's tint/alpha multiplies it.
    auto mkv = [](float x, float y, float u, float v) {
        Zelda3D::CmbVertex vtx{};
        vtx.pos[0] = x; vtx.pos[1] = y; vtx.pos[2] = 0.0f;
        vtx.nrm[2] = 1.0f;
        vtx.uv[0] = u; vtx.uv[1] = v;
        vtx.weights[0] = 1.0f;
        vtx.color[0] = vtx.color[1] = vtx.color[2] = vtx.color[3] = 1.0f;
        return vtx;
    };
    Zelda3D::CmbVertex bl = mkv(-31, -31, u0, v0), br = mkv(32, -31, u1, v0),
                       tl = mkv(-31,  32, u0, v1), tr = mkv(32,  32, u1, v1);
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
    fprintf(stderr, "[Zelda3D] billboard %s|%s%s: %dx%d tex\n", zarPath.c_str(), ctxbName.c_str(),
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
    // Optional trailing "#u0,v0,u1,v1" clamps the sampled texture region to that UV subrect —
    // e.g. "#0,0.4,0.5,1" samples only the upper-left quadrant. Used for the fine_lensflare
    // atlas: its rainbow-ring halo lives in the upper-left quadrant of the texture, and the
    // sun-flare orbs live in the right half. To render just the halo behind the moon we
    // sample only the ring region.
    {
        bool add = false;
        const char* pfx = nullptr;
        if (key.rfind("BILLBOARDADD:", 0) == 0) { add = true; pfx = "BILLBOARDADD:"; }
        else if (key.rfind("BILLBOARD:", 0) == 0) { pfx = "BILLBOARD:"; }
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
                    u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
                }
            }
            loadBillboard(out, zp, ctxb, add, u0, v0, u1, v1);
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
    if (!r) return;
    auto zarBytes = r->read(zarPath);
    if (zarBytes.empty()) { fprintf(stderr, "[Zelda3D] auto: zar not found: %s\n", zarPath.c_str()); return; }
    out->zar = std::make_unique<Zelda3D::Zar>(std::move(zarBytes));
    if (!out->zar->ok()) { fprintf(stderr, "[Zelda3D] auto Zar %s: %s\n", zarPath.c_str(), out->zar->error().c_str()); return; }

    // Forced-CMB selection: load exactly the named CMB (first match) and skip the heuristic.
    if (!forcedCmb.empty()) {
        for (const auto& f : out->zar->files()) {
            if (f.name.size() < 4 || f.name.compare(f.name.size() - 4, 4, ".cmb") != 0) continue;
            if (f.name.find(forcedCmb) == std::string::npos) continue;
            auto cmb = std::make_unique<Zelda3D::Cmb>(out->zar->read(f));
            if (!cmb->ok()) { fprintf(stderr, "[Zelda3D] auto forced-cmb %s '%s': %s\n", zarPath.c_str(), f.name.c_str(), cmb->error().c_str()); return; }
            out->cmb = std::move(cmb);
            out->skinned = out->cmb->bones().size() > 1;
            buildFromCmb(out, /*bakedVertexColor=*/sky);
            if (sky) {
                for (auto& grp : out->cGroups) grp.depthWrite = 0; // never occlude the world
            }
            fprintf(stderr, "[Zelda3D] auto-loaded model %d (%s | %s)%s: cmb '%s', height=%.1f, %zu groups, %zu textures\n",
                   modelId, zarPath.c_str(), forcedCmb.c_str(), sky ? " [sky]" : "", f.name.c_str(),
                   bboxHeight(out->groups), out->cGroups.size(), out->cTexs.size());
            return;
        }
        fprintf(stderr, "[Zelda3D] auto forced-cmb %s: no cmb matches '%s' -> heuristic pick\n", zarPath.c_str(), forcedCmb.c_str());
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
        std::vector<std::unique_ptr<Zelda3D::Cmb>> cmbs;
        for (const auto& want : asmSpec->cmbNames) {
            // Each substring merges EVERY matching .cmb (in archive order), so one prefix can
            // pull a whole subassembly (e.g. "kanban_L_" = all 4 left board segments).
            int matched = 0;
            for (const auto& zf : out->zar->files()) {
                if (zf.name.size() < 4 || zf.name.compare(zf.name.size() - 4, 4, ".cmb") != 0) continue;
                if (zf.name.find(want) == std::string::npos) continue;
                auto c = std::make_unique<Zelda3D::Cmb>(out->zar->read(zf));
                if (!c->ok()) { fprintf(stderr, "[Zelda3D] assembly %s: '%s': %s\n", zarPath.c_str(), zf.name.c_str(), c->error().c_str()); continue; }
                cmbs.push_back(std::move(c));
                matched++;
            }
            if (!matched) fprintf(stderr, "[Zelda3D] assembly %s: no cmb matches '%s'\n", zarPath.c_str(), want.c_str());
        }
        if (!cmbs.empty()) {
            size_t nMerged = cmbs.size();
            out->skinned = false; // hand-listed assemblies are static props (no skinning)
            buildFromCmbs(out, cmbs);
            out->cmb = std::move(cmbs[0]); // keep a resident CMB (the main part)
            fprintf(stderr, "[Zelda3D] auto-loaded ASSEMBLY model %d (%s): %zu cmbs merged, height=%.1f, %zu groups, %zu textures\n",
                   modelId, zarPath.c_str(), nMerged, bboxHeight(out->groups), out->cGroups.size(), out->cTexs.size());
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
        if (f.name.size() < 4 || f.name.compare(f.name.size() - 4, 4, ".cmb") != 0) continue;
        nCmb++;
        if (isDebrisCmbName(f.name)) continue;
        auto cmb = std::make_unique<Zelda3D::Cmb>(out->zar->read(f));
        if (!cmb->ok()) continue;
        auto groups = cmb->buildDrawGroups();
        float d = bboxDiag(groups);
        if (d > fbDiag) { fbDiag = d; fbFile = &f; fbCmb = std::make_unique<Zelda3D::Cmb>(out->zar->read(f)); }
        if (isFlatGroups(groups)) continue; // billboard/sprite/decal -> not the main mesh
        size_t nv = vertCountGroups(groups);
        if (nv > bestVerts) { bestVerts = nv; best = &f; bestCmb = std::move(cmb); }
    }
    if (!bestCmb) { best = fbFile; bestCmb = std::move(fbCmb); } // all flat? take largest non-debris
    // Last resort: if every CMB looked like debris (or none parsed), take the first .cmb.
    if (!bestCmb) {
        const Zelda3D::ZarFile* f = out->zar->firstWithSuffix(".cmb");
        if (f) { bestCmb = std::make_unique<Zelda3D::Cmb>(out->zar->read(*f)); best = f; }
    }
    if (!bestCmb || !bestCmb->ok()) { fprintf(stderr, "[Zelda3D] auto: no usable .cmb in %s\n", zarPath.c_str()); return; }
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
    appendFacialFrames(out, zarPath); // eye/mouth .cmab frames (keystone #3)
    // Note: `skinned` on its own doesn't mean the model is skipped — with the
    // default N64ANIM path (ZELDA3D_N64ANIM=1, gZelda3dAnimLive=1) skinned
    // characters render as their OoT3D model driven by the live N64
    // SkelAnime joint table (see Zelda3D_AutoModelSkinned callers). The
    // old " (skinned->skip)" tag was misleading; state=3 (real skip) only
    // fires when the retarget path is unavailable.
    fprintf(stderr, "[Zelda3D] auto-loaded model %d (%s): cmb '%s' of %d, height=%.1f, bones=%zu%s, %zu groups, %zu textures\n",
           modelId, zarPath.c_str(), best ? best->name.c_str() : "?", nCmb, bboxHeight(out->groups),
           out->cmb->bones().size(),
           out->skinned ? " (skinned=SkelAnime retarget)" : "",
           out->cGroups.size(), out->cTexs.size());
}

} // end anonymous namespace — loadModel needs EXTERNAL linkage (declared in zelda3d_model_internal.h
  // so zelda3d_anim.cpp can resolve a model). It still sees the internal-linkage loaders above
  // (anonymous-namespace members are visible throughout this TU).

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

namespace { // resume internal-linkage model-core helpers

// Renderer provider: hand back the CPU data for a model id (loads lazily).
int provider(int modelId, const Zelda3DGlGroup** groups, int* groupCount, const Zelda3DGlTex** texs, int* texCount) {
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
// relief is preserved. Mirrors tools/zelda3d_warp.py (the offline-verified oracle). ---

constexpr float kWarpStep = 100.0f;   // grid spacing (world units)
constexpr float kWarpReject = 120.0f; // |D| above this = structure, not ground -> hole-fill
constexpr float kNoFloor = -31000.0f; // floorFn returns <= this when there is no floor

// Topmost-or-nearest upward-facing (floor) triangle Y at (x,z) over a room's draw groups;
// returns false if no floor covers the point. If hasTarget, picks the floor hit closest
// to target (isolates the same surface across datasets, avoiding roof-vs-ground mixups).
static bool meshFloor(const std::vector<Zelda3D::CmbDrawGroup>& groups, float x, float z, bool hasTarget,
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
// they stand on the visible OoT3D ground) via Zelda3D_RoomGroundDeltaAt. This is the inverse of
// the old render-mesh warp, which had to smooth D and so smeared corrections across N64
// collision steps (ledges), wrongly lifting already-correct ground and floating fences/posts.
static void computeRoomGroundDelta(LoadedModel* lm, Zelda3D_FloorFn floorFn) {
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
    fprintf(stderr, "[Zelda3D] ground-delta field: %dx%d grid, %d ground cells (actors offset to OoT3D ground)\n",
            nx, nz, nValid);
}

} // namespace

extern "C" {

// Register the renderer's model provider once. Safe to call repeatedly.
void Zelda3D_EnsureModelProvider(void) {
    if (!g_registered) {
        Zelda3D_GL_SetModelProvider(provider);
        g_registered = true;
    }
}

float Zelda3D_ModelScaleById(int modelId) {
    if (modelId < 0 || modelId >= (int)(sizeof(kModels) / sizeof(kModels[0]))) return 1.0f;
    return kModels[modelId].worldScale;
}

// Get-or-allocate a stable model id for a scene room, keyed by its ZSI path
// (/scene/<name>_<R>_info.zsi). The geometry loads lazily on first draw via the
// provider. Returns -1 if sceneName is null/empty. The game calls this from its
// room-draw hook with the OoT3D scene name (kZelda3dSceneNames) + room number.
int Zelda3D_RoomModelId(const char* sceneName, int roomNum) {
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
void Zelda3D_SetStairs(int on) {
    gZelda3dStairs = on ? 1 : 0;
    for (auto it = g_loaded.begin(); it != g_loaded.end();) {
        if (it->first >= kSceneModelBase && it->first < kAutoModelBase) it = g_loaded.erase(it);
        else ++it;
    }
}
int Zelda3D_GetStairs(void) { return gZelda3dStairs; }

// #5 — set the generated step rise (world-units/step). Larger = bigger steps. Drops the cached
// scene-room CPU models so the provider rebuilds their stair geometry with the new rise, and asks
// the GL layer to evict the matching uploads so the change shows live (next render pass). Collision
// keeps the previous rise until the next scene load (render is what the user is tuning here).
void Zelda3D_SetStairRiserY(float v) {
    if (v < 1.0f) v = 1.0f;
    if (v == gZelda3dStairRiserY) return;
    gZelda3dStairRiserY = v;
    for (auto it = g_loaded.begin(); it != g_loaded.end();) {
        if (it->first >= kSceneModelBase && it->first < kAutoModelBase) it = g_loaded.erase(it);
        else ++it;
    }
    Zelda3D_GL_RequestEvictRange(kSceneModelBase, kAutoModelBase);
}
float Zelda3D_GetStairRiserY(void) { return gZelda3dStairRiserY; }


// Get-or-allocate a stable model id for an auto-replaced actor model, keyed by its ZAR
// path (e.g. "/actor/zelda_box.zar"). The geometry loads lazily on first draw via the
// provider. Returns -1 if zarPath is null/empty. The game calls this from the ZELDA3D_AUTO
// actor path with the ZAR resolved from the actor's object id (kZelda3dObjectZars).
int Zelda3D_AutoModelId(const char* zarPath) {
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
float Zelda3D_AutoModelHeight(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return 0.0f;
    return bboxHeight(lm->groups);
}

// Bind-pose local-space minimum Y of a model (its lowest vertex, i.e. the feet). The N64-anim
// auto path uses groundOffset = -minY so the model's feet land on the actor's world Y (ground)
// after scaling. Returns 0 if no geometry.
float Zelda3D_AutoModelMinY(int modelId) {
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
int Zelda3D_AutoModelExtentXZ(int modelId, float* outX, float* outZ) {
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
int Zelda3D_AutoModelSkinned(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return 1;
    return lm->skinned ? 1 : 0;
}

// The ZAR path an auto model was allocated from (e.g. "/actor/zelda_kw1.zar"), or NULL. Lets the
// actor draw path identify WHICH model is loaded by archive name (stable), since the numeric model
// id is allocation-order dependent. Used to pick a shared-CMB variant subset (e.g. En_Ko Kokiri
// kids: kokiripeople/kokirimaster bake multiple head variants on distinct mesh_ids).
const char* Zelda3D_AutoModelZar(int modelId) {
    int idx = modelId - kAutoModelBase;
    if (idx < 0 || idx >= (int)g_autoModelPaths.size()) return nullptr;
    return g_autoModelPaths[idx].c_str();
}

// Number of bones in a loaded model's OoT3D skeleton (0 if none/failed). The N64-anim retarget
// maps N64 jointTable[i+1] -> OoT3D bone i, so a correct retarget needs the OoT3D bone count to
// match the actor's N64 limb count; the auto path uses this to refuse mismatched rigs (which
// would pose giant/malformed) and fall back to N64.
int Zelda3D_AutoModelBoneCount(int modelId) {
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok || !lm->cmb) return 0;
    return (int)lm->cmb->bones().size();
}

// Facial material-anim (keystone #3): the GL texture index of the eye/mouth material's frame-N
// sprite (decoded from the sibling .cmab at load; see appendFacialFrames / kFacialAssets). Returns
// -1 if this model has no facial frames for `materialIndex` or `frame` is out of range. The override
// driver (zelda3d_anim_override.cpp) reads the live N64 eye/mouth index and binds this via
// Zelda3D_GL_SetMatTexOverride. Returns the frame count for this material when frame < 0 (query).
int Zelda3D_FacialFrameTex(int modelId, int materialIndex, int frame) {
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
float Zelda3D_AutoModelBoneLenSum(int modelId, int boneCap) {
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
const char* Zelda3D_AutoModelDefaultAnim(int modelId) {
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
        fprintf(stderr, "[Zelda3D] model %d default anim = '%s'\n", modelId,
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
extern "C" int Zelda3D_AutoModelHasCsab(int modelId, const char* base) {
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
extern "C" void Zelda3D_AutoModelCsabList(int modelId, char* out, int outsz) {
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
        if (cmb) { Zelda3D::Csab c(lm->zar->read(f)); if (c.ok()) dur = (int)c.duration(); }
        int w = snprintf(out + pos, (size_t)(outsz - pos), "%s%s(%d)", pos ? " " : "", base.c_str(), dur);
        if (w <= 0 || w >= outsz - pos) break;
        pos += w;
    }
}

// ORACLE DUMP (gated by the caller): print the OoT3D model's skeleton — per-bone rest
// translation/rotation/scale + parent + world-space rest position (FK), plus mesh height and
// the world rest extent (bone span). Used offline to design the programmatic N64<->OoT3D scale
// and bone-correspondence (see PROGRESS "replace ALL characters"). stderr, parseable.
void Zelda3D_DumpModelBones(int modelId) {
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
int Zelda3D_RoomMeshFloorAt(int modelId, float x, float z, float* outY) {
    if (modelId < kSceneModelBase) return 0;
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return 0;
    return meshFloor(lm->groups, x, z, false, 0.0f, outY) ? 1 : 0;
}

// OoT3D render-mesh floor Y at (x,z) for a scene room, picking the floor hit CLOSEST to
// `target` (the actor's N64 floor, so multi-level spots pick the right surface). Returns 1 +
// *outY on a hit. Used to ground actors exactly on the visible OoT3D ground (per-actor, so
// meshFloor's XZ-bbox reject keeps it cheap). Exact — no grid approximation.
int Zelda3D_RoomOoT3DFloorAt(int modelId, float x, float z, float target, float* outY) {
    if (modelId < kSceneModelBase) return 0;
    LoadedModel* lm = loadModel(modelId);
    if (!lm || !lm->ok) return 0;
    return meshFloor(lm->groups, x, z, /*hasTarget=*/true, target, outY) ? 1 : 0;
}

// Compute & cache a scene-room model's ground-delta field (N64 - OoT3D per XZ), once.
// `floorFn` raycasts the N64 collision (provided by zelda3d.c, which has the PlayState). The
// render mesh is NOT modified; actors are offset by -D via Zelda3D_RoomGroundDeltaAt. Call
// before the room is first drawn.
void Zelda3D_ComputeRoomGroundDelta(int modelId, Zelda3D_FloorFn floorFn) {
    if (modelId < kSceneModelBase) return; // scene rooms only
    LoadedModel* lm = loadModel(modelId);
    if (lm && lm->ok) computeRoomGroundDelta(lm, floorFn);
}

// Sample the cached ground-delta field: *outD = N64_floor - OoT3D_floor at world (x,z) for a
// scene room (bilinear). Returns 1 on success, 0 if the model isn't a scene room or the field
// isn't ready. Actors add -(*outD) to their render Y to stand on the visible OoT3D ground.
int Zelda3D_RoomGroundDeltaAt(int modelId, float x, float z, float* outD) {
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


#include "zelda3d_collision.h"

extern "C" int Zelda3D_LoadSceneCollisionRaw(const char* sceneName, Zelda3D_RawCollision* out) {
    if (!sceneName || !*sceneName || !out) return 0;
    memset(out, 0, sizeof(*out));
    Zelda3D::CtrRom* r = rom();
    if (!r) return 0;
    std::string path = "/scene/" + std::string(sceneName) + "_info.zsi";
    auto bytes = r->read(path);
    if (bytes.empty()) { fprintf(stderr, "[Zelda3D] collision zsi not found: %s\n", path.c_str()); return 0; }
    Zelda3D::OoT3DCollision col(bytes);
    if (!col.ok()) { fprintf(stderr, "[Zelda3D] collision %s: %s\n", path.c_str(), col.error().c_str()); return 0; }
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
        Zelda3D_FreeRawCollision(out);
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
    fprintf(stderr, "[Zelda3D] loaded scene collision %s: %d verts, %d polys, %d surface types\n",
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
// gZelda3dSceneScale=1/off=0), matching the collision frame. Returns 1 with malloc'd arrays (0
// verts/tris when the scene has no kaidan stairs); free with Zelda3D_FreeStairTreads.
extern "C" int Zelda3D_CollectSceneStairTreads(const char* sceneName,
                                             float** outVerts, int* outNVerts,
                                             int** outTris, int* outNTris) {
    if (outVerts) *outVerts = nullptr;
    if (outTris) *outTris = nullptr;
    if (outNVerts) *outNVerts = 0;
    if (outNTris) *outNTris = 0;
    if (!sceneName || !*sceneName || !outVerts || !outTris || !outNVerts || !outNTris) return 0;
    ensureStairsEnv();
    if (!gZelda3dStairs) return 1; // stairs disabled -> empty (smooth ramp collision)
    Zelda3D::CtrRom* r = rom();
    if (!r) return 0;

    std::vector<float> verts; // 3 floats per vertex (world x,y,z)
    std::vector<int> tris;    // 3 vertex indices per triangle

    for (int room = 0; room < 64; room++) {
        std::string path = "/scene/" + std::string(sceneName) + "_" + std::to_string(room) + "_info.zsi";
        auto bytes = r->read(path);
        if (bytes.empty()) break; // rooms are contiguous 0..n-1
        Zelda3D::Zsi zsi(std::move(bytes));
        if (!zsi.ok() || !zsi.hasGeometry()) continue;
        Zelda3D::Cmb cmb(zsi.cmbBytes());
        if (!cmb.ok()) continue;
        std::vector<Zelda3D::CmbDrawGroup> groups = cmb.buildDrawGroups({});
        for (const Zelda3D::CmbDrawGroup& g : groups) {
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

extern "C" void Zelda3D_FreeStairTreads(float* verts, int* tris) {
    free(verts);
    free(tris);
}

extern "C" void Zelda3D_FreeRawCollision(Zelda3D_RawCollision* out) {
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
// (generic libultraship plumbing, not Zelda3D-specific). Returns 1 if the deck consumed the event,
// 0 if not, -1 if no control deck. Used by the REPL `key` command.
extern "C" int Zelda3D_InjectKey(int scancode, int down) {
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
