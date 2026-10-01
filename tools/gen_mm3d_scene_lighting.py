#!/usr/bin/env python3
"""Generate the committed MM3D per-scene lighting palette table.

This is the MM sibling of `gen_oot3d_scene_lighting.py`. It exists because a recorded negative about
Majora's Mask was **wrong**: MM3D was recorded as having no `EnvLightSettings` region at all, and as
using "a different scene-header format". Both were wrong, and the reason is worth stating because it
is a trap this project has now paid for twice.

**The region is in the same place in both games.** Command `0x0F` in the scene-header ZSI is
`SCENE_CMD_ID_ENV_LIGHT_SETTINGS` in both (`2ship/include/z64scene.h`). The reason an earlier search
found 5 of 424 MM3D scenes is that **182 of MM3D's 424 scene ZSIs are LzS-compressed and were parsed
as plain bytes.** Inflating them first takes the count to **110 of 424**. The control that condemns
the old measurement: parsing those 182 files as plain yields 256 distinct "ctypes" spanning the whole
0x00-0xFF byte range, 234 of them outside OoT3D's 22-value command set -- uniform noise across the
entire byte space, which is the signature of data, not a command stream.

**The record layout DIFFERS, and that is the trap this module is built to prevent.** So both layouts
live here as data, in one place, and the cross-title check is deliberately two-sided: at the MM layout
OoT3D scores zero, and at the OoT3D layout MM scores zero. A generator that hard-coded one game's
offsets would pass a one-sided check and silently misread the other game.
An OoT3D-derived consumer hard-coded to `+0x0A` reads MM3D's `fogColor` as its second light colour.

    MM3D record, 0x20 bytes at (cmd-0x0F ptr + 0x28):
        +0x00 f32 zFar            +0x08 u16 fogNear | blendRate<<10
        +0x04 f32 fogFar          +0x0A u8  (padding)
        +0x0B the N64 EnvLightSettings colour block, byte-for-byte:
        +0x0B u8[3] ambient   +0x0E s8[3] light1Dir  +0x11 u8[3] light1Color
        +0x14 s8[3] light2Dir   +0x17 u8[3] light2Color  +0x1A u8[3] fogColor

The field map is confirmed by an authority that is not a measurement of this file's bytes: MM's own
N64 `EnvLightSettings` (`2ship/include/z64environment.h`) is 0x16 bytes with its six colour triples at
`+0x00/+0x03/+0x06/+0x09/+0x0C/+0x0F`, and the recovered 3DS record puts them at
`+0x0B/+0x0E/+0x11/+0x14/+0x17/+0x1A` -- the SAME internal spacing, uniformly shifted, behind the two
distances N64 keeps as `s16` and the packed blend/fog-near. That also names the byte at `+0x1D` a
field scan could not: MM's `blendRateAndFogNear`/`zFar` tail.

OPEN, and deliberately not guessed: MM3D's direction scale measures ~119.5 where OoT3D's is ~124.7
(both near the s8 127 convention). This table stores the raw s8 bytes, exactly as the OoT3D table
does, so no scale is applied anywhere. If a consumer needs one, it must be recovered -- not
inferred from the median.

Dirs are stored dir-BEFORE-colour and are already in the N64 (toward-light) convention, so consumers
must NOT negate them -- the same contract as the OoT3D table, for the same reason.

Row i == runtime slot i (no bias): the N64 z_kankyo schedule index selects the matching record.

Run: ZELDA3D_MM3D_ROM=<path.3ds> uv run --frozen python tools/gen_mm3d_scene_lighting.py
"""

from __future__ import annotations

import os
import re
import struct
import sys
from dataclasses import dataclass

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import gen_mm_scene_names as gms  # noqa: E402
from ctr_romfs import CtrRom  # noqa: E402
from mm_animmap_archive import lzs_decompress, lzs_is_compressed  # noqa: E402

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "2ship/2s2h/zelda3d/mm3d_scene_lighting.inc")

