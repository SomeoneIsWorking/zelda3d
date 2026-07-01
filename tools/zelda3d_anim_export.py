#!/usr/bin/env python3
# N64-animation -> OoT3D-CSAB MAPPING pipeline (sibling of zelda3d_skel_export.py).
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
#      the human picks in the HAND-MAINTAINED Shipwright/.../zelda3d_animmap.inc. This tool only writes
#      a *.seed.inc reference (mirrors the bonemap design — generator seeds, human owns the .inc).
#                            -> tools/skeldata/animmap.json + tools/skeldata/zelda3d_animmap.seed.inc
# Run with ZELDA3D_OOT3D_ROM set (3DS) + the N64 ROM at n64_skel_extract.ROM. See PROGRESS / handoff.
import os, sys, json, re
sys.path.insert(0, 'tools')
from ctr_romfs import CtrRom
from zar import Zar
from csab import Csab
import n64_skel_extract as N
from gen_object_zars import ALIAS  # N64 object base -> OoT3D zar base, for renamed creatures

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SK = os.path.join(REPO, 'tools', 'skeldata')

# ZAR -> N64 object name. Default zelda_<x> -> object_<x>; renamed creatures via the ALIAS inverse
# (zelda_nw -> object_niw) so an aliased OoT3D zar finds its N64 animations.
OBJ_OVERRIDE = {("/actor/zelda_%s.zar" % v): ("object_%s" % k) for k, v in ALIAS.items()}

# Some character skeletons keep their animations in a SHARED anim-bank object rather than the
# skeleton's own object, so zar_to_object() finds no AnimationHeaders and the character drops out
# of the match entirely (-> every instance falls back to the default idle). En_Ko (Kokiri kids)
# is the case: the km1/kw1 skeletons animate from object_os_anime (gKokiri*Anim). Map such a zar
# to the extra bank object(s) whose AnimationHeaders are matched against this zar's CSABs; the
# runtime OTR key stays "objects/<bank>/<anim>" (skelAnime->animation), shared across every body
# that plays that anim. km1 and kw1 carry the SAME km1_* CSAB set, so associating the bank with
# ONE of them (km1) yields one entry per anim that resolves correctly for both bodies.
# A bank entry is either the object name (match ALL its anims against the zar's CSABs) or a
# (object, [anim_names]) tuple (match only the listed anims). The Cucco Lady (En_Niw_Lady,
# gCuccoLadySkel in object_ane) animates from object_os_anime too, but via only 4 specific anims;
# her own object_ane has NO AnimationHeaders, so without this she never enters the match and every
# instance falls to the wrong km1 mapping for those shared OTR paths -> bind pose (T-pose). Allowlist
# just her 4 anims so they match her zelda_ane CSABs; the runtime resolver disambiguates the shared
# OTR paths per model ZAR (object_os_anime is km1/Kokiri territory otherwise).
SHARED_ANIM_BANKS = {
    "/actor/zelda_km1.zar": ["object_os_anime"],
    "/actor/zelda_ane.zar": [("object_os_anime", ["gObjOsAnim_07D0", "gObjOsAnim_9F94",
                                                  "gObjOsAnim_0718", "gObjOsAnim_A630"])],
}


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


