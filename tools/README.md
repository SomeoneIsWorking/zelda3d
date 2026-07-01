# Zelda3D extraction tooling

Dependency-free readers for OoT3D (decrypted .3ds) assets. No emulator/bootrom
needed for extraction (the dump is decrypted).

- `ctr_romfs.py` — NCSD/CCI -> NCCH (partition 0) -> RomFS (IVFC). List/extract files.
- `zar.py`       — OoT3D ZAR archive reader (holds .cmb/.ctxb/...).
- `cmb.py`       — CMB model parser: geometry (VATR/SEPD/PRM), skeleton (SKL),
                   materials (MATS: per-material primary texture + wrap + UV
                   coordinator + alpha test), bind-pose bone matrices.
                   `to_obj()` dumps geometry to OBJ. Run directly to dump a CMB's
                   materials/meshes/texture mapping.
- `cmb_to_c.py`  — CMB -> self-contained C (Vtx[] + per-texture RGBA32 arrays +
                   F3DEX2 Gfx[] dlist) that Ship of Harkinian compiles directly.
                   MULTI-MATERIAL: groups triangles by material and re-binds the
                   texture/combiner per material in one dlist. See its header for
                   the two non-obvious LUS gotchas it works around (gsDPLoadBlockWide
                   for textures > 4096 texels; explicit OPA_SURF render mode so the
                   RDP blender doesn't paint the model the scene fog colour).

Verified: zelda_tsubo.zar -> tubo2_model.cmb -> 130 verts / 160 tris (a pot,
1 material). zelda_gs.zar -> gossip_stone2_model.cmb -> 2 materials / 2 distinct
opaque textures (multi-material proof; renders in-game via En_Gs).

### Bone transforms (multi-mesh)
Meshes are stored in their bound bone's LOCAL space. A single-bone model (the pot)
needs no transform (bone 0 = identity), but multi-bone props (treasure-chest lid,
etc.) do: `cmb.py` computes each bone's world (bind-pose) matrix (local =
T*Rz*Ry*Rx*S, world = parent*local) and `triangles()` applies the bound bone's
matrix to each vertex. Without it, bone-local meshes render scrambled.

### Limitation — PICA multi-texture combiners
Some materials bind 2-3 textures blended by a per-fragment combiner where NO single
texture is the visible surface (e.g. the treasure chest: both "textures" are ~95%
transparent decals composited over each other). `cmb_to_c.py` takes only binding 0
and modulates it, so such materials render wrong. Pick models whose materials each
use a distinct mostly-opaque texture (most props/characters do).

Key gotchas (vs. CloudModding wiki, which is partly wrong/MM3D-mixed):
- OoT3D (cmb version 6) has NO tangent attribute; MM3D (v0x0A) does.
- SEPD VertexList stride is 0x1C (includes constant vec4), not 0x14.
- VATR slice entries are (size u32, offset u32) -- size FIRST.
- Bone struct stride is 0x28.
