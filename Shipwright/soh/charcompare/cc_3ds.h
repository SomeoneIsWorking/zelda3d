// charcompare — 3DS (OoT3D) viewport.
//
// Loads an OoT3D character from the decrypted .3ds (ZELDA3D_3DS_ROM) via the
// self-contained zelda3d asset/ parsers, registers it with the existing zelda3d
// model bridge (zelda3d_model.cpp) + direct-GL renderer (Zelda3D_GL_*), and emits a
// Fast3D display list that draws it through the engine's OTR_G_ZELDA3D_DRAW /
// _RENDERPASS opcodes — exactly the in-game path, so the pixels match the game.
#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <fast/types.h>            // MtxF, Mtx
#include <libultraship/libultra/gbi.h> // Gfx, Vp

namespace cc {

// One loaded 3DS character: its model id (in the zelda3d bridge), bind-pose bbox
// (for auto-fit framing), and the list of animation base names found in its ZAR.
struct Model3ds {
    bool ok = false;
    std::string zarPath;
    int modelId = -1;
    float bboxLo[3] = { 0, 0, 0 };
    float bboxHi[3] = { 0, 0, 0 };
    std::vector<std::string> anims; // CSAB base names (no "Anim/" / ".csab")
    std::unordered_map<std::string, int> animLen; // CSAB base name -> frame count (Csab::duration)
    std::string error;
};

// Frame count of a CSAB (by base name) for this model, or 0 if unknown. Used to bound the GUI
// frame slider / wrap playback to the animation's true length.
int AnimLength(const Model3ds& m, const std::string& csabBase);

// Open the ROM once (ZELDA3D_3DS_ROM). Returns false (and sets a message in `err`)
// if the env var is unset or the image can't be parsed. Safe to call repeatedly.
bool InitRom(std::string& err);

// Load a character by ZAR path (e.g. "/actor/zelda_ge1.zar"). Computes the bind-pose
// bbox and enumerates animations, and registers the model with the zelda3d bridge so the
// renderer can draw it. Re-loading the same path returns the cached entry.
Model3ds Load(const std::string& zarPath);

// Set the model's animated pose for this frame (CSAB base name + frame). Empty
// animName resets to the bind pose. Call once per frame before EmitDlist.
void SetAnim(const Model3ds& m, const std::string& animName, float frame);

// Append the draw commands for this model to `dl` (and any matrix entries to `mtx`).
// The model is auto-fit into the viewport's NDC and oriented by (rx,ry,rz) degrees.
// `keys` holds the projection+modelview MtxF storage referenced by the dlist; it must
// outlive the interp->Run call (one entry is appended per EmitDlist call).
// vp likewise must outlive Run (the gsSPViewport command points at it).
struct DlistKeys {
    std::vector<std::unique_ptr<Mtx>> mtxStore; // stable addresses used as map keys
    std::vector<std::unique_ptr<Vp>> vpStore;
    // Stable raw buffers (e.g. the N64 flex-skeleton per-limb fixed-point Mtx array bound to
    // segment 0x0D); the dlist references these by address so they must outlive interp->Run.
    std::vector<std::unique_ptr<std::vector<int32_t>>> blobs;
    // Stable light blocks (N64 F3D lighting: gSPLight points at these by address).
    std::vector<std::unique_ptr<Lights1>> lightStore;
};
// Target sub-rectangle of the screen (native N64 px, 0..SCREEN_WIDTH/HEIGHT) a model is
// drawn into, for the side-by-side split. Full screen = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT }.
// The framing compensates X for the rect's aspect so a half-width rect isn't squashed.
struct Rect {
    int x0, y0, w, h;
};

void EmitDlist(const Model3ds& m, std::vector<Gfx>& dl, std::unordered_map<Mtx*, MtxF>& mtx, DlistKeys& keys,
               float rx, float ry, float rz, const Rect& vp);

} // namespace cc
