#!/usr/bin/env python3
"""Parser for OoT3D CSAB skeletal animations (CTR Skeletal Animation Binary).

Port of noclip.website OcarinaOfTime3D/csab.ts (Ocarina/version-6 path only;
Majora/LM not needed here). Verified against zelda_ge1's ge1_s_wait.csab.

Layout (Ocarina, subversion 3):
  0x00 'csab'  0x04 size  0x08 subversion(=3)  0x0C 0
  0x10 1 (numAnims?)  0x14 0x18 (anod-base location)  0x18..0x24 zero
  0x28 duration-1     0x2C loopMode?
  0x30 anodCount      0x34 boneCount
  0x38 boneToAnimTable[boneCount] (int16)
  align(.,4) -> anod offset table (u32 each, relative to 0x18)
Each 'anod': 0x04 boneIndex(u16) 0x06 isRotationInt16(u16)
  0x08..0x19 nine u16 track offsets (tX tY tZ rX rY rZ sX sY sZ), rel to anod start
Each track: 0x00 type(u32: 0 const,1 linear,2 hermite) 0x04 nKf 0x08 tStart 0x0C tEnd
  linear   keyframe: u32 time, f32 value           (0x08 stride)
  hermite  keyframe: u32 time, f32 value, f32 tIn, f32 tOut (0x10 stride)
  (isRotationInt16 hermite: u16 time, 3x int16 — present in format, unused by ge1)

GOTCHA (cost a session, 2026-07-23): `AnimNode.tracks` is keyed by track NAME —
"tX","tY","tZ","rX","rY","rZ","sX","sY","sZ" — NOT by the integer slot index. A scan
written as `node.tracks.get(0)` returns None for every clip and reports the false
conclusion "no OoT3D player clip has any translation track". They all do: e.g.
nml_Fclimb_upL bone1 tY runs 4772->6272 (the ladder rung's root motion).

Rotation values are radians; the matrix convention (T·Rz·Ry·Rx·S) is IDENTICAL to
cmb.py's bind-pose _compute_bone_matrices, so with NO animation applied the animated
world matrices equal the bind matrices and skinMatrix = animWorld·bindInverse = I
(the static bind-pose render is unchanged — the property the renderer relies on).
"""
from __future__ import annotations
import struct, math
from dataclasses import dataclass

import cmb as _cmb  # reuse matrix helpers + Cmb

CONSTANT, LINEAR, HERMITE = 0x00, 0x01, 0x02


def csab_track_type_name(t):
    return {CONSTANT: "const", LINEAR: "linear", HERMITE: "hermite"}.get(t, f"?{t}")


def _u16(b, o): return struct.unpack_from("<H", b, o)[0]
def _i16(b, o): return struct.unpack_from("<h", b, o)[0]
def _u32(b, o): return struct.unpack_from("<I", b, o)[0]
def _f32(b, o): return struct.unpack_from("<f", b, o)[0]


@dataclass
class Track:
    type: int
    time_start: int
    time_end: int
    frames: list   # linear: [(time,value)]; hermite: [(time,value,tIn,tOut)]


@dataclass
class AnimNode:
    bone_index: int
    is_rot_int16: bool
    tracks: dict    # name -> Track | None  (keys: tX tY tZ rX rY rZ sX sY sZ)


# ---- low-level parsing ----
_TRACK_NAMES = ["tX", "tY", "tZ", "rX", "rY", "rZ", "sX", "sY", "sZ"]


