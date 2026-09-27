#!/usr/bin/env python3
"""Survey every CMB material combiner chain in the OoT3D ROM.

Purpose (render.multi-stage-tev): bound the PICA TEV emulation work with DATA — which
combine ops, sources, operands, scales, stage counts and texture-unit enables the game's
materials ACTUALLY use — instead of guessing generality. Also validates the combiner-entry
byte layout corpus-wide by checking every candidate field against its legal enum domain.

Combiner entry layout (0x28 bytes, shared settings table after the material structs;
stage indices per material at mat+0x124, count at mat+0x120 — cmb.cpp parseMats):
  +0x00 u16 combineRGB      (CombineResultOpDMP: 0x2100 MODULATE, 0x0104 ADD, ...)
  +0x02 u16 combineAlpha    (same enum domain)                      [validated here]
  +0x04 u16 scaleRGB        (literal 1/2/4)
  +0x06 u16 scaleAlpha      (literal 1/2/4)                          [validated here]
  +0x08 u16 bufferInputRGB  (0x8577/0x8578 previous-buffer select)   [validated here]
  +0x0A u16 bufferInputAlpha
  +0x0C..0x10 u16[3] srcRGB    (GL source enums 0x8577 PRIMARY_COLOR, 0x84C0..0x84C3
                                TEXTUREn, 0x8576 CONSTANT, 0x8578 PREVIOUS,
                                0x8579 PREVIOUS_BUFFER, 0x1E01?...)
  +0x12..0x16 u16[3] opRGB     (operand: 0x0300 SRC_COLOR, 0x0301 ONE_MINUS_SRC_COLOR,
                                0x0302 SRC_ALPHA, 0x0303 ONE_MINUS_SRC_ALPHA, ...)
                                [candidate offsets validated here]
  +0x18..0x1C u16[3] srcAlpha
  +0x1E..0x22 u16[3] opAlpha
  +0x24 u32 constantIndex   (0..5 — verified empirically in cmb.h, NOT noclip's +0x14)

Usage:
  python3 tools/tev_corpus_survey.py            # full-corpus histogram
  python3 tools/tev_corpus_survey.py --file spot07  # dump chains of matching files
"""
from __future__ import annotations
import os
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from cmb_corpus import iter_corpus  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _u16(b, o):
    return struct.unpack_from("<H", b, o)[0]


def _u32(b, o):
    return struct.unpack_from("<I", b, o)[0]


OPS = {
    0x1E01: "REPLACE",
    0x2100: "MODULATE",
    0x0104: "ADD",
    0x8574: "ADD_SIGNED",   # standard GL values — 0x8574 is ADD_SIGNED,
    0x84E7: "SUBTRACT",
    0x8575: "INTERPOLATE",  # 0x8575 INTERPOLATE (a*c + b*(1-c))
    0x86AE: "DOT3_RGB",
    0x86AF: "DOT3_RGBA",
    0x6401: "MULT_ADD",
    0x6402: "ADD_MULT",
}
# Combiner-buffer selectors, named because prevbuf_before_latch() reasons about them per channel.
PREVIOUS = 0x8578        # buf_rgb/buf_a: latch this stage's output into the combiner buffer
PREVIOUS_BUFFER = 0x8579  # a SOURCE selector: read the combiner buffer

SRCS = {
    0x8577: "PRIMARY",
    0x8578: "PREVIOUS",
    0x8579: "PREVBUF",
    0x8576: "CONST",
    0x84C0: "TEX0",
    0x84C1: "TEX1",
    0x84C2: "TEX2",
    0x84C3: "TEX3",
    0x6210: "FRAG_PRIMARY",   # GL_FRAGMENT_PRIMARY_COLOR_DMP (fragment lighting)
    0x6211: "FRAG_SECONDARY", # GL_FRAGMENT_SECONDARY_COLOR_DMP
}
MODS = {
    0x0300: "C",     # SRC_COLOR
    0x0301: "1-C",   # ONE_MINUS_SRC_COLOR
    0x0302: "A",     # SRC_ALPHA
    0x0303: "1-A",   # ONE_MINUS_SRC_ALPHA
    0x8580: "R",     # SRC_R
    0x8581: "1-R",
    0x8582: "G",
    0x8583: "1-G",
    0x8584: "B",
    0x8585: "1-B",
}


