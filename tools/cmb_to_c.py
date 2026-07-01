#!/usr/bin/env python3
"""cmb_to_c.py — convert an OoT3D CMB model into a self-contained N64/F3DEX2
display list + full-res RGBA32 textures, emitted as a C source + header that
Ship of Harkinian compiles directly.

This is Zelda3D approach "A done right": rather than feeding textures through the
modelled 4 KB TMEM, we emit manual load commands with the *real* pixel width.
libultraship's interpreter (GfxDpLoadBlock / ImportTextureRgba32) reads the
texture straight from the source pointer at full resolution — exactly the path
SoH's HD texture packs use — so there is no 4 KB limit. The actor's world matrix
is already on the RSP stack when the dlist runs, so the mesh lands at the correct
in-world transform for free.

Multi-material: a real model has several meshes, each bound to a material, each
material bound to its own texture (+ wrap modes + UV coordinator). We group
triangles by material, and the dlist re-binds the texture/combiner before each
group's geometry. Geometry is batched into <=10 triangles per gSPVertex load
(<=30 of the 32 vtx slots) so it works regardless of mesh size.

Usage: cmb_to_c.py <model.cmb> <out_basename> [--scale F] [--lit] [--no-flip-v]
Emits  <out_basename>.c and <out_basename>.h.
"""
import sys, os, struct, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmb import Cmb, WRAP_REPEAT, WRAP_CLAMP, WRAP_CLAMP_EDGE, WRAP_MIRROR
import pica_texture

def clamp_s16(v):
    v = int(round(v))
    return -32768 if v < -32768 else 32767 if v > 32767 else v

def pack_n8(nx, ny, nz):
    """Normalize a normal and pack each component as an s8 byte (two's complement),
    the layout F3DEX2 reads from the Vtx color slot under G_LIGHTING."""
    l = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
    def b(v):
        i = int(round(max(-1.0, min(1.0, v / l)) * 127.0))
        return i & 0xFF
    return b(nx), b(ny), b(nz)

def gbi_wrap(gl_wrap):
    """Map a GL wrap enum to the F3DEX2 G_TX_* clamp/mirror flag token."""
    if gl_wrap in (WRAP_CLAMP, WRAP_CLAMP_EDGE): return "G_TX_CLAMP"
    if gl_wrap == WRAP_MIRROR: return "G_TX_MIRROR"
    return "G_TX_WRAP"   # REPEAT (default)

