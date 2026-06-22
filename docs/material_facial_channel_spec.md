# Implementation spec: per-limb material/facial + mesh-show/hide override channel (keystone #3 / P0)

> ## ✅ IMPLEMENTED (commit eaf2075, kanban #94, 2026-06-22)
> The facial eye/mouth material-anim channel and the Saria ocarina mesh-toggle are BUILT and the
> eye swap is **live-verified** (En_Ko girl, Kokiri Forest: forcing the frame swaps the on-screen eye
> texture open↔closed — `scratch/screenshots/facial_eye_ab.png`). Key realities vs this spec:
> - The cmab does NOT have a `tex `/`strt`-pair the way Part A2 imagined: it has header+0x18 → `strt`
>   (frame-name table, count at strt+4) and header+0x1c → `texDataOffset` (frames concatenated raw,
>   each = the CMB base eye/mouth texture's size+format). No per-frame headers — slice evenly by the
>   base `data_len`. Implemented in `soh3d_model.cpp appendFacialFrames()` (NOT `SoH3D_FindFaceTexFrame`
>   over CMB textures, which can't work — frames aren't in the CMB).
> - Channel = `SoH3D_GL_SetMatTexOverride` (GL + Vulkan), driven by `soh3d_anim_override.cpp`
>   FacialActor table reading the N64 eye/mouth index. REPL `facial 0|1`, `faceframe <n>`.
> - Ocarina = `SoH3D_AutoActorMidMask` (En_Sa, scene 0x56). Material slots/offsets per the resolved
>   unknowns below were all correct.
> - **Still TODO:** live-confirm mouth + ocarina (needs Saria in the Meadow); facial tables for
>   Mido/Malon/En_Hy (dump pass needed); En_Sa blink-overlay mesh (optional). See kanban #94.



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

---

## Resolved unknowns (2026-06-22, read-only dump pass; tools `tools/face_cmb_dump.py` + extended `tools/cmab.py` use)

> ⚠ **The spec's central premise about facial is WRONG and the implementation MUST change.** The
> alternate eye/mouth frame sprites are **NOT bundled in the CMB `tex ` chunk** (`*eye00/01/...`).
> The CMB holds exactly ONE base eye and ONE base mouth texture. The per-frame alternate sprites live
> in **separate `.cmab` material-anim files** (e.g. `misc/saria_eye.cmab`), each of which embeds its
> own `tex ` chunk of frame textures + a `strt` string table naming them in order. The "mat-anim frame
> index" is a **TexturePalette** integer track (frame N → name N → that cmab-embedded texture).
> Therefore `SoH3D_FindFaceTexFrame` cannot work over `Cmb::textures()` alone — those frame textures
> are not in the CMB at all. The facial channel must **parse the face `.cmab`, decode + upload its
> embedded eye/mouth frame textures, and override the eye/mouth material to sample the cmab frame
> texture** (not a CMB tex index). This is a real new asset path (parse cmab `tex `/`strt` + upload),
> not just an index redirect. See unknown #1 below.

### 1. CMB eye/mouth material slot + ORDERED frame→texture mapping

The eye/mouth **material slot** is in the CMB (single base texture); the **frame textures** are in the
sibling `.cmab` (embedded `tex ` chunk, ordered by the `strt` string table; TexturePalette track value =
frame index = `strt` index). Verified by parsing both.

**Saria — `/actor/zelda_sa.zar` → `Model/saria.cmb`** (5 mats, 5 base tex):
- base CMB tex: `tex[0] saria_00`, `tex[1] saria_02`, `tex[2] sa_eye01`, `tex[3] sa_mouth01`, `tex[4] saria_01`
- base CMB materials: `mat[0]→saria_00`, `mat[1]→saria_02`, `mat[2]→sa_eye01` (**EYE slot = material 2**),
  `mat[3]→sa_mouth01` (**MOUTH slot = material 3**), `mat[4]→saria_01`
- **EYE frames** (`misc/saria_eye.cmab`, TexturePalette **mat=2**, duration 5, INTEGER track 0..5):
  frame 0→`sa_eye01`, 1→`sa_eye02`, 2→`sa_eye03`, 3→`sa_eye04`, 4→`sa_eye05`, 5→`sa_eye06` (6 frames)
- **MOUTH frames** (`misc/saria_mouth.cmab`, TexturePalette **mat=3**, duration 5, track 0..4):
  frame 0→`sa_mouth01`, 1→`sa_mouth02`, 2→`sa_mouth03`, 3→`sa_mouth04`, 4→`sa_mouth05` (5 frames)

**Kokiri Master (km1) — `/actor/zelda_km1.zar` → `Model/kokirimaster.cmb`** (3 mats, 3 base tex):
- base materials: `mat[0]→kokirimaster_00`, `mat[1]→ksh_eye01` (**EYE slot = material 1**), `mat[2]→kokirimaster_01`. **No mouth material.**
- **EYE frames** (`Misc/kokirimaster_eye.cmab`, TexturePalette **mat=1**, duration 3, track 0..2):
  frame 0→`ksh_eye01`, 1→`ksh_eye02`, 2→`ksh_eye03` (3 frames)

**Kokiri People (kw1, girl/fado shared body) — `/actor/zelda_kw1.zar` → `Model/kokiripeople.cmb`** (4 mats, 4 base tex):
- base materials: `mat[0]→kokiripeople_00`, `mat[1]→fa_eye01_CI00` (**FADO eye slot = material 1**),
  `mat[2]→kw1_eye01_CI00` (**KW1-girl eye slot = material 2**), `mat[3]→fado_00`. **No mouth.**
- TWO eye cmabs (one per body variant baked in the shared CMB):
  - `Misc/kokiripeople_a_eye.cmab` → TexturePalette **mat=1** (Fado): 0→`fa_eye01_CI00`, 1→`fa_eye02_CI00`, 2→`fa_eye03_CI00`
  - `Misc/kokiripeople_b_eye.cmab` → TexturePalette **mat=2** (kw1 girl): 0→`kw1_eye01_CI00`, 1→`kw1_eye02_CI00`, 2→`kw1_eye03_CI00`

**Frame-count cross-check vs N64** (z_en_ko.c / z_en_sa.c): N64 km1/kw1 eye tables are 3 frames
(`{open, half, closed}`) — matches the 3-frame cmabs exactly. N64 En_Sa eye = 5 (`{open, half, closed,
surprised, sad}`) and mouth = 5 — the cmab has **6** eye frames / 5 mouth frames, so eyeIndex 0..4 maps
directly into the 6-frame cmab (frame 5 unused by N64 logic), mouthIndex 0..4 needs the {0,3,4,1,2}
remap (decomp `DAT_001b943c`) because the cmab mouth ORDER differs from the N64 `mouthTextures[]` order
(N64: `{Closed2, SmilingOpen, Frowning, Suprised, Closed}`).

### 2. N64-side eye/mouth index field offsets (SoH3D reads the N64 actor struct)

From the SoH N64 structs (these are the live indices the N64 logic already computes each frame):

- **En_Ko** (`z_en_ko.h`, `EnKo`): `s16 eyeTextureIndex @ 0x216`. (Also `s16 blinkTimer @ 0x214`.)
  Range 0..2. NOTE the N64 km1 head (`OBJECT_KM1`) has `eyeTextures = NULL` (z_en_ko.c:81) — N64 **km1
  does not blink** (index stays valid but only kw1/fado have eye tables). Driven by `EnKo_OverrideLimbDraw`
  limb (N64 numbering) via `sHead[headId].eyeTextures[eyeTextureIndex]`.
- **En_Sa** (`z_en_sa.h`, `EnSa`): `s16 rightEyeIndex @ 0x212`, `s16 leftEyeIndex @ 0x214`,
  `s16 mouthIndex @ 0x216` (also `s16 alpha @ 0x218`). Use `rightEyeIndex` for the eye frame (left/right
  are equal except during the surprised/sad asymmetric cases; N64 binds both eye segments 0x08/0x09 — the
  single OoT3D eye material maps to one index, use `rightEyeIndex`). Mouth = `mouthRemap[mouthIndex]`,
  `mouthRemap = {0,3,4,1,2}`.

These are **N64** offsets (consistent with `kTrackActors`), NOT the OoT3D-native +0x480/+0x482 from the
decomp doc — confirmed they live in the N64 `EnSa`/`EnKo` structs above.

### 3. zelda_sa ocarina vs empty-hand mesh_ids (for the ocarina show/hide via midMask)

Parsed `Model/saria.cmb` (10 meshes; `mesh_id` is the parsed `mshs[i].mesh_id` byte SoH3D's midMask
keys on). The right hand (bones 17,18) appears as TWO mesh_id groups:

| parsed mesh | mesh_id | mat | tex | bones | role |
|---|---|---|---|---|---|
| 2 | **2** | 0 | `saria_00` | 17,18 | **empty/default right hand** |
| 8 | **5** | 0 | `saria_00` | 17,18 | ocarina-hand pose geometry |
| 9 | **5** | 1 | `saria_02` | 18 | **the OCARINA itself** (`saria_02` = unique ocarina texture) |

⇒ **empty hand = mesh_id 2; ocarina hand = mesh_id 5.** This matches the decomp's OoT3D mesh INDICES
(hide mesh 2 in Meadow / show meshes 5 & 2 elsewhere) — here the parsed `mesh_id` byte HAPPENS to equal
the OoT3D mesh index (2↔2, 5↔5), so the spec's "indices may not equal mesh_id" caveat resolves favorably
for Saria. Implementation: in scene 0x56 set the midMask to show mesh_id 5 and HIDE mesh_id 2 (ocarina
out); elsewhere show both 2 and 5 (decomp shows 5&2 — note that draws ocarina geometry but it's the
non-playing idle hand; the N64 equivalent is the empty hand, so for faithfulness OUTSIDE the Meadow show
mesh_id 2 and HIDE mesh_id 5 — the ocarina should only appear in the Meadow. The decomp's "show 5 & 2
elsewhere" is the OoT3D default; cross-check live whether OoT3D Saria carries the ocarina outside the
Meadow — N64 does not. Recommend: Meadow → mesh_id 5 only; else → mesh_id 2 only.)
Other mesh_ids: 0=body(`saria_00`), 1=hair?(`saria_01` bone10 head), 3=head cluster (body+hair+mouth+eye,
bones 9,10), 4=neck/face(`saria_00`), and mesh_id 5 mesh 8/9 = ocarina hand.

### 4. Sacred Forest Meadow scene id

**Confirmed `SCENE_SACRED_FOREST_MEADOW = 0x56`** (`Shipwright/soh/include/tables/scene_table.h:98`,
`/* 0x56 */ DEFINE_SCENE(spot05_scene, ... SCENE_SACRED_FOREST_MEADOW ...)`). The spec's guess of 0x56 is
correct; OoT3D and N64 scene ids align here. `z_en_sa.c` gates the ocarina DL swap on exactly this enum.

### Implementation impact summary (corrections to Parts A/A2/A3)

- **A2 `SoH3D_FindFaceTexFrame` over `Cmb::textures()` does NOT work** — frame textures aren't in the CMB.
  Replace with a cmab-backed path: at model load, parse the sibling face `.cmab` (`tools/cmab.py` logic:
  TexturePalette `mmad` mat index + INTEGER track + embedded `tex `/`strt`), decode + upload its frame
  textures (reuse the CMB texture decode), and build `{eyeMaterial → [glTex per frame]}`,
  `{mouthMaterial → [glTex per frame]}`. The override binds `frameTex[eyeIndex]` for the eye material.
- The **material slots are constants per ZAR** (no name search needed): Saria eye=mat2/mouth=mat3;
  km1 eye=mat1; kw1 fado eye=mat1 / girl eye=mat2. The `FacialActor` table can hardcode these (and the
  cmab filename) per ZAR rather than name-matching.
- **No mouth** for En_Ko (km1/kw1) — `mouthIdxOff = -1`.
- kw1 shared body needs **per-ENKO_TYPE eye-material selection** (mat1 Fado vs mat2 girl) — same head-variant
  split already handled by `SoH3D_EnKoMidMask`; the facial channel must pick the matching eye cmab/material
  for the live variant.
- Ocarina (Part B1): midMask mesh_id 5 = ocarina hand, mesh_id 2 = empty hand; scene-0x56-gated.

New tool added (read-only, uncommitted): `tools/face_cmb_dump.py` (per-CMB material/tex/mesh dump). The
cmab frame names were extracted with `tools/cmab.py` (TexturePalette path) + a `strt`-table read; consider
folding a `--names` mode into `tools/cmab.py` when implementing.
