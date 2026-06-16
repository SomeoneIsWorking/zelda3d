#!/usr/bin/env python3
# Data-driven character-replacement maps. Dump skeleton data from BOTH games to text/JSON, then
# emit a precomputed bone-correspondence map the game references directly (no runtime matching).
#   OoT3D side: parsed offline from each actor ZAR (cmb.py) -> tools/skeldata/oot3d_skeletons.json
#   N64 side:   captured in-game via SOH3D_SKELDUMP -> tools/skeldata/n64/<zarbase>.txt
#   bonemap:    soh3d_skel_match.match() per character (where N64 data exists) -> bonemap.json
# Run with SOH3D_3DS_ROM set. See SKILL soh3d-game-control + PROGRESS.
import os, sys, json, math, re
sys.path.insert(0, 'tools')
from ctr_romfs import CtrRom
from zar import Zar
import cmb as C
import soh3d_skel_match as M

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SK = os.path.join(REPO, 'tools', 'skeldata')

def oot3d_skeleton(rom, zarname):
    zf = Zar(rom.read(rom.get(zarname)))
    cmbs = [f for f in zf.files if f.name.lower().endswith('.cmb')]
    if not cmbs:
        return None
    main = max(cmbs, key=lambda f: f.size)
    m = C.Cmb(zf.read(main))
    bones = []
    for b in m.bones:
        L = math.sqrt(sum(c*c for c in b.trans))
        bones.append(dict(id=b.id, parent=b.parent,
                          trans=[round(x,2) for x in b.trans], length=round(L,2)))
    return dict(cmb=main.name, bones=bones)

def main():
    rom = CtrRom(os.environ["SOH3D_3DS_ROM"])
    inc = open(os.path.join(REPO, 'Shipwright/soh/src/soh3d/soh3d_object_zars.inc')).read()
    zars = sorted(set(re.findall(r'"(/actor/[a-z0-9_]+\.zar)"', inc)))
    skels = {}
    for z in zars:
        try:
            sk = oot3d_skeleton(rom, z)
        except Exception:
            sk = None
        if sk and len(sk['bones']) > 1:
            skels[z] = sk
    os.makedirs(SK, exist_ok=True)
    json.dump(skels, open(os.path.join(SK, 'oot3d_skeletons.json'), 'w'), indent=1)
    print("OoT3D skeletons dumped:", len(skels), "-> tools/skeldata/oot3d_skeletons.json")

    # Build the bonemap for EVERY skinned character: extract its N64 skeleton offline from the
    # N64 ROM (zar /actor/zelda_<x>.zar -> N64 object_<x>), match against the OoT3D skeleton.
    import n64_skel_extract as N
    n64rom = open(N.ROM, 'rb').read()
    # zar -> N64 object name (zelda_boj -> object_boj). Override the few that don't fit the pattern.
    OBJ_OVERRIDE = {}
    want = {}
    for z in skels:
        base = os.path.basename(z)[:-4]            # zelda_boj
        obj = OBJ_OVERRIDE.get(z) or ('object_' + base[len('zelda_'):] if base.startswith('zelda_') else None)
        if obj:
            want[obj] = z
    objs, dma = N.load_objects(n64rom, set(want))
    print("N64 ROM dmadata @ 0x%X; objects extracted: %d/%d" % (dma, len(objs), len(want)))
    bonemap = {}
    miss = []
    for obj, zar in sorted(want.items()):
        try:
            so = N.skel_offset_from_xml(obj)
            if so is None or obj not in objs:
                miss.append((obj, 'no-xml-skel' if so is None else 'no-rom-file')); continue
            lc, limbs = N.parse_skeleton(objs[obj], so)
            nN = M.build_n64_nodes(limbs)
            nO = {b['id']: dict(id=b['id'], parent=b['parent'], length=b['length']) for b in skels[zar]['bones']}
            bmap = M.match(nN, nO)
            # QUALITY GATE: only keep a map if (almost) every non-root OoT3D bone got a live N64
            # limb. A partial map leaves bones at rest while neighbors animate -> the skinned mesh
            # collapses/distorts (worse than the N64 model). Such chars get NO entry -> the in-game
            # guard falls them back to N64 until the matcher/data improves.
            nbones = len(skels[zar]['bones'])
            # count over ALL OoT3D bones (1..n-1; bone 0 is the root, -1 by design). Bones the
            # matcher never paired aren't in bmap at all -> treated as unmapped.
            mapped = sum(1 for b in range(1, nbones) if bmap.get(b, -1) >= 0)
            quality = mapped / (nbones - 1) if nbones > 1 else 0.0
            if quality < 0.9:
                miss.append((obj, 'partial-map %d/%d' % (mapped, nbones - 1))); continue
            n64len = sum(nN[i]['length'] for i in nN if nN[i]['parent'] >= 0)
            oot3dlen = sum(b['length'] for b in skels[zar]['bones'] if b['parent'] >= 0)
            bonemap[zar] = dict(
                object=obj,
                n64_limb_count=len(nN),
                oot3d_bone_count=len(skels[zar]['bones']),
                scale_ratio=round(n64len / oot3dlen, 5) if oot3dlen else 0,  # * actor->scale = world scale
                bone_to_limb={str(b): bmap[b] for b in sorted(bmap)},
            )
        except Exception as e:
            miss.append((obj, str(e)[:40]))
    json.dump(bonemap, open(os.path.join(SK, 'bonemap.json'), 'w'), indent=1)
    print("bonemap entries:", len(bonemap), "-> tools/skeldata/bonemap.json   (missed %d)" % len(miss))
    if miss:
        print("  missed:", ', '.join('%s(%s)' % m for m in miss[:25]))

    # Emit the game-side include: a flat table the retarget references directly (data-driven,
    # no runtime matching). boneToLimb is indexed by OoT3D bone id (-1 = no live joint -> rest).
    inc_path = os.path.join(REPO, 'Shipwright/soh/src/soh3d/soh3d_bonemap.inc')
    with open(inc_path, 'w') as f:
        f.write("// GENERATED by tools/soh3d_skel_export.py from tools/skeldata/bonemap.json — do not hand-edit.\n")
        f.write("// Per-character N64<->OoT3D bone correspondence + scale, precomputed offline.\n")
        f.write("typedef struct {\n")
        f.write("    const char* zar;\n")
        f.write("    float scaleRatio;   // * actor->scale = OoT3D->world scale\n")
        f.write("    int boneCount;\n")
        f.write("    signed char boneToLimb[64]; // [oot3d bone id] -> N64 limb index (-1 = rest)\n")
        f.write("} SoH3DBoneMap;\n\n")
        f.write("static const SoH3DBoneMap kSoH3dBoneMaps[] = {\n")
        for zar in sorted(bonemap):
            e = bonemap[zar]
            n = e['oot3d_bone_count']
            arr = [str(e['bone_to_limb'].get(str(i), -1)) for i in range(n)]
            f.write('    { "%s", %.5ff, %d, { %s } },\n' % (zar, e['scale_ratio'], n, ', '.join(arr)))
        f.write("};\n")
    print("game include:", len(bonemap), "-> Shipwright/soh/src/soh3d/soh3d_bonemap.inc")

if __name__ == '__main__':
    main()
