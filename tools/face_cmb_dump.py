#!/usr/bin/env python3
"""Dump eye/mouth material+texture mapping and per-mesh mesh_id for a face CMB zar.

For the facial/material override channel (#3): prints
  - the full ORDERED tex` chunk (index -> name) so frame-index -> texture can be coded,
  - per-material tex0 binding (material slot -> texture name) so the eye/mouth material slot is identified,
  - per-mesh mesh_id + material + tex name (for ocarina/hand mesh-visibility selection).

Usage: face_cmb_dump.py /actor/zelda_sa.zar [cmb_name_substr]
       (default picks the LARGEST cmb = body, like Zelda3D_AutoModelId)
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from ctr_romfs import CtrRom
from zar import Zar
import cmb as cmblib


def main():
    zar_path = sys.argv[1]
    want = sys.argv[2] if len(sys.argv) > 2 else None
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = Zar(rom.read(rom.get(zar_path)))
    cmbs = [f for f in z.files if f.name.lower().endswith(".cmb")]
    if want:
        cmbs = [f for f in cmbs if want.lower() in f.name.lower()] or cmbs
    cmbs.sort(key=lambda f: f.size, reverse=True)
    print(f"# zar {zar_path}: {len(z.files)} files, cmbs={[f.name for f in z.files if f.name.lower().endswith('.cmb')]}")
    body = cmbs[0]
    c = cmblib.Cmb(z.read(body))
    print(f"# === CMB {body.name} ({body.size}B): name='{c.name}' bones={len(c.bones)} "
          f"meshes={len(c.meshes)} mats={len(c.materials)} tex={len(c.textures)} ===")

    print("# --- tex` chunk (ordered; frame index selects among same-family textures) ---")
    for i, t in enumerate(c.textures):
        print(f"  tex[{i:2}] {t.name:<24} {t.width}x{t.height}")

    print("# --- materials (slot -> tex0) ---")
    for m in c.materials:
        ti = m.tex0_idx
        tn = c.textures[ti].name if 0 <= ti < len(c.textures) else "?"
        print(f"  mat[{m.index:2}] tex0={ti:2} ({tn})")

    print("# --- meshes (mesh_id, material, tex) ---")
    for mi, mesh in enumerate(c.meshes):
        ti = c.material_texture(mesh.material_index)
        tn = c.textures[ti].name if 0 <= ti < len(c.textures) else "?"
        print(f"  mesh[{mi:2}] mesh_id={mesh.mesh_id:2} mat={mesh.material_index:2} tex={tn}")


if __name__ == "__main__":
    main()
