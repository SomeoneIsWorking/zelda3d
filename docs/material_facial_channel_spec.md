# Implementation spec: per-limb material/facial + mesh-show/hide override channel (keystone #3 / P0)

READ-ONLY research spec, 2026-06-22. Closes the last gap in the CSAB auto-draw framework
(`docs/skeletal_parity_backlog.md` P0): **frozen faces** (no eye-blink / mouth) on every NPC, and
**missing held items** (Saria's ocarina). Ground truth: `oot3d-decomp/docs/enko_override_and_ensa_facial.md`.

## TL;DR recommended approach

The OoT3D mechanism is two distinct things, and SoH3D already has the infrastructure for BOTH:

1. **Facial = a per-material texture-index swap.** OoT3D's "material-animation FRAME INDEX" (eye slot /
   mouth slot) is, mechanically, *which texture a single eye/mouth material samples this frame*. The
   alternate eye/mouth sprites are separate textures bundled in the same CMB `tex ` chunk
   (`c_eye`/`*_eye01`/`c_mouth` naming, confirmed in `link_mesh_id_map.md`). The N64 actors animate
   this with `gSPSegment(0x08/0x09/0x0A)`; OoT3D animates it with a CMB mat-anim frame index. SoH3D
   currently binds `cg.texIndex` **fixed at upload time** (`makeCgroup`, `cmb.materialTexture()`).
   → **Add a per-draw, per-material texture-index OVERRIDE channel** (analogous to `SoH3D_SetBonePostRot`):
   a small per-model `{materialIndex → texIndex}` map, snapshotted at emit like `midMask`, applied in
   the GL group loop where `texIndex` is bound.

2. **Mesh show/hide (ocarina, blink-overlay mesh) = the EXISTING `SoH3D_GL_SetMidMask`.** OoT3D's
   "mesh visibility toggle" (`FUN_0037266c` show / `FUN_0036932c` hide, on mesh indices) is byte-for-byte
   the same idea as the `mesh_id` visibility mask SoH3D already uses for En_Ko head variants and Link
   equipment. The ocarina is NOT a DL/segment swap — it's mesh-visibility on a CMB mesh index. So the
   ocarina and the En_Sa blink-overlay mesh need **no new mechanism** — just the right mesh_id bits set
   per frame from actor state, the same way `SoH3D_EnKoMidMask` already does.

So the only genuinely new code is the **per-material texture-index override channel** for facial; mesh
show/hide reuses `SoH3D_GL_SetMidMask`. Both are driven from a new generic table keyed by ZAR (parallel
to `kTrackActors`), filled in from `SoH3D_ApplyActorOverrides`.

---

## Why facial cannot reuse mesh_id, and why CMB material-anim is NOT parsed

- **CMB/CSAB carry NO material/texture/UV/color animation.** `Cmb::parseMats` (`asset/cmb.cpp:123`)
  reads only *static* material state and exactly ONE texture binding per material
  (`m.tex0_idx`, `materialTexture()` returns it). `Csab` is purely skeletal (TRS tracks per bone).
  The OoT3D `.cmab` (material-anim binary) is a *separate* file we do not parse, and the runtime
  mat-anim state lives in the actor struct (`matAnim` base, En_Sa `+0x228`), not the CMB. So there is
  no "frame N of this material" already in the parsed model — we must reproduce the *effect* (bind a
  different texture for that material) ourselves.
- **The alternate eye/mouth frames are real textures in the CMB.** A face CMB's `tex ` chunk holds the
  open/half/closed eye sprites and the mouth-shape sprites as distinct `CmbTexture`s. Mat-anim frame
  index → texture index is a small per-ZAR mapping (see "Unknowns"). The eye/mouth material's *static*
  binding (`tex0_idx`) is the index-0 (default/open) frame; the override redirects it to frame N.
- **Mesh_id can't express facial** because the eye/mouth are ONE material on ONE mesh whose *texture*
  changes; they are not separate meshes you toggle. (En_Sa's blink-CLOSE overlay IS a separate mesh —
  that part is mesh_id. The open/half/closed eye SPRITE is texture-swap.) Two mechanisms, by design.

---

## Part A — New per-material texture-index override channel (the facial swap)

### A1. GL backend: `Shipwright/libultraship/src/fast/soh3d_gl.cpp` (+ `.../include/fast/soh3d_gl.h`)

Mirror the `midMask` plumbing exactly — it is the proven template for "per-emit, deferred, per-model
draw state".