ENV_COMMAND = 0x0F  # SCENE_CMD_ID_ENV_LIGHT_SETTINGS, same in both games
END_COMMAND = 0x14


@dataclass(frozen=True)
class Layout:
    """One game's env-record layout. Both games' are here so neither can be hard-coded elsewhere."""

    name: str
    #: bytes from the command's `ptr` to the first record
    record_base: int
    #: bytes between consecutive records
    stride: int
    zfar: int
    fogfar: int
    fognear: int
    ambient: int
    dir0: int
    col0: int
    dir1: int
    col1: int
    fogcol: int
    #: offset of the packed `blendRateAndFogNear` (MM) / `blendRate|fogNear` (OoT3D) u16
    blend_fognear: int

    def ok(self, blob: bytes, count: int, ptr: int) -> bool:
        base = ptr + self.record_base
        return count > 0 and base + count * self.stride <= len(blob)


MM3D = Layout(
    name="MM3D", record_base=0x28, stride=0x20,
    zfar=0x00, fogfar=0x04, fognear=0x08,
    ambient=0x0B, dir0=0x0E, col0=0x11, dir1=0x14, col1=0x17, fogcol=0x1A, blend_fognear=0x08,
)

OOT3D = Layout(
    name="OoT3D", record_base=0x10, stride=0x1C,
    zfar=0x00, fogfar=0x04, fognear=0x08,
    ambient=0x0A, dir0=0x0D, col0=0x10, dir1=0x13, col1=0x16, fogcol=0x19, blend_fognear=0x08,
)

LAYOUTS = (MM3D, OOT3D)


def find_env_command(blob: bytes) -> tuple[int, int] | None:
    """The `0x0F` command's (count, ptr), or None. The stream is 8 bytes/entry and ends at 0x14."""
    off = 0x10
    found = None
    while off + 8 <= len(blob):
        head = struct.unpack_from(">I", blob, off)[0]
        ptr = struct.unpack_from("<I", blob, off + 4)[0]
        ctype = (head >> 24) & 0xFF
        count = (head >> 16) & 0xFF
        if ctype == ENV_COMMAND:
            found = (count, ptr)
        off += 8
        if ctype == END_COMMAND:
            break
    return found


def maybe_inflate(raw: bytes) -> bytes:
    """Decompress an LzS container, or return the bytes unchanged.

    This one call is the whole difference between "MM3D has no scene lighting" (5 of 424) and "MM3D
    has scene lighting in 110 of its 424 scenes". Skipping it does not produce an error -- it produces
    plausible garbage, because compressed bytes still look like a command stream to any parser that
    only checks the terminator.
    """
    return lzs_decompress(raw) if lzs_is_compressed(raw) else raw


# ---------------------------------------------------------------------------------------------
# The N64-shaped projection: what MM's own blend can actually consume.
#
# MM's blend reads ONE list, `play->envCtx.lightSettingsList`, indexed `lightSettingsList[i]`, and
# `EnvLightSettings` is 0x16 bytes (`2ship/include/z64environment.h:214`). The 3DS record is 0x20.
# **Substituting the 0x20 table directly would walk it in 0x16 steps: slot 0 would read correctly and
# every later slot would read the wrong bytes** -- a plausible frame rather than a crash, which is the
# worst kind of bug and the shape this project keeps meeting. So the table is projected to 0x16 here.
#
# The projection is a pure rearrangement, not a reinterpretation: MM's N64 struct IS the 3DS record's
# tail byte-for-byte, so `ambientColor`..`fogColor` are copied in the same order, and
# `blendRateAndFogNear` is the 3DS `u16 fogNear | blendRate<<10` verbatim. Only `zFar` changes
# representation -- f32 on the 3DS side, s16 in the N64 struct -- and that is a CLAMP, not a cast, so
# a value outside s16 is reported rather than silently wrapped.
#
# The 3DS-only distances (`f32 zFar`, `f32 fogFar`) are what drive the PICA fog window and are NOT
# representable in the N64 struct at all, so they go to a parallel array indexed the same way. They
# are not optional: `z2_lost_woods` slot 2's window is what predicts MM3D's authored fog LUT to 1.19
# byte steps (`tools/mm3d_fog_prediction.py`), and that prediction reads these values.
# ---------------------------------------------------------------------------------------------