def _parse_track(b, o, is_rot_int16):
    typ = _u32(b, o); nkf = _u32(b, o + 4)
    tstart = _u32(b, o + 8); tend = _u32(b, o + 0xC) + 1
    p = o + 0x10
    frames = []
    if typ == LINEAR:
        if is_rot_int16:
            # Quantized LINEAR rotation: u16 time + s16 fixed-point angle, the same 0x10000 = full
            # circle scale the HERMITE int16 path below uses. MUST mirror the C++ authority
            # (cmb3d/asset/csab.cpp) -- this branch was missing here while the C++ had it fixed, so
            # every offline consumer (csab_render, csab_xcheck, link_sweep, model_match, the
            # pose-parity A/B) decoded 2951 of Link's rotation tracks as garbage: denormals ~1e-45
            # standing in for real angles, plus outliers like 1.77e22 rad from reading the following
            # "anod" chunk's ASCII as a float. Verified against the fix: boy/anim/sude_nwait decodes
            # to clean 3.1415 / -3.1415 / -1.5556 once read this way.
            ANG_L = math.pi / 32768.0
            for _ in range(nkf):
                frames.append((_u16(b, p), _i16(b, p + 2) * ANG_L)); p += 4
        else:
            for _ in range(nkf):
                frames.append((_u32(b, p), _f32(b, p + 4))); p += 8
    elif typ == HERMITE:
        if is_rot_int16:
            # Quantized rotation: u16 time + s16 value/in-tangent/out-tangent as fixed-point ANGLES
            # (full circle = 0x10000 -> radians = s16 * pi/0x8000). Tangents are angle/frame, same scale.
            ANG = math.pi / 32768.0
            for _ in range(nkf):
                frames.append((_u16(b, p), _i16(b, p + 2) * ANG, _i16(b, p + 4) * ANG, _i16(b, p + 6) * ANG))
                p += 8
        else:
            for _ in range(nkf):
                frames.append((_u32(b, p), _f32(b, p + 4), _f32(b, p + 8), _f32(b, p + 0xC)))
                p += 0x10
    else:
        raise ValueError(f"bad track type {typ}")
    return Track(typ, tstart, tend, frames)


def _parse_anod(b, o):
    assert b[o:o + 4] == b"anod", "bad anod"
    bone_index = _u16(b, o + 4)
    is_rot_int16 = bool(_u16(b, o + 6))
    offs = [_u16(b, o + 8 + 2 * i) for i in range(9)]
    tracks = {}
    for name, off in zip(_TRACK_NAMES, offs):
        is_rot = name in ("rX", "rY", "rZ")
        # int16 quantization applies only to the rotation slots; translation/scale stay float.
        tracks[name] = _parse_track(b, o + off, is_rot_int16 and is_rot) if off else None
    return AnimNode(bone_index, is_rot_int16, tracks)


class Csab:
    def __init__(self, data: bytes):
        b = self.data = data
        assert b[0:4] == b"csab", "not a CSAB"
        self.subversion = _u32(b, 0x08)
        assert self.subversion == 0x03, f"only Ocarina subversion 3 supported (got {self.subversion})"
        self.anod_base = _u32(b, 0x14)  # = 0x18
        self.duration = _u32(b, 0x28) + 1
        anod_count = _u32(b, 0x30)
        bone_count = _u32(b, 0x34)
        assert anod_count <= bone_count
        self.bone_count = bone_count
        self.bone_to_anim = [_i16(b, 0x38 + 2 * i) for i in range(bone_count)]
        idx = 0x38 + 2 * bone_count
        idx = (idx + 3) & ~3
        self.nodes = []
        for i in range(anod_count):
            off = _u32(b, idx + 4 * i)
            self.nodes.append(_parse_anod(b, self.anod_base + off))

    def node_for_bone(self, bone_id: int):
        if 0 <= bone_id < len(self.bone_to_anim):
            ai = self.bone_to_anim[bone_id]
            if ai >= 0:
                return self.nodes[ai]
        return None


# ---- sampling (matches noclip) ----
def _anim_frame(csab: Csab, frame: float) -> float:
    last = csab.duration
    # REPEAT loop mode (the only mode ge1 anims use)
    while frame > last:
        frame -= last
    return frame


def _get_coeff_hermite(p0, p1, s0, s1):
    return (2 * p0 - 2 * p1 + s0 + s1,
            -3 * p0 + 3 * p1 - 2 * s0 - s1,
            s0,
            p0)


