#!/usr/bin/env python3
"""Whole-game audit of OoT3D room geometry: parse + texture-decode EVERY room CMB.

Iterates all `/scene/<name>_<R>_info.zsi` room files in the ROM, extracts the embedded
room CMB (via the ZSI parser), parses it, and decodes every texture — reporting any
parse failures, undecodable texture formats, and the format histogram. This is the
cheap (~seconds) exhaustive check that scopes "does the scene pipeline cover the whole
game" BEFORE launching the game hundreds of times.

NOTE: cmb.py (this oracle) is stricter than the C++ runtime loader on a couple of tiny
degenerate boss-arena CMBs (~1792 bytes, 12-tri placeholders) — the C++ Cmb parses them
fine and the runtime never crashes on them. Treat a handful of cmb.py parse-throws on
those as oracle-only, cross-checked with scratch/bin/zsi_test.

Run: ZELDA3D_OOT3D_ROM=<path.3ds> python3 tools/scene_sweep.py
"""
import os, sys, re
sys.path.insert(0, os.path.dirname(__file__))
from ctr_romfs import CtrRom
import zsi as zsimod, cmb as cmbmod, pica_texture as pt


def main():
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    rooms = sorted({p for f in rom.iter_files()
                    for p in [f if isinstance(f, str) else getattr(f, "path", str(f))]
                    if re.match(r"/scene/[a-zA-Z0-9_]+_\d+_info\.zsi$", p)})
    nparse = ntex = 0
    pfail, tfail = [], []
    fmts, unk = {}, {}
    for p in rooms:
        try:
            z = zsimod.Zsi(rom.read(rom.get(p)))
            if not z.has_geometry():
                pfail.append((p, "no-geometry"))
                continue
            m = cmbmod.Cmb(z.cmb_bytes())
            list(m.triangles())
            nparse += 1
        except Exception as e:
            pfail.append((p, repr(e)))
            continue
        ok = True
        for t in m.textures:
            fmts[t.gl_format] = fmts.get(t.gl_format, 0) + 1
            if t.gl_format not in pt.GLFMT:
                unk[t.gl_format] = unk.get(t.gl_format, 0) + 1
            try:
                w, h, px = pt.decode_cmb_texture(m, t)
                assert len(px) == w * h * 4
            except Exception as e:
                tfail.append((p, t.name, hex(t.gl_format), repr(e)))
                ok = False
        if ok:
            ntex += 1
    print(f"room files: {len(rooms)}  cmb-parse-ok: {nparse}  all-tex-decode-ok: {ntex}")
    print("texture formats:", {pt.GLFMT.get(k, hex(k)): v for k, v in sorted(fmts.items())})
    print("UNKNOWN formats:", {hex(k): v for k, v in unk.items()} or "(none)")
    print(f"PARSE FAILS: {len(pfail)}")
    for p, why in pfail:
        print("  P", p, why)
    print(f"TEX-DECODE FAILS: {len(tfail)}")
    for row in tfail:
        print("  T", *row)


if __name__ == "__main__":
    main()