1. **`GlModel`** (`soh3d_gl.cpp:53`): add
   ```cpp
   // Per-frame material→texIndex override (facial eye/mouth swap), set via SoH3D_GL_SetMatTexOverride
   // before EmitPose; snapshotted into ItemPose so it survives the deferred render. Empty = none.
   std::unordered_map<int,int> pendingMatTex; // materialIndex -> texIndex
   ```
   NB groups don't currently store their `materialIndex` in `GlGroup` (only `meshId`/`texIndex`). Add
   `int materialIndex = -1;` to `GlGroup` (`soh3d_gl.cpp:33`) AND to `SoH3DGlGroup`
   (`soh3d_gl.h:31`), and set it in `uploadModel` (`soh3d_gl.cpp:615`) from `groups[i].materialIndex`,
   and in `makeCgroup` (`soh3d_model.cpp:200`) from `g.material_index`. This is the key the override map
   uses. (Alternatively key the override by `mesh_id`, but material is the OoT3D-faithful key — the
   mat-anim slot is a material slot — and one eye material can span meshes.)

2. **`ItemPose`** (`soh3d_gl.cpp:79`): add `std::unordered_map<int,int> matTex;` alongside `midMask`.

3. **New entry point** (next to `SoH3D_GL_SetMidMask`, `soh3d_gl.cpp:590`):
   ```cpp
   // Set a per-material texture-index override for this model (facial eye/mouth frame). Call before
   // EmitPose, like SetMidMask. texIndex<0 clears that material's override. Empty map = no override.
   extern "C" void SoH3D_GL_SetMatTexOverride(int modelId, int materialIndex, int texIndex);
   extern "C" void SoH3D_GL_ClearMatTexOverrides(int modelId);
   ```
   `SetMatTexOverride` writes `g_models[modelId].pendingMatTex[materialIndex] = texIndex;`
   `Clear` does `.clear()`. Declare both in `soh3d_gl.h`.

4. **`SoH3D_GL_EmitPose`** (`soh3d_gl.cpp:594`): snapshot it —
   `p.matTex = it->second.pendingMatTex;` (right next to `p.midMask = ...`).

5. **Carry into the deferred `DrawItem`.** `midMask` rides on `DrawItem` (`it.midMask`, set at
   `soh3d_gl.cpp:1358` from the paired pose). Add `std::unordered_map<int,int> matTex;` to the DrawItem
   struct (near `soh3d_gl.cpp:953`) and copy it the same place `it.midMask` is copied from the pose
   (`cit->second[k].midMask`, ~line 1358 GL and the Vulkan poseOf path).

6. **Apply in the group draw loop** (`drawOne`, `soh3d_gl.cpp:927`): change the texture-bind line so a
   material override wins. `drawOne` needs the per-item override passed in (add a
   `const std::unordered_map<int,int>* matTex` param, defaulting null; pass `&it.matTex`):
   ```cpp
   int texIndex = grp.texIndex;
   if (matTex && grp.materialIndex >= 0) {
       auto ov = matTex->find(grp.materialIndex);
       if (ov != matTex->end() && ov->second >= 0) texIndex = ov->second;
   }
   if (texIndex >= 0 && texIndex < (int)m.textures.size()) { glBindTexture(... m.textures[texIndex]); ... }
   ```
   This is the WHOLE GL-side cost: it swaps which already-uploaded CMB texture the eye/mouth group
   samples this frame. No re-upload, no extra VBO.

   IMPORTANT — the **Vulkan** backend path must mirror this (the codebase ships Vulkan as default,
   per memory). Find the Vulkan per-group draw analogue to `drawOne` (`SoH3D_Vk_*Draw`, referenced
   `soh3d_gl.cpp:1430/1445/1456`) and apply the same `matTex` override there, plus thread the map
   through `poseOf(it)`. If the Vulkan group draw lives in a `.cpp` under `src/fast/`, grep for the
   `grp.texIndex`/texture-bind equivalent and patch identically. (Flag: not yet read in this pass —
   confirm the Vulkan group-draw call site before implementing.)

### A2. Model layer: `Shipwright/soh/src/soh3d/soh3d_model.cpp`

