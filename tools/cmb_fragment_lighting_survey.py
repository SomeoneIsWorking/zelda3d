#!/usr/bin/env python3
"""Survey the retail CMB fixed-function fragment-lighting surface offline.

The CMB material byte at +0 enables the PICA fragment-lighting setup recovered as
OoT3D ``FUN_003fa5d0``.  That function consumes the five RGBA8 material colors at
+0xA0..+0xB3.  TEV stages observe its two outputs through FRAGMENT_PRIMARY and
FRAGMENT_SECONDARY. The material's nested PICA descriptor at +0xCC is also
reported, so a differing real descriptor can be selected before running a new
oracle counterfactual. This tool joins those independently-authored facts for
every CMB in the user ROM; it never starts the oracle.
"""

from __future__ import annotations

import argparse
import struct
import sys
from collections import Counter
from dataclasses import dataclass

from cmb_corpus import iter_corpus
from tev_corpus_survey import (
    material_chunk_pointer,
    parse_mats,
    slots_used,
)

FRAGMENT_PRIMARY = 0x6210
FRAGMENT_SECONDARY = 0x6211

# A probe value, named for what it IS rather than for what it might mean. 0x62C884C0 is the u32 at
# material+0xDC in 99% of BOTH games' materials, and it is a COMPOSITE of two descriptor fields the
# parser already separates: its low half 0x84C0 is the TEV texture-source code TEX0 and its high half
# 0x62C8 is the neighbouring u16. Nothing recovered here establishes what 0x62C8 selects, so it is not
# named as a colour or a light enum. It is used purely as a fingerprint: if this word is the most common
# one at exactly a single offset, that offset is a real table rather than a coincidence.
DESCRIPTOR_PROBE_WORD = 0x62C884C0


@dataclass(frozen=True)
class MaterialLighting:
    label: str
    material_index: int
    enabled: bool
    emission: tuple[int, int, int, int]
    ambient: tuple[int, int, int, int]
    diffuse: tuple[int, int, int, int]
    specular0: tuple[int, int, int, int]
    specular1: tuple[int, int, int, int]
    descriptor_words: tuple[int, int, int, int, int, int, int]
    primary_uses: int
    secondary_uses: int
    chain: str


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _rgba(data: bytes, offset: int) -> tuple[int, int, int, int]:
    return (
        data[offset],
        data[offset + 1],
        data[offset + 2],
        data[offset + 3],
    )


def _active_sources(stage: object) -> list[int]:
    rgb_count = slots_used(stage.rgb_op)
    alpha_count = slots_used(stage.a_op)
    return list(stage.rgb_src[:rgb_count]) + list(stage.a_src[:alpha_count])


def scan_materials(label: str, data: bytes) -> list[MaterialLighting]:
    if data[:4] != b"cmb ":
        return []
    version = _u32(data, 0x08)
    # The single owner of the material chunk pointer. Reading 0x28 unconditionally points at MM3D's
    # `qtrs` chunk (version >= 7 inserts it), which is how every MM3D material used to parse as zero.
    mats_offset = material_chunk_pointer(data)
    if mats_offset is None:
        return []
    stride = 0x15C if version <= 6 else 0x16C
    records: list[MaterialLighting] = []
    for material_index, _, _, _, stages in parse_mats(data):
        offset = mats_offset + 0x0C + material_index * stride
        sources = [source for stage in stages for source in _active_sources(stage)]
        primary_uses = sources.count(FRAGMENT_PRIMARY)
        secondary_uses = sources.count(FRAGMENT_SECONDARY)
        enabled = data[offset] != 0
        if not enabled and primary_uses == 0 and secondary_uses == 0:
            continue
        records.append(
            MaterialLighting(
                label=label,
                material_index=material_index,
                enabled=enabled,
                emission=_rgba(data, offset + 0xA0),
                ambient=_rgba(data, offset + 0xA4),
                diffuse=_rgba(data, offset + 0xA8),
                specular0=_rgba(data, offset + 0xAC),
                specular1=_rgba(data, offset + 0xB0),
                descriptor_words=tuple(_u32(data, offset + 0xCC + field) for field in range(0x10, 0x2C, 4)),
                primary_uses=primary_uses,
                secondary_uses=secondary_uses,
                chain=" | ".join(stage.sig() for stage in stages),
            )
        )
    return records


def _format_rgba(color: tuple[int, int, int, int]) -> str:
    return ",".join(str(component) for component in color)