def main():
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(1)
    cmb_path, out_base = sys.argv[1], sys.argv[2]
    scale = 1.0
    flip_v = True
    lit = False  # default unlit: full-bright texture (3DS look). --lit uses N64
                 # per-vertex lighting from CMB normals (bands on low-poly meshes
                 # in low-ambient scenes; kept for when smoother models warrant it).
    solid = None  # diagnostic: (r,g,b) -> replace ALL texture data with this solid
                  # colour (keeps dims/dlist). Renders the model flat in that colour
                  # iff the texture-upload path is sound — isolates upload/binding
                  # bugs from texture-decode/data bugs.
    rot = [0.0, 0.0, 0.0]  # bake a rotation (deg) about model X/Y/Z into the verts
    ground = False         # after rotation, translate so min-Y == 0 (stand on origin)
    args = sys.argv[3:]
    i = 0
    while i < len(args):
        a = args[i]
        if a == "--scale": scale = float(args[i+1]); i += 2
        elif a == "--no-flip-v": flip_v = False; i += 1
        elif a == "--lit": lit = True; i += 1
        elif a == "--solid": solid = tuple(int(x) for x in args[i+1].split(",")); i += 2
        elif a == "--rotx": rot[0] = float(args[i+1]); i += 2
        elif a == "--roty": rot[1] = float(args[i+1]); i += 2
        elif a == "--rotz": rot[2] = float(args[i+1]); i += 2
        elif a == "--ground": ground = True; i += 1
        else: print("unknown arg", a); sys.exit(1)

    c = Cmb(open(cmb_path, "rb").read())
    ident = os.path.basename(out_base).replace("-", "_")

    # --- group triangles by material; collect the materials actually drawn ---
    # groups[mat_index] = list of faces; each face = [vtx, vtx, vtx]
    # vtx (pre-scaled to a given texture's s,t happens later, since s,t depend on
    # the bound texture dims + UV coordinator) so store raw pos/normal/uv here.
    groups = {}        # mat_index -> list of (pos, nrm, uv) faces
    mat_order = []     # materials in first-seen draw order
    for sepd_i, mat_i, tri in c.triangles():
        if mat_i not in groups:
            groups[mat_i] = []
            mat_order.append(mat_i)
        face = []
        for idx, pos, nrm, uv in tri:
            face.append((pos, nrm, uv))
        groups[mat_i].append(face)

    # --- bake an orientation (proper rotation, no mirror) into the geometry ---
    # OoT3D character rest meshes are Y-up but HEAD-DOWN in model space; the in-game
    # Zelda3D_DrawModel only does Translate*RotateY*Scale, so the model must already be
    # upright + standing at the origin in actor-local space (like the props). --rotx
    # 180 flips head-up; --ground drops the feet to y=0. Rotations compose Rz*Ry*Rx.
    if any(rot) or ground:
        import math
        rx, ry, rz = (math.radians(d) for d in rot)
        def matmul(A, B):
            return [[sum(A[r][k]*B[k][c] for k in range(3)) for c in range(3)] for r in range(3)]
        Rx = [[1,0,0],[0,math.cos(rx),-math.sin(rx)],[0,math.sin(rx),math.cos(rx)]]
        Ry = [[math.cos(ry),0,math.sin(ry)],[0,1,0],[-math.sin(ry),0,math.cos(ry)]]
        Rz = [[math.cos(rz),-math.sin(rz),0],[math.sin(rz),math.cos(rz),0],[0,0,1]]
        R = matmul(Rz, matmul(Ry, Rx))
        def apply(v):
            return tuple(R[r][0]*v[0] + R[r][1]*v[1] + R[r][2]*v[2] for r in range(3))
        miny = 1e30
        for faces in groups.values():
            for fi, face in enumerate(faces):
                nf = []
                for pos, nrm, uv in face:
                    p = apply(pos); n = apply(nrm)
                    miny = min(miny, p[1])
                    nf.append((p, n, uv))
                faces[fi] = nf
        if ground and miny < 1e29:
            for faces in groups.values():
                for fi, face in enumerate(faces):
                    faces[fi] = [((p[0], p[1]-miny, p[2]), n, uv) for p, n, uv in face]
        print(f"  oriented: rot={rot} ground={ground} (min-Y was {miny:.0f})")

    # --- decode every texture referenced by a drawn material (deduped) ---
    tex_used = {}      # tex_index -> (TW, TH, rgba bytes)
    for mat_i in mat_order:
        ti = c.material_texture(mat_i)
        if ti not in tex_used:
            tex = c.textures[ti]
            tdata = c.data[c.texdata_ptr + tex.data_offset :
                           c.texdata_ptr + tex.data_offset + tex.data_len]
            rgba = pica_texture.decode(tex.gl_format, tex.width, tex.height, tdata)
            assert len(rgba) == tex.width * tex.height * 4, \
                f"tex{ti} decoded {len(rgba)}, expected {tex.width*tex.height*4}"
            if solid is not None:
                rgba = bytearray((solid[0], solid[1], solid[2], 255)) * (tex.width * tex.height)
            tex_used[ti] = (tex.width, tex.height, rgba)

    # --- build vertex array + per-material batch plan ---
    # One flat Vtx[] across all materials; each material's batches index a
    # contiguous slice. s,t are baked per-material using that material's texture
    # dims and UV coordinator (scale/translate).
    BATCH = 10  # <=10 tris -> <=30 vtx slots (cache holds 32)
    all_vtx = []          # list of (x,y,z,s,t,nx,ny,nz)
    mat_plan = []         # list of dicts: {mat, tex, batches:[(vbase,ntri)], ...}
    for mat_i in mat_order:
        mat = c.materials[mat_i] if mat_i < len(c.materials) else None
        ti = c.material_texture(mat_i)
        TW, TH, _ = tex_used[ti]
        sS = mat.scale_s if mat else 1.0
        sT = mat.scale_t if mat else 1.0
        tS = mat.trans_s if mat else 0.0
        tT = mat.trans_t if mat else 0.0
        if mat and abs(mat.rot) > 1e-4:
            print(f"  WARN: mat{mat_i} has UV rotation {mat.rot:.3f} (not applied)")
        faces = groups[mat_i]
        batches = []
        for bstart in range(0, len(faces), BATCH):
            chunk = faces[bstart:bstart+BATCH]
            vbase = len(all_vtx)
            for face in chunk:
                for pos, nrm, uv in face:
                    x = clamp_s16(pos[0] * scale)
                    y = clamp_s16(pos[1] * scale)
                    z = clamp_s16(pos[2] * scale)
                    u = uv[0] * sS + tS
                    v = uv[1] * sT + tT
                    if flip_v: v = 1.0 - v
                    s = clamp_s16(u * TW * 32.0)
                    t = clamp_s16(v * TH * 32.0)
                    nx, ny, nz = pack_n8(nrm[0], nrm[1], nrm[2])
                    all_vtx.append((x, y, z, s, t, nx, ny, nz))
            batches.append((vbase, len(chunk)))
        mat_plan.append({"mat": mat_i, "tex": ti, "TW": TW, "TH": TH,
                         "wrap_s": gbi_wrap(mat.wrap_s) if mat else "G_TX_WRAP",
                         "wrap_t": gbi_wrap(mat.wrap_t) if mat else "G_TX_WRAP",
                         "alpha": bool(mat.alpha_test) if mat else False,
                         "batches": batches})

    n_tris = len(all_vtx) // 3

    h = []
    h.append(f"// Generated by tools/cmb_to_c.py from {os.path.basename(cmb_path)}")
    h.append(f"// model '{c.name}' v{c.version}: {n_tris} tris, "
             f"{len(mat_order)} materials, {len(tex_used)} textures")
    h.append("#ifndef ZELDA3D_%s_H" % ident.upper())
    h.append("#define ZELDA3D_%s_H" % ident.upper())
    h.append("#include <libultraship/libultra/gbi.h>")
    h.append(f"extern Gfx {ident}_dl[];")
    h.append("#endif")

    s = []
    s.append(f"// Generated by tools/cmb_to_c.py — do not edit by hand.")
    s.append(f'#include "{ident}.h"')
    s.append("")
    # textures (RGBA32), one array per used texture index
    for ti in sorted(tex_used):
        TW, TH, rgba = tex_used[ti]
        s.append(f"static const u8 {ident}_tex{ti}[{TW*TH*4}] __attribute__((aligned(8))) = {{")
        line = "    "
        for bi, byte in enumerate(rgba):
            line += "%d," % byte
            if (bi & 15) == 15:
                s.append(line); line = "    "
        if line.strip(): s.append(line)
        s.append("};")
        s.append("")
    # vertices: one flat array
    s.append(f"static const Vtx {ident}_vtx[] = {{")
    for (x, y, z, sc, tc, nx, ny, nz) in all_vtx:
        # cn[4]: packed s8 normal for G_LIGHTING (--lit), else white color
        # (unlit -> shade = white -> texture shown at full brightness).
        cn = f"{nx},{ny},{nz},255" if lit else "255,255,255,255"
        s.append(f"    {{{{ {{{x},{y},{z}}}, 0, {{{sc},{tc}}}, {{{cn}}} }}}},")
    s.append("};")
    s.append("")
    # display list
    s.append(f"Gfx {ident}_dl[] = {{")
    s.append("    gsDPPipeSync(),")
    if not lit:
        # Unlit: drop scene lighting/fog so the white vertex color is the shade
        # (full-bright texture) instead of being read as a normal.
        s.append("    gsSPClearGeometryMode(G_LIGHTING | G_FOG),")
    # Force a plain opaque, z-buffered render mode. Clearing the G_FOG *geometry*
    # bit stops the RSP computing per-vertex fog, but the RDP *blender* set by the
    # caller's SetupDL can still blend the framebuffer toward the fog colour — in a
    # foggy scene (e.g. Kakariko at night) that paints the whole model the fog
    # colour (looked solid black/blue) regardless of the texture/combiner output.
    # An explicit OPA_SURF blender removes the fog cycle. (The pot only escaped
    # this because Deku Tree's fog setup didn't tint.)
    s.append("    gsDPSetRenderMode(G_RM_AA_ZB_OPA_SURF, G_RM_AA_ZB_OPA_SURF2),")
    s.append("    gsSPTexture(0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON),")
    # Unlit colour comes from the PRIMITIVE register, not vertex SHADE, so the
    # caller can apply a flat per-draw scene tint (G_CC_*_PRIM = TEXEL0 * PRIM).
    # Zelda3D_DrawModel sets gDPSetPrimColor to the live scene light colour before
    # the dlist runs; we deliberately do NOT emit a prim colour here (it would
    # override the caller's). --lit keeps SHADE-based N64 per-vertex lighting.
    cc = "G_CC_MODULATERGBA" if lit else "G_CC_MODULATERGBA_PRIM"
    for plan in mat_plan:
        ti, TW, TH = plan["tex"], plan["TW"], plan["TH"]
        masks = (TW.bit_length() - 1)
        maskt = (TH.bit_length() - 1)
        line_words = (TW * 4) >> 3  # 64-bit words per row, RGBA32
        s.append(f"    // material {plan['mat']} -> tex{ti} ({TW}x{TH}), {len(plan['batches'])} batch(es)")
        s.append("    gsDPPipeSync(),")
        if plan["alpha"]:
            s.append(f"    gsDPSetCombineMode({cc}, {cc}),")
            s.append("    gsDPSetRenderMode(G_RM_AA_ZB_TEX_EDGE, G_RM_AA_ZB_TEX_EDGE2),")
        else:
            s.append(f"    gsDPSetCombineMode({cc}, {cc}),")
        s.append(f"    gsDPSetTextureImage(G_IM_FMT_RGBA, G_IM_SIZ_32b, {TW}, {ident}_tex{ti}),")
        s.append("    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_32b, 0, 0, G_TX_LOADTILE, 0,")
        s.append("                G_TX_WRAP, 0, 0, G_TX_WRAP, 0, 0),")
        # gsDPLoadBlock encodes lrs (texel count - 1) in a 12-bit field, so it
        # SILENTLY TRUNCATES for textures with > 4096 texels (e.g. 128x128) — the
        # RDP then computes a wrong size_bytes and the texture loads garbled/partial
        # (verified: 128x128 sampled stale TMEM, not our data). gsDPLoadBlockWide
        # carries lrs in the full w1 word, so use it past the 12-bit limit.
        if TW * TH - 1 > 0xFFF:
            s.append(f"    gsDPLoadBlockWide(G_TX_LOADTILE, 0, 0, {TW*TH-1}, 0),")
        else:
            s.append(f"    gsDPLoadBlock(G_TX_LOADTILE, 0, 0, {TW*TH-1}, 0),")
        s.append(f"    gsDPSetTile(G_IM_FMT_RGBA, G_IM_SIZ_32b, {line_words}, 0, G_TX_RENDERTILE, 0,")
        s.append(f"                {plan['wrap_t']}, {maskt}, 0, {plan['wrap_s']}, {masks}, 0),")
        s.append(f"    gsDPSetTileSize(G_TX_RENDERTILE, 0, 0, {(TW-1)<<2}, {(TH-1)<<2}),")
        for vbase, ntri in plan["batches"]:
            s.append(f"    gsSPVertex(&{ident}_vtx[{vbase}], {ntri*3}, 0),")
            for ti2 in range(ntri):
                a = ti2*3
                s.append(f"    gsSP1Triangle({a}, {a+1}, {a+2}, 0),")
    s.append("    gsSPEndDisplayList(),")
    s.append("};")
    s.append("")

    with open(out_base + ".h", "w") as f: f.write("\n".join(h) + "\n")
    with open(out_base + ".c", "w") as f: f.write("\n".join(s) + "\n")
    print(f"wrote {out_base}.c / .h: {n_tris} tris, {len(mat_order)} materials, "
          f"{len(tex_used)} textures "
          f"({', '.join('tex%d=%dx%d'%(t,tex_used[t][0],tex_used[t][1]) for t in sorted(tex_used))})")

if __name__ == "__main__":
    main()
