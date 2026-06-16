#!/usr/bin/env python3
# N64-animation -> OoT3D-CSAB MAPPING pipeline (sibling of soh3d_skel_export.py).
# "Dump every animation from both games and try to match them" (user direction). Three steps:
#   1. OoT3D side: every Anim/*.csab in each skinned character ZAR (name + duration + boneCount),
#      parsed offline with csab.py.                       -> tools/skeldata/oot3d_anims.json
#   2. N64 side:   every <Animation Name Offset> in the object XML, with frameCount read from the
#      decompressed ROM object bytes (AnimationHeader.frameCount = first big-endian s16 at offset).
#      The OTR resource path (objects/<obj>/<Name>) is the stable runtime key (skelAnime->animation
#      is exactly that string in SoH).                    -> tools/skeldata/n64_anims.json
#   3. MATCH per character: Grezzo re-exported the same animation data, so frame count is the
#      strongest mechanical signal (an N64 frameCount usually equals one CSAB's duration). But it is
#      AMBIGUOUS (a character often has two ~equal-length idles; ge1 has Idle=2 matching ge1_matsu=2
#      yet the GOOD idle is ge1_s_wait=22). So we emit ALL candidates ranked, plus a best-guess, and
#      the human picks in the HAND-MAINTAINED Shipwright/.../soh3d_animmap.inc. This tool only writes
#      a *.seed.inc reference (mirrors the bonemap design — generator seeds, human owns the .inc).
#                            -> tools/skeldata/animmap.json + tools/skeldata/soh3d_animmap.seed.inc
# Run with SOH3D_3DS_ROM set (3DS) + the N64 ROM at n64_skel_extract.ROM. See PROGRESS / handoff.
import os, sys, json, re
sys.path.insert(0, 'tools')
from ctr_romfs import CtrRom
from zar import Zar
from csab import Csab
import n64_skel_extract as N

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SK = os.path.join(REPO, 'tools', 'skeldata')

# ZAR -> N64 object name. Default zelda_<x> -> object_<x>; a few don't fit the pattern.
OBJ_OVERRIDE = {}


def zar_to_object(zar):
    base = os.path.basename(zar)[:-4]            # zelda_sa
    if zar in OBJ_OVERRIDE:
        return OBJ_OVERRIDE[zar]
    return 'object_' + base[len('zelda_'):] if base.startswith('zelda_') else None


def oot3d_anims(rom, zar):
    """[{name, csab (Anim/<base>.csab), base, duration, bones}] for every CSAB in the ZAR."""
    zf = Zar(rom.read(rom.get(zar)))
    out = []
    for f in zf.files:
        if not f.name.lower().endswith('.csab'):
            continue
        base = os.path.basename(f.name)[:-5]     # ge1_s_wait
        try:
            c = Csab(zf.read(f))
            out.append(dict(name=f.name, base=base, duration=c.duration, bones=c.bone_count))
        except Exception as e:
            out.append(dict(name=f.name, base=base, duration=None, bones=None, error=str(e)[:40]))
    return out


def n64_anims(objbytes, objname):
    """[{name, otr (objects/<obj>/<name>), offset, frameCount}] from the object XML + ROM bytes."""
    xml = os.path.join(N.XMLDIR, objname + '.xml')
    if not os.path.exists(xml):
        return []
    txt = open(xml).read()
    out = []
    for name, off in re.findall(r'<Animation Name="([^"]+)" Offset="(0x[0-9A-Fa-f]+)"', txt):
        o = int(off, 16)
        fc = N.be16(objbytes, o) if o + 2 <= len(objbytes) else None   # AnimationHeader.frameCount
        out.append(dict(name=name, otr='objects/%s/%s' % (objname, name), offset=off, frameCount=fc))
    return out


# --- light romaji <-> english token hints (weak signal; frame count dominates) ---
# Grezzo CSAB bases are romaji (wait/matsu/hanasi/aruki...); decomp N64 names are English.
NAME_HINTS = {
    'wait': ['wait', 'idle', 'stand', 'matsu'],
    'matsu': ['wait', 'idle', 'stand'],
    'aruki': ['walk'], 'walk': ['walk', 'aruki'], 'run': ['run', 'hasiri', 'dash'],
    'hanasi': ['talk', 'speak', 'dismissive', 'tell'], 'talk': ['talk', 'hanasi'],
    'akeru': ['open', 'clap', 'gate'], 'damage': ['damage', 'hit'], 'down': ['down', 'fall'],
    'jump': ['jump', 'tobi'], 'attack': ['attack', 'kougeki'], 'think': ['think', 'kangae'],
}


def name_score(csab_base, n64_name):
    """Crude token overlap in [0,1]. Strips the per-char prefix from the CSAB base."""
    nl = n64_name.lower()
    parts = [p for p in re.split(r'[_]', csab_base.lower()) if p]
    score = 0.0
    for p in parts:
        for hint in NAME_HINTS.get(p, [p]):
            if hint and hint in nl:
                score = max(score, 1.0 if hint == p or len(hint) > 3 else 0.5)
    return score


IDLE_TOK = ('idle', 'wait', 'stand')                  # idle-ish N64 anim name tokens
CSAB_IDLE_TOK = ('wait', 'matsu', 'tachi', 'kihon')   # idle-ish CSAB base tokens


def _is_idle(n64_name):
    nl = n64_name.lower()
    return any(t in nl for t in IDLE_TOK)


def _csab_idle(base):
    bl = base.lower()
    return any(t in bl for t in CSAB_IDLE_TOK)


