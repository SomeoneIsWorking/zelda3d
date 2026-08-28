#!/usr/bin/env python3
"""Decode a CMB material's packed TEV chain into readable per-stage expressions.

The renderer dumps each material's chain as triples of hex words (the `sgdump` /
FRAGDBG path prints `w0/w1/w2` per stage). Those words are unreadable by eye, and
reading them by eye is exactly how a wrong conclusion gets made about which stage
is at fault — so decode them here instead.

Packing is `Zelda3DGlGroup::tevStagePack` (documented in zelda3d_gl.h, produced by
`PackTevStage` in cmb3d/asset/cmb_glgroups.cpp):

    w0 = rgbSrc0 | rgbSrc1<<4 | rgbSrc2<<8 | aSrc0<<16 | aSrc1<<20 | aSrc2<<24
    w1 = rgbMod0 | rgbMod1<<4 | rgbMod2<<8 | aMod0<<12 | aMod1<<16 | aMod2<<20 | constIdx<<24
    w2 = rgbOp | aOp<<4 | log2(rgbScale)<<8 | log2(aScale)<<10

Usage:
  # decode one chain
  tev_decode.py "00e30e30/00000000/00000111, 00e1ff43/00000002/00000101"

  # diff two chains stage-by-stage (the useful mode: a broken material against a
  # known-good control that takes the same shader path)
  tev_decode.py --diff "<chainA>" "<chainB>" [--labels bracelet gauntlet]

Chains may be given with stages separated by commas and/or whitespace; the three
words of a stage separated by '/'. Both are what the renderer already prints, so a
dump line can be pasted in verbatim.
"""
import re
import sys

# PICA source codes -> name. From TevSrcCode(); the gaps (7..12) are unmapped and
# reaching one means the packer saw a GL enum it does not know, which is itself a
# finding rather than a value to silently render as "Primary".
SRC = {
    0: "Primary", 1: "FragPrimary", 2: "FragSecondary",
    3: "Tex0", 4: "Tex1", 5: "Tex2", 6: "Tex3",
    13: "PrevBuffer", 14: "Constant", 15: "Previous",
}
# TevColorModCode() -- what the stage takes FROM the source, in the RGB chain.
RGB_MOD = {
    0: "rgb", 1: "1-rgb", 2: "a", 3: "1-a",
    4: "r", 5: "1-r", 8: "g", 9: "1-g", 12: "b", 13: "1-b",
}
# TevAlphaModCode() -- same, alpha chain. Note the codes are a DIFFERENT numbering
# from the RGB ones (2 means SRC_R here and SRC_ALPHA there); decoding an alpha
# operand with the RGB table is a silent, plausible-looking error.
A_MOD = {0: "a", 1: "1-a", 2: "r", 3: "1-r", 4: "g", 5: "1-g", 6: "b", 7: "1-b"}
OP = {
    0: "Replace", 1: "Modulate", 2: "Add", 3: "AddSigned", 4: "Lerp",
    5: "Subtract", 6: "Dot3RGB", 7: "Dot3RGBA", 8: "MulThenAdd", 9: "AddThenMul",
}
# How many operands each op actually consumes. A stage's unused operand slots are
# whatever the exporter left there -- comparing them between two materials produces
# differences that mean nothing, which is the main way a chain diff misleads.
ARITY = {0: 1, 1: 2, 2: 2, 3: 2, 4: 3, 5: 2, 6: 2, 7: 2, 8: 3, 9: 3}


def _name(table, code):
    return table.get(code, "?%d" % code)


def operand(src, mod, mod_table):
    s = _name(SRC, src)
    m = _name(mod_table, mod)
    return s if m in ("rgb",) else "%s.%s" % (s, m)


def expr(op, ops):
    """Render the op applied to its operands the way PICA evaluates it."""
    if op == 0:
        return ops[0]
    if op == 1:
        return "%s * %s" % (ops[0], ops[1])
    if op == 2:
        return "%s + %s" % (ops[0], ops[1])
    if op == 3:
        return "%s + %s - 0.5" % (ops[0], ops[1])
    if op == 4:
        return "lerp(%s, %s, %s)" % (ops[0], ops[1], ops[2])
    if op == 5:
        return "%s - %s" % (ops[0], ops[1])
    if op in (6, 7):
        return "dot3(%s, %s)" % (ops[0], ops[1])
    if op == 8:
        return "%s * %s + %s" % (ops[0], ops[1], ops[2])
    if op == 9:
        return "(%s + %s) * %s" % (ops[0], ops[1], ops[2])
    return "?op%d(%s)" % (op, ", ".join(ops))


