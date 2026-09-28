#!/usr/bin/env python3
"""PICA200 *lighting* register field map, and the shading-term reducer built on it.

`tools/pica_texturing_registers.py` owns the texturing register map. There was no equivalent
owner for the lighting registers, and that absence is why a wrong reading survived: the only
`config1` bit assignment in the project lived in `oot3d-decomp/tools/pica_lighting_config.py`
under the 3DS's own name for a *source field* (`MODE_SPOT`), and the two captions that quoted it
(`docs/project-state.md`: "a real per-material difference at bit `0x11`, which the recovered
builder names `MODE_SPOT_INDEX`") disagree with it AND with the register map. Nothing in the tree
was able to say which was right, because deciding what a lighting word *means* needs the register
map. That is what this module is for.

The field map is PARSED OUT OF THE SOURCE, not transcribed
(`Azahar/src/video_core/pica/regs_lighting.h`). Transcribing a bitfield is exactly the failure
this project has hit repeatedly this campaign: a wrong element does not raise, it produces a
clean-looking number. `regs_lighting.h` is not vendored-generated, so it is a legitimate source of
truth, and parsing it means a corrected upstream field name cannot silently rot here.

WHAT THE REDUCER IS FOR. The port blocker for MM3D is not the equation, it is that the equation
is large: `ComputeFragmentsColors`
(`Azahar/src/video_core/renderer_software/sw_lighting.cpp`) evaluates six PICA lighting LUTs,
distance attenuation, spotlight attenuation, a bump map and shadow. A port that implements "all
of it" is a large speculative surface. The two words `config0`/`config1` say, per draw, which of
those terms can be non-trivial, so `reduce()` answers the question the porter actually has -- "for
this draw, which terms must the shader model?" -- from the same registers the oracle captured.

The reducer is deliberately a CONTROL-FIRST instrument: it reports what it parsed, and it is
required to be able to return the *other* answer (every term active) for a deliberately
maximally-enabled config. An instrument that can only ever say "nothing is needed" would be
indistinguishable from one that is simply broken.

Rejected inputs are a failure, not a default: an unparseable register source, or a `config1`
whose bit-18 "dummy, always set as 1" bit is clear, both raise. Bit 18 is the only bit the
hardware defines as hardwired (`regs_lighting.h:203-205`), so a captured word with it clear did
not come off real hardware and every other field decoded from it is suspect.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
REGS_LIGHTING_H = REPO / "Azahar/src/video_core/pica/regs_lighting.h"
SW_LIGHTING_CPP = REPO / "Azahar/src/video_core/renderer_software/sw_lighting.cpp"

# `regs_lighting.h`'s LightingConfig values. `Config7 = 8` and NOT 7: 7 is not a valid
# configuration on hardware, so the raw 4-bit field has a hole. Reading it as a dense enum index
# would silently mislabel it, so the hole is preserved here.
LIGHTING_CONFIG_NAMES: dict[int, str] = {
    0: "Config0",
    1: "Config1",
    2: "Config2",
    3: "Config3",
    4: "Config4",
    5: "Config5",
    6: "Config6",
    8: "Config7",
}
CONFIG_ENUM = "LightingConfig"

# `IsLightingSamplerSupported` (regs_lighting.h:100-124), transcribed as EXCLUSIONS because the
# source is written as exclusions and the Config7 == 8 hole makes a positive list misleading.
# Each entry is the set of configs for which the sampler is NOT supported.
_UNSUPPORTED: dict[str, frozenset[int]] = {
    "Distribution0": frozenset({1}),
    "Distribution1": frozenset({0, 1, 5}),
    "SpotlightAttenuation": frozenset({2, 3}),
    "Fresnel": frozenset({0, 2, 4}),
    "ReflectRed": frozenset({3}),
    "ReflectGreen": frozenset(set(LIGHTING_CONFIG_NAMES) - {4, 5, 8}),
    "ReflectBlue": frozenset(set(LIGHTING_CONFIG_NAMES) - {4, 5, 8}),
}

SAMPLER_NAMES = tuple(_UNSUPPORTED)

# The subset that config1 gates with a single `disable_lut_*` bit. `SpotlightAttenuation` is a
# sampler too, but config1 gates it PER SLOT through `disable_spot_atten[0-7]`, not with a
# `disable_lut_sp` bit -- regs_lighting.h:203-205 records bit 18 as a hardwired dummy precisely
# because there is no such bit. Conflating the two sets produced a KeyError on the first real
# capture, which is the reason the split is explicit rather than a single list.
LUT_SAMPLER_NAMES = (
    "Distribution0",
    "Distribution1",
    "Fresnel",
    "ReflectRed",
    "ReflectGreen",
    "ReflectBlue",
)

_BITFIELD_RE = re.compile(
    r"BitField<\s*(\d+)\s*,\s*(\d+)\s*,\s*[^>]+>\s*(\w+)\s*;")
_UNION_END_RE = re.compile(r"\}\s*(\w+)\s*;")


class LightingRegisterMapError(RuntimeError):
    """The register source could not be read, or a captured word is not from real hardware."""


def _parse_union_fields(source: str, union_name: str) -> dict[int, tuple[str, int]]:
    """Return {bit_offset: (field_name, width)} for one `union { ... } <union_name>;` block."""
    end = _UNION_END_RE.search(source)
    # The unions are the only ones in this header terminated by a *named* block, and there are
    # exactly two of them. Scan every block and keep the one that closes on this name.
    for match in re.finditer(r"union\s*\{", source):
        depth = 1
        pos = match.end()
        while depth and pos < len(source):
            if source[pos] == "{":
                depth += 1
            elif source[pos] == "}":
                depth -= 1
            pos += 1
        body = source[match.end(): pos - 1]
        closer = _UNION_END_RE.match(source, pos - 1)
        if closer and closer.group(1) == union_name:
            fields: dict[int, tuple[str, int]] = {}
            for lo, width, name in _BITFIELD_RE.findall(body):
                fields[int(lo)] = (name, int(width))
            return fields
    raise LightingRegisterMapError(f"union {union_name!r} not found in {REGS_LIGHTING_H}")


@dataclass(frozen=True)
class LightingRegisterMap:
    """The parsed `config0` / `config1` field layouts, with where each came from."""

    config0: dict[int, tuple[str, int]]
    config1: dict[int, tuple[str, int]]
    source: Path

    def fields(self, word: str) -> dict[int, tuple[str, int]]:
        return self.config0 if word == "config0" else self.config1

    def named_bits(self, word: str, value: int) -> list[tuple[int, str]]:
        """Every set bit in `value`, as (bit, field_name), lowest first."""
        out = []
        for bit in range(32):
            if (value >> bit) & 1:
                fields = self.fields(word)
                out.append((bit, fields[bit][0] if bit in fields else f"UNNAMED_{bit}"))
        return out

    def describe(self) -> str:
        lines = [f"parsed from {self.source}"]
        for word in ("config0", "config1"):
            fields = self.fields(word)
            lines.append(f"  {word}: {len(fields)} named fields")
            for bit in sorted(fields):
                name, width = fields[bit]
                lines.append(f"    bit {bit:2d} width {width} {name}")
        return "\n".join(lines)


def load_register_map() -> LightingRegisterMap:
    if not REGS_LIGHTING_H.exists():
        raise LightingRegisterMapError(f"missing PICA lighting register source: {REGS_LIGHTING_H}")
    source = REGS_LIGHTING_H.read_text(encoding="utf-8", errors="replace")
    return LightingRegisterMap(
        config0=_parse_union_fields(source, "config0"),
        config1=_parse_union_fields(source, "config1"),
        source=REGS_LIGHTING_H,
    )


@dataclass(frozen=True)
class LightingReduction:
    """Which terms of `ComputeFragmentsColors` a (config0, config1) pair can make non-trivial."""

    config0: int
    config1: int
    config: int
    config_name: str
    bump_mode: int
    enable_shadow: bool
    clamp_highlights: bool
    enable_primary_alpha: bool
    enable_secondary_alpha: bool
    # Sampler name -> False when the config forbids it, True when the config allows it.
    config_supports: dict[str, bool]
    # Sampler name -> True when config1 leaves its LUT enabled. Per-slot terms (shadow, spot
    # atten, dist atten) are handled separately because config1 gates them per light slot.
    lut_enabled: dict[str, bool]
    all_slots_shadow_disabled: bool
    all_slots_spot_atten_disabled: bool
    all_slots_dist_atten_disabled: bool

    def active_terms(self) -> list[str]:
        """Shading terms a port must model for this configuration, most specific first."""
        terms: list[str] = []
        if self.bump_mode != 0:
            terms.append("bump")
        if self.enable_shadow:
            terms.append("shadow-source")
        if self.clamp_highlights:
            terms.append("clamp-highlights")
        for slot_term, all_off in (
            ("shadow", self.all_slots_shadow_disabled),
            ("spot-attenuation", self.all_slots_spot_atten_disabled),
            ("distance-attenuation", self.all_slots_dist_atten_disabled),
        ):
            if not all_off:
                terms.append(slot_term)
        for name in LUT_SAMPLER_NAMES:
            if self.config_supports[name] and self.lut_enabled[name]:
                terms.append(f"lut:{name}")
        if self.enable_primary_alpha:
            terms.append("fresnel-primary-alpha")
        if self.enable_secondary_alpha:
            terms.append("fresnel-secondary-alpha")
        return terms

    def summary(self) -> str:
        terms = self.active_terms()
        lines = [
            f"config0=0x{self.config0:08x} config1=0x{self.config1:08x}",
            f"  lighting config = {self.config} ({self.config_name})",
            f"  bump_mode={self.bump_mode} clamp_highlights={self.clamp_highlights} "
            f"enable_shadow={self.enable_shadow}",
        ]
        for name in LUT_SAMPLER_NAMES:
            lines.append(
                f"  {name:<22s} config_supports={str(self.config_supports[name]):<5s} "
                f"lut_enabled={self.lut_enabled[name]}")
        lines.append(
            f"  {'SpotlightAttenuation':<22s} config_supports="
            f"{str(self.config_supports['SpotlightAttenuation']):<5s} "
            f"lut_enabled=n/a (gated per slot by config1 disable_spot_atten)")
        lines.append(
            f"  per-slot: shadow={not self.all_slots_shadow_disabled} "
            f"spot_atten={not self.all_slots_spot_atten_disabled} "
            f"dist_atten={not self.all_slots_dist_atten_disabled}")
        lines.append(f"  ACTIVE TERMS ({len(terms)}): " + (", ".join(terms) if terms else "none"))
        return "\n".join(lines)


def _bit(regmap: LightingRegisterMap, word: str, name: str) -> int:
    for bit, (field, _width) in regmap.fields(word).items():
        if field == name:
            return bit
    raise LightingRegisterMapError(f"field {name!r} not found in {word}")


def _bit_is_set(regmap: LightingRegisterMap, word: str, name: str, value: int) -> bool:
    return (value >> _bit(regmap, word, name)) & 1 == 1


def _field_bits(regmap: LightingRegisterMap, word: str, name: str) -> int:
    """Every bit position a (possibly multi-bit) field occupies."""
    lo = _bit(regmap, word, name)
    return lo


def _mask(regmap: LightingRegisterMap, word: str, name: str) -> tuple[int, int]:
    for bit, (field, width) in regmap.fields(word).items():
        if field == name:
            return bit, ((1 << width) - 1) << bit
    raise LightingRegisterMapError(f"field {name!r} not found in {word}")


def reduce_lighting(
    config0: int,
    config1: int,
    regmap: LightingRegisterMap | None = None,
) -> LightingReduction:
    """Reduce a captured (config0, config1) pair to the shading terms it can make non-trivial.

    Mirrors `ComputeFragmentsColors` in `sw_lighting.cpp` term for term. `config0`/`config1` are
    32-bit raw words exactly as the oracle reports them.
    """
    regmap = regmap or load_register_map()

    cfg_lo, cfg_mask = _mask(regmap, "config0", "config")
    config = (config0 & cfg_mask) >> cfg_lo
    if config not in LIGHTING_CONFIG_NAMES:
        raise LightingRegisterMapError(
            f"config0 lighting config field = {config}, not a valid configuration "
            f"(valid: {sorted(LIGHTING_CONFIG_NAMES)}); word=0x{config0:08x}")

    dummy_bit = _bit(regmap, "config1", "raw") if "raw" in [
        f for _, (f, _) in regmap.config1.items()] else None
    # Bit 18 is the hardwired dummy ("always set as 1", regs_lighting.h:203-205). A captured
    # word with it clear did not come from hardware, so nothing else decoded from it is trusted.
    if not (config1 >> 18) & 1:
        raise LightingRegisterMapError(
            f"config1=0x{config1:08x} has the hardwired bit 18 clear; regs_lighting.h:203-205 "
            f"documents it as always set, so this word is not from real hardware")
    del dummy_bit

    bump_lo, bump_mask = _mask(regmap, "config0", "bump_mode")
    bump_mode = (config0 & bump_mask) >> bump_lo

    supports = {name: config not in _UNSUPPORTED[name] for name in SAMPLER_NAMES}
    lut_enabled = {
        "Distribution0": not _bit_is_set(regmap, "config1", "disable_lut_d0", config1),
        "Distribution1": not _bit_is_set(regmap, "config1", "disable_lut_d1", config1),
        "Fresnel": not _bit_is_set(regmap, "config1", "disable_lut_fr", config1),
        "ReflectRed": not _bit_is_set(regmap, "config1", "disable_lut_rr", config1),
        "ReflectGreen": not _bit_is_set(regmap, "config1", "disable_lut_rg", config1),
        "ReflectBlue": not _bit_is_set(regmap, "config1", "disable_lut_rb", config1),
    }

    def _all_set(word_field: str) -> bool:
        _lo, mask = _mask(regmap, "config1", word_field)
        return config1 & mask == mask

    return LightingReduction(
        config0=config0,
        config1=config1,
        config=config,
        config_name=LIGHTING_CONFIG_NAMES[config],
        bump_mode=bump_mode,
        enable_shadow=_bit_is_set(regmap, "config0", "enable_shadow", config0),
        clamp_highlights=_bit_is_set(regmap, "config0", "clamp_highlights", config0),
        enable_primary_alpha=_bit_is_set(regmap, "config0", "enable_primary_alpha", config0),
        enable_secondary_alpha=_bit_is_set(regmap, "config0", "enable_secondary_alpha", config0),
        config_supports=supports,
        lut_enabled=lut_enabled,
        all_slots_shadow_disabled=_all_set("disable_shadow"),
        all_slots_spot_atten_disabled=_all_set("disable_spot_atten"),
        all_slots_dist_atten_disabled=_all_set("disable_dist_atten"),
    )


def diff_config_words(a: int, b: int, word: str, regmap: LightingRegisterMap | None = None) -> list[tuple[int, str]]:
    """(bit, field name) for every bit where two captured words of `word` differ.

    This is the function that settles "config1 is not a constant". The project's recorded claim
    was that `0xff7fffff` vs `0xff7effff` differ at bit `0x11`; the XOR is `0x00010000`, which is
    bit 16. Naming the bit through the parsed map -- rather than through a caption -- is what makes
    that checkable instead of arguable.
    """
    regmap = regmap or load_register_map()
    out = []
    for bit in range(32):
        if ((a >> bit) & 1) != ((b >> bit) & 1):
            fields = regmap.fields(word)
            out.append((bit, fields[bit][0] if bit in fields else f"UNNAMED_{bit}"))
    return out


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--show-map", action="store_true",
                        help="print the parsed config0/config1 field map and exit")
    parser.add_argument("--config0", type=lambda s: int(s, 0), default=None)
    parser.add_argument("--config1", type=lambda s: int(s, 0), default=None)
    parser.add_argument("--diff", nargs=2, type=lambda s: int(s, 0), default=None, metavar=("A", "B"),
                        help="name the bits where two captured words of the same register differ")
    args = parser.parse_args(argv)

    regmap = load_register_map()
    if args.show_map:
        print(regmap.describe())
        return 0
    if args.diff:
        a, b = args.diff
        diffs = diff_config_words(a, b, "config1", regmap)
        print(f"config1 0x{a:08x} vs 0x{b:08x}: XOR=0x{a ^ b:08x}")
        print(f"  {len(diffs)} differing bit(s)")
        for bit, name in diffs:
            print(f"  bit {bit:2d} (0x{bit:x}) {name}")
        return 0
    if args.config0 is None or args.config1 is None:
        parser.error("need --config0 and --config1, or --show-map, or --diff")
    try:
        print(reduce_lighting(args.config0, args.config1, regmap).summary())
    except LightingRegisterMapError as exc:
        print(f"REFUSED: {exc}")
        return 2
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
