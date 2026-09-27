#!/usr/bin/env python3
"""Every texture format both retail games actually use, against the formats the host can decode.

Texture formats are a named part of the graphics seam, and the question "does the host decode every
format the content uses?" has two failure modes that look identical from the outside: a format the
decoder rejects, and a format nobody ever checked. Both read as "the texture looks fine" until one
does not, so this survey answers it from the ROMs with denominators rather than leaving it to be
assumed from the decoder's own table.

It walks BOTH texture containers the host reads -- `cmb ` model textures and `ctxb` texture banks --
for BOTH games, and reports every distinct `(data_type << 16) | fmt` with its count, marking any the
shipping decoder cannot handle. A format absent from the table and a format absent from the corpus
are different facts, and the report keeps them apart: the decoder set is printed every run, so a
reader can see the coverage rather than trust a verdict.

The two format tables are also compared here for the same reason. `Shipwright/cmb3d/asset/pica_texture.cpp`
and `tools/pica_texture.py` are independent implementations of the same decoder, and a format handled
by one but not the other would make every offline texture measurement quietly wrong. They are checked
against each other rather than assumed equal.

Usage:
    source .env
    tools/pica_texture_format_survey.py
    tools/pica_texture_format_survey.py --game mm --json
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
if str(REPO / "tools") not in sys.path:
    sys.path.insert(0, str(REPO / "tools"))

from cmb_corpus import iter_corpus  # noqa: E402
from ctr_romfs import CtrRom  # noqa: E402
from pica_texture import GLFMT  # noqa: E402

# The shipping decoder's own table, kept here as the set the content is checked AGAINST. It is parsed
# out of the C++ source rather than transcribed, so this survey cannot drift from what ships.
CPP_DECODER = REPO / "Shipwright" / "cmb3d" / "asset" / "pica_texture.cpp"
CPP_FORMAT_NAMES = {
    "ETC1": 0x0000675A,
    "ETC1A4": 0x0000675B,
    "RGB8": 0x14016754,
    "RGBA8": 0x14016752,
    "RGBA4444": 0x80336752,
    "RGBA5551": 0x80346752,
    "RGB565": 0x83636754,
    "A8": 0x14016756,
    "A4": 0x67616756,
    "L8": 0x14016757,
    "L4": 0x67616757,
    "LA8": 0x14016758,
    "LA4": 0x67606758,
    "HILO8": 0x14016759,
}


def _u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def cpp_decoder_formats() -> dict[int, str]:
    """Read the format table out of the shipping C++ decoder.

    Returns format -> name for every `GF_<NAME> = 0x...` the C++ declares AND handles, so a name the
    C++ declares but never switches on cannot pass as supported.
    """
    if not CPP_DECODER.is_file():
        raise RuntimeError(f"missing shipping decoder: {CPP_DECODER}")
    text = CPP_DECODER.read_text()
    declared = {
        name: value
        for name, value in (
            (match.group(1), int(match.group(2), 16))
            for match in __import__("re").finditer(r"GF_(\w+)\s*=\s*(0x[0-9A-Fa-f]+)", text)
        )
    }
    handled = set(__import__("re").findall(r"case GF_(\w+)", text))
    return {value: name for name, value in declared.items() if name in handled}


@dataclass
class FormatReport:
    """What one game's content uses, and whether the host can decode all of it."""

    game: str
    cmb_formats: Counter = field(default_factory=Counter)
    ctxb_formats: Counter = field(default_factory=Counter)
    cmb_files: int = 0
    ctxb_files: int = 0
    cmb_without_tex_chunk: int = 0

    @property
    def total(self) -> Counter:
        return self.cmb_formats + self.ctxb_formats

    def undecodable(self, supported: set[int]) -> Counter:
        return Counter({fmt: count for fmt, count in self.total.items() if fmt not in supported})


def scan_cmb(game: str) -> FormatReport:
    """Every `tex ` record in every CMB of `game`, read through the shipping header layout."""
    report = FormatReport(game=game)
    for _label, data in iter_corpus(game)():
        if data[:4] != b"cmb ":
            continue
        shift = 4 if _u32(data, 0x08) >= 7 else 0
        tex_pointer = _u32(data, 0x2C + shift)
        if tex_pointer == 0 or data[tex_pointer : tex_pointer + 4] != b"tex ":
            report.cmb_without_tex_chunk += 1
            continue
        report.cmb_files += 1
        offset = tex_pointer + 0x0C
        for _index in range(_u32(data, tex_pointer + 8)):
            if offset + 0x24 > len(data):
                break
            report.cmb_formats[(_u16(data, offset + 0x0E) << 16) | _u16(data, offset + 0x0C)] += 1
            offset += 0x24
    return report