def pick_best(n64_name, fc, cands):
    """Choose the best CSAB. Frame delta is the primary mechanical signal, BUT idle-ish N64 anims
    need care: an N64 idle that is itself a 2-frame STUB (ge1's gGerudoWhiteIdleAnim fc=2, alive only
    via procedural fidget) frame-matches a lifeless stub CSAB (ge1_matsu d2) when the LIVELY idle
    (ge1_s_wait d22) is what reads as "standing". So for idle names that map to a stub (fc<=4), pick
    the liveliest (longest) idle-token CSAB; for a real idle LOOP (e.g. Saria fc=24) keep the
    frame-close idle CSAB. Non-idle: plain frame-delta then name score. SEED suggestion only — the
    .inc is hand-owned, so a semantic match frame count can't see (ge1 Dismissive->ge1_hanasi) is a
    hand fix."""
    if not cands:
        return None
    if _is_idle(n64_name):
        idles = [c for c in cands if _csab_idle(c['base'])]
        if idles:
            if fc is not None and fc <= 4:                       # N64 idle is a stub: frame is noise
                idles.sort(key=lambda c: -(c['duration'] or 0))  # -> liveliest (longest) idle
            else:                                                # real idle loop: frame-close idle
                idles.sort(key=lambda c: (c['dframe'], -(c['duration'] or 0)))
            return idles[0]['base']
    best = sorted(cands, key=lambda c: (c['dframe'], -c['namescore']))[0]
    return best['base']


def match_char(n64_list, csab_list):
    """For each N64 anim, rank CSAB candidates. Returns [{n64, best, candidates:[...]}]. Candidates
    are sorted by frame delta then name score (the raw signals); `best` applies pick_best()."""
    csabs = [c for c in csab_list if c.get('duration') is not None]
    rows = []
    for a in n64_list:
        fc = a['frameCount']
        cands = []
        for c in csabs:
            dframe = abs((c['duration'] or 0) - fc) if fc is not None else 9999
            cands.append(dict(base=c['base'], duration=c['duration'],
                              dframe=dframe, namescore=round(name_score(c['base'], a['name']), 2)))
        cands.sort(key=lambda c: (c['dframe'], -c['namescore']))
        rows.append(dict(n64=a['name'], otr=a['otr'], frameCount=fc,
                         best=pick_best(a['name'], fc, cands), candidates=cands))
    return rows


def main():
    rom = CtrRom(os.environ["SOH3D_3DS_ROM"])
    sk = json.load(open(os.path.join(SK, 'oot3d_skeletons.json')))
    zars = sorted(sk.keys())

    # 1. OoT3D CSABs per ZAR
    o3 = {}
    for z in zars:
        try:
            a = oot3d_anims(rom, z)
        except Exception as e:
            a = []
        if a:
            o3[z] = a
    json.dump(o3, open(os.path.join(SK, 'oot3d_anims.json'), 'w'), indent=1)
    tot = sum(len(v) for v in o3.values())
    print("OoT3D anims: %d CSABs across %d ZARs -> tools/skeldata/oot3d_anims.json" % (tot, len(o3)))

    # 2. N64 anims per object
    n64rom = open(N.ROM, 'rb').read()
    want = {}
    for z in zars:
        obj = zar_to_object(z)
        if obj:
            want[obj] = z
    objs, dma = N.load_objects(n64rom, set(want))
    n64 = {}
    for obj, zar in sorted(want.items()):
        if obj not in objs:
            continue
        a = n64_anims(objs[obj], obj)
        if a:
            n64[zar] = dict(object=obj, anims=a)
    json.dump(n64, open(os.path.join(SK, 'n64_anims.json'), 'w'), indent=1)
    totn = sum(len(v['anims']) for v in n64.values())
    print("N64 anims: %d AnimationHeaders across %d objects (dma @ 0x%X) -> tools/skeldata/n64_anims.json"
          % (totn, len(n64), dma))

    # 3. match per character (ZAR present in both dumps)
    animmap = {}
    for z in sorted(set(o3) & set(n64)):
        rows = match_char(n64[z]['anims'], o3[z])
        animmap[z] = dict(object=n64[z]['object'], rows=rows)
    json.dump(animmap, open(os.path.join(SK, 'animmap.json'), 'w'), indent=1)
    print("matched: %d characters -> tools/skeldata/animmap.json" % len(animmap))

    # SEED .inc (reference only; the master Shipwright/.../soh3d_animmap.inc is hand-maintained).
    # One SOH3D_ANIMMAP per N64 anim: key = the runtime OTR path (skelAnime->animation minus the
    # __OTR__ prefix), value = the best-guess CSAB base. Candidate list + signals in a trailing
    # comment so the human can override an ambiguous guess.
    seed = os.path.join(SK, 'soh3d_animmap.seed.inc')
    with open(seed, 'w') as f:
        f.write("// SEED (reference only) from tools/soh3d_anim_export.py — paste/adjust entries into\n")
        f.write("// the hand-maintained Shipwright/soh/src/soh3d/soh3d_animmap.inc. NOT compiled.\n")
        f.write("// Key = runtime N64 anim OTR path (skelAnime->animation w/o __OTR__). Value = CSAB base.\n")
        f.write("// Trailing comment: n64 frameCount; then each CSAB candidate as base(duration,dFrame,nameScore).\n")
        for z in sorted(animmap):
            f.write("    // ==== %s (%s) ====\n" % (z, animmap[z]['object']))
            for r in animmap[z]['rows']:
                cand = ' '.join('%s(d%s,Δ%s,n%s)' % (c['base'], c['duration'], c['dframe'], c['namescore'])
                                for c in r['candidates'][:4])
                f.write('    SOH3D_ANIMMAP("%s", "%s"), // fc=%s | %s\n'
                        % (r['otr'], r['best'] or '', r['frameCount'], cand))
    print("seed -> tools/skeldata/soh3d_animmap.seed.inc (master .inc is hand-maintained)")


if __name__ == '__main__':
    main()