def color_slot_histograms(records: list[MaterialLighting]) -> dict[str, Counter]:
    """Value distribution of the five material colours, per slot.

    This is the cheap discriminator for whether an OoT3D-recovered offset is even the right offset in
    another game. A colour block read from the WRONG place is arbitrary bytes: its byte values spread
    roughly uniformly over 0..255 with no dominant entries. A real colour block is a PALETTE: a handful
    of values dominate (opaque white, transparent black, and a few authored tints), because these are
    RGBA8 literals the content authors chose. So "spiky" is the signature of a correct offset and
    "flat" the signature of a wrong one.

    That is evidence, not proof -- an offset could land on another structured table -- so this narrows
    where to spend the binary read rather than replacing it.
    """
    slots = {
        "emission": lambda record: record.emission,
        "ambient": lambda record: record.ambient,
        "diffuse": lambda record: record.diffuse,
        "specular0": lambda record: record.specular0,
        "specular1": lambda record: record.specular1,
    }
    histograms: dict[str, Counter] = {}
    for name, extract in slots.items():
        counter: Counter = Counter()
        for record in records:
            counter[extract(record)] += 1
        histograms[name] = counter
    return histograms


def descriptor_word_histograms(records: list[MaterialLighting]) -> list[Counter]:
    """Per-position value distribution of the nested PICA descriptor words at +0xCC.

    Stronger than the colour check: in OoT3D these are PICA configuration words, so most materials
    carry zero and only bounded enums appear (that boundedness is what the Morpha counterfactual in
    oot3d-decomp/docs/fragment_lighting.md rests on). Structure that repeats in another game is the
    same table; a flat spread would be the wrong offset.
    """
    width = len(records[0].descriptor_words) if records else 0
    return [
        Counter(record.descriptor_words[index] for record in records) for index in range(width)
    ]


def offset_control_distinct(
    label_data: list[tuple[str, bytes]],
    offset: int,
) -> int:
    """Distinct RGBA tuples at one offset, over every material in the corpus.

    Used both for the measurement and for the control. One offset is not a control: large parts of a
    material record are zero padding, so an arbitrary wrong offset is trivially constant and would
    "confirm" anything. `offset_control_sweep` therefore samples many offsets.
    """
    seen: set[tuple[int, int, int, int]] = set()
    for _label, data in label_data:
        mats_offset = material_chunk_pointer(data)
        if mats_offset is None:
            continue
        version = _u32(data, 0x08)
        stride = 0x15C if version <= 6 else 0x16C
        count = _u32(data, mats_offset + 8)
        for index in range(min(count, 64)):
            base = mats_offset + 0x0C + index * stride
            if base + offset + 4 > len(data):
                continue
            seen.add(_rgba(data, base + offset))
    return len(seen)


def offset_control_sweep(label_data: list[tuple[str, bytes]], stride: int = 0x08) -> list[tuple[int, int]]:
    """Distinct-count at many offsets across the material record, EXCLUDING the measured block.

    The negative control the palette argument needs. If a correct offset is spiky because the bytes
    are authored colours, then other offsets must be noisier; if they are not, "spiky" is measuring
    the container's regularity and the argument is void. Offsets inside +0xA0..+0xB3 are skipped so the
    control cannot accidentally include the thing it is a control for.
    """
    results = []
    for offset in range(0x00, 0x150, stride):
        if 0xA0 <= offset <= 0xB0:
            continue
        results.append((offset, offset_control_distinct(label_data, offset)))
    return results


def descriptor_enum_sweep(label_data: list[tuple[str, bytes]], stride: int = 0x04) -> list[tuple[int, int]]:
    """For each word offset in the material record, how many materials hold DESCRIPTOR_PROBE_WORD.

    The palette argument failed its own control, so this tests a stronger and DIFFERENT claim: the
    nested descriptor at +0xCC holds typed fields, and the same field values appear at the same offsets
    in the other game. That is only evidence if the value is distinctive to its offset -- a value that
    dominated at every offset would be noise. So sweep every word offset and count how often the probe
    word is the most common value; a sharp peak at one offset is the signature, and a plateau is the
    refutation.
    """
    results = []
    for offset in range(0x00, 0x150, stride):
        counter: Counter = Counter()
        for _label, data in label_data:
            mats_offset = material_chunk_pointer(data)
            if mats_offset is None:
                continue
            version = _u32(data, 0x08)
            material_stride = 0x15C if version <= 6 else 0x16C
            count = _u32(data, mats_offset + 8)
            for index in range(min(count, 64)):
                base = mats_offset + 0x0C + index * material_stride
                if base + offset + 4 > len(data):
                    continue
                counter[_u32(data, base + offset)] += 1
        if not counter:
            continue
        top_value, top_count = counter.most_common(1)[0]
        results.append((offset, top_count if top_value == DESCRIPTOR_PROBE_WORD else 0))
    return results