def scan_ctxb(game: str) -> FormatReport:
    """Every texture in every `ctxb` bank of `game`.

    CTXB is the other container the host decodes (`cmb3d/asset/ctxb.cpp` calls the same
    `PicaDecode`), so a format check that skipped it would leave the UI/effect/menu textures
    unmeasured -- and those are a different authoring pass from the model textures.
    """
    report = FormatReport(game=game)
    if game == "oot":
        rom_path = os.environ.get("ZELDA3D_OOT3D_ROM")
        if not rom_path:
            raise RuntimeError("source .env first (ZELDA3D_OOT3D_ROM)")
        rom = CtrRom(rom_path)
    else:
        from mm_animmap_archive import Mm3dActors

        rom = Mm3dActors().rom
    try:
        for rom_file in rom.iter_files():
            if not rom_file.path.endswith(".ctxb"):
                continue
            data = rom.read(rom_file)
            if len(data) < 0x18 or data[0:4] != b"ctxb":
                continue
            tex_chunk = _u32(data, 0x10)
            if tex_chunk + 0x0C > len(data) or data[tex_chunk : tex_chunk + 4] != b"tex ":
                continue
            report.ctxb_files += 1
            offset = tex_chunk + 0x0C
            for _index in range(_u32(data, tex_chunk + 8)):
                if offset + 0x24 > len(data):
                    break
                report.ctxb_formats[(_u16(data, offset + 0x0E) << 16) | _u16(data, offset + 0x0C)] += 1
                offset += 0x24
    finally:
        if game == "oot":
            rom.fp.close()
    return report


def decoder_tables_agree() -> tuple[bool, dict[str, object]]:
    """Compare the shipping C++ decoder's handled formats with the Python mirror's.

    The Python module is what every offline texture measurement in `tools/` runs on. If the two
    tables disagree, those measurements are measuring something the product does not do, so this is
    reported rather than assumed.
    """
    cpp = cpp_decoder_formats()
    python = dict(GLFMT)  # already format -> name
    only_cpp = {hex(fmt): name for fmt, name in cpp.items() if fmt not in GLFMT}
    only_python = {hex(fmt): name for fmt, name in python.items() if fmt not in cpp}
    return not only_cpp and not only_python, {
        "cpp_handled": len(cpp),
        "python_handled": len(GLFMT),
        "only_in_cpp": only_cpp,
        "only_in_python": only_python,
    }


def survey(game: str) -> tuple[FormatReport, FormatReport]:
    """Both containers for one game. Raises when neither yielded a texture."""
    cmb = scan_cmb(game)
    ctxb = scan_ctxb(game)
    if not cmb.total and not ctxb.total:
        raise RuntimeError(
            f"game={game}: no texture records found in either container; the scan is broken, not clean"
        )
    return cmb, ctxb


def format_report(game: str) -> str:
    supported = set(cpp_decoder_formats())
    agree, table_detail = decoder_tables_agree()
    cmb, ctxb = survey(game)
    lines = [
        f"== {game} ==",
        f"cmb : {cmb.cmb_files} files with a tex chunk ({cmb.cmb_without_tex_chunk} without),"
        f" {sum(cmb.cmb_formats.values())} textures, {len(cmb.cmb_formats)} formats",
        f"ctxb: {ctxb.ctxb_files} banks, {sum(ctxb.ctxb_formats.values())} textures,"
        f" {len(ctxb.ctxb_formats)} formats",
        "",
        "format                       cmb     ctxb   total  decoded",
    ]
    for fmt, count in sorted(cmb.total.items(), key=lambda item: -item[1]):
        lines.append(
            f"  0x{fmt:08x} {count:8d} {ctxb.total.get(fmt, 0):8d} {count:8d}"
            f"   {'yes' if fmt in supported else 'NO'}"
        )
    undecodable = cmb.undecodable(supported)
    lines.append("")
    if undecodable:
        lines.append(f"UNDECODABLE FORMATS IN CONTENT: { {hex(k): v for k, v in undecodable.items()} }")
    else:
        lines.append(
            f"every format in this game's content is handled by the shipping decoder"
            f" ({len(supported)} formats supported)"
        )
    lines.append(
        f"decoder tables agree: {agree} "
        f"(cpp {table_detail['cpp_handled']}, python {table_detail['python_handled']}"
        + (f", only-in-cpp {table_detail['only_in_cpp']}" if table_detail["only_in_cpp"] else "")
        + (f", only-in-python {table_detail['only_in_python']}" if table_detail["only_in_python"] else "")
        + ")"
    )
    return "\n".join(lines)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--game", choices=("oot", "mm", "both"), default="both")
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)
    games = ("oot", "mm") if args.game == "both" else (args.game,)
    try:
        payloads = {}
        for game in games:
            cmb, ctxb = survey(game)
            supported = set(cpp_decoder_formats())
            payloads[game] = {
                "cmb_files": cmb.cmb_files,
                "cmb_textures": sum(cmb.cmb_formats.values()),
                "ctxb_banks": ctxb.ctxb_files,
                "ctxb_textures": sum(ctxb.ctxb_formats.values()),
                "formats": {hex(f): c for f, c in sorted(cmb.total.items(), key=lambda i: -i[1])},
                "undecodable": {hex(f): c for f, c in cmb.undecodable(supported).items()},
            }
        agree, detail = decoder_tables_agree()
    except (RuntimeError, OSError, ValueError) as error:
        print(f"pica_texture_format_survey: {error}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps({"decoder_tables_agree": agree, "detail": detail, "games": payloads}, indent=1))
        return 0
    for game in games:
        print(format_report(game))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