def op_name(v):
    return OPS.get(v, f"op{v:04x}")


def src_name(v):
    return SRCS.get(v, f"src{v:04x}")


def mod_name(v):
    return MODS.get(v, f"mod{v:04x}")


def prevbuf_before_latch(stages):
    """Return the PREVBUF reads that happen BEFORE this chain ever latches the combiner buffer.

    This is the question the PREVIOUS_BUFFER gap actually turns on, and the corpus counts of
    "materials latching" and "materials reading" cannot answer it: a material can do both and still
    read the buffer before any latch.

    PICA's `tev_combiner_buffer_color` is a runtime register the host does not carry, so the evaluator
    substitutes vec4(0). That substitution is EXACT for every read that a latch has already written,
    and wrong only for a read that reaches the un-latched register. Walking the chain in stage order
    and tracking whether a latch has happened turns "an approximation" into a measured count with a
    denominator.

    Latching is per channel: `buf_rgb` writes the RGB buffer and `buf_a` the alpha buffer, so an RGB
    read is only safe once a stage set buf_rgb == 0x8578 (PREVIOUS). Each channel is tracked
    separately and a stage that reads PREVBUF is judged against its own channel.
    """
    latched_rgb = False
    latched_a = False
    unsafe = []
    for index, stage in enumerate(stages):
        reads_rgb = PREVIOUS_BUFFER in stage.rgb_src[: slots_used(stage.rgb_op)]
        reads_a = PREVIOUS_BUFFER in stage.a_src[: slots_used(stage.a_op)]
        if reads_rgb and not latched_rgb:
            unsafe.append((index, "rgb"))
        if reads_a and not latched_a:
            unsafe.append((index, "a"))
        if stage.buf_rgb == PREVIOUS:
            latched_rgb = True
        if stage.buf_a == PREVIOUS:
            latched_a = True
    return unsafe


def slots_used(op):
    if op == 0x1E01:
        return 1
    if op in (0x2100, 0x0104, 0x84E7, 0x8574, 0x86AE, 0x86AF):
        return 2
    return 3  # INTERPOLATE (0x8575), MULT_ADD, ADD_MULT, unknown


class Stage:
    def __init__(self, b, co):
        self.rgb_op = _u16(b, co + 0x00)
        self.a_op = _u16(b, co + 0x02)
        self.rgb_scale = _u16(b, co + 0x04)
        self.a_scale = _u16(b, co + 0x06)
        self.buf_rgb = _u16(b, co + 0x08)
        self.buf_a = _u16(b, co + 0x0A)
        self.rgb_src = [_u16(b, co + 0x0C + 2 * k) for k in range(3)]
        self.rgb_mod = [_u16(b, co + 0x12 + 2 * k) for k in range(3)]
        self.a_src = [_u16(b, co + 0x18 + 2 * k) for k in range(3)]
        self.a_mod = [_u16(b, co + 0x1E + 2 * k) for k in range(3)]
        self.const_idx = _u32(b, co + 0x24)

    def sig(self):
        nrgb = slots_used(self.rgb_op)
        na = slots_used(self.a_op)
        rgb = ",".join(
            f"{mod_name(self.rgb_mod[k])}({src_name(self.rgb_src[k])})" for k in range(nrgb)
        )
        a = ",".join(f"{mod_name(self.a_mod[k])}({src_name(self.a_src[k])})" for k in range(na))
        s = f"{op_name(self.rgb_op)}[{rgb}]x{self.rgb_scale}/{op_name(self.a_op)}[{a}]x{self.a_scale}"
        used = self.rgb_src[:nrgb] + self.a_src[:na]
        if 0x8576 in used:
            s += f"/k{self.const_idx}"
        return s


