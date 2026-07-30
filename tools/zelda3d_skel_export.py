#!/usr/bin/env python3
# Data-driven character-replacement maps. Dump skeleton data from BOTH games to text/JSON, then
# emit a precomputed bone-correspondence map the game references directly (no runtime matching).
#   OoT3D side: parsed offline from each actor ZAR (cmb.py) -> tools/skeldata/oot3d_skeletons.json
#   N64 side:   captured in-game via ZELDA3D_SKELDUMP -> tools/skeldata/n64/<zarbase>.txt
#   bonemap:    zelda3d_skel_match.match() per character (where N64 data exists) -> bonemap.json
# Run with ZELDA3D_OOT3D_ROM set. See SKILL zelda3d-game-control + PROGRESS.
import os, sys, json, math, re
sys.path.insert(0, 'tools')
from ctr_romfs import CtrRom
from zar import Zar
import cmb as C
import zelda3d_skel_match as M
from gen_object_zars import ALIAS  # N64 object base -> OoT3D zar base, for renamed creatures

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SK = os.path.join(REPO, 'tools', 'skeldata')

# zar path -> N64 object name, the INVERSE of the renamed-creature ALIAS (zelda_nw -> object_niw).
# Lets the offline N64-skeleton match find the right N64 object for an aliased OoT3D zar.
OBJ_OVERRIDE = {("/actor/zelda_%s.zar" % v): ("object_%s" % k) for k, v in ALIAS.items()}

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
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    inc = open(os.path.join(REPO, 'Shipwright/soh/src/zelda3d/tables/zelda3d_object_zars.inc')).read()
    zars = sorted(set(re.findall(r'"(/actor/[a-zA-Z0-9_]+\.zar)"', inc)))  # case-insensitive: catch oF1d etc.
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
    # zar -> N64 object name (zelda_boj -> object_boj); OBJ_OVERRIDE (module-level) handles renamed creatures.
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

    # Emit a SEED reference (NOT the game file). The game's Shipwright/.../zelda3d_bonemap.inc is the
    # HAND-MAINTAINED master C++ table: this tool only seeds good auto-matches for reference / for
    # bootstrapping new characters; hand-fix partial rigs (e.g. Saria) directly in the master and
    # they are never clobbered. Lines here are ready to paste into kZelda3dBoneMaps.
    seed_path = os.path.join(SK, 'zelda3d_bonemap.seed.inc')
    with open(seed_path, 'w') as f:
        f.write("// SEED (reference only) from tools/zelda3d_skel_export.py — paste/adjust entries into\n")
        f.write("// the hand-maintained Shipwright/soh/src/zelda3d/zelda3d_bonemap.inc. NOT compiled.\n")
        for zar in sorted(bonemap):
            e = bonemap[zar]
            n = e['oot3d_bone_count']
            arr = [str(e['bone_to_limb'].get(str(i), -1)) for i in range(n)]
            f.write('    ZELDA3D_BONEMAP("%s", %.5ff, %d, %s), // %s\n' % (zar, e['scale_ratio'], n, ', '.join(arr), e['object']))
    print("seed:", len(bonemap), "-> tools/skeldata/zelda3d_bonemap.seed.inc (master .inc is hand-maintained)")

if __name__ == '__main__':
    main()