def _point_cubic(cf, t):
    return ((cf[0] * t + cf[1]) * t + cf[2]) * t + cf[3]


def _hermite_interp(k0, k1, t, length, rotation=False):
    p0, p1 = k0[1], k1[1]
    if rotation:
        # int16 rotation values are wrapped to [-pi,pi); the cubic must interpolate the
        # CONTINUOUS curve, so unwrap p1 to the branch nearest p0 (take the short way).
        # Without this, a keyframe pair straddling +-pi (e.g. +176deg -> -168deg, a +16deg
        # move) sweeps the long way (~-344deg) and the bone spins all the way around.
        # Tangents are slopes (angle/frame), invariant under adding 2*pi, so they stay valid.
        p1 = p0 + ((p1 - p0 + math.pi) % (2 * math.pi) - math.pi)
    s0 = k0[3] * length   # tangentOut of k0
    s1 = k1[2] * length   # tangentIn  of k1
    return _point_cubic(_get_coeff_hermite(p0, p1, s0, s1), t)


def _lerp(a, b, t): return a + (b - a) * t


def _lerp_angle(v0, v1, t, maxa=2 * math.pi):
    da = (v1 - v0) % maxa
    dist = (2 * da) % maxa - da
    return v0 + dist * t


def _find_idx1(frames, frame):
    for i, k in enumerate(frames):
        if frame < k[0]:
            return i
    return -1


def _sample_linear(track: Track, frame, rotation):
    f = track.frames
    i1 = _find_idx1(f, frame)
    if i1 == 0:
        return f[0][1]
    if i1 < 0:
        return f[-1][1]
    k0, k1 = f[i1 - 1], f[i1]
    t = (frame - k0[0]) / (k1[0] - k0[0])
    return _lerp_angle(k0[1], k1[1], t) if rotation else _lerp(k0[1], k1[1], t)


def _sample_hermite(track: Track, frame, rotation=False):
    f = track.frames
    i1 = _find_idx1(f, frame)
    if i1 <= 0:
        k0, k1 = f[-1], f[0]
    else:
        k0, k1 = f[i1 - 1], f[i1]
    length = (k1[0] - k0[0]) % track.time_end
    if length == 0:
        return k0[1]
    t = (frame - k0[0]) / length
    return _hermite_interp(k0, k1, t, length, rotation)


def sample_track(track: Track, frame, rotation=False):
    if track is None:
        return None
    if track.type == LINEAR:
        return _sample_linear(track, frame, rotation)
    if track.type == HERMITE:
        return _sample_hermite(track, frame, rotation)
    raise ValueError("bad track type")


# ---- animated skeleton ----
def bone_local_trs(bone: "_cmb.Bone", node: "AnimNode | None", frame: float):
    """Return (scale, rot, trans) for a bone at `frame`, overriding the bone's rest
    TRS with any present animation tracks (noclip calcBoneMatrix)."""
    sx, sy, sz = bone.scale
    rx, ry, rz = bone.rot
    tx, ty, tz = bone.trans
    if node is not None:
        def g(name, cur, rot=False):
            v = sample_track(node.tracks[name], frame, rot)
            return cur if v is None else v
        sx = g("sX", sx); sy = g("sY", sy); sz = g("sZ", sz)
        rx = g("rX", rx, True); ry = g("rY", ry, True); rz = g("rZ", rz, True)
        tx = g("tX", tx); ty = g("tY", ty); tz = g("tZ", tz)
    return (sx, sy, sz), (rx, ry, rz), (tx, ty, tz)


def _local_matrix(scale, rot, trans):
    """T · Rz · Ry · Rx · S  — identical convention to cmb._compute_bone_matrices."""
    L = _cmb._mat_mul(_cmb._mat_T(*trans),
                      _cmb._mat_mul(_cmb._mat_mul(_cmb._mat_Rz(rot[2]), _cmb._mat_Ry(rot[1])),
                                    _cmb._mat_Rx(rot[0])))
    return _cmb._mat_mul(L, _cmb._mat_S(*scale))