def decode_stage(w0, w1, w2):
    rgb_src = [(w0 >> (4 * i)) & 0xF for i in range(3)]
    a_src = [(w0 >> (16 + 4 * i)) & 0xF for i in range(3)]
    rgb_mod = [(w1 >> (4 * i)) & 0xF for i in range(3)]
    a_mod = [(w1 >> (12 + 4 * i)) & 0xF for i in range(3)]
    const_idx = (w1 >> 24) & 7
    rgb_op, a_op = w2 & 0xF, (w2 >> 4) & 0xF
    rgb_scale, a_scale = 1 << ((w2 >> 8) & 3), 1 << ((w2 >> 10) & 3)

    n_rgb, n_a = ARITY.get(rgb_op, 3), ARITY.get(a_op, 3)
    rgb_ops = [operand(rgb_src[i], rgb_mod[i], RGB_MOD) for i in range(n_rgb)]
    a_ops = [operand(a_src[i], a_mod[i], A_MOD) for i in range(n_a)]

    rgb = expr(rgb_op, rgb_ops)
    a = expr(a_op, a_ops)
    if rgb_scale != 1:
        rgb = "%d * (%s)" % (rgb_scale, rgb)
    if a_scale != 1:
        a = "%d * (%s)" % (a_scale, a)
    return {
        "rgb": rgb, "a": a, "constIdx": const_idx,
        "rgbOp": _name(OP, rgb_op), "aOp": _name(OP, a_op),
        "usesConst": 14 in rgb_src[:n_rgb] or 14 in a_src[:n_a],
        "words": (w0, w1, w2),
    }


def parse(text):
    """Pull every `hhhhhhhh/hhhhhhhh/hhhhhhhh` triple out of a dump line."""
    out = []
    for m in re.finditer(r"([0-9a-fA-F]{1,8})/([0-9a-fA-F]{1,8})/([0-9a-fA-F]{1,8})", text):
        out.append(tuple(int(g, 16) for g in m.groups()))
    if not out:
        sys.exit("no `w0/w1/w2` stage triples found in: %r" % text[:120])
    return out


def show(chain, label=None):
    if label:
        print("%s (%d stages)" % (label, len(chain)))
    for i, (w0, w1, w2) in enumerate(chain):
        d = decode_stage(w0, w1, w2)
        c = "  const%d" % d["constIdx"] if d["usesConst"] else ""
        print("  stage %d  rgb = %s" % (i, d["rgb"]))
        print("           a   = %s%s" % (d["a"], c))


def diff(a, b, labels):
    la, lb = labels
    n = max(len(a), len(b))
    if len(a) != len(b):
        print("STAGE COUNT DIFFERS: %s=%d, %s=%d -- the chains are not comparable "
              "stage-for-stage below that point.\n" % (la, len(a), lb, len(b)))
    same = 0
    for i in range(n):
        sa = decode_stage(*a[i]) if i < len(a) else None
        sb = decode_stage(*b[i]) if i < len(b) else None
        if sa and sb and sa["rgb"] == sb["rgb"] and sa["a"] == sb["a"] \
           and sa["constIdx"] == sb["constIdx"]:
            same += 1
            continue
        print("stage %d DIFFERS" % i)
        for lab, s in ((la, sa), (lb, sb)):
            if s is None:
                print("  %-10s (no such stage)" % lab)
                continue
            c = "  const%d" % s["constIdx"] if s["usesConst"] else ""
            print("  %-10s rgb = %s" % (lab, s["rgb"]))
            print("  %-10s a   = %s%s" % ("", s["a"], c))
        print()
    # Say so when nothing differs -- an empty diff and a diff that failed to run look
    # identical otherwise, and that ambiguity is how a null result gets read as agreement.
    if same == n:
        print("chains are IDENTICAL in decoded form (%d stages). If the two materials "
              "render differently, the cause is NOT the TEV chain." % n)
    else:
        print("%d/%d stages identical." % (same, n))


def main():
    args = [a for a in sys.argv[1:]]
    labels = ["A", "B"]
    if "--labels" in args:
        i = args.index("--labels")
        labels = args[i + 1:i + 3]
        del args[i:i + 3]
    if "--diff" in args:
        args.remove("--diff")
        if len(args) != 2:
            sys.exit("--diff needs exactly two chains")
        diff(parse(args[0]), parse(args[1]), labels)
        return
    if len(args) != 1:
        sys.exit(__doc__)
    show(parse(args[0]))


if __name__ == "__main__":
    main()