- In `makeCgroup` (line 200) set `cg.materialIndex = g.material_index;` (new field).
- Add a helper to resolve eye/mouth material indices + frame→texture mapping for a model. The cleanest
  place is a new small function that, given a `LoadedModel`, returns the material index whose primary
  texture name matches an eye/mouth pattern:
  ```cpp
  // Returns the material index of the first material whose tex0 texture name contains `needle`
  // (e.g. "eye", "mouth"/"kuti"), or -1. Used by the facial channel to find the eye/mouth slot.
  extern "C" int SoH3D_FindMaterialByTexName(int modelId, const char* needle);
  // Returns the texture index for `needle` + frame N (the Nth texture whose name matches the eye/mouth
  // family), or -1. The mat-anim "frame index" selects among the bundled eye/mouth sprites.
  extern "C" int SoH3D_FindFaceTexFrame(int modelId, const char* needle, int frame);
  ```
  These walk `lm->cmb->materials()` / `lm->cmb->textures()` (both already exposed on `Cmb`). Texture
  name is `CmbTexture::name`; material's tex is `cmb.materialTexture(matIdx)`.
  - **Open question to resolve at impl time (see Unknowns):** whether the alternate eye frames are
    distinct *textures* (name-suffix `eye00/eye01/...`) — the expected case — or distinct *materials*,
    or sub-rects of one texture (UV-based mat-anim). For the common OoT3D NPC face the frames are
    distinct textures; build the name→frame table from the live CMB dump (`tools/link_cmb_dump.py`
    already dumps per-mesh material/texture, extend it to list all `*eye*`/`*mouth*` texture names per
    face CMB for En_Ko/En_Sa so the frame order can be confirmed, not guessed).

### A3. Driver: `Shipwright/soh/src/soh3d/soh3d_anim_override.{h,cpp}`

Extend the existing override framework — it is already the per-draw, ZAR-keyed apply point.

- Add a facial table parallel to `kTrackActors`:
  ```cpp
  struct FacialActor {
      const char* zar;
      int eyeIdxOff;    // byte offset of the live eye-index s16 in the N64 actor struct (-1 = none)
      int mouthIdxOff;  // byte offset of the live mouth-index s16 (-1 = none)
      const char* eyeTexNeedle;   // e.g. "eye"
      const char* mouthTexNeedle; // e.g. "mouth"/"kuti" (-1/null = no mouth, e.g. En_Ko)
      const int* mouthRemap; int mouthRemapLen; // OoT3D mouth-frame remap, En_Sa = {0,3,4,1,2}
  };
  ```
- In `SoH3D_ApplyActorOverrides` (after the track rows), look up the facial row, read the live
  eye/mouth indices from the N64 actor (SoH3D runs N64 logic, so `rightEyeIndex`/`mouthIndex` are
  already computed — same as the track-row reads), map index→texture via `SoH3D_FindFaceTexFrame`, and
  call `SoH3D_GL_SetMatTexOverride(modelId, eyeMat, eyeTex)` / mouth. Clear overrides at the top
  (add `SoH3D_GL_ClearMatTexOverrides(modelId);` next to `SoH3D_ClearBonePostRots`).
  - **Mouth remap:** En_Sa remaps N64 mouthIndex {0,1,2,3,4} → frame {0,3,4,1,2}
    (`DAT_001b943c`). Eye index is used directly (no remap). Apply the remap from the table.
  - These offsets are **N64** struct offsets (consistent with `kTrackActors` reading N64 structs).
    The N64 eye/mouth state lives in the actor's `SkelAnime`/actor-specific fields (En_Sa N64
    `eyeIndex`/`mouthIndex`; En_Ko `eyeTexIndex`). Derive from SoH's N64 `z_en_sa.c`/`z_en_ko.c`,
    NOT the OoT3D offsets (+0x480/+0x482 are OoT3D-native; do not use here — same rule as the
    interactInfo +0x1E8 vs +0x450 note already in `soh3d_anim_override.cpp:41-46`).

- Gate behind a feature flag like `gSoH3dTrack` (e.g. `gSoH3dFacial`, env `SOH3D_FACIAL`, REPL
  `facial 0|1`) so it can be A/B'd live, mirroring the track gate.

---

## Part B — Mesh show/hide (ocarina + blink-overlay) via the EXISTING mid-mask

No new mechanism. `SoH3D_GL_SetMidMask(modelId, mask)` already culls groups whose `mesh_id` bit is
clear (`soh3d_gl.cpp:896`, `drawOne`). En_Ko already drives it per-actor via `SoH3D_EnKoMidMask`
(`soh3d.c:2063`), called at `soh3d.c:2244`.

