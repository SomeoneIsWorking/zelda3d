#!/usr/bin/env python3
"""Cross-check the C++ CSAB skinning (asset/csab.cpp) against the verified Python
oracle (tools/csab.py), element-wise.

The C++ asset_test dumps skinned vertices grouped by material (first-encounter
order). This reproduces that exact ordering from skinned_triangles(), then diffs
position+normal against the C++ float32 dump.

Usage:
  ZELDA3D_OOT3D_ROM=... scratch/bin/asset_test  # with ZELDA3D_ANIM/FRAME/ANIM_DUMP set
  ZELDA3D_OOT3D_ROM=... python3 tools/csab_xcheck.py <cpp_dump.bin> [anim] [frame]
"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cmb as C
import csab as A
from ctr_romfs import CtrRom
from zar import Zar


def main():
    dump = sys.argv[1]
    anim = sys.argv[2] if len(sys.argv) > 2 else "ge1_s_wait"
    frame = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
    zar_path = os.environ.get("ZELDA3D_XCHECK_ZAR", "/actor/zelda_ge1.zar")
    cmb_name = os.environ.get("ZELDA3D_XCHECK_CMB", "Model/geldwoman.cmb")

    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    z = Zar(rom.read(rom.get(zar_path)))
    model = C.Cmb(z.read([f for f in z.files if f.name == cmb_name][0]))
    nm = "Anim/%s.csab" % anim if not anim.startswith("Anim/") else anim
    csab = A.Csab(z.read([f for f in z.files if f.name == nm][0]))

    # group skinned tris by material in first-encounter order (matches C++ groupFor)
    order = []
    bymat = {}
    for sidx, midx, tri in A.skinned_triangles(model, csab, frame):
        if midx not in bymat:
            bymat[midx] = []
            order.append(midx)
        bymat[midx].append(tri)
    py = []
    for midx in order:
        for tri in bymat[midx]:
            for pos, nrm, uv in tri:
                py.extend(pos); py.extend(nrm)

    raw = open(dump, "rb").read()
    cpp = struct.unpack("<%df" % (len(raw) // 4), raw)
    if len(cpp) != len(py):
        print(f"FAIL: vertex count mismatch  py={len(py)//6} cpp={len(cpp)//6}")
        sys.exit(1)

    maxp = maxn = 0.0
    for i in range(0, len(py), 6):
        for k in range(3):
            maxp = max(maxp, abs(py[i + k] - cpp[i + k]))
            maxn = max(maxn, abs(py[i + 3 + k] - cpp[i + 3 + k]))
    n = len(py) // 6
    # tolerance: C++ is float32, Python float64; coords up to ~6500 -> ~1e-3 abs is
    # float32 epsilon at that magnitude. Normals are unit-ish -> tighter.
    ok = maxp < 5e-2 and maxn < 5e-4
    print(f"xcheck anim={anim} frame={frame}: {n} verts  max|Δpos|={maxp:.3e}  "
          f"max|Δnrm|={maxn:.3e}  ({'PASS' if ok else 'FAIL'})")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