# --- romaji/english CSAB-token -> N64-name-token dictionary (NAME is the PRIMARY signal) ---
# Grezzo CSAB bases mix English (wait/walk/run/jump/damage/dead/fly/attack) and romaji
# (matsu=wait, aruki=walk, hashiri=run, furimuki=turn, banzai=cheer, suwari=sit, kihon=idle...).
# Frame count is AMBIGUOUS (a character often has many same-length waits), so a confident name
# match wins; frame delta only breaks ties / fills anims with no name signal. Keys = tokens that
# appear in a CSAB base; values = substrings to look for in the (English) N64 anim name.
NAME_HINTS = {
    # idle / stand
    'wait': ['wait', 'idle', 'stand', 'stop', 'neutral'], 'matsu': ['wait', 'idle', 'stand'],
    'matteru': ['wait', 'idle', 'stand'], 'machi': ['wait', 'idle'], 'mati': ['wait', 'idle'],
    'kihon': ['idle', 'wait', 'neutral', 'stand'], 'tachi': ['stand', 'idle'], 'tati': ['stand', 'idle'],
    'stand': ['stand', 'idle'], 'stop': ['stop', 'halt', 'idle'], 'idle': ['idle', 'wait', 'stand'],
    # locomotion
    'walk': ['walk'], 'aruki': ['walk'], 'aruku': ['walk'], 'ayumi': ['walk'],
    'run': ['run'], 'hashiri': ['run', 'dash'], 'hasiri': ['run', 'dash'], 'dash': ['dash', 'run'],
    'fastrun': ['run', 'dash', 'gallop', 'fast', 'charge'], 'slowrun': ['run', 'trot', 'jog', 'walk'],
    'jump': ['jump', 'leap', 'hop'], 'tobi': ['jump', 'leap', 'hop'], 'leap': ['leap', 'jump'],
    # combat
    'damage': ['damage', 'flinch', 'recoil', 'hurt', 'hit'], 'dmg': ['damage', 'flinch', 'recoil', 'hit'],
    'hit': ['hit', 'damage'], 'yarare': ['damage', 'hit'],
    'atack': ['attack', 'slash', 'swing', 'shoot', 'lunge', 'slam', 'strike', 'stab', 'spin'],
    'attack': ['attack', 'slash', 'swing', 'shoot', 'lunge', 'slam', 'strike', 'stab', 'spin'],
    'swing': ['swing', 'slash', 'attack'], 'kiru': ['slash', 'cut', 'attack', 'swing', 'slice'],
    'fire': ['fire', 'breath', 'flame', 'shoot', 'spit'], 'shot': ['shoot', 'shot', 'fire'],
    'defend': ['block', 'guard', 'defend', 'shield'], 'defense': ['block', 'guard', 'defend', 'shield'],
    'mamori': ['block', 'guard', 'defend'], 'gad': ['block', 'guard'],
    # death / down
    'dead': ['die', 'death', 'dead', 'defeat', 'kill'], 'die': ['die', 'death', 'dead', 'defeat'],
    'down': ['down', 'fall', 'knock', 'collapse'], 'taore': ['fall', 'down', 'collapse'],
    'daun': ['down', 'fall'], 'predead': ['die', 'death', 'hit'],
    # flight / float
    'fly': ['fly', 'float', 'hover', 'air', 'flight'], 'float': ['float', 'fly', 'hover'],
    # social / gestures
    'hanasi': ['talk', 'speak', 'tell', 'dismiss'], 'hanashi': ['talk', 'speak', 'tell'],
    'syaberi': ['talk', 'speak'], 'aisatsu': ['greet', 'hello', 'meet'],
    'banzai': ['cheer', 'raise', 'celebrate', 'banzai', 'give', 'wave', 'happy'],
    'dance': ['dance'], 'suwari': ['sit', 'seated'], 'sit': ['sit', 'seated'],
    'furimuki': ['turn', 'lookover', 'lookaround', 'shoulder', 'overshoulder', 'look'],
    'hand': ['hand', 'wave', 'arm', 'hold'], 'nod': ['nod'], 'unazuki': ['nod'],
    'warai': ['laugh'], 'laugh': ['laugh'], 'uresi': ['happy', 'joy', 'glad'],
    'odoroki': ['surprise', 'shock', 'startle', 'wake'], 'okoru': ['angry', 'anger', 'mad'],
    # appear / leave / get up
    'start': ['start', 'appear', 'begin', 'rise', 'spawn'], 'end': ['end', 'disappear', 'finish', 'leave'],
    'getup': ['getup', 'standup', 'rise', 'recover'], 'mukuri': ['getup', 'rise', 'standup'],
    'okarina': ['ocarina', 'flute', 'song'], 'ocarina': ['ocarina', 'flute', 'song'],
    'eat': ['eat', 'eating'], 'sleep': ['sleep', 'nod'], 'open': ['open', 'clap', 'gate'],
}