### B1. The ocarina (En_Sa, `zelda_sa.zar`)
OoT3D ground truth (`enko_override_and_ensa_facial.md` §En_Sa): the ocarina is a **mesh-visibility
toggle on the model**, scene-gated to the Sacred Forest Meadow (scene `0x56`): on limb 18 it *hides*
mesh 2 there, and *shows* meshes 5 & 2 elsewhere — i.e. the no-ocarina hand mesh vs the
ocarina-in-hand mesh are separate CMB meshes selected by visibility. The `EnSa_OverrideLimbDraw`
limb-18 condition is just *when* the toggle flips; the OUTPUT is mesh show/hide.

- Implementation: in the En_Sa branch of a `SoH3D_*MidMask` (either extend `SoH3D_EnKoMidMask` into a
  generic `SoH3D_AutoActorMidMask`, or add an En_Sa arm), set the mask so the correct hand mesh shows:
  in scene `0x56` show the ocarina hand mesh, else the empty hand — using the `zelda_sa` CMB mesh_ids.
- **Needs (Unknown):** which `zelda_sa` CMB **mesh_ids** are the ocarina-hand vs empty-hand meshes.
  The OoT3D mesh INDICES (2 / 5) in the decomp doc are OoT3D draw-time mesh indices and may not equal
  the parsed CMB `mesh_id` byte (`mshs[i].mesh_id`); confirm by dumping `zelda_sa`'s meshes
  (extend `tools/link_cmb_dump.py` to print per-mesh `mesh_id` + bones + material for `zelda_sa.zar`),
  framing En_Sa in the Meadow live, and finding which mesh is the ocarina. Same workflow that mapped
  the En_Ko head variants (`link_cmb_dump.py`, `soh3d.c:2053-2056`).
- Wire it in next to the En_Ko mask at `soh3d.c:2244` (generalize the call so it covers any auto actor
  with a mid-mask row, not just En_Ko).

