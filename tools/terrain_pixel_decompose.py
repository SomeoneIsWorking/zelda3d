#!/usr/bin/env python3
"""terrain_pixel_decompose.py — analytic single-pixel decomposition of the title terrain shading.

For chosen screen pixels of the pixel-aligned az=500/soh=908 title pair, raycasts the actual
spot99 room-0 geometry from the live (harness-read, byte-matched between engines) camera pose,
recovers the exact ROM texel, baked vertex color and live ambient behind each pixel, computes
the decomp-proven formula
    expected = saturate(2 * texel * bakedVertexColor * sceneAmbient)        [title_env_lighting.md]
and prints it against BOTH rendered panes (SoH pack-off PPM and Az PPM). This decides which
side deviates from the formula and by what factor (terrain-2x residual, debug_journal/
2026-07-10-title-terrain-uboverify-and-followups.md).

Inputs (baked below, all captured live 2026-07-10 at az=500/soh=908):
  camera: eye=(3846.26,-95.90,7235.25) at=(3871.48,-132.13,7139.66) up=(0.09,0.94,-0.33)
          fov=45.40 deg (harness `compare camera`; |Δeye| Az-vs-SoH < 0.01)
  ambient=(43,63,116)/255, fogNear=996 (harness `compare lighting`; both engines identical)

Usage: tools/terrain_pixel_decompose.py [--pixels x,y ...]
Requires scratch/fireglow/spot99_room0.cmb and scratch/title_ab/texpackoff500.{az,soh}.ppm.
"""
from __future__ import annotations

import argparse
import math
import struct
import sys
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
from cmb import Cmb, DT_SIZE, DT_FMT  # noqa: E402
from pica_texture import decode_cmb_texture  # noqa: E402

EYE = np.array([3846.26, -95.90, 7235.25])
AT = np.array([3871.48, -132.13, 7139.66])
UP = np.array([0.09, 0.94, -0.33])
FOV_DEG = 45.40
W, H = 400, 240
AMBIENT = np.array([43, 63, 116]) / 255.0


def read_ppm(path: Path) -> np.ndarray:
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        line = f.readline()
        while line.startswith(b"#"):
            line = f.readline()
        w, h = map(int, line.split())
        assert int(f.readline()) == 255
        return np.frombuffer(f.read(w * h * 3), dtype=np.uint8).reshape(h, w, 3)


def load_room(cmb_path: Path):
    """Return (tris, texs): tris = list of (P[3,3], UV[3,2], C[3,3], tex_idx); texs decoded."""
    c = Cmb(open(cmb_path, "rb").read())
    texs = []
    for t in c.textures:
        w, h, rgba = decode_cmb_texture(c, t)
        texs.append(np.frombuffer(bytes(rgba), dtype=np.uint8).reshape(h, w, 4).astype(np.float64) / 255.0)
    b = c.data
    P, UV, C, TI = [], [], [], []
    for mesh in c.meshes:
        sepd = c.sepds[mesh.sepd_index]
        tex_idx = c.material_texture(mesh.material_index)
        pa, ua, ca = sepd.attrs["position"], sepd.attrs["texCoord0"], sepd.attrs["color"]
        for prms in sepd.prms:
            prm = prms.prms[0]
            isz = DT_SIZE[prm.index_type]
            ibase = c.idx_ptr + prm.first * isz
            idxs = [struct.unpack_from("<" + DT_FMT[prm.index_type], b, ibase + k * isz)[0]
                    for k in range(prm.count)]
            for k in range(0, len(idxs) - 2, 3):
                tri = idxs[k:k + 3]
                P.append([c.read_attr(pa, "position", i, 3) for i in tri])
                UV.append([c.read_attr(ua, "texCoord0", i, 2) for i in tri])
                C.append([c.read_attr(ca, "color", i, 4)[:3] for i in tri])
                TI.append(tex_idx)
    return (np.array(P), np.array(UV), np.array(C), np.array(TI)), texs


def ray_dir(px: float, py: float) -> np.ndarray:
    f = AT - EYE
    f = f / np.linalg.norm(f)
    r = np.cross(f, UP)
    r = r / np.linalg.norm(r)
    u = np.cross(r, f)
    tan_y = math.tan(math.radians(FOV_DEG) / 2.0)
    tan_x = tan_y * W / H
    x = (px + 0.5) / W * 2.0 - 1.0
    y = 1.0 - (py + 0.5) / H * 2.0
    d = f + x * tan_x * r + y * tan_y * u
    return d / np.linalg.norm(d)