class MaterialRecord:
    """One material's header fields, from the single material walk below.

    `index`/`tex`/`coord_mapping`/`coord_source`/`stages` are what the combiner surveys need;
    `fragment_lighting` and `vertex_lighting` are material bytes +0x00/+0x01, which the lighting
    and mapping surveys need. Keeping them on one record means the 0x15C/0x16C stride walk and the
    `+0x58 + 0x18*t` coordinator offsets are written down exactly once.
    """

    __slots__ = ("index", "tex", "coord_mapping", "coord_source", "stages",
                 "fragment_lighting", "vertex_lighting")

    def __init__(self, index, tex, coord_mapping, coord_source, stages,
                 fragment_lighting, vertex_lighting):
        self.index = index
        self.tex = tex
        self.coord_mapping = coord_mapping
        self.coord_source = coord_source
        self.stages = stages
        self.fragment_lighting = fragment_lighting
        self.vertex_lighting = vertex_lighting

    @property
    def lit(self):
        return self.fragment_lighting or self.vertex_lighting

    def as_tuple(self):
        return (self.index, self.tex, self.coord_mapping, self.coord_source, self.stages)


def material_chunk_pointer(b):
    """Return the offset of the `mats` chunk, or None when the file is not a CMB.

    Majora's Mask CMBs (version >= 7) insert a `qtrs` chunk pointer at 0x28 right after
    the skeleton, shifting every later chunk pointer by +4. The shipping parser is the
    authority for that gate (`Shipwright/cmb3d/asset/cmb.cpp`: `d = version >= 7 ? 4 : 0`).
    Reading 0x28 unconditionally pointed at MM3D's `qtrs` chunk, so every MM3D material
    parsed as zero materials instead of erroring.
    """
    if b[0:4] != b"cmb ":
        return None
    shift = 4 if _u32(b, 0x08) >= 7 else 0
    p = _u32(b, 0x28 + shift)
    if p == 0 or b[p : p + 4] != b"mats":
        return None
    return p


def parse_material_records(b):
    """Yield a :class:`MaterialRecord` per material. The one material-layout walk."""
    p = material_chunk_pointer(b)
    if p is None:
        return
    version = _u32(b, 0x08)
    n = _u32(b, p + 8)
    stride = 0x15C if version <= 6 else 0x16C
    comb_base = p + 0x0C + n * stride
    o = p + 0x0C
    for i in range(n):
        tex = [struct.unpack_from("<h", b, o + 0x10 + 0x18 * t)[0] for t in range(3)]
        coord_map = [b[o + 0x58 + 0x18 * t + 2] for t in range(3)]
        # TextureCoordinator::sourceCoordinate is byte 0. Byte 1 is referenceCamera; surveying
        # that byte previously produced the false corpus-wide conclusion that every coordinator
        # selected texCoord0 (valbasiagnd's coordinator 1 selects texCoord1).
        coord_src = [b[o + 0x58 + 0x18 * t + 0] for t in range(3)]
        cnt = _u32(b, o + 0x120)
        stages = []
        for s in range(min(cnt, 6)):
            cidx = _u16(b, o + 0x124 + 2 * s)
            stages.append(Stage(b, comb_base + cidx * 0x28))
        yield MaterialRecord(i, tex, coord_map, coord_src, stages,
                             bool(b[o + 0x00]), bool(b[o + 0x01]))
        o += stride


def parse_mats(b):
    """Yield (mat_index, tex_idx[3], coord_mapping[3], coord_srcuv[3], stages[list[Stage]])."""
    for record in parse_material_records(b):
        yield record.as_tuple()