def name_score(csab_base, n64_name):
    """NAME-FIRST signal: count CSAB-base tokens whose dictionary synonyms appear in the (English)
    N64 anim name. Tokens NOT in the dict (per-character prefixes like 'saria'/'km1', ids) are
    IGNORED so they can't inflate the score uniformly. Higher = more semantic agreement."""
    nl = n64_name.lower()
    score = 0.0
    for p in re.split(r'[_]', csab_base.lower()):
        hints = NAME_HINTS.get(p)
        if not hints:
            continue
        if any(h in nl for h in hints):
            score += 1.0
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
    # NAME-FIRST: a confident name match beats frame count (which is ambiguous across same-length
    # CSABs). Among the best-named candidates, break ties by frame delta.
    best_name = max(c['namescore'] for c in cands)
    if best_name >= 1.0:
        named = sorted([c for c in cands if c['namescore'] == best_name], key=lambda c: c['dframe'])
        return named[0]['base']
    # No name signal: idle-stub handling (a 2-frame N64 idle stub should pick the lively idle CSAB).
    if _is_idle(n64_name):
        idles = [c for c in cands if _csab_idle(c['base'])]
        if idles:
            if fc is not None and fc <= 4:
                idles.sort(key=lambda c: -(c['duration'] or 0))
            else:
                idles.sort(key=lambda c: (c['dframe'], -(c['duration'] or 0)))
            return idles[0]['base']
    return sorted(cands, key=lambda c: (c['dframe'], -c['namescore']))[0]['base']


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
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
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
    bank_objs = set()
    for banks in SHARED_ANIM_BANKS.values():
        for b in banks:
            bank_objs.add(b[0] if isinstance(b, tuple) else b)
    objs, dma = N.load_objects(n64rom, set(want) | bank_objs)
    n64 = {}
    for obj, zar in sorted(want.items()):
        if obj not in objs:
            continue
        a = n64_anims(objs[obj], obj)
        if a:
            n64[zar] = dict(object=obj, anims=a)
    # Shared anim banks: append the bank object's AnimationHeaders to each zar that uses it, so a
    # skeleton whose own object has no anims (km1/kw1) still matches its CSABs against the bank.
    for zar, banks in sorted(SHARED_ANIM_BANKS.items()):
        extra = []
        for b in banks:
            bobj, allow = (b[0], set(b[1])) if isinstance(b, tuple) else (b, None)
            if bobj in objs:
                a = n64_anims(objs[bobj], bobj)
                if allow is not None:
                    a = [x for x in a if x['name'] in allow]
                extra += a
        if not extra:
            continue
        first = banks[0][0] if isinstance(banks[0], tuple) else banks[0]
        if zar in n64:
            n64[zar]['anims'] += extra
        else:
            n64[zar] = dict(object=first, anims=extra)
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

    # SEED .inc (reference only; the master Shipwright/.../zelda3d_animmap.inc is hand-maintained).
    # One ZELDA3D_ANIMMAP per N64 anim: key = the runtime OTR path (skelAnime->animation minus the
    # __OTR__ prefix), value = the best-guess CSAB base. Candidate list + signals in a trailing
    # comment so the human can override an ambiguous guess.
    seed = os.path.join(SK, 'zelda3d_animmap.seed.inc')
    with open(seed, 'w') as f:
        f.write("// SEED (reference only) from tools/zelda3d_anim_export.py — paste/adjust entries into\n")
        f.write("// the hand-maintained Shipwright/soh/src/zelda3d/zelda3d_animmap.inc. NOT compiled.\n")
        f.write("// Key = runtime N64 anim OTR path (skelAnime->animation w/o __OTR__). Value = CSAB base.\n")
        f.write("// Trailing comment: n64 frameCount; then each CSAB candidate as base(duration,dFrame,nameScore).\n")
        for z in sorted(animmap):
            f.write("    // ==== %s (%s) ====\n" % (z, animmap[z]['object']))
            for r in animmap[z]['rows']:
                cand = ' '.join('%s(d%s,Δ%s,n%s)' % (c['base'], c['duration'], c['dframe'], c['namescore'])
                                for c in r['candidates'][:4])
                f.write('    ZELDA3D_ANIMMAP("%s", "%s"), // fc=%s | %s\n'
                        % (r['otr'], r['best'] or '', r['frameCount'], cand))
    print("seed -> tools/skeldata/zelda3d_animmap.seed.inc (master .inc is hand-maintained)")


if __name__ == '__main__':
    main()
