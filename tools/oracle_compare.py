#!/usr/bin/env python3
"""oracle_compare.py — data-driven texture-decode oracle.

Decodes a CMB texture two ways from the SAME raw ROM bytes and diffs them:
  1. Azahar's own PICA decoder (the emulator ground truth), via the C++ tool
     Azahar/build/bin/Release/zelda3d_oracle (Pica::Texture::LookupTexture).
  2. The Zelda3D converter's decoder (tools/pica_texture.py).
A divergence means the converter mis-decodes that format (channel order, tiling,
ETC1, etc.) — caught numerically, not by eyeball. No emulator run needed.

Usage: oracle_compare.py <model.cmb> [tex_index]
"""
import sys, os, struct, subprocess, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmb import Cmb
import pica_texture

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ORACLE = os.path.join(REPO, "Azahar", "build", "bin", "Release", "zelda3d_oracle")

# CMB glFormat -> Pica::TexturingRegs::TextureFormat int (see zelda3d_oracle main.cpp)
GLFMT_TO_PICA = {
    0x0000675A: 12, 0x0000675B: 13,            # ETC1, ETC1A4
    0x14016752: 0, 0x14016754: 1,              # RGBA8, RGB8
    0x80346752: 2, 0x83636754: 3, 0x80336752: 4,  # RGB5A1, RGB565, RGBA4
    0x14016758: 5, 0x14016759: 6, 0x14016757: 7,  # IA8, RG8/HILO8, I8
    0x14016756: 8, 0x67606758: 9, 0x67616757: 10, 0x67616756: 11,  # A8, IA4, I4, A4
}


def read_ppm(path):
    with open(path, "rb") as f:
        assert f.readline().strip() == b"P6"
        w, h = (int(x) for x in f.readline().split())
        assert f.readline().strip() == b"255"
        return w, h, f.read()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    if not os.path.exists(ORACLE):
        sys.exit(f"oracle binary missing: {ORACLE}\n  build it: cmake --build Azahar/build --target zelda3d_oracle")
    cmb_path = sys.argv[1]
    ti = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    c = Cmb(open(cmb_path, "rb").read())
    tex = c.textures[ti]
    raw = c.data[c.texdata_ptr + tex.data_offset: c.texdata_ptr + tex.data_offset + tex.data_len]
    pica_fmt = GLFMT_TO_PICA.get(tex.gl_format)
    if pica_fmt is None:
        sys.exit(f"no PICA mapping for glFormat {hex(tex.gl_format)}")
    print(f"tex{ti}: {tex.width}x{tex.height} glFormat={hex(tex.gl_format)} -> pica fmt {pica_fmt}, {len(raw)} raw bytes")

    with tempfile.TemporaryDirectory() as td:
        rawpath = os.path.join(td, "raw.bin")
        open(rawpath, "wb").write(raw)
        azahar_ppm = os.path.join(td, "azahar.ppm")
        subprocess.run([ORACLE, rawpath, str(tex.width), str(tex.height), str(pica_fmt), azahar_ppm], check=True)
        aw, ah, abytes = read_ppm(azahar_ppm)

        conv_rgba = pica_texture.decode(tex.gl_format, tex.width, tex.height, raw)  # RGBA, row-major top-down
        # converter is RGBA top-down; azahar PPM is RGB top-down (main.cpp flips). Drop alpha.
        conv_rgb = bytes(conv_rgba[i] for i in range(len(conv_rgba)) if i % 4 != 3)

    n = min(len(abytes), len(conv_rgb))
    # try converter as-is and vertically flipped (in case of an origin mismatch)
    def diff(a, b):
        worst = 0; total = 0
        for i in range(n):
            d = abs(a[i] - b[i]); total += d
            if d > worst: worst = d
        return worst, total / n

    worst, mean = diff(abytes, conv_rgb)
    # flipped converter
    row = tex.width * 3
    flipped = b"".join(conv_rgb[(tex.height - 1 - y) * row:(tex.height - y) * row] for y in range(tex.height))
    fworst, fmean = diff(abytes, flipped)

    print(f"Azahar vs converter (top-down):  worst|Δ|={worst}  mean|Δ|={mean:.2f}")
    print(f"Azahar vs converter (V-flipped): worst|Δ|={fworst}  mean|Δ|={fmean:.2f}")
    # distribution of per-channel deltas in the better orientation
    best = conv_rgb if mean <= fmean else flipped
    buckets = {0: 0, 2: 0, 8: 0, 32: 0, 128: 0}
    for i in range(n):
        d = abs(abytes[i] - best[i])
        for t in (128, 32, 8, 2, 0):
            if d > t:
                buckets[t] += 1
                break
    print(f"  per-channel |Δ| histogram (of {n}): "
          f">128:{buckets[128]} 33-128:{buckets[32]} 9-32:{buckets[8]} 3-8:{buckets[2]} 0-2:{buckets[0]}")
    # which in-8x8-tile (x%8,y%8) positions differ? (pinpoints the ETC1 sub-case)
    pos = {}
    W = tex.width
    for p in range(n // 3):
        x, y = p % W, p // W
        d = max(abs(abytes[3 * p + k] - best[3 * p + k]) for k in range(3))
        if d > 8:
            pos[(x % 8, y % 8)] = pos.get((x % 8, y % 8), 0) + 1
    if pos:
        print("  differing pixels by (x%8,y%8):")
        for yy in range(8):
            print("   " + " ".join(f"{pos.get((xx, yy), 0):4d}" for xx in range(8)))
    if "--dump" in sys.argv:
        from PIL import Image
        sd = os.path.join(REPO, "scratch", "screenshots")
        os.makedirs(sd, exist_ok=True)
        best = conv_rgb if mean <= fmean else flipped
        Image.frombytes("RGB", (tex.width, tex.height), bytes(abytes[:n])).save(os.path.join(sd, "oracle_azahar.png"))
        Image.frombytes("RGB", (tex.width, tex.height), bytes(best[:n])).save(os.path.join(sd, "oracle_conv.png"))
        # amplified diff mask (5x) so small deltas are visible
        dmask = bytes(min(255, abs(abytes[i] - best[i]) * 5) for i in range(n))
        Image.frombytes("RGB", (tex.width, tex.height), dmask).save(os.path.join(sd, "oracle_diff.png"))
        print("dumped oracle_{azahar,conv,diff}.png to scratch/screenshots/")

    best_mean = min(mean, fmean)
    orient = "top-down" if mean <= fmean else "V-flipped"
    if best_mean < 1.0:
        print(f"MATCH ({orient}, mean|Δ|={best_mean:.2f}) — converter decode agrees with Azahar.")
    else:
        print(f"DIVERGENCE ({orient} best, mean|Δ|={best_mean:.2f}) — converter mis-decodes this format.")


if __name__ == "__main__":
    main()