def animated_bone_world(model: "_cmb.Cmb", csab: "Csab | None", frame: float):
    """World matrix per bone id at `frame` (rest pose if csab is None)."""
    by_id = {b.id: b for b in model.bones}
    out = {}

    def world(bid):
        if bid in out:
            return out[bid]
        b = by_id[bid]
        node = csab.node_for_bone(bid) if csab is not None else None
        s, r, t = bone_local_trs(b, node, frame)
        L = _local_matrix(s, r, t)
        W = L if b.parent < 0 else _cmb._mat_mul(world(b.parent), L)
        out[bid] = W
        return W

    for b in model.bones:
        world(b.id)
    return out


def _mat_inv(M):
    """Inverse of a 4x4 affine (rotation/scale + translation) matrix via numpy-free
    Gauss-Jordan. General 4x4 — the bind matrices may carry non-uniform scale."""
    n = 4
    A = [list(M[i]) + [1.0 if i == j else 0.0 for j in range(n)] for i in range(n)]
    for col in range(n):
        piv = max(range(col, n), key=lambda r: abs(A[r][col]))
        if abs(A[piv][col]) < 1e-12:
            raise ValueError("singular matrix")
        A[col], A[piv] = A[piv], A[col]
        d = A[col][col]
        A[col] = [v / d for v in A[col]]
        for r in range(n):
            if r != col:
                f = A[r][col]
                A[r] = [a - f * b for a, b in zip(A[r], A[col])]
    return [row[n:] for row in A]


def skin_matrices(model: "_cmb.Cmb", csab: "Csab | None", frame: float):
    """Per-bone-id skinning matrix = animWorld(bone) · bindInverse(bone).
    At the rest pose (csab None, or anim==rest) every matrix is identity."""
    aw = animated_bone_world(model, csab, frame)
    out = {}
    for bid, bindW in model.bone_matrix.items():
        out[bid] = _cmb._mat_mul(aw[bid], _mat_inv(bindW))
    return out


def _vertex_bones(model, sepd, prms, idx, bd, smooth, rigid_pv=False):
    """Per-vertex [(bone_id, weight)] in MODEL-space terms (see skinned_triangles).
    rigid -> single bound bone, weight 1; smooth -> boneIndices(local)+boneWeights.
    rigid_pv (RIGID_SKINNING) -> the vertex's own bone index (1 per vertex), weight 1."""
    bt = prms.bone_table or [0]
    if rigid_pv:
        bi_attr = sepd.attrs["boneIndices"]; base, _ = model.vatr["boneIndices"]
        sz = _cmb.DT_SIZE[bi_attr.data_type]; fmt = _cmb.DT_FMT[bi_attr.data_type]
        li = struct.unpack_from("<" + fmt, model.data, base + bi_attr.start + idx * bd * sz)[0]
        return [(bt[int(li)] if 0 <= int(li) < len(bt) else bt[0], 1.0)]
    if not smooth:
        return [(bt[0], 1.0)]
    bi_attr = sepd.attrs["boneIndices"]; bw_attr = sepd.attrs["boneWeights"]
    # read raw indices (no scale) + weights (scaled to sum 1)
    base, _ = model.vatr["boneIndices"]
    sz = _cmb.DT_SIZE[bi_attr.data_type]; fmt = "<%d%s" % (bd, _cmb.DT_FMT[bi_attr.data_type])
    idxs = struct.unpack_from(fmt, model.data, base + bi_attr.start + idx * bd * sz)
    wts = model.read_attr(bw_attr, "boneWeights", idx, bd)
    out = []
    for li, w in zip(idxs, wts):
        if w <= 0:
            continue
        out.append((bt[int(li)], float(w)))
    return out or [(bt[0], 1.0)]