def _report_offset_shape(records: list[MaterialLighting]) -> None:
    """Print the offset-shape evidence for the colour block and the descriptor words."""
    if not records:
        print("  (no records)")
        return
    total = len(records)
    print("\n== offset-shape evidence: are +0xA0..+0xB3 and +0xCC real tables in THIS game? ==")
    for name, counter in color_slot_histograms(records).items():
        distinct = len(counter)
        top_value, top_count = counter.most_common(1)[0]
        share = 100.0 * top_count / total
        print(
            f"  {name:10s} distinct={distinct:5d}/{total}  most common "
            f"{_format_rgba(top_value)} = {top_count} ({share:.1f}%)"
        )
        print(f"             next: {[(_format_rgba(v), n) for v, n in counter.most_common(4)[1:3]]}")
    for index, counter in enumerate(descriptor_word_histograms(records)):
        distinct = len(counter)
        top_value, top_count = counter.most_common(1)[0]
        print(
            f"  desc[+0x{0xCC + 0x10 + 4 * index:02X}] distinct={distinct:5d}/{total}  "
            f"most common 0x{top_value:08x} = {top_count} ({100.0 * top_count / total:.1f}%)"
        )
    print(
        "  a flat spread (distinct ~= total) means the offset is wrong here;"
        " a spiky one means it is a real table"
    )


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--details", action="store_true", help="print every relevant material")
    parser.add_argument(
        "--game",
        choices=("oot", "mm"),
        default="oot",
        help="retail corpus to survey (default: oot)",
    )
    args = parser.parse_args(arguments)

    # WHAT IS CERTAIN, for a game other than oot. The CONSUMER counts are certain for any game: they
    # come from each material's own combiner stage records through the shared parser, with no layout
    # assumption. The `+0` fragment-lighting flag and the +0xA0..+0xB3 colour block are OoT3D-RECOVERED
    # (OoT3D FUN_003fa5d0 consuming five RGBA8 material colours) and have NOT been confirmed for
    # another game by a binary read, so read the flag, the colours and the descriptor words as
    # OoT3D-derived.
    #
    # Two independent lines of evidence, of very different strength, and the report below prints both
    # with a control against each so neither has to be taken on trust:
    #
    #   * PALETTE -- REFUTED by its own control. "Authored colours are spiky, a wrong offset is flat"
    #     does not hold here: the material record is mostly constant, so the median control offset holds
    #     only 4-8 distinct values, and MM3D's specular0 is MORE varied (219) than that. Do not use this
    #     argument. The report says so out loud rather than quietly dropping it.
    #   * DESCRIPTOR PROBE WORD -- validated by its own control. The u32 0x62C884C0 at material +0xDC is
    #     the most common word at exactly ONE of 84 sampled offsets, in BOTH games (11,136 OoT3D
    #     materials, 2,942 MM3D). Being distinctive to its offset is what makes the same value at the
    #     same offset in another game structural evidence rather than a coincidence. That u32 is a
    #     COMPOSITE of two typed fields -- its low half 0x84C0 is the TEV source code TEX0 -- and is
    #     deliberately not named as a colour or a light enum, because nothing recovered establishes
    #     what its high half selects.
    #
    # So the descriptor layout is strongly corroborated and the colour block is plausible by adjacency,
    # but NEITHER is the binary read. mm3d-decomp/docs has no fragment-lighting recovery and the MM3D
    # binary's equivalent function is unlocated; that read is the named next RE step. The numbers that
    # decide effort today are the consumer counts, which assume nothing.
    #
    files = 0
    materials = 0
    failures = 0
    records: list[MaterialLighting] = []
    corpus: list[tuple[str, bytes]] = []
    # What the population actually IS, so a percentage is never read without its denominator. The two
    # games are NOT the same shape: OoT3D reaches 610 inline scene CMBs from .zsi files, while MM3D
    # ships no scene container under /actors/ and its iterator reaches actor archives only. Every
    # percentage this tool prints is therefore an ACTOR-MATERIAL figure for mm and an
    # actor+scene figure for oot.
    corpus_kinds: Counter = Counter()
    try:
        for label, data in iter_corpus(args.game)():
            files += 1
            corpus.append((label, data))
            corpus_kinds["zsi-scene" if label.endswith(".zsi") else "archive-member"] += 1
            try:
                parsed = scan_materials(label, data)
                records.extend(parsed)
                mats_offset = material_chunk_pointer(data)
                if mats_offset is not None:
                    materials += _u32(data, mats_offset + 8)
            except (AssertionError, IndexError, KeyError, struct.error, ValueError):
                failures += 1
    except RuntimeError as error:
        print(error, file=sys.stderr)
        return 2

    enabled = [record for record in records if record.enabled]
    primary = [record for record in records if record.primary_uses]
    secondary = [record for record in records if record.secondary_uses]
    enabled_primary = [record for record in primary if record.enabled]
    enabled_secondary = [record for record in secondary if record.enabled]
    source_without_flag = [
        record
        for record in records
        if not record.enabled and (record.primary_uses or record.secondary_uses)
    ]
    flag_without_source = [
        record
        for record in records
        if record.enabled and not record.primary_uses and not record.secondary_uses
    ]

    if args.details:
        for record in records:
            print(
                f"{record.label} mat={record.material_index} enabled={int(record.enabled)} "
                f"frag_primary={record.primary_uses} frag_secondary={record.secondary_uses} "
                f"emission={_format_rgba(record.emission)} ambient={_format_rgba(record.ambient)} "
                f"diffuse={_format_rgba(record.diffuse)} spec0={_format_rgba(record.specular0)} "
                f"spec1={_format_rgba(record.specular1)} "
                f"descriptor={','.join(f'{word:08x}' for word in record.descriptor_words)} "
                f"chain={record.chain}"
            )

    chain_histogram = Counter(record.chain for record in enabled)
    print("enabled_chain_histogram:")
    for chain, count in chain_histogram.most_common():
        print(f"  {count:4d} {chain}")
    _report_offset_shape(records)
    # Negative control: the same RGBA read at many OTHER offsets in the material record. A palette is
    # spiky; a wrong offset is noise. If the control is as spiky as the measurement, this argument is
    # void and only the binary read settles it.
    sweep = offset_control_sweep(corpus)
    control_values = [value for _offset, value in sweep]
    control_median = sorted(control_values)[len(control_values) // 2]
    control_max = max(control_values)
    measured_max = max(len(counter) for counter in color_slot_histograms(records).values())
    print("\n== negative control: distinct RGBA at OTHER offsets in the material record ==")
    print(f"  offsets sampled: {len(sweep)} (the +0xA0..+0xB0 block excluded)")
    print(
        f"  control distinct: median={control_median} max={control_max}"
    )
    print(f"  measured block's worst slot distinct: {measured_max}")
    verdict = (
        "the measured block is SPIKIER than the typical offset -> the palette argument holds here"
        if measured_max < control_median
        else "NOT spikier than the typical offset -> this discriminator proves NOTHING and the"
        " binary read is the only way to settle the offsets"
    )
    print(f"  verdict: {verdict}")
    # The palette argument is settled (it failed). Test the structural claim instead: the descriptor
    # enum must be distinctive to its offset, or "same enum at the same offset" means nothing.
    sweep = descriptor_enum_sweep(corpus)
    hits = [(offset, count) for offset, count in sweep if count]
    total_sampled = max(1, sum(count for _o, count in sweep))
    print("\n== structural control: is the descriptor probe word distinctive to ONE offset? ==")
    print(f"  word offsets sampled: {len(sweep)}")
    print(f"  offsets where 0x{DESCRIPTOR_PROBE_WORD:08X} is the most common word: {len(hits)}")
    for offset, count in sorted(hits, key=lambda item: -item[1])[:6]:
        print(f"    +0x{offset:02X}: {count} materials")
    if len(hits) == 1:
        print(
            f"  verdict: DISTINCTIVE -- this word is dominant at exactly one offset"
            f" (+0x{hits[0][0]:02X}), so a match at the same offset in another game is structural"
            " evidence and not a coincidence of a common constant. It is a composite of two typed"
            " fields (low half 0x84C0 = TEV source code TEX0); nothing here names what the high half"
            " selects."
        )
    else:
        print(
            "  verdict: NOT distinctive -- the word is common at several offsets, so this proves"
            " nothing"
        )
    print(
        f"corpus: {dict(corpus_kinds)}"
        + (
            "  <- ACTOR MATERIALS ONLY; MM3D scene/environment materials are NOT in this population"
            if args.game == "mm"
            else "  (actors + inline scene CMBs)"
        )
    )
    print(
        f"files={files} materials={materials} fragment_enabled={len(enabled)} "
        f"fragment_primary_consumers={len(primary)} "
        f"fragment_secondary_consumers={len(secondary)} "
        f"enabled_primary_consumers={len(enabled_primary)} "
        f"enabled_secondary_consumers={len(enabled_secondary)} "
        f"source_without_flag={len(source_without_flag)} "
        f"flag_without_source={len(flag_without_source)} parse_failures={failures}"
    )
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
