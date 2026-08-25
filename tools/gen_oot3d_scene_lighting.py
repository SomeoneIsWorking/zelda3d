#!/usr/bin/env python3
"""Generate the committed OoT3D per-scene lighting palette table for the #111
vertex-lighting port.

Parses each OoT3D scene-header ZSI (`/scene/<name>_info.zsi`) env-settings command
(0x0F). Layout RE-DERIVED 2026-07-22 by byte-matching the raw ZSI region against the LIVE
Azahar oracle's runtime EnvLightSettings list (play+0x3230 -> heap list, Kokiri gameplay)
AND the live CmbVShader light uniforms (harness `vsuni_log`: c82/c85 LightAmbientColor):

    region = cmd-0x0F pointer -> [16-byte header][count x 28-byte records][path strings]

The prior parse treated the 16-byte header as a 28-byte "metadata entry 0", which phase-
shifted every record by 12 bytes. A "+1 slot bias" downstream happened to re-align l0col/
l0dir/l1col/l1dir (they repeat at stride 28) but left "amb" reading [trueAmbBlue, 72, 72]
— the constant G=B=72 across ALL scenes was dir bytes (0x48=72), not color. Record layout
(verified byte-for-byte vs the runtime list, which is a straight copy of this region):

    +0x00 f32 zFar     (projection far plane / draw distance; Kokiri 12000)
    +0x04 f32 fogFar   (fog saturation distance, eye units; Kokiri 2400)
    +0x08 u16 fogNear  (N64-style packed: low 10 bits = fogNear eye units, high 6 = blend rate;
                        Kokiri day 0xff20 -> 800)
    +0x0a .. +0x1B  the N64 EnvLightSettings colour block, byte-for-byte (DIR BEFORE COLOUR):
    +0x0a u8[3] ambient
    +0x0d s8[3] light1Dir     +0x10 u8[3] light1Color
    +0x13 s8[3] light2Dir     +0x16 u8[3] light2Color
    +0x19 u8[3] fogColor

CORRECTED 2026-07-22 (Zora's Domain divergence). The previous field map read colour BEFORE
dir inside each light group, which shifted the whole block by 3 bytes: "l0dir" was really
light2Dir, "l1dir" was really the FOG COLOUR, and the fog colour was therefore never
extracted at all (the renderer fell back to the N64 scene's fogColor). Because the ZSI
always stores light2Dir = -light1Dir, the consumer's compensating negation made light1Dir
come out right by accident — that negation is now gone with the offsets fixed. Independent
confirmation: the TITLE path (render/title_light_slots.cpp Zelda3D_TitleLightSlotsConvert, derived
from the decompiled Environment_Update consumer, title_env_lighting.md §6) already reads
this same record with dir-before-colour.

Ground truth for the fog colour (harness `vsuni_log`, Zora's Domain entrance 0x109 @0x6000):
every scene draw runs with PICA fog_mode=5 and fog_color=(104,135,181) — byte-identical to
spot07 record 1's +0x19. The N64 scene's fogColor there is (25,100,100), a dark teal, which
is what SoH was hazing toward (measured far cave wall: ours (23,83,85) vs oracle (77,110,143)).

Kokiri noon check: record 1 ambient=(181,181,160) == the live c82/c85 uniform
(0.7098,0.7098,0.62745) exactly; l0col=(255,255,219) == the live actor dif1 uniform.
Fog check: the live per-draw PICA fog LUT (harness `az_fog` + per-draw lutS) is 1.0 through
entry 125, entry 126 = 1.0/-0.0215, entry 127 = 0.979/-0.9795; with the measured gameplay
projection (camNear 7.0, zFar 12000: vsuni proj2 = (1.0006, 7.0041)) the eye-linear fog
window (fogNear=800, fogFar=2400) reproduces those node values exactly:
node(127/128): eye = b/(a-t) = 834 -> (2400-834)/(2400-800) = 0.979.

Record i == runtime slot i (NO bias): the N64 z_kankyo schedule index selects the
matching OoT3D record directly.

Output: a positional array `kZelda3dSceneLighting[]` indexed by SoH sceneNum (same order as
kZelda3dSceneNames / scene_table.h), each row = the scene's slot palette. Values only (tiny
tuning ints) — safe to commit, like zelda3d_scene_names.inc and n64_scene_lighting.json.

Run: ZELDA3D_OOT3D_ROM=<path.3ds> python3 tools/gen_oot3d_scene_lighting.py
"""
import os, re, sys, struct
sys.path.insert(0, os.path.dirname(__file__))
from ctr_romfs import CtrRom
import gen_scene_names as gsn  # reuse OVERRIDES + SCENE_TABLE row derivation

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "Shipwright/soh/src/zelda3d/tables/zelda3d_scene_lighting.inc")
STRIDE = 0x1C