def skinned_triangles(model: "_cmb.Cmb", csab: "Csab | None", frame: float):
    """Yield (sepd_index, material_index, [(pos,normal,uv) x3]) with CSAB skinning
    applied. Each vertex is taken to MODEL space exactly as cmb.triangles() does
    (rigid: ×bindWorld; smooth: raw), then transformed by the weighted blend of its
    bones' skinMatrix = animWorld·bindInverse. With csab=None this is byte-identical
    to cmb.triangles() (skin matrices are all identity)."""
    sm = skin_matrices(model, csab, frame)
    b = model.data
    for mesh in model.meshes:
        sepd = model.sepds[mesh.sepd_index]
        bd = sepd.bone_dimension
        has_normal = sepd.attrs["normal"].mode in (_cmb.MODE_ARRAY, _cmb.MODE_CONSTANT)
        for prms in sepd.prms:
            prm = prms.prms[0]
            bt = prms.bone_table or [0]
            smooth = bd > 1 and sepd.attrs["boneIndices"].mode == _cmb.MODE_ARRAY \
                            and sepd.attrs["boneWeights"].mode == _cmb.MODE_ARRAY
            # RIGID_SKINNING (skinning_mode==1): per-vertex single bone index into bone_table; the
            # vertex lives in that bone's local space. Binding all verts to bt[0] scatters the rig
            # (stalchild #109) -> resolve the bone + bind matrix per vertex below.
            bi = sepd.attrs["boneIndices"]
            rigid_pv = (not smooth) and prms.skinning_mode == 1 and bi.mode == _cmb.MODE_ARRAY and len(bt) > 1
            # rigid: bake to model space with the bound bone's bind matrix (as cmb.triangles)
            Mmodel = _cmb._mat_id() if smooth else model.bone_matrix.get(bt[0], _cmb._mat_id())
            isz = _cmb.DT_SIZE[prm.index_type]; ifmt = _cmb.DT_FMT[prm.index_type]
            ibase = model.idx_ptr + prm.first * isz
            idxs = struct.unpack_from("<%d%s" % (prm.count, ifmt), b, ibase)
            bibase, _ = model.vatr["boneIndices"]; bisz = _cmb.DT_SIZE[bi.data_type]; bifmt = _cmb.DT_FMT[bi.data_type]
            verts = []
            for idx in idxs:
                pos = model.read_attr(sepd.attrs["position"], "position", idx, 3)
                nrm = model.read_attr(sepd.attrs["normal"], "normal", idx, 3) if has_normal else (0, 0, 1)
                uv = model.read_attr(sepd.attrs["texCoord0"], "texCoord0", idx, 2)
                Mvert = Mmodel
                if rigid_pv:
                    li = struct.unpack_from("<" + bifmt, b, bibase + bi.start + idx * bd * bisz)[0]
                    gb = bt[int(li)] if 0 <= int(li) < len(bt) else bt[0]
                    Mvert = model.bone_matrix.get(gb, _cmb._mat_id())
                pos = _cmb._mat_apply_pos(Mvert, pos)
                nrm = _cmb._mat_apply_dir(Mvert, nrm)
                # weighted skin blend
                bones = _vertex_bones(model, sepd, prms, idx, bd, smooth, rigid_pv)
                px = py = pz = nx = ny = nz = 0.0
                for bid, w in bones:
                    S = sm.get(bid, _cmb._mat_id())
                    sp = _cmb._mat_apply_pos(S, pos); sn = _cmb._mat_apply_dir(S, nrm)
                    px += w * sp[0]; py += w * sp[1]; pz += w * sp[2]
                    nx += w * sn[0]; ny += w * sn[1]; nz += w * sn[2]
                verts.append(((px, py, pz), (nx, ny, nz), uv))
            for i in range(0, len(verts) - 2, 3):
                yield (mesh.sepd_index, mesh.material_index, verts[i:i + 3])


