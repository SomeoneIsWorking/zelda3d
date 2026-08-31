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

from cmb_corpus import iter_cmbs
from tev_corpus_survey import parse_mats, slots_used

FRAGMENT_PRIMARY = 0x6210
FRAGMENT_SECONDARY = 0x6211


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
    mats_offset = _u32(data, 0x28)
    if mats_offset == 0 or data[mats_offset : mats_offset + 4] != b"mats":
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


def main(arguments: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--details", action="store_true", help="print every relevant material")
    args = parser.parse_args(arguments)

    files = 0
    materials = 0
    failures = 0
    records: list[MaterialLighting] = []
    try:
        for label, data in iter_cmbs():
            files += 1
            try:
                parsed = scan_materials(label, data)
                records.extend(parsed)
                mats_offset = _u32(data, 0x28)
                if mats_offset != 0 and data[mats_offset : mats_offset + 4] == b"mats":
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
                f"descriptor={','.join(f'{word:08x}' for word in record.descriptor_words)} chain={record.chain}"
            )

    chain_histogram = Counter(record.chain for record in enabled)
    print("enabled_chain_histogram:")
    for chain, count in chain_histogram.most_common():
        print(f"  {count:4d} {chain}")
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