def raycast(tris, d: np.ndarray):
    """Vectorized Moller-Trumbore over all triangles; returns nearest hit info or None."""
    P, UV, C, TI = tris
    v0, v1, v2 = P[:, 0], P[:, 1], P[:, 2]
    e1, e2 = v1 - v0, v2 - v0
    pvec = np.cross(d, e2)
    det = np.einsum("ij,ij->i", e1, pvec)
    ok = np.abs(det) > 1e-9
    inv = np.where(ok, 1.0 / np.where(ok, det, 1.0), 0.0)
    tvec = EYE - v0
    u = np.einsum("ij,ij->i", tvec, pvec) * inv
    qvec = np.cross(tvec, e1)
    v = np.einsum("j,ij->i", d, qvec) * inv
    t = np.einsum("ij,ij->i", e2, qvec) * inv
    hit = ok & (u >= 0) & (v >= 0) & (u + v <= 1) & (t > 1.0)
    if not hit.any():
        return None
    i = np.where(hit)[0][np.argmin(t[hit])]
    w0, w1, w2 = 1.0 - u[i] - v[i], u[i], v[i]
    uv = w0 * UV[i, 0] + w1 * UV[i, 1] + w2 * UV[i, 2]
    col = w0 * C[i, 0] + w1 * C[i, 1] + w2 * C[i, 2]
    return {"t": t[i], "tri": int(i), "uv": uv, "vcol": col, "tex": int(TI[i])}


def sample_tex(tex: np.ndarray, uv, bilinear=True):
    h, w, _ = tex.shape
    # CMB UV space: V=0 at bottom; decoded texture row 0 = top (same convention the renderer
    # flips with `1 - v`). Wrap = repeat.
    fu = (uv[0] % 1.0) * w - 0.5
    fv = ((1.0 - (uv[1] % 1.0)) % 1.0) * h - 0.5
    if not bilinear:
        return tex[int(round(fv)) % h, int(round(fu)) % w, :3]
    x0, y0 = int(math.floor(fu)), int(math.floor(fv))
    ax, ay = fu - x0, fv - y0
    p = lambda x, y: tex[y % h, x % w, :3]
    return ((1 - ax) * (1 - ay) * p(x0, y0) + ax * (1 - ay) * p(x0 + 1, y0)
            + (1 - ax) * ay * p(x0, y0 + 1) + ax * ay * p(x0 + 1, y0 + 1))


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--pixels", nargs="+", default=["100,210", "200,220", "320,205"],
                    help="screen pixels x,y (400x240)")
    args = ap.parse_args(argv)

    tris, texs = load_room(REPO / "scratch/fireglow/spot99_room0.cmb")
    az = read_ppm(REPO / "scratch/title_ab/texpackoff500.az.ppm").astype(np.float64)
    soh = read_ppm(REPO / "scratch/title_ab/texpackoff500.soh.ppm").astype(np.float64)

    print("pixel      | dist  tex        texel(RGB)      vcol(RGB)       ambient        "
          "| expected=sat(2tva) | SoH px        | Az px         | SoH/exp  Az/exp")
    for spec in args.pixels:
        px, py = map(int, spec.split(","))
        hit = raycast(tris, ray_dir(px, py))
        if hit is None:
            print(f"({px:3d},{py:3d}) | NO HIT")
            continue
        t = sample_tex(texs[hit["tex"]], hit["uv"])
        v = np.array(hit["vcol"])
        exp = np.clip(2.0 * t * v * AMBIENT, 0, 1) * 255.0
        s = soh[py, px]
        a = az[py, px]
        rs = (s[1] / exp[1]) if exp[1] > 0.5 else float("nan")
        ra = (a[1] / exp[1]) if exp[1] > 0.5 else float("nan")
        print(f"({px:3d},{py:3d}) | {hit['t']:5.0f} tex{hit['tex']:>2} "
              f"({t[0]*255:5.1f},{t[1]*255:5.1f},{t[2]*255:5.1f}) "
              f"({v[0]:.3f},{v[1]:.3f},{v[2]:.3f}) "
              f"({AMBIENT[0]:.3f},{AMBIENT[1]:.3f},{AMBIENT[2]:.3f}) "
              f"| ({exp[0]:5.1f},{exp[1]:5.1f},{exp[2]:5.1f}) "
              f"| ({s[0]:3.0f},{s[1]:3.0f},{s[2]:3.0f}) | ({a[0]:3.0f},{a[1]:3.0f},{a[2]:3.0f}) "
              f"| G {rs:5.2f}   G {ra:5.2f}")


if __name__ == "__main__":
    main(sys.argv[1:])