if __name__ == "__main__":
    import sys, os
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    from ctr_romfs import CtrRom
    from zar import Zar

    rom_path = os.environ.get("ZELDA3D_OOT3D_ROM")
    zar_path = sys.argv[1] if len(sys.argv) > 1 else "/actor/zelda_ge1.zar"
    cmb_name = sys.argv[2] if len(sys.argv) > 2 else "Model/geldwoman.cmb"
    anim_name = sys.argv[3] if len(sys.argv) > 3 else "Anim/ge1_s_wait.csab"
    if not rom_path:
        print("set ZELDA3D_OOT3D_ROM"); sys.exit(1)
    rom = CtrRom(rom_path)
    z = Zar(rom.read(rom.get(zar_path)))
    model = _cmb.Cmb(z.read([f for f in z.files if f.name == cmb_name][0]))
    csab = Csab(z.read([f for f in z.files if f.name == anim_name][0]))
    print(f"CSAB {anim_name}: subver={csab.subversion} duration={csab.duration} "
          f"bones={csab.bone_count} anods={len(csab.nodes)}")
    print(f"  boneToAnim={csab.bone_to_anim}")
    for n in csab.nodes:
        present = [k for k, v in n.tracks.items() if v]
        types = {k: csab_track_type_name(v.type) for k, v in n.tracks.items() if v}
        print(f"  anod bone{n.bone_index}: {present}  ({types})")

    # ---- VERIFY 1: rest pose (csab=None) skin matrices are identity ----
    sm = skin_matrices(model, None, 0.0)
    worst = 0.0
    for bid, S in sm.items():
        for i in range(4):
            for j in range(4):
                worst = max(worst, abs(S[i][j] - (1.0 if i == j else 0.0)))
    print(f"\n[verify] rest-pose skinMatrix max |dev from I| = {worst:.3e} "
          f"({'PASS' if worst < 1e-6 else 'FAIL'})")

    # ---- VERIFY 2: csab=None skinned tris == cmb.triangles() (bit-for-bit) ----
    a = list(model.triangles()); b = list(skinned_triangles(model, None, 0.0))
    na = sum(1 for _ in a); maxd = 0.0
    for (s0, m0, t0), (s1, m1, t1) in zip(a, b):
        for (_, p0, n0, u0), (p1, n1, u1) in zip(t0, t1):
            for c in range(3):
                maxd = max(maxd, abs(p0[c] - p1[c]), abs(n0[c] - n1[c]))
    # threshold ~1e-2 model units: rigid meshes are baked to model space by bindWorld
    # then skinned by bindWorld·inv(bindWorld), so the bind-inverse round-trip leaves
    # ~1e-8 *relative* float noise (≈1e-4 abs on coords up to ~6500). Not a bug.
    print(f"[verify] csab=None skinned vs bind-pose: {na} tris, max |Δpos/Δnrm| = "
          f"{maxd:.3e} ({'PASS' if maxd < 1e-2 else 'FAIL'})")

    # ---- VERIFY 3: an animated frame actually deforms ----
    def bbox(tris):
        xs = []; ys = []; zs = []
        for _, _, t in tris:
            for p, _, _ in t:
                xs.append(p[0]); ys.append(p[1]); zs.append(p[2])
        return (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))
    rest = list(skinned_triangles(model, None, 0.0))
    for fr in [0.0, csab.duration / 2.0, csab.duration - 1.0]:
        anim = list(skinned_triangles(model, csab, fr))
        d = 0.0
        for (_, _, t0), (_, _, t1) in zip(rest, anim):
            for (p0, _, _), (p1, _, _) in zip(t0, t1):
                d = max(d, sum((p0[c] - p1[c]) ** 2 for c in range(3)) ** 0.5)
        bb = bbox(anim)
        print(f"[frame {fr:5.1f}] max vert displacement vs rest = {d:8.2f}  "
              f"bbox y[{bb[2]:.0f},{bb[3]:.0f}]")