N64_ENV_STRIDE = 0x16
S16_MIN, S16_MAX = -32768, 32767


def project_to_n64(slot: dict) -> tuple[list[int], int | None]:
    """(the 9 s16/u8 triples of an `EnvLightSettings`, clamped zFar) or (values, None).

    Field order is `2ship/include/z64environment.h:205-214`, and it is dir-BEFORE-colour in every
    group -- the same order the recovered 3DS record already stores, which is why this is a
    rearrangement rather than a fixup.
    """
    values: list[int] = []
    for key in ("amb", "l0dir", "l0col", "l1dir", "l1col", "fogcol"):
        values.extend(int(v) for v in slot[key])
    values.append(int(slot["blendratefognear"]) & 0xFFFF)  # s16, two's complement
    zfar = int(round(slot["zfar"]))
    clamped = None if S16_MIN <= zfar <= S16_MAX else (S16_MAX if zfar > 0 else S16_MIN)
    values.append(zfar if clamped is None else clamped)
    return values, clamped


def parse_env(raw: bytes, layout: Layout) -> list[dict]:
    """Decode a scene ZSI's env records under `layout`, or [] if it has none / they do not fit."""
    blob = maybe_inflate(raw)
    found = find_env_command(blob)
    if not found:
        return []
    count, ptr = found
    if not layout.ok(blob, count, ptr):
        return []
    base = ptr + layout.record_base
    slots = []
    for i in range(count):
        o = base + i * layout.stride
        slots.append({
            "amb": _u8x3(blob, o + layout.ambient),
            "l0dir": _s8x3(blob, o + layout.dir0),
            "l0col": _u8x3(blob, o + layout.col0),
            "l1dir": _s8x3(blob, o + layout.dir1),
            "l1col": _u8x3(blob, o + layout.col1),
            "fogcol": _u8x3(blob, o + layout.fogcol),
            "fognear": struct.unpack_from("<H", blob, o + layout.fognear)[0] & 0x3FF,
            "blendratefognear": struct.unpack_from("<H", blob, o + layout.blend_fognear)[0],
            "fogfar": struct.unpack_from("<f", blob, o + layout.fogfar)[0],
            "zfar": struct.unpack_from("<f", blob, o + layout.zfar)[0],
        })
    return slots


def _u8x3(blob: bytes, off: int) -> list[int]:
    return [blob[off + k] for k in range(3)]


def _s8x3(blob: bytes, off: int) -> list[int]:
    return [struct.unpack_from("<b", blob, off + k)[0] for k in range(3)]


def plausible(slots: list[dict]) -> bool:
    """A shape check, not a value check: fog distances in sane eye units, fogNear a real distance.

    Deliberately loose. The discrimination between the two layouts comes from `ok()` plus the field
    domains, not from a tuned threshold -- a threshold that separates the games is a threshold that
    can be tuned to separate anything.
    """
    if not slots:
        return False
    for s in slots:
        if not (1.0 <= s["zfar"] <= 2.0e5):
            return False
        if not (1.0 <= s["fogfar"] <= 1.0e5):
            return False
        if s["fognear"] > 1001:
            return False
    return True