def parse_env(d):
    """Return list of slots (skipping entry 0 metadata); each slot is a dict of int lists."""
    off = 16
    env = None
    while off + 8 <= len(d):
        cmd1 = struct.unpack_from(">I", d, off)[0]
        cmd2 = struct.unpack_from("<I", d, off + 4)[0]
        ctype = (cmd1 >> 24) & 0xFF
        count = (cmd1 >> 16) & 0xFF
        if ctype == 0x0F:
            env = (count, cmd2)
        off += 8
        if ctype == 0x14:
            break
    if not env:
        return []
    count, addr = env
    base = addr if (16 <= addr and addr + 16 + count * STRIDE <= len(d)) else (addr & 0xFFFFFF)
    base += 16  # 16-byte region header, then count 28-byte records (see module docstring)
    if base + count * STRIDE > len(d):
        return []
    slots = []
    for i in range(0, count):
        o = base + i * STRIDE
        def u(off, n):  # unsigned bytes
            return list(d[o + off:o + off + n])
        def s(off, n):  # signed bytes
            return [b - 256 if b >= 128 else b for b in d[o + off:o + off + n]]
        zfar, fogfar = struct.unpack_from("<ff", d, o)
        fognear_raw = struct.unpack_from("<H", d, o + 0x08)[0]
        slots.append({
            "amb": u(0x0a, 3), "l0dir": s(0x0d, 3), "l0col": u(0x10, 3),
            "l1dir": s(0x13, 3), "l1col": u(0x16, 3), "fogcol": u(0x19, 3),
            "zfar": zfar, "fogfar": fogfar, "fognear": fognear_raw & 0x3FF,
        })
    return slots


def main():
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    # name -> file path, case-insensitive
    name_path = {}
    for f in rom.iter_files():
        p = f if isinstance(f, str) else getattr(f, "path", str(f))
        m = re.match(r"/scene/([a-zA-Z0-9_]+)_info\.zsi$", p)
        if m:
            name_path[m.group(1)] = p
    ci = {n.lower(): n for n in name_path}

    # Reproduce gen_scene_names row order + name resolution.
    rows = []  # (soh, enum, oot3d_name|None)
    for line in open(gsn.SCENE_TABLE):
        m = re.search(r"DEFINE_SCENE\(\s*([A-Za-z0-9_]+)_scene\s*,\s*[A-Za-z0-9_]+\s*,\s*(SCENE_[A-Z0-9_]+)", line)
        if not m:
            continue
        soh, enum = m.group(1), m.group(2)
        name = None
        if soh in gsn.OVERRIDES:
            cand = gsn.OVERRIDES[soh]
            if cand in name_path:
                name = cand
        elif soh.lower() in ci:
            name = ci[soh.lower()]
        rows.append((soh, enum, name))

    # Parse each distinct scene's slots once.
    parsed = {}
    for _, _, name in rows:
        if name and name not in parsed:
            parsed[name] = parse_env(rom.read(rom.get(name_path[name])))

    mapped = sum(1 for _, _, n in rows if n and parsed.get(n))
    with open(OUT, "w") as o:
        o.write("// GENERATED by tools/gen_oot3d_scene_lighting.py — do not edit by hand.\n")
        o.write("// OoT3D per-scene env-light palette for the #111 vertex-lighting port.\n")
        o.write("// Native-3DS ZSI cmd 0x0F: 16-byte header + 28-byte records\n")
        o.write("// (f32 zFar, f32 fogFar, u16 fogNear, then the N64 EnvLightSettings colour\n")
        o.write("// block byte-for-byte: amb, l0dir, l0col, l1dir, l1col, fogCol).\n")
        o.write("// Row i == runtime slot i (NO bias): the N64 z_kankyo schedule index selects\n")
        o.write("// the matching OoT3D row directly. Dirs are stored dir-BEFORE-colour and are\n")
        o.write("// already in the N64 (toward-light) convention — consumers must NOT negate.\n")
        o.write("// See the generator docstring for the byte-level derivation.\n")
        o.write(f"// {mapped}/{len(rows)} scenes have a palette.\n\n")
        emitted = {}
        for name in sorted(parsed):
            slots = parsed[name]
            if not slots:
                continue
            sym = "kSlots_" + re.sub(r"[^A-Za-z0-9_]", "_", name)
            emitted[name] = sym
            o.write(f"static const Zelda3dLightSlot {sym}[] = {{ // {name}\n")
            for s in slots:
                o.write("    {{%3d,%3d,%3d},{%4d,%4d,%4d},{%3d,%3d,%3d},{%4d,%4d,%4d},{%3d,%3d,%3d},{%3d,%3d,%3d},%4d,%.0f,%.0f},\n" % (
                    *s["amb"], *s["l0dir"], *s["l0col"], *s["l1dir"], *s["l1col"], *s["fogcol"],
                    s["fognear"], s["fogfar"], s["zfar"]))
            o.write("};\n")
        o.write("\nstatic const Zelda3dSceneLight kZelda3dSceneLighting[] = {\n")
        for i, (soh, enum, name) in enumerate(rows):
            if name and parsed.get(name):
                o.write(f"    /* 0x{i:02X} {enum:<36} */ {{ {len(parsed[name])}, {emitted[name]} }},\n")
            else:
                o.write(f"    /* 0x{i:02X} {enum:<36} */ {{ 0, 0 }},\n")
        o.write("};\n")
    print(f"wrote {OUT}: {mapped}/{len(rows)} scenes have a palette")


if __name__ == "__main__":
    main()
