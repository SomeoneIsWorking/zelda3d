#!/usr/bin/env python3
"""Dump per-mesh equipment/hand-variant info for a Link body CMB inside a romfs ZAR.

Reuses the method that mapped childlink_v2 (see soh/src/zelda3d/link_mesh_id_map.md):
for every mesh in the body CMB print its mesh_id (mid), material, primary texture name,
the bone_table it is rigidly bound to, and its posed (bind-pose) centroid/bbox. Combined
with an in-game `linkmid only <n>` render sweep this labels each baked equipment/hand
variant so Zelda3D_LinkComputeMidMask can select the live subset.

Usage: link_cmb_dump.py /actor/zelda_link_boy_new.zar [out.cmb]
       (defaults to the boy zar; picks the LARGEST .cmb = the body, like Zelda3D_AutoModelId)
"""
import os, sys, struct
sys.path.insert(0, os.path.dirname(__file__))
from ctr_romfs import CtrRom
from zar import Zar
import cmb as cmblib


def pick_body_cmb(zar: Zar):
    cmbs = [f for f in zar.files if f.name.lower().endswith(".cmb")]
    if not cmbs:
        raise SystemExit("no .cmb in zar")
    cmbs.sort(key=lambda f: f.size, reverse=True)
    return cmbs[0], cmbs


def mesh_bone_table(c: cmblib.Cmb, mesh):
    """Union of bone_table indices across the mesh's sepd prms (the bones it binds to)."""
    sepd = c.sepds[mesh.sepd_index]
    bones = set()
    for prms in sepd.prms:
        for bid in (prms.bone_table or [0]):
            bones.add(bid)
    return sorted(bones)


def mesh_posed_bbox(c: cmblib.Cmb, mesh_idx):
    """Posed bind-pose centroid + bbox for one mesh (replicates Cmb.triangles, per-mesh)."""
    mesh = c.meshes[mesh_idx]
    sepd = c.sepds[mesh.sepd_index]
    bd = sepd.bone_dimension
    b = c.data
    xs = []
    pts = []
    for prms in sepd.prms:
        prm = prms.prms[0]
        bt = prms.bone_table or [0]
        smooth = bd > 1 and sepd.attrs["boneIndices"].mode == cmblib.MODE_ARRAY \
                          and sepd.attrs["boneWeights"].mode == cmblib.MODE_ARRAY
        M = cmblib._mat_id() if smooth else c.bone_matrix.get(bt[0], cmblib._mat_id())
        isz = cmblib.DT_SIZE[prm.index_type]; ifmt = cmblib.DT_FMT[prm.index_type]
        ibase = c.idx_ptr + prm.first * isz
        idxs = struct.unpack_from("<%d%s" % (prm.count, ifmt), b, ibase)
        for idx in idxs:
            pos = c.read_attr(sepd.attrs["position"], "position", idx, 3)
            pts.append(cmblib._mat_apply_pos(M, pos))
    if not pts:
        return None
    xs = [p[0] for p in pts]; ys = [p[1] for p in pts]; zs = [p[2] for p in pts]
    n = len(pts)
    cen = (sum(xs)/n, sum(ys)/n, sum(zs)/n)
    bb = ((min(xs), min(ys), min(zs)), (max(xs), max(ys), max(zs)))
    return cen, bb, n


def main():
    zar_path = sys.argv[1] if len(sys.argv) > 1 else "/actor/zelda_link_boy_new.zar"
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    fe = rom.get(zar_path)
    z = Zar(rom.read(fe))
    body, allcmbs = pick_body_cmb(z)
    print(f"# zar {zar_path}")
    print(f"# {len(allcmbs)} CMBs; body = {body.name} ({body.size} bytes)")
    for f in allcmbs:
        if f is not body:
            print(f"#   other CMB: {f.name} ({f.size})")
    data = z.read(body)
    if len(sys.argv) > 2:
        open(sys.argv[2], "wb").write(data)
        print(f"# wrote {sys.argv[2]}")
    c = cmblib.Cmb(data)
    print(f"# CMB {c.name} bones={len(c.bones)} meshes={len(c.meshes)} "
          f"mats={len(c.materials)} tex={len(c.textures)}")
    print(f"# {'mid':>4} {'mesh':>4} {'mat':>3} {'tex':<22} {'bones':<18} "
          f"{'verts':>6}  centroid(x,y,z)            bbox-size")
    rows = []
    for mi, mesh in enumerate(c.meshes):
        ti = c.material_texture(mesh.material_index)
        tname = c.textures[ti].name if 0 <= ti < len(c.textures) else "?"
        bones = mesh_bone_table(c, mesh)
        bb = mesh_posed_bbox(c, mi)
        if bb:
            cen, (lo, hi), n = bb
            size = (hi[0]-lo[0], hi[1]-lo[1], hi[2]-lo[2])
            rows.append((mesh.mesh_id, mi, mesh.material_index, tname, bones, n, cen, size))
        else:
            rows.append((mesh.mesh_id, mi, mesh.material_index, tname, bones, 0, None, None))
    rows.sort(key=lambda r: (r[0], r[1]))
    for mid, mi, mat, tname, bones, n, cen, size in rows:
        bs = ",".join(map(str, bones))
        if cen:
            print(f"  {mid:>4} {mi:>4} {mat:>3} {tname:<22} {bs:<18} {n:>6}  "
                  f"({cen[0]:7.1f},{cen[1]:7.1f},{cen[2]:7.1f})  "
                  f"({size[0]:6.1f},{size[1]:6.1f},{size[2]:6.1f})")
        else:
            print(f"  {mid:>4} {mi:>4} {mat:>3} {tname:<22} {bs:<18} {n:>6}  (empty)")


if __name__ == "__main__":
    main()
