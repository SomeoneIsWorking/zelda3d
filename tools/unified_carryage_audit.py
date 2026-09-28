#!/usr/bin/env python3
"""Audit which of the native CMB packer's per-draw uniform fields the unified route actually carries.

**Why this exists.** The unified route re-packs the native `SgUbo` into its own `CommonUbo`, and it
got that wrong twice in a row, in the same way: it copied the FRAME-level values and dropped the
PER-DRAW one. The fog mode gate (`SgUbo::uFog[3]`, set only when the frame's 3DS fog is on AND the
material's CMB sets `is_fog`) was missing, so every unified draw was unfogged -- 9.91 mean-abs at
title cs=1093. The per-draw texcoord scroll was missing next, and 2 of 101 title draws carry a
non-zero one (the OoT3D sky cloud band), so they sat still. Both were found by reading the packer by
hand, which is exactly the kind of check that finds the first two and misses the third.

So this is that hand audit, mechanised. It reads three things out of the real tree:

* the `SgUbo` field list from `zelda3d_sg_ubo.h` -- the authority for what exists;
* which fields the native per-DRAW packer writes (`ubo.X = ...` inside the group loop) versus which
  it only sets per FRAME (`base.X = ...`), because a per-frame value is a different question from a
  per-draw one and conflating them is the bug;
* which fields each route's GENERATED GLSL actually reads, so "the packer writes it" and "a shader
  consumes it" are separate questions.

Then it classifies every per-draw native field as carried (the unified packer reads it) or
substituted (an entry in `SUBSTITUTIONS` names the unified field that carries it, with a reason), and
fails on anything else. Fields the native packer writes that NO route's shader reads are reported
separately as dead, and are not failures -- a written-but-unread field is a real thing here
(`uShadow`, and the F3DEX `uFog2`/`uParams[0..1]` that #113 turned off), and conflating "dead" with
"dropped" would make the tool cry wolf.

Usage: `python3 tools/unified_carryage_audit.py [--json]`. Prints what it scanned and what it matched
either way, and exits non-zero when a per-draw field is neither carried nor substituted.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LUS = REPO / "Shipwright" / "libultraship"

UBO_HEADER = LUS / "include" / "fast" / "zelda3d_sg_ubo.h"
PACKER = LUS / "src" / "fast" / "zelda3d_sdl3gpu_pass.cpp"
NATIVE_SHADER = LUS / "src" / "fast" / "zelda3d_sdl3gpu_shaders.cpp"
UNIFIED_SHADER = LUS / "src" / "fast" / "backends" / "unified_shader.cpp"
UNIFIED_UBO = LUS / "include" / "fast" / "unified_ubo.h"

# Per-draw native fields the unified route deliberately does NOT copy field-for-field, and what
# carries the value instead. Every entry needs a reason, because an unexplained substitution is how a
# second policy gets in.
SUBSTITUTIONS: dict[str, tuple[str, str]] = {
    "uParams": (
        "uParams0/uParams1",
        "the native packs alphaRef, lit, polygonOffset and extra-w into one vec4; the unified splits "
        "them across two lanes with its own meanings (lightingMode, cycleCount, frame_count, "
        "hasSkin, alreadyTransformed), so a field-for-field copy would be meaningless",
    ),
    "uExtra": (
        "uUvScroll + uPrimColor.a",
        "draw alpha rides uPrimColor.a (the same value the native reads from uExtra.x) and the "
        "per-draw UV scroll now rides uUvScroll; uExtra.y/z. See render.per-draw-uniform-carriage",
    ),
    "uTintSkin": (
        "uPrimColor.rgb + uParams1.z",
        "the native's uTintSkin.xyz is the per-draw RGB modulation (uPrimColor.rgb on the unified) "
        "and .w is the skinning flag (uParams1.z)",
    ),
}

# Per-FRAME fields the native packer writes that no shader on either route reads. Listed here rather
# than in SUBSTITUTIONS so the substitution table holds only fields the audit actually reaches -- an
# unreachable entry is an untested one, and a table nothing consults is also a misleading all-clear.
# `uShadow` went with the removed shadow map (the native writes 0.0 and says so); `uFog2` is the
# F3DEX ramp, dead since gZelda3dFogEnable stayed 0 (#113), and the native's own PICA branch tests
# uFog.w > 1.5 first so it is not read there either. `uLightVP` is NOT here: the packer never writes
# it, so the audit's "written per frame" filter excludes it and listing it would assert a fact about
# the packer that is false.
PER_FRAME_UNREAD = ("uShadow", "uFog2")

@dataclass
class Audit:
    ubo_fields: list[str] = field(default_factory=list)
    per_frame: set[str] = field(default_factory=set)
    per_draw: set[str] = field(default_factory=set)
    read_native: set[str] = field(default_factory=set)
    read_unified: set[str] = field(default_factory=set)
    carried: set[str] = field(default_factory=set)
    substituted: dict[str, str] = field(default_factory=dict)
    dead: set[str] = field(default_factory=set)
    dropped: list[str] = field(default_factory=list)
    frame_unread: list[str] = field(default_factory=list)


def ubs_fields(text: str) -> list[str]:
    """The `SgUbo` field names, in declaration order, from the owning header."""
    body = text.split("struct SgUbo {", 1)[1].split("\n};", 1)[0]
    names: list[str] = []
    for line in body.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("//") or stripped.startswith("/*"):
            continue
        # `float uFoo[16];` / `int uBar;` / `uint32_t uTevStages[6 * 4];` -- a declaration, not a
        # method. The subscript must be matched loosely: a numeric-only pattern silently skipped
        # `uTevStages[6 * 4]`, so the field list came out one short and the test that checks the
        # list is not vacuous is what caught it.
        m = re.match(r"^(?:const\s+)?(?:unsigned\s+|signed\s+)?[A-Za-z_0-9]+\s+([A-Za-z_][A-Za-z_0-9]*)"
                     r"\s*(?:\[[^\]]*\])?\s*(?:=[^;]*)?;", stripped)
        if m:
            names.append(m.group(1))
    return names


def written_fields(text: str, receiver: str) -> set[str]:
    """Fields assigned through `receiver`, e.g. `ubo.uFog[3] =` or `base.uSheen[0] =`."""
    return {m.group(1) for m in re.finditer(rf"\b{receiver}\.([A-Za-z_][A-Za-z_0-9]*)\s*(?:\[[^\]]*\])?\s*=",
                                           text)}


PIPELINES = LUS / "src" / "fast" / "zelda3d_sdl3gpu_pipelines.cpp"

# The pipeline-state fields that decide a frame's appearance. Anything the two builders disagree
# about is a carriage bug in the same family as the UBO ones, in a different place, so it is checked
# the same way rather than being left to a hand read.
PIPELINE_STATE_FIELDS = (
    "rasterizer_state.cull_mode",
    "rasterizer_state.front_face",
    "rasterizer_state.fill_mode",
    "rasterizer_state.enable_depth_clip",
    "depth_stencil_state.enable_depth_test",
    "depth_stencil_state.enable_depth_write",
    "depth_stencil_state.compare_op",
    "blend_state.enable_blend",
    "blend_state.src_color_blendfactor",
    "blend_state.dst_color_blendfactor",
    "blend_state.color_blend_op",
    "blend_state.src_alpha_blendfactor",
    "blend_state.dst_alpha_blendfactor",
    "blend_state.alpha_blend_op",
    "depth_stencil_format",
)


def pipeline_state_fields(body: str) -> set[str]:
    """Which `PIPELINE_STATE_FIELDS` a builder body assigns, with the assignment captured.

    Returns `field` for a plain assignment and `field = <rhs>` when the right-hand side differs
    between the two builders, so a divergence in HOW a field is derived is caught as well as one in
    WHETHER it is set.
    """
    out: set[str] = set()
    for field in PIPELINE_STATE_FIELDS:
        leaf = field.rsplit(".", 1)[-1]
        m = re.search(rf"\.{re.escape(leaf)}\s*=\s*([^;]+);", body)
        if m is not None:
            out.add(f"{field} = {' '.join(m.group(1).split())}")
    return out


def pipeline_audit() -> dict[str, object]:
    """Compare `getPipeline` (native) with `getUnifiedPipeline` on the state they set.

    This is the SECOND carriage class. The UBO audit found two live bugs of the shape "the unified
    route copies the frame-level values and drops the per-draw one"; pipeline state is the other
    place a per-draw decision lives, so it gets the same mechanical check rather than a hand read.
    """
    text = PIPELINES.read_text()
    bodies: dict[str, str] = {}
    for name in ("getUnifiedPipeline", "getPipeline"):
        # Built by concatenation rather than an f-string. `\{name}` inside an f-string emits a
        # backslash followed by the LITERAL name, which `re` reads as a `\g` group reference and
        # rejects -- and the resulting `re.PatternError` looks nothing like "your braces are wrong",
        # which is how this cost a debugging round trip.
        pattern = r"::" + name + r"\s*\([^)]*\)\s*\{"
        m = re.search(pattern, text)
        if m is None:
            bodies[name] = ""
            continue
        start = m.end()
        depth = 1
        i = start
        while i < len(text) and depth:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
            i += 1
        bodies[name] = text[start : i - 1]
    native = pipeline_state_fields(bodies["getPipeline"])
    unified = pipeline_state_fields(bodies["getUnifiedPipeline"])
    return {
        "native": sorted(native),
        "unified": sorted(unified),
        "only_native": sorted(native - unified),
        "only_unified": sorted(unified - native),
        "ok": bool(native) and native == unified,
    }


def read_fields(text: str, receiver: str = "ubo") -> set[str]:
    """Fields mentioned as `receiver.X` in a shader body, minus the assignments."""
    out: set[str] = set()
    for m in re.finditer(rf"\b{receiver}\.([A-Za-z_][A-Za-z_0-9]*)", text):
        out.add(m.group(1))
    return out - written_fields(text, receiver)


def helper_bodies(header: str) -> dict[str, str]:
    """The inline helper bodies in the unified UBO header, by function name."""
    out: dict[str, str] = {}
    for m in re.finditer(r"inline\s+(?:void|float|int)\s+([A-Za-z_][A-Za-z_0-9]*)\s*\([^)]*\)\s*\{", header):
        start = m.end()
        depth = 1
        i = start
        while i < len(header) and depth:
            if header[i] == "{":
                depth += 1
            elif header[i] == "}":
                depth -= 1
            i += 1
        out[m.group(1)] = header[start : i - 1]
    return out


def carried_fields(text: str, header: str) -> set[str]:
    """Native `SgUbo` fields the unified packer actually carries.

    Two sources, and the second one is the one that matters: the packer's own unified block reads
    `ubo.X` directly, and it calls `Zelda3DUnified::<helper>` functions whose bodies read
    `source.X`. **Resolving the call sites, not the helper definitions**, is load-bearing: the first
    version scanned the header for `source.uX` and so counted `PackCmbFogGate`'s `source.uFog[3]`
    as carried whether or not the packer called it -- and deleting the call, which is the original
    bug this whole audit exists to catch, still reported all clear. A definition nobody invokes is
    not carriage.
    """
    block = text.split("Zelda3DUnified::UnifiedDrawUbo uu{}", 1)
    if len(block) < 2:
        return set()
    unified_block = block[1]
    out = {m.group(1) for m in re.finditer(r"\bub[ou]\.([A-Za-z_][A-Za-z_0-9]*)", unified_block)}

    bodies = helper_bodies(header)
    for name in re.findall(r"Zelda3DUnified::([A-Za-z_][A-Za-z_0-9]*)\s*\(", unified_block):
        body = bodies.get(name)
        if body is None:
            continue
        out |= {m.group(1) for m in re.finditer(r"\b(?:source|ubo)\.([A-Za-z_][A-Za-z_0-9]*)", body)}
    return out


def audit() -> Audit:
    result = Audit()
    header = UBO_HEADER.read_text()
    packer = PACKER.read_text()
    native_shader = NATIVE_SHADER.read_text()
    unified_shader = UNIFIED_SHADER.read_text()
    unified_ubo = UNIFIED_UBO.read_text()

    result.ubo_fields = ubs_fields(header)
    # A field is PER-DRAW if any group can override it, i.e. if the packer ever writes it through
    # `ubo.`. Fields written only through `base.` are per-FRAME. The naive rule -- "written on ubo,
    # minus those also written on base" -- is wrong and silently empty: uParams, uExtra, uTintSkin
    # and uFog are all set on `base` for the frame and then OVERRIDDEN per group, so subtracting the
    # base set deletes exactly the four fields whose per-draw part is the interesting one. The first
    # run of this audit reported 10 per-draw fields and consulted no substitution at all, which is the
    # tell: an audit table nothing ever reaches is an untested table and a misleading "all clear".
    result.per_draw = written_fields(packer, "ubo")
    result.per_frame = written_fields(packer, "base") - result.per_draw
    result.read_native = read_fields(native_shader) | read_fields(packer, "ubo")
    result.read_unified = read_fields(unified_shader)
    result.carried = carried_fields(packer, unified_ubo)

    for name in sorted(result.per_draw):
        if name in result.carried:
            continue
        if name in SUBSTITUTIONS:
            result.substituted[name] = SUBSTITUTIONS[name][0]
            continue
        if not (result.read_native | result.read_unified):
            result.dead.add(name)
            continue
        result.dropped.append(name)
    result.frame_unread = [name for name in PER_FRAME_UNREAD if name in result.per_frame]
    return result


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--json", action="store_true", help="emit the audit as JSON")
    args = parser.parse_args(argv)

    result = audit()
    payload = {
        "ubo_fields": len(result.ubo_fields),
        "per_frame_written": sorted(result.per_frame),
        "per_draw_written": sorted(result.per_draw),
        "carried": sorted(result.carried & result.per_draw),
        "substituted": result.substituted,
        "dead": sorted(result.dead),
        "dropped": result.dropped,
    }
    if args.json:
        print(json.dumps(payload, indent=2))
    else:
        print("== unified per-draw uniform carriage ==")
        print(f"  SgUbo fields declared:            {len(result.ubo_fields)}")
        print(f"  written per FRAME (base.*):       {len(result.per_frame)}")
        print(f"  written per DRAW  (ubo.* only):    {len(result.per_draw)}")
        print(f"  carried field-for-field:          {len(payload['carried'])}"
              f"  {payload['carried']}")
        print(f"  substituted (named in SUBSTITUTIONS): {len(result.substituted)}")
        for name, target in sorted(result.substituted.items()):
            print(f"      {name} -> {target}")
        print(f"  dead (per-draw, written, read by no shader): {len(payload['dead'])} {payload['dead']}")
        print(f"  per-FRAME fields no route reads: {result.frame_unread}")
        print(f"  DROPPED (per-draw, read, not carried): {len(result.dropped)} {result.dropped}")

    pipes = pipeline_audit()
    if args.json:
        payload["pipeline"] = pipes
    else:
        print("== unified pipeline-state carriage (getPipeline vs getUnifiedPipeline) ==")
        print(f"  state fields compared: {len(PIPELINE_STATE_FIELDS)}")
        print(f"  native sets:  {len(pipes['native'])}")
        print(f"  unified sets: {len(pipes['unified'])}")
        print(f"  only native:  {pipes['only_native']}")
        print(f"  only unified: {pipes['only_unified']}")
    if result.dropped or not pipes["ok"]:
        for label, fields in (("per-draw uniform", result.dropped),
                              ("pipeline state", pipes["only_native"] or pipes["only_unified"])):
            if fields:
                print(f"unified_carryage_audit: {label} divergence: {fields}; the unified route must "
                      "reproduce the native one, or SUBSTITUTIONS must name the difference",
                      file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