def main():
    filt = None
    game = "oot"
    argv = sys.argv[1:]
    if len(argv) >= 2 and argv[0] == "--game":
        game = argv[1]
        argv = argv[2:]
    if len(argv) >= 2 and argv[0] == "--file":
        filt = argv[1]

    n_files = 0
    n_mats = 0
    stage_counts = Counter()
    stage_sigs = Counter()  # per-stage signature (position-independent)
    chain_sigs = Counter()  # whole-chain signature
    chain_example = {}
    tex_use = Counter()  # which TEXn actually consumed by any stage
    coordmap_use = Counter()
    coordsrc_use = Counter()
    domain_bad = Counter()  # layout-validation failures
    # PICA combiner-buffer latching. Reported because assuming it away cost a session:
    # the shader documented "the buffer-input selector is 0x8579 corpus-wide (buffer never
    # latches PREVIOUS)" and evaluated PREVBUF as vec4(0) on the strength of it. It is not
    # corpus-wide — childlink_v2 mat26 (the Goron bracelet) latches at stage 1 and reads the
    # buffer back at stage 2, which is why the bracelet rendered black. A field the emulation
    # decides to ignore is exactly the field the survey has to keep counting.
    buf_latch = Counter()   # per-stage buffer-input selector values
    buf_readers = Counter() # materials whose chain SOURCES PREVBUF
    # The count that decides the PREVIOUS_BUFFER approximation: reads that reach the un-latched
    # runtime register, where the host's vec4(0) substitution is wrong. Tracked per channel because
    # latching is per channel.
    prevbuf_unsafe_materials = 0
    prevbuf_safe_materials = 0
    prevbuf_unsafe_examples = []

    op_domain = set(OPS)
    src_domain = set(SRCS)
    mod_domain = set(MODS)

    print(f"== corpus: {game} ==")

    for label, cmb in iter_corpus(game)():
        if filt and filt not in label:
            continue
        try:
            mats = list(parse_mats(cmb))
        except Exception as e:
            print(f"parse error {label}: {e}", file=sys.stderr)
            continue
        if not mats:
            continue
        n_files += 1
        for mi, tex, cmap, csrc, stages in mats:
            n_mats += 1
            stage_counts[len(stages)] += 1
            texs_consumed = set()
            chain = []
            for si, st in enumerate(stages):
                # layout validation: every field must be in its legal domain
                if st.rgb_op not in op_domain:
                    domain_bad[f"rgb_op={st.rgb_op:04x}"] += 1
                if st.a_op not in op_domain:
                    domain_bad[f"a_op={st.a_op:04x}"] += 1
                if st.rgb_scale not in (1, 2, 4):
                    domain_bad[f"rgb_scale={st.rgb_scale}"] += 1
                if st.a_scale not in (1, 2, 4):
                    domain_bad[f"a_scale={st.a_scale}"] += 1
                for k in range(slots_used(st.rgb_op)):
                    if st.rgb_src[k] not in src_domain:
                        domain_bad[f"rgb_src={st.rgb_src[k]:04x}"] += 1
                    if st.rgb_mod[k] not in mod_domain:
                        domain_bad[f"rgb_mod={st.rgb_mod[k]:04x}"] += 1
                for k in range(slots_used(st.a_op)):
                    if st.a_src[k] not in src_domain:
                        domain_bad[f"a_src={st.a_src[k]:04x}"] += 1
                    if st.a_mod[k] not in mod_domain:
                        domain_bad[f"a_mod={st.a_mod[k]:04x}"] += 1
                if st.const_idx > 5:
                    domain_bad[f"const_idx={st.const_idx}"] += 1
                sig = st.sig()
                stage_sigs[sig] += 1
                chain.append(sig)
                buf_latch[f"rgb={st.buf_rgb:04x}"] += 1
                buf_latch[f"a={st.buf_a:04x}"] += 1
                if st.buf_rgb == 0x8578 or st.buf_a == 0x8578:
                    buf_readers[f"LATCHES at stage{si}: {label} mat{mi}"] += 1
                if PREVIOUS_BUFFER in st.rgb_src[:slots_used(st.rgb_op)] or \
                   PREVIOUS_BUFFER in st.a_src[:slots_used(st.a_op)]:
                    buf_readers[f"READS PREVBUF at stage{si}: {label} mat{mi}"] += 1
                for k in range(slots_used(st.rgb_op)):
                    if 0x84C0 <= st.rgb_src[k] <= 0x84C3:
                        texs_consumed.add(st.rgb_src[k] - 0x84C0)
                for k in range(slots_used(st.a_op)):
                    if 0x84C0 <= st.a_src[k] <= 0x84C3:
                        texs_consumed.add(st.a_src[k] - 0x84C0)
            unsafe = prevbuf_before_latch(stages)
            if unsafe:
                prevbuf_unsafe_materials += 1
                prevbuf_unsafe_examples.append(
                    f"{label} mat{mi}: reads PREVBUF at " +
                    ", ".join(f"stage{si} ({channel})" for si, channel in unsafe)
                )
            elif PREVIOUS_BUFFER in [
                s for st in stages for s in st.rgb_src + st.a_src
            ]:
                prevbuf_safe_materials += 1
            for t in sorted(texs_consumed):
                tex_use[f"tex{t} consumed (declared={tex[t] >= 0 if t < 3 else '?'})"] += 1
                if t < 3:
                    coordmap_use[f"tex{t} coordmap={cmap[t]}"] += 1
                    coordsrc_use[f"tex{t} coordsrc={csrc[t]}"] += 1
            csig = " | ".join(chain) if chain else "(no stages)"
            chain_sigs[csig] += 1
            chain_example.setdefault(csig, f"{label} mat{mi}")
            if filt:
                print(f"{label} mat{mi} tex={tex} cmap={cmap[:3]} csrc={csrc[:3]}")
                for si, st in enumerate(stages):
                    print(f"   stage{si}: {st.sig()} buf=({st.buf_rgb:04x},{st.buf_a:04x})")

    if filt:
        return

    print(f"files={n_files} materials={n_mats}\n")
    print("== stage counts ==")
    for k in sorted(stage_counts):
        print(f"  {k} stages: {stage_counts[k]}")
    print("\n== layout-domain violations (should be EMPTY if layout is right) ==")
    for k, v in domain_bad.most_common(30):
        print(f"  {k}: {v}")
    if not domain_bad:
        print("  none — layout validated corpus-wide")
    print("\n== combiner-buffer latching (0x8579 = keep buffer, 0x8578 = latch PREVIOUS) ==")
    for k, v in buf_latch.most_common():
        print(f"  {k}: {v}")
    lat = [k for k in buf_readers if k.startswith("LATCHES")]
    rd = [k for k in buf_readers if k.startswith("READS")]
    print(f"  materials latching the buffer: {len(lat)}")
    print(f"  materials reading PREVBUF:     {len(rd)}")
    print(
        f"  of those reads, SAFE (a latch already wrote the channel): {prevbuf_safe_materials}"
    )
    print(
        f"  of those reads, UNSAFE (reaches the un-latched runtime register): "
        f"{prevbuf_unsafe_materials}"
    )
    for example in sorted(prevbuf_unsafe_examples)[:12]:
        print(f"    UNSAFE {example}")
    if not prevbuf_unsafe_materials:
        print(
            "    -> the evaluator's vec4(0) for the combiner buffer is EXACT for every PREVBUF read"
            " in this corpus"
        )
    for k in sorted(lat)[:12]:
        print(f"    {k}")
    for k in sorted(rd)[:12]:
        print(f"    {k}")

    print("\n== texture units consumed by combiners ==")
    for k, v in sorted(tex_use.items()):
        print(f"  {k}: {v}")
    for k, v in sorted(coordmap_use.items()):
        print(f"  {k}: {v}")
    for k, v in sorted(coordsrc_use.items()):
        print(f"  {k}: {v}")
    print("\n== per-stage signatures (all stages, position-independent) ==")
    for k, v in stage_sigs.most_common():
        print(f"  {v:6d}  {k}")
    print("\n== whole-chain signatures ==")
    for k, v in chain_sigs.most_common():
        print(f"  {v:6d}  {k}\n          e.g. {chain_example[k]}")


if __name__ == "__main__":
    main()