### B2. En_Sa blink-CLOSE overlay mesh
OoT3D draws the *closing* eye as a separate colored overlay mesh (mesh index 1), faded by the blink
alpha (`+0x484`), distinct from the eyeIndex texture swap. This is ALSO a mesh-visibility toggle:
show the overlay mesh when the blink-close timer is active. Reuse the mid-mask. The color/alpha fade
is optional fidelity (the eyeIndex frames already give open/half/closed for the common blink) — defer
unless the blink looks wrong without it. If needed, it requires a per-material CONSTANT-color override
(a *second* small channel like Part A but for the material's blend/constant color) — flag as a
follow-up, not part of the P0 minimum.

---

## Files to touch (summary)

| File | Change |
|---|---|
| `Shipwright/libultraship/include/fast/soh3d_gl.h` | add `materialIndex` to `SoH3DGlGroup`; declare `SoH3D_GL_SetMatTexOverride` / `…ClearMatTexOverrides` |
| `Shipwright/libultraship/src/fast/soh3d_gl.cpp` | `GlGroup.materialIndex`; `GlModel.pendingMatTex`; `ItemPose.matTex`; `DrawItem.matTex`; set/clear fns; snapshot in `EmitPose`; apply override in `drawOne` texture-bind; **mirror in the Vulkan group-draw path** |
| `Shipwright/soh/src/soh3d/soh3d_model.cpp` | set `cg.materialIndex` in `makeCgroup`; add `SoH3D_FindMaterialByTexName` / `SoH3D_FindFaceTexFrame` over `Cmb` materials/textures |
| `Shipwright/soh/src/soh3d/soh3d_anim_override.{h,cpp}` | `FacialActor` table (ZAR-keyed, parallel to `kTrackActors`); read live eye/mouth indices from N64 actor; map index→tex; call `SoH3D_GL_SetMatTexOverride`; clear at top; `gSoH3dFacial` gate |
| `Shipwright/soh/src/soh3d/soh3d.c` | generalize `SoH3D_EnKoMidMask` → `SoH3D_AutoActorMidMask` covering En_Sa ocarina (scene-`0x56`-gated) + En_Sa blink-overlay; keep the `SetMidMask` call at ~2244 |
| `tools/link_cmb_dump.py` | extend to dump per-mesh `mesh_id` + all `*eye*`/`*mouth*` texture names for `zelda_sa` / `zelda_km1` / `zelda_kw1` (feeds the frame→tex + ocarina-mesh tables) |

## New per-model channel API (analogous to `SoH3D_SetBonePostRot`)

```c
// Facial: per-material texture-index swap (eye/mouth mat-anim frame). Set before EmitPose; cleared
// each draw by the override driver. texIndex<0 = clear that material. (GL: soh3d_gl.cpp)
void SoH3D_GL_SetMatTexOverride(int modelId, int materialIndex, int texIndex);
void SoH3D_GL_ClearMatTexOverrides(int modelId);

// Model introspection to resolve the eye/mouth slot + frame texture (model layer: soh3d_model.cpp)
int  SoH3D_FindMaterialByTexName(int modelId, const char* needle);   // eye/mouth material index, -1 none
int  SoH3D_FindFaceTexFrame(int modelId, const char* needle, int frame); // Nth eye/mouth texture, -1 none

// Mesh show/hide (ocarina, blink overlay): REUSE the existing
void SoH3D_GL_SetMidMask(int modelId, unsigned long long mask); // already exists
```

Lifecycle (per auto actor, per draw), inside `SoH3D_ApplyActorOverrides` and the mid-mask call already
on the `SoH3D_DoRetarget` auto branch (`soh3d.c:2148`+, `SetMidMask` at 2244):
1. `SoH3D_GL_ClearMatTexOverrides(modelId)` (clean slate, like `ClearBonePostRots`).
2. facial row? read live eye/mouth index from N64 actor → `eyeTex = SoH3D_FindFaceTexFrame(...eye, idx)` →
   `SoH3D_GL_SetMatTexOverride(modelId, eyeMat, eyeTex)`; same for mouth (with remap).
3. mid-mask: `SoH3D_GL_SetMidMask(modelId, SoH3D_AutoActorMidMask(modelId, actor))` (En_Ko heads +
   En_Sa ocarina/overlay folded in).
4. (existing) `SoH3D_GL_EmitPose` snapshots bones + midMask + matTex together for the deferred draw.

---

## Unknowns / blockers (needs the decomp agent or a live CMB dump — do NOT guess)

1. **Eye/mouth material slot + frame→texture mapping per face CMB** (En_Ko km1/kw1, En_Sa). Whether the
   eye frames are distinct textures (`*eye00/01/..`, expected) or UV sub-rects; the frame ORDER. Resolve
   by extending `link_cmb_dump.py` to list every `*eye*`/`*mouth*` texture + owning material per CMB and
   cross-checking against the OoT3D mat-anim order. The decomp doc gives the index SEMANTICS (En_Sa eye
   = direct, mouth = remap {0,3,4,1,2}); it does NOT give the SoH3D CMB texture indices.
2. **N64-side live eye/mouth index field offsets** in `z_en_sa.c` / `z_en_ko.c` (these are N64 offsets,
   like the existing `0x1E8`/`0x1E0` interactInfo). En_Ko: eye index per `headId` via `eyeTbl[headId]`,
   table `{0,0,1,0}` (decomp doc) — derive the N64 equivalent. En_Sa: eyeIndex/mouthIndex fields.
3. **`zelda_sa` CMB mesh_ids** for ocarina-hand vs empty-hand (and the blink-overlay mesh). The decomp
   doc's mesh indices (2/5, overlay 1) are OoT3D draw indices, NOT guaranteed equal to the parsed
   `mesh_id` byte — confirm by dumping `zelda_sa` meshes + a live Meadow framing of En_Sa.
4. **Vulkan group-draw call site** — confirm where the Vulkan backend binds per-group textures and
   mirror the `matTex` override there (GL was the only path read in this pass).
5. **En_Sa scene gate** — the ocarina toggle is gated to scene `0x56` (Sacred Forest Meadow). Confirm
   SoH's scene id for the Meadow matches `0x56` (OoT3D and N64 scene ids should align, but verify).

## Verification plan (per the project's evidence rule)
- Unit: dump the chosen eye/mouth material + per-frame texture indices from the live CMB (REPL/log),
  confirming `SoH3D_FindFaceTexFrame` returns the expected textures for frames 0..N.
- Live: drive En_Ko (Kokiri Forest) and En_Sa (Lost Woods / Meadow) headless, hold the actor with
  `asel`/`afreeze`, force-drive the eye/mouth index, and capture before/after screenshots showing the
  eye/mouth texture actually changing and (En_Sa, Meadow) the ocarina appearing. Attach via
  `tools/kanban.py evidence`. A frozen-cam single-frame harness is NOT sufficient (project rule:
  verify the full user-facing path).