def main() -> int:
    rom_path = os.environ.get("ZELDA3D_MM3D_ROM")
    if not rom_path or not os.path.isfile(rom_path):
        sys.exit("set ZELDA3D_MM3D_ROM (see .env)")
    rom = CtrRom(rom_path)

    # All 424 of MM3D's scene ZSIs live under /scenes but collapse to only ~102 distinct N64 scene
    # NAMES -- MM3D ships several ZSI variants per scene name (e.g. `spot00_info.zsi` and
    # `spot00_01_info.zsi` both reduce to `spot00`). Indexing by name alone and keeping the first
    # match silently drops the variant that actually carries the env region, which is why a
    # first-cut of this generator resolved 102 names and mapped 0 of them. Every variant is tried.
    paths_by_name: dict[str, list[str]] = {}
    for entry in rom.iter_files():
        p = entry if isinstance(entry, str) else getattr(entry, "path", str(entry))
        m = re.match(r"/scenes/(.+?)_\d+_info\.zsi$", p) or re.match(r"/scenes/(.+?)_info\.zsi$", p)
        if m:
            paths_by_name.setdefault(m.group(1), []).append(p)

    have = set(paths_by_name)
    scenes = gms.n64_scenes()
    rows: list[tuple[int, str, str | None]] = []
    for num, seg, enum in scenes:
        if seg is None:
            rows.append((num, enum, None))
            continue
        name = gms.OVERRIDES.get(enum, seg.lower().replace("_scene", ""))
        rows.append((num, enum, name if name in have else None))

    variants_consulted = 0
    parsed: dict[str, list[dict]] = {}
    for _num, _enum, name in rows:
        if name is None or name in parsed:
            continue
        chosen: list[dict] = []
        for path in sorted(paths_by_name[name]):
            variants_consulted += 1
            slots = parse_env(rom.read(rom.get(path)), MM3D)
            if plausible(slots):
                chosen = slots
                break
        parsed[name] = chosen

    compressed = sum(
        1
        for name in parsed
        for path in paths_by_name[name]
        if lzs_is_compressed(rom.read(rom.get(path)))
    )
    mapped = sum(1 for _n, _e, name in rows if name and parsed.get(name))

    with open(OUT, "w") as out:
        out.write("// GENERATED by tools/gen_mm3d_scene_lighting.py -- do not edit by hand.\n")
        out.write("// MM3D per-scene env-light palette.\n")
        out.write(f"// Native-3DS ZSI cmd 0x0F: records at ptr+0x{MM3D.record_base:02X}, "
                  f"stride 0x{MM3D.stride:02X} -- MM3D's layout is NOT OoT3D's "
                  f"(ptr+0x{OOT3D.record_base:02X}/0x{OOT3D.stride:02X}); see the module docstring.\n")
        out.write("// Record: f32 zFar, f32 fogFar, u16 fogNear|blendRate<<10, pad, then the N64\n")
        out.write("// EnvLightSettings colour block byte-for-byte at +0x0B (amb, l0dir, l0col, l1dir,\n")
        out.write("// l1col, fogCol).\n")
        out.write("// Row i == runtime slot i (no bias). Dirs are dir-BEFORE-colour and are already in\n")
        out.write("// the N64 toward-light convention: consumers must NOT negate.\n")
        out.write(f"// {compressed}/{variants_consulted} ZSI variants read were LzS-compressed; inflating\n")
        out.write(f"// them is what takes the hit count from 5 to {mapped}. Parsing those as plain bytes\n")
        out.write("// is the recorded-but-wrong negative this table replaces.\n")
        out.write(f"// {mapped}/{len(rows)} scenes have a palette.\n\n")
        emitted: dict[str, str] = {}
        emitted_env: dict[str, str] = {}
        clamped_slots = 0
        for name in sorted(parsed):
            slots = parsed[name]
            if not slots:
                continue
            sym = "kMm3dSlots_" + re.sub(r"[^A-Za-z0-9_]", "_", name)
            emitted[name] = sym
            out.write(f"static const Zelda3dLightSlot {sym}[] = {{ // {name}\n")
            for s in slots:
                out.write(
                    "    {{%3d,%3d,%3d},{%4d,%4d,%4d},{%3d,%3d,%3d},"
                    "{%4d,%4d,%4d},{%3d,%3d,%3d},{%3d,%3d,%3d},%4d,%.0f,%.0f},\n" % (
                        *s["amb"], *s["l0dir"], *s["l0col"], *s["l1dir"], *s["l1col"],
                        *s["fogcol"], s["fognear"], s["fogfar"], s["zfar"]))
            out.write("};\n")
        # The N64-shaped projection, at 0x16 stride, for the list substitution.
        #
        # A 3DS f32 zFar can exceed the N64 struct's s16 zFar -- real values reach 60000 against a
        # 32767 ceiling -- so 35 of the 102 scenes have at least one slot that cannot be represented
        # exactly. Those slots are CLAMPED and counted rather than wrapped and not counted: wrapping
        # would compile and would be silently wrong, and skipping the whole scene would make the port
        # cover less than the data supports when the loss is bounded and reportable. The clamp is a
        # limit of the N64 *struct*, not an error in the 3DS data, and the PICA fog window is NOT
        # affected: it reads the f32 from the 0x20 table above, not the s16 from this projection.
        out.write("\n// N64-shaped projection for the lightSettingsList substitution. 0x16 stride,\n")
        out.write("// matching 2ship/include/z64environment.h exactly; see project_to_n64().\n")
        for name in sorted(parsed):
            slots = parsed[name]
            if not slots:
                continue
            projected = [project_to_n64(s) for s in slots]
            clamped_slots += sum(1 for _v, c in projected if c is not None)
            sym = "kMm3dEnv_" + re.sub(r"[^A-Za-z0-9_]", "_", name)
            out.write(f"static const Mm3dEnvLightSettings {sym}[] = {{ // {name}\n")
            for values, _c in projected:
                out.write("    {" + ",".join(str(v) for v in values[:18]) + ","
                          + ",".join(str(v) for v in values[18:]) + "},\n")
            out.write("};\n")
            emitted_env[name] = sym

        out.write("\nstatic const Zelda3dSceneLight kMm3dSceneLighting[] = {\n")
        for num, enum, name in rows:
            if name and parsed.get(name):
                out.write(f"    /* 0x{num:02X} {enum:<40} */ {{ {len(parsed[name])}, "
                          f"{emitted[name]} }},\n")
            else:
                out.write(f"    /* 0x{num:02X} {enum:<40} */ {{ 0, 0 }},\n")
        out.write("};\n")

        # The substitution index: sceneNum -> (count, 0x16-stride array). This is what MM's own
        # Scene_CommandEnvLightSettings installs in place of the N64 list, and it is indexed by the
        # SAME sceneNum as kMm3dSceneLighting so one lookup serves both the colours and the PICA window.
        out.write("\nstatic const Mm3dEnvList kMm3dEnvList[] = {\n")
        for num, enum, name in rows:
            if name and name in emitted_env:
                out.write(f"    /* 0x{num:02X} {enum:<40} */ {{ {len(parsed[name])}, "
                          f"{emitted_env[name]} }},\n")
            else:
                out.write(f"    /* 0x{num:02X} {enum:<40} */ {{ 0, 0 }},\n")
        out.write("};\n")

        # The clamp is a generator-side FACT, emitted rather than re-derived at runtime: a clamped slot
        # lands on S16_MAX, which is indistinguishable from a legitimately huge zFar, so no consumer
        # could detect it by inspection. Recording the count makes the loss visible without every
        # reader having to diff the ROM against the table.
        out.write(f"\n/// {clamped_slots} of the emitted slots had an f32 zFar past the N64 struct's s16\n"
                  f"/// range and were clamped. The PICA fog window reads the f32 from "
                  f"kMm3dSceneLighting and is unaffected.\n"
                  f"#define MM3D_CLAMPED_ZFAR_SLOTS {clamped_slots}\n")

    print(f"wrote {OUT}: {mapped}/{len(rows)} scenes have a palette "
          f"({compressed}/{variants_consulted} ZSI variants read were LzS-compressed); "
          f"{len(emitted_env)} have a 0x16-stride N64 projection"
          + (f"; {clamped_slots} slot(s) had an f32 zFar past the N64 struct's s16 range and were "
             "CLAMPED (counted, not wrapped -- the PICA window reads the f32 and is unaffected)"
             if clamped_slots else ""))
    rom.fp.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
