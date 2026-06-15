# SoH3D extraction tooling

Dependency-free readers for OoT3D (decrypted .3ds) assets. No emulator/bootrom
needed for extraction (the dump is decrypted).

- `ctr_romfs.py` — NCSD/CCI -> NCCH (partition 0) -> RomFS (IVFC). List/extract files.
- `zar.py`       — OoT3D ZAR archive reader (holds .cmb/.ctxb/...).
- `cmb.py`       — CMB model parser: geometry (VATR/SEPD/PRM), skeleton (SKL),
                   material/texture refs. `to_obj()` dumps geometry to OBJ.

Verified: zelda_tsubo.zar -> tubo2_model.cmb -> 130 verts / 160 tris (a pot).

Key gotchas (vs. CloudModding wiki, which is partly wrong/MM3D-mixed):
- OoT3D (cmb version 6) has NO tangent attribute; MM3D (v0x0A) does.
- SEPD VertexList stride is 0x1C (includes constant vec4), not 0x14.
- VATR slice entries are (size u32, offset u32) -- size FIRST.
- Bone struct stride is 0x28.
