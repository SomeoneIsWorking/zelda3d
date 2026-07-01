#!/usr/bin/env python3
# Oracle: match an N64 actor skeleton (ZELDA3D_SKELDUMP) to an OoT3D CMB skeleton -> the
# OoT3D-bone -> N64-limb correspondence for the N64-anim retarget. Same Grezzo-ported character,
# so bone lengths + tree structure align. Key: OoT3D inserts zero-length REORIENT bones (and a
# pure root) that N64 folds into a single limb; we collapse zero-length internal bones on BOTH
# sides, pair each node's effective children by |bone length|, and align the collapsed chains.
import os, sys, math, re
sys.path.insert(0, 'tools')
from ctr_romfs import CtrRom
from zar import Zar
import cmb as C
EPS = 1.0

def load_oot3d(zarname):
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"]); z = Zar(rom.read(rom.get(zarname)))
    cmbs = [f for f in z.files if f.name.lower().endswith('.cmb')]
    m = C.Cmb(z.read(max(cmbs, key=lambda f: f.size)))
    nodes = {b.id: dict(id=b.id, parent=b.parent, length=math.sqrt(sum(c*c for c in b.trans))) for b in m.bones}
    return nodes

def build_n64_nodes(limbs_in):
    # limbs_in: list of {id, jp:(x,y,z), child, sibling}. Returns the matcher node dict
    # {id: {child,sibling,length,parent}} with parents derived from child/sibling (255 = none).
    limbs = {}
    for l in limbs_in:
        x,y,z = l['jp']
        limbs[l['id']] = dict(id=l['id'], child=l['child'], sibling=l['sibling'],
                              length=math.sqrt(x*x+y*y+z*z), parent=-1)
    parent = {0: -1}
    for i,l in limbs.items():
        if l['child'] != 255: parent[l['child']] = i
    changed = True
    while changed:
        changed = False
        for i,l in limbs.items():
            if l['sibling'] != 255 and l['sibling'] not in parent and i in parent:
                parent[l['sibling']] = parent[i]; changed = True
    for i in limbs: limbs[i]['parent'] = parent.get(i, -1)
    return limbs

def load_n64(dumpfile):
    limbs_in = []
    for ln in open(dumpfile):
        mo = re.search(r'N64 limb=(\d+) jointPos=\((-?\d+),(-?\d+),(-?\d+)\) child=(\d+) sibling=(\d+)', ln)
        if mo:
            i,x,y,z,c,s = map(int, mo.groups())
            limbs_in.append(dict(id=i, jp=(x,y,z), child=c, sibling=s))
    return build_n64_nodes(limbs_in)

def children_of(nodes):
    ch = {n: [] for n in nodes}
    for n,info in nodes.items():
        if info['parent'] in ch: ch[info['parent']].append(n)
    return ch

def eff_children(node, ch, nodes):
    # returns list of (real_child, [collapsed zero-len chain bones from node down to real_child])
    out = []
    for c in ch[node]:
        if nodes[c]['length'] < EPS and ch[c]:           # zero-length internal -> collapse
            for gc, chain in eff_children(c, ch, nodes): out.append((gc, [c] + chain))
        else:
            out.append((c, []))
    return out

def match(nN, nO):
    chN, chO = children_of(nN), children_of(nO)
    rootN = [n for n in nN if nN[n]['parent'] < 0][0]
    rootO = [n for n in nO if nO[n]['parent'] < 0][0]
    bmap = {rootO: -1}    # OoT3D pure root -> identity (N64 root translation handles placement)
    def rec(nNode, oNode):
        bmap.setdefault(oNode, nNode)
        ecN = eff_children(nNode, chN, nN)
        ecO = eff_children(oNode, chO, nO)
        usedN = set()
        for oc, ochain in ecO:
            best, bd = None, 1e18
            for nc, nchain in ecN:
                if nc in usedN: continue
                d = abs(nN[nc]['length'] - nO[oc]['length'])
                if d < bd: best, bd, bestchain = nc, d, nchain
            if best is None:
                continue
            usedN.add(best)
            # align the collapsed zero-length chains: ochain[i] <-> nchain[i]; if N64 chain is
            # shorter, extra OoT3D reorient bones map to the parent node's N64 limb.
            for i, ob in enumerate(ochain):
                bmap[ob] = bestchain[i] if i < len(bestchain) else nNode
            rec(best, oc)
    rec(rootN, rootO)
    return bmap

if __name__ == '__main__':
    zar = sys.argv[1] if len(sys.argv) > 1 else '/actor/zelda_boj.zar'
    dump = sys.argv[2] if len(sys.argv) > 2 else 'scratch/skeldump/boj_market_session16.txt'
    nO = load_oot3d(zar); nN = load_n64(dump)
    bmap = match(nN, nO)
    hand = {0:-1,1:0,2:1,3:2,4:3,5:4,6:5,7:6,8:7,9:14,10:8,11:9,12:10,13:11,14:12,15:13}
    print("bone -> limb   (len_oot3d ~ len_n64)   [hand]  ok")
    okall = True
    for b in sorted(bmap):
        n = bmap[b]; nl = nN[n]['length'] if n in nN else 0
        h = hand.get(b, '?'); ok = (h == n)
        okall &= ok
        print("  b%-2d -> %-3d  (%5.0f ~ %5.0f)   [%s]  %s" % (b, n, nO[b]['length'], nl, h, 'OK' if ok else 'XX'))
    print("MATCHES HAND-DERIVED MAP" if okall and len(bmap)==len(hand) else "DIFFERS from hand map")
