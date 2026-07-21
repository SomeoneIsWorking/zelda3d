#!/usr/bin/env python3
"""Generate the `kMMAnimMaps` table (N64 animation OTR key -> MM3D CSAB clip name).

Fully OFFLINE: reads the N64 (2ship) object assets from the repo and the MM3D ROM
(`$ZELDA3D_MM3D_ROM`, see <repo>/.env). No emulator, no game, no network.

Pipeline:
  1. enumerate every N64 object's animation symbols   (2ship/assets/objects/*/, /g\\w+Anim\\b/)
  2. resolve each object to its MM3D /actors/ GAR and enumerate that GAR's CSAB clip names
     (GAR2 + LzS readers ported 1:1 from Shipwright/cmb3d/asset/{gar,lzs}.cpp)
  3. match symbol <-> clip (token/synonym scoring; ambiguous or weak => UNMATCHED, because a
     wrong animation is worse than the idle fallback)
  4. emit C entries for kMMAnimMaps + a coverage report (markdown & json)

  python3 tools/gen_mm_animmap.py [--only object_dog] [--min-confidence 0.9]
                                  [--out scratch/mm_animmap.inc] [--report scratch/mm_animmap]
"""
from __future__ import annotations

import argparse
import json
import os
import re
import struct
import sys
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OBJ_DIR = os.path.join(REPO, "2ship", "assets", "objects")


# =============================================================== env / romfs

def _load_env() -> None:
    p = os.path.join(REPO, ".env")
    if not os.path.exists(p):
        return
    with open(p) as fh:
        for line in fh:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            k = k.strip()
            if k.startswith("export "):
                k = k[7:].strip()
            os.environ.setdefault(k, v.strip().strip('"').strip("'"))


# =============================================================== LzS (lzs.cpp)

LZS_MAGIC = b"LzS\x01"


def lzs_is_compressed(data: bytes) -> bool:
    return len(data) >= 16 and data[:4] == LZS_MAGIC


def lzs_decompress(data: bytes) -> bytes:
    if not lzs_is_compressed(data):
        raise ValueError("not an LzS\\1 buffer")
    dec_size, comp_size = struct.unpack_from("<II", data, 8)
    if comp_size == 0 or 16 + comp_size > len(data):
        raise ValueError("LzS comp_size overruns buffer")
    src = data[16:16 + comp_size]
    in_len = comp_size
    out = bytearray()
    buf = bytearray(4096)
    writeidx, fidx = 0xFEE, 0
    while fidx < in_len:
        flags8 = src[fidx]
        fidx += 1
        for _ in range(8):
            if fidx >= in_len:
                break
            if flags8 & 1:
                b = src[fidx]
                fidx += 1
                out.append(b)
                buf[writeidx] = b
                writeidx = (writeidx + 1) & 0xFFF
            else:
                if fidx + 1 >= in_len:
                    break
                b1, b2 = src[fidx], src[fidx + 1]
                fidx += 2
                readidx = b1 | ((b2 & 0xF0) << 4)
                for _j in range((b2 & 0x0F) + 3):
                    v = buf[readidx]
                    out.append(v)
                    buf[writeidx] = v
                    readidx = (readidx + 1) & 0xFFF
                    writeidx = (writeidx + 1) & 0xFFF
            flags8 >>= 1
    if len(out) != dec_size:
        raise ValueError("LzS decompressed size mismatch: %d != %d" % (len(out), dec_size))
    return bytes(out)


# =============================================================== GAR2 (gar.cpp)

GAR2_MAGIC = b"GAR\x02"
CSAB_MAGIC = b"csab"


def _u16(b, o): return struct.unpack_from("<H", b, o)[0]
def _u32(b, o): return struct.unpack_from("<I", b, o)[0]


def _cstr(b: bytes, o: int) -> str:
    if o >= len(b):
        return ""
    end = b.find(b"\x00", o)
    return b[o:(end if end >= 0 else len(b))].decode("ascii", "replace")


@dataclass
class GarFile:
    name: str = ""
    path: str = ""
    type: str = ""
    offset: int = 0
    size: int = 0
    data: bytes = b""


class Gar:
    """GAR version-2 archive; transparently LzS-inflates a compressed blob.

    NOTE (measured): the ".gar.lzs" extension is NOT a compression indicator -- most
    archives so named are stored raw. Always sniff the LzS magic, never the filename.
    """

    def __init__(self, data: bytes):
        self.was_compressed = False
        if lzs_is_compressed(data):
            data = lzs_decompress(data)
            self.was_compressed = True
        self.blob = b = data
        n = len(b)
        if n < 0x20 or b[:4] != GAR2_MAGIC:
            raise ValueError("not a GAR2 archive")
        n_types, n_files = _u16(b, 0x08), _u16(b, 0x0A)
        types_off, files_off, datahdr_off = _u32(b, 0x0C), _u32(b, 0x10), _u32(b, 0x14)
        self.codec = _cstr(b, 0x18)
        if types_off + 16 * n_types > n or files_off + 12 * n_files > n \
                or datahdr_off + 4 * n_files > n:
            raise ValueError("GAR2 table out of range")
        self.entries: List[GarFile] = []
        for i in range(n_files):
            fe = files_off + 12 * i
            fsize = _u32(b, fe)
            off = _u32(b, datahdr_off + 4 * i)
            f = GarFile(name=_cstr(b, _u32(b, fe + 4)), path=_cstr(b, _u32(b, fe + 8)),
                        offset=off, size=fsize)
            f.data = b[off:off + fsize] if off + fsize <= n else b""
            self.entries.append(f)
        for t in range(n_types):
            e = types_off + 16 * t
            cnt, idx_off = _u32(b, e), _u32(b, e + 4)
            tname = _cstr(b, _u32(b, e + 8))
            if idx_off == 0xFFFFFFFF or idx_off + 4 * cnt > n:
                continue
            for k in range(cnt):
                fi = _u32(b, idx_off + 4 * k)
                if fi < len(self.entries):
                    self.entries[fi].type = tname

    def clip_names(self, verify: bool = True) -> List[str]:
        """CSAB clip names, i.e. the kMMAnimMaps csab-side strings.

        The name is the GAR member SHORT name minus a .csab extension -- MM3D CSABs
        (subversion 5) carry no internal name field. Members are selected by the GAR type
        table ('csab'), falling back to the suffix, and (when verify) checked for the
        'csab' magic so a mistyped member can't leak in.
        """
        out, seen = [], set()
        for f in self.entries:
            leaf = os.path.basename((f.name or f.path).replace("\\", "/"))
            is_csab = f.type == "csab" or leaf.lower().endswith(".csab") \
                or f.path.lower().endswith(".csab")
            if not is_csab:
                continue
            if verify and f.data and f.data[:4] != CSAB_MAGIC:
                continue
            if leaf.lower().endswith(".csab"):
                leaf = leaf[:-5]
            if leaf and leaf not in seen:
                seen.add(leaf)
                out.append(leaf)
        return out


class Mm3dActors:
    """Index of /actors/*.gar[.lzs] in the MM3D ROM, keyed by archive basename."""

    def __init__(self, rom_path: Optional[str] = None):
        sys.path.insert(0, os.path.join(REPO, "tools"))
        from ctr_romfs import CtrRom  # noqa: E402
        rom_path = rom_path or os.environ.get("ZELDA3D_MM3D_ROM")
        if not rom_path:
            raise SystemExit("ZELDA3D_MM3D_ROM not set (see <repo>/.env)")
        self.rom_path = rom_path
        self.rom = CtrRom(rom_path)
        self.actors: Dict[str, object] = {}
        for f in self.rom.iter_files():
            if not f.path.startswith("/actors/"):
                continue
            base = os.path.basename(f.path)
            for suf in (".gar.lzs", ".gar"):
                if base.endswith(suf):
                    self.actors.setdefault(base[:-len(suf)], f)
                    break
        self._cache: Dict[str, List[str]] = {}

    def clips(self, basename: str) -> Optional[List[str]]:
        if basename in self._cache:
            return self._cache[basename]
        fe = self.actors.get(basename)
        if fe is None:
            return None
        names = Gar(self.rom.read(fe)).clip_names()
        self._cache[basename] = names
        return names


# =============================================================== (a) N64 side

ANIM_RE = re.compile(r"\bg[A-Za-z0-9_]+Anim\b")
NON_SKEL_TOKENS = ("tex", "texture", "eye", "mouth", "uv")


def n64_anims(objects_dir: str = OBJ_DIR) -> Dict[str, List[str]]:
    """{object_name: ["objects/<object>/<gFooAnim>", ...]} -- the OTR key without __OTR__."""
    out: Dict[str, List[str]] = {}
    for obj in sorted(os.listdir(objects_dir)):
        d = os.path.join(objects_dir, obj)
        if not os.path.isdir(d):
            continue
        syms = set()
        for root, _dirs, files in os.walk(d):
            for fn in files:
                try:
                    with open(os.path.join(root, fn), "r", errors="replace") as fp:
                        syms.update(ANIM_RE.findall(fp.read()))
                except OSError:
                    continue
        if syms:
            out[obj] = ["objects/%s/%s" % (obj, s) for s in sorted(syms)]
    # Union in every animation the asset XMLs declare -- this is what picks up the address-named
    # ones the header regex cannot see (see xml_anim_symbols).
    for obj, syms in xml_anim_symbols().items():
        if not os.path.isdir(os.path.join(objects_dir, obj)):
            continue
        keys = out.setdefault(obj, [])
        have = set(keys)
        for sym in syms:
            k = "objects/%s/%s" % (obj, sym)
            if k not in have:
                keys.append(k)
                have.add(k)
        keys.sort()
    return out


def all_object_dirs(objects_dir: str = OBJ_DIR) -> List[str]:
    return sorted(d for d in os.listdir(objects_dir)
                  if os.path.isdir(os.path.join(objects_dir, d)))


def symbol_of(otr_key: str) -> str:
    return otr_key.rsplit("/", 1)[-1]


# =============================================================== (b) object -> GAR

def object_to_gar(object_name: str, known: Optional[set] = None) -> List[str]:
    """Candidate MM3D /actors/ archive basenames, in preference order.

    MEASURED rules against the real listing: object_X -> zelda2_X (main rule);
    OoT-inherited actors -> zelda_X (no '2'); a few -> zelda2_X_new; gameplay_X_keep ->
    zelda2_X_keep; names are lowercase; a handful carry no prefix (dk_trap).
    """
    name = object_name.lower()
    stem = name
    for pref in ("object_", "obj_"):
        if stem.startswith(pref):
            stem = stem[len(pref):]
            break
    if name.startswith("gameplay_"):
        stem = name[len("gameplay_"):]
    cands = ["zelda2_" + stem, "zelda_" + stem,
             "zelda2_" + stem + "_new", "zelda_" + stem + "_new",
             stem, "zelda2_" + name, name]
    ordered, seen = [], set()
    for c in cands:
        if c not in seen:
            seen.add(c)
            ordered.append(c)
    return ordered if known is None else [c for c in ordered if c in known]


# =============================================================== (c) matcher

SYNONYM_CLASSES: List[Tuple[str, ...]] = [
    ("wait", "idle", "stand", "matsu", "matteru", "machi", "mati", "kihon", "tachi", "tati", "neutral"),
    ("walk", "aruki", "aruku", "ayumi"),
    ("run", "hashiri", "hasiri", "dash"),
    ("jump", "tobi", "leap", "hop"),
    ("damage", "dmg", "hit", "yarare", "flinch", "recoil", "hurt"),
    ("attack", "atack", "kougeki"),
    ("slash", "kiru", "swing", "cut"),
    ("die", "dead", "death", "shinu"),
    ("down", "taore", "daun", "fall", "collapse", "knock", "knockover", "knockedover"),
    ("getup", "standup", "mukuri", "rise", "okiru"),
    ("fly", "flight", "hover", "tobu"),
    ("float", "ukabu"),
    ("talk", "speak", "hanasi", "hanashi", "syaberi", "shaberi"),
    ("sit", "suwari", "seated"),
    ("sleep", "nemuri", "neru"),
    ("eat", "eating", "taberu", "kuu"),
    ("turn", "furimuki", "furimuku"),
    ("nod", "unazuki"),
    ("laugh", "warai"),
    ("surprise", "odoroki", "shock", "startle"),
    ("angry", "anger", "okoru", "mad"),
    ("happy", "uresi", "ureshi", "joy", "glad"),
    ("dance", "odori"),
    ("cheer", "banzai", "celebrate"),
    ("greet", "aisatsu"),
    ("start", "begin", "hajime", "appear"),
    ("end", "finish", "owari", "ending", "disappear"),
    ("open", "ake", "aku"),
    ("close", "shime", "shimeru"),
    ("throw", "nage", "nageru"),
    ("swim", "oyogi", "oyogu"),
    ("ocarina", "okarina", "flute"),
    ("wake", "okiru", "wakeup"),
    ("push", "osu"),
    ("pull", "hiku"),
    ("catch", "tsukamu", "grab", "hold"),
    ("shout", "sakebi", "sakebu", "scream", "roar", "call", "yell"),
    ("lookup", "kaoage", "faceup"),
    ("lookaround", "kyoro", "kyorokyoro", "lookabout"),
    ("stab", "tukisasae", "tsukisasu", "thrust", "lunge"),
    ("support", "sasaeru", "sasae"),
    ("suffer", "kurusimu", "kurushimu", "struggle", "writhe"),
    ("transform", "hensin", "henshin", "morph"),
    ("return", "modori", "modoru"),
    ("play", "ensou", "perform"),
    ("salute", "keirei"),
    ("whip", "muti", "muchi"),
    ("stop", "tome", "tomaru", "halt"),
    ("escape", "nige", "nigeru", "flee"),
    ("stumble", "koke", "kokeru", "trip"),
    ("fall", "rakka", "ochiru"),
    ("cutscene", "demo", "cs"),
    ("verticalslash", "tategiri"),
    ("pillar", "hashira"),
    ("standup", "tachiagari", "tachiagaru", "riseup"),
    ("jmp",), ("dam",),
]
_ALIAS = {"jmp": "jump", "dam": "damage", "dmg": "damage"}
_SYN: Dict[str, int] = {}
for _i, _cls in enumerate(SYNONYM_CLASSES):
    for _t in _cls:
        _SYN.setdefault(_t, _i)
for _a, _b in _ALIAS.items():
    _SYN[_a] = _SYN[_b]


def _canon(tok: str) -> str:
    for cand in (tok, tok + "e"):
        c = _SYN.get(cand)
        if c is not None:
            return "#%d" % c
    return tok


def _canon_seq(toks: List[str]) -> List[str]:
    j = "".join(toks)
    if len(toks) > 1 and (j in _SYN or (j + "e") in _SYN):
        return [_canon(j)]
    return [_canon(t) for t in toks]


def _stem(t: str) -> str:
    for suf in ("ing", "ed"):
        if len(t) > len(suf) + 3 and t.endswith(suf):
            base = t[:-len(suf)]
            if len(base) > 3 and base[-1] == base[-2]:
                base = base[:-1]
            return base
    return t


def _atoms(tok: str) -> Tuple[List[str], bool]:
    parts = re.findall(r"[a-z]+|\d+", tok.lower())
    return [_stem(p) for p in parts if not p.isdigit()], any(p.isdigit() for p in parts)


def split_symbol(symbol: str) -> Tuple[List[str], bool]:
    s = symbol[1:] if symbol.startswith("g") else symbol
    s = re.sub(r"Anim$", "", s)
    toks, num = [], False
    for p in re.findall(r"[A-Z]+(?![a-z])|[A-Z][a-z0-9]*|[a-z0-9]+", s):
        w, n = _atoms(p)
        toks += w
        num = num or n
    return toks, num


def split_clip(clip: str) -> Tuple[List[str], bool]:
    toks, num = [], False
    for p in re.split(r"[_\-.]+", clip.lower()):
        if not p:
            continue
        w, n = _atoms(p)
        toks += w
        num = num or n
    return toks, num


def _common_lead(seqs: List[List[str]]) -> int:
    if len(seqs) < 2:
        return 0
    limit = min(len(s) for s in seqs) - 1
    k = 0
    while k < limit and all(s[k] == seqs[0][k] for s in seqs):
        k += 1
    return k


def _strip_actor(seqs: List[List[str]], extra: Tuple[str, ...]) -> List[List[str]]:
    k = _common_lead(seqs)
    res = []
    for s in (x[k:] for x in seqs):
        while len(s) > 1 and s[0] in extra:
            s = s[1:]
        res.append(s)
    return res


def _noise_heads(seqs: List[List[str]]) -> set:
    heads: Dict[str, int] = {}
    for s in seqs:
        if len(s) > 1:
            heads[s[0]] = heads.get(s[0], 0) + 1
    return {h for h, n in heads.items() if n >= 2 and h not in _SYN and h != "loop"}


def actor_tokens_for(object_name: str) -> Tuple[str, ...]:
    stem = re.sub(r"^(object_|obj_)", "", object_name)
    toks = {stem}
    toks.update(t for t in stem.split("_") if t)
    return tuple(toks)


@dataclass
class Match:
    symbol: str
    clip: Optional[str]
    confidence: float
    why: str


def _score(sym_toks: List[str], clip_toks: List[str]) -> Tuple[float, str]:
    if not sym_toks or not clip_toks:
        return 0.0, "empty"
    if "".join(sym_toks) == "".join(clip_toks):
        return 1.0, "exact (token join)"
    a, b = _canon_seq(sym_toks), _canon_seq(clip_toks)
    ja, jb = "".join(a), "".join(b)
    sa, sb = set(a), set(b)
    if sa == sb:
        return 1.0, "exact token set" + ("" if sym_toks == clip_toks else " (synonym)")
    if ja == jb:
        return 0.97, "exact after token join (%s)" % ja
    inter = sa & sb
    if not inter:
        return 0.0, "no shared token"
    return (len(inter) / float(len(sa | sb)),
            "partial overlap %s (%d/%d)" % ("+".join(sorted(inter)), len(inter), len(sa | sb)))


ACCEPT = 0.90
TIE_MARGIN = 0.05



# ---------------------------------------------- authoritative XML "Original name" annotations
# The 2ship decomp XMLs annotate most animations with the asset's ORIGINAL (romaji) name, which is
# exactly what the MM3D GAR names its CSAB clip:
#     <Animation Name="gDogBarkAnim" Offset="0x998" /> <!-- Original name is "dog_bark" -->
# 1555 of 1746 Animation entries carry it. This is an AUTHORITATIVE mapping written by the decomp
# authors, so it beats any lexical guess — and it is the only thing that bridges the romaji gap
# (MM3D clips are Japanese: an_hokiwalk, dnt_iyaiyaTOmuun), which pure name matching cannot do.
XML_DIRS = ("N64_US", "GC_US")
# KNOWN GAP: 11 animation symbols live in assets/overlays/ (e.g. ovl_En_Sth) rather than
# objects/. They are deliberately NOT scanned: an overlay has no MM3D actor GAR of its own
# (its model comes from some object), and that overlay->object association is not recorded
# in the XML, so scanning them would only produce unresolvable entries.
XML_SUBDIRS = ("objects",)
_ANIM_ORIG_RE = re.compile(
    r'<Animation\s+Name="([A-Za-z0-9_]+)"[^>]*/>\s*<!--[^>]*?Original name is\s+"([^"]+)"')

_ANIM_ANY_RE = re.compile(r'<Animation\s+Name="([A-Za-z0-9_]+)"')

def xml_anim_symbols(repo: str = REPO) -> Dict[str, List[str]]:
    r"""{object_name: [animation symbol...]} declared in the 2ship asset XMLs.

    AUTHORITATIVE and broader than grepping the headers for /g\w+Anim/: many animations are still
    address-named (object_daiku_Anim_00B690) because they have not been given a symbolic name, and
    566 such entries exist -- 536 of them WITH an "Original name" annotation, i.e. fully mappable.
    A g-prefixed regex silently drops all of them."""
    out: Dict[str, List[str]] = {}
    for d in XML_DIRS:
        for sub in XML_SUBDIRS:
            base = os.path.join(repo, "2ship", "assets", "xml", d, sub)
            if not os.path.isdir(base):
                continue
            for fn in sorted(os.listdir(base)):
                if not fn.endswith(".xml"):
                    continue
                obj = fn[:-4]
                try:
                    txt = open(os.path.join(base, fn), encoding="utf-8", errors="ignore").read()
                except OSError:
                    continue
                for sym in _ANIM_ANY_RE.findall(txt):
                    lst = out.setdefault(obj, [])
                    if sym not in lst:
                        lst.append(sym)
    return out


def xml_original_names(repo: str = REPO) -> Dict[str, Dict[str, str]]:
    """{object_name: {n64_symbol: original_clip_name}} from the 2ship asset XMLs."""
    out: Dict[str, Dict[str, str]] = {}
    for d in XML_DIRS:
      for sub in XML_SUBDIRS:
        base = os.path.join(repo, "2ship", "assets", "xml", d, sub)
        if not os.path.isdir(base):
            continue
        for fn in sorted(os.listdir(base)):
            if not fn.endswith(".xml"):
                continue
            obj = fn[:-4]
            try:
                txt = open(os.path.join(base, fn), encoding="utf-8", errors="ignore").read()
            except OSError:
                continue
            for sym, orig in _ANIM_ORIG_RE.findall(txt):
                # first XML dir wins; don't let a later variant overwrite a known name
                out.setdefault(obj, {}).setdefault(sym, orig)
    return out


def match_anims(symbols: List[str], clip_names: List[str], object_name: str = "",
                accept: float = ACCEPT, orig: Optional[Dict[str, str]] = None) -> List[Match]:
    """Match each N64 animation symbol to at most one CSAB clip; ambiguous/weak => unmatched.

    `orig` = {symbol: original_clip_name} from the decomp XML annotations. When the annotated name
    is actually present in this actor's GAR it is taken verbatim at confidence 1.0 — authoritative,
    and the only signal that crosses the English<->romaji vocabulary gap."""
    if not object_name and symbols and "/" in symbols[0]:
        object_name = symbols[0].split("/")[1]
    extra = actor_tokens_for(object_name)
    raw_sym = [split_symbol(symbol_of(s)) for s in symbols]
    raw_clip = [split_clip(c) for c in clip_names]
    sym_toks = _strip_actor([t for t, _n in raw_sym], extra)
    clip_toks = _strip_actor([t for t, _n in raw_clip], extra)
    noise = _noise_heads(clip_toks)
    if noise:
        clip_toks = [(s[1:] if len(s) > 1 and s[0] in noise else s) for s in clip_toks]
    clip_num = [n for _t, n in raw_clip]

    orig = orig or {}
    clipset = set(clip_names)
    out: List[Match] = []
    for s, toks in zip(symbols, sym_toks):
        # AUTHORITATIVE: the decomp XML's "Original name is ..." annotation, when that clip exists.
        o = orig.get(symbol_of(s))
        if o and o in clipset:
            out.append(Match(s, o, 1.0, "xml original-name annotation"))
            continue
        if any(t in NON_SKEL_TOKENS for t in toks):
            out.append(Match(s, None, 0.0, "non-skeletal symbol (%s)" % "+".join(toks)))
            continue
        scored = []
        for c, ct, cn in zip(clip_names, clip_toks, clip_num):
            sc, why = _score(toks, ct)
            if sc > 0:
                scored.append((sc, cn, c, why))
        if not scored:
            out.append(Match(s, None, 0.0,
                             "no candidate shares a token with [%s]" % " ".join(toks)))
            continue
        scored.sort(key=lambda x: (-x[0], x[1], x[2]))
        top_sc, top_num, top_c, top_why = scored[0]
        if top_sc < accept:
            out.append(Match(s, None, round(top_sc, 2), "best %s too weak: %s" % (top_c, top_why)))
            continue
        rivals = [c for sc, num, c, _w in scored[1:]
                  if sc >= top_sc - TIE_MARGIN and (num == top_num or top_num)]
        if rivals:
            out.append(Match(s, None, round(top_sc, 2),
                             "ambiguous: %s vs %s" % (top_c, ",".join(rivals[:3]))))
            continue
        why = top_why
        if any(sc >= top_sc - TIE_MARGIN for sc, _n, _c, _w in scored[1:]):
            why += "; preferred over numbered variant"
        out.append(Match(s, top_c, round(top_sc, 2), why))
    return out


# =============================================================== driver

@dataclass
class ActorResult:
    obj: str
    gar: Optional[str]
    clips: List[str]
    matches: List[Match]
    # Informational ONLY: when the resolved GAR holds no CSAB at all, a same-family archive
    # that does (object_stk2 -> zelda2_stk). NOT matched against -- borrowing another actor's
    # clips would risk a wrong-skeleton animation, which is worse than the idle fallback.
    alt_gar_hint: Optional[str] = None


def _alt_gar_hint(obj: str, gar: str, actors: "Mm3dActors") -> Optional[str]:
    stem = re.sub(r"^(object_|obj_|gameplay_)", "", obj.lower())
    bases = {re.sub(r"\d+$", "", stem), stem.split("_")[0]}
    for b in sorted(bases):
        for cand in ("zelda2_" + b, "zelda_" + b, b):
            if cand != gar and actors.clips(cand):
                return cand
    return None


def build(only: Optional[str] = None, accept: float = ACCEPT) -> Tuple[List[ActorResult], dict]:
    anims = n64_anims()
    if only:
        anims = {k: v for k, v in anims.items() if k == only}
        if not anims:
            raise SystemExit("no animation symbols for object %r" % only)
    actors = Mm3dActors()
    known = set(actors.actors)
    orig_all = xml_original_names()
    results: List[ActorResult] = []
    for obj in sorted(anims):
        cands = object_to_gar(obj, known)
        gar = cands[0] if cands else None
        clips = actors.clips(gar) if gar else []
        clips = clips or []
        matches = match_anims(anims[obj], clips, obj, accept, orig_all.get(obj)) if clips else [
            Match(s, None, 0.0, "no GAR" if not gar else "GAR has no CSAB clips")
            for s in anims[obj]]
        hint = _alt_gar_hint(obj, gar, actors) if (gar and not clips) else None
        results.append(ActorResult(obj, gar, clips, matches, hint))
    meta = {
        "rom": getattr(actors.rom, "product_code", "MM3D"),
        "actor_gars_in_rom": len(known),
        "object_dirs_total": len(all_object_dirs()),
        "objects_with_anims": len(anims),
        "min_confidence": accept,
    }
    return results, meta


def emit_inc(results: List[ActorResult], meta: dict) -> str:
    lines = [
        "// Generated by tools/gen_mm_animmap.py -- DO NOT EDIT BY HAND.",
        "// N64 animation OTR key (no __OTR__ prefix) -> MM3D CSAB clip name.",
        "// Source: 2ship/assets/objects/*/ + the MM3D ROM's /actors/zelda*_*.gar[.lzs]",
        "// Accepted at confidence >= %.2f; ambiguous/weak matches are intentionally omitted so"
        % meta["min_confidence"],
        "// the actor falls back to its default idle CSAB rather than playing a wrong animation.",
    ]
    n = 0
    for r in results:
        got = [m for m in r.matches if m.clip]
        if not got:
            continue
        width = max(len(m.symbol) for m in got) + 3
        lines.append("// ---- %s (%s: %d/%d anims, %d clips) ----"
                     % (r.obj, r.gar, len(got), len(r.matches), len(r.clips)))
        for m in got:
            key = '"%s",' % m.symbol
            lines.append('    { %-*s "%s" },' % (width, key, m.clip))
            n += 1
    lines.insert(5, "// %d entries across %d actors."
                 % (n, sum(1 for r in results if any(m.clip for m in r.matches))))
    return "\n".join(lines) + "\n"


def _reason_bucket(why: str) -> str:
    if why.startswith("no candidate shares"):
        return "no shared token (romaji vs english vocabulary gap)"
    if why.startswith("best "):
        return "weak partial overlap (below min-confidence)"
    if why.startswith("non-skeletal"):
        return "non-skeletal symbol (Tex/UV/eye/mouth)"
    if why.startswith("ambiguous"):
        return "ambiguous tie between clips"
    if why == "no GAR":
        return "object has no MM3D actor GAR"
    return why


def build_report(results: List[ActorResult], meta: dict) -> Tuple[str, dict]:
    tot_sym = sum(len(r.matches) for r in results)
    tot_ok = sum(1 for r in results for m in r.matches if m.clip)
    with_gar = [r for r in results if r.gar]
    no_gar = [r for r in results if not r.gar]
    reasons: Dict[str, int] = {}
    for r in results:
        for m in r.matches:
            if not m.clip:
                reasons[_reason_bucket(m.why)] = reasons.get(_reason_bucket(m.why), 0) + 1

    js = {
        "meta": dict(meta, symbols_total=tot_sym, symbols_matched=tot_ok,
                     symbols_unmatched=tot_sym - tot_ok,
                     objects_with_gar=len(with_gar), objects_without_gar=len(no_gar)),
        "unmatched_reasons": reasons,
        "actors": [{
            "object": r.obj, "gar": r.gar, "clips": len(r.clips),
            "alt_gar_hint": r.alt_gar_hint,
            "symbols": len(r.matches),
            "matched": [{"n64otr": m.symbol, "csab": m.clip, "confidence": m.confidence,
                         "why": m.why} for m in r.matches if m.clip],
            "unmatched": [{"n64otr": m.symbol, "confidence": m.confidence, "why": m.why}
                          for m in r.matches if not m.clip],
        } for r in results],
    }

    md = ["# kMMAnimMaps coverage report", "",
          "Generated by `tools/gen_mm_animmap.py` (offline: repo assets + MM3D ROM `%s`)." % meta["rom"], "",
          "| metric | value |", "| --- | --- |",
          "| N64 object dirs | %d |" % meta["object_dirs_total"],
          "| objects with animation symbols | %d |" % meta["objects_with_anims"],
          "| MM3D `/actors/` GARs in ROM | %d |" % meta["actor_gars_in_rom"],
          "| anim-bearing objects resolved to a GAR | %d |" % len(with_gar),
          "| anim-bearing objects with NO GAR | %d |" % len(no_gar),
          "| animation symbols total | %d |" % tot_sym,
          "| symbols matched (conf >= %.2f) | %d (%.1f%%) |"
          % (meta["min_confidence"], tot_ok, 100.0 * tot_ok / max(1, tot_sym)),
          "| symbols unmatched | %d |" % (tot_sym - tot_ok), "",
          "## Unmatched reasons", "", "| reason | count |", "| --- | --- |"]
    for k, v in sorted(reasons.items(), key=lambda kv: -kv[1]):
        md.append("| %s | %d |" % (k, v))
    md += ["", "## Per actor", "", "| object | GAR | clips | symbols | matched | unmatched |",
           "| --- | --- | --- | --- | --- | --- |"]
    for r in sorted(results, key=lambda r: (-sum(1 for m in r.matches if m.clip), r.obj)):
        ok = sum(1 for m in r.matches if m.clip)
        md.append("| %s | %s | %d | %d | %d | %d |"
                  % (r.obj, r.gar or "(none)", len(r.clips), len(r.matches), ok,
                     len(r.matches) - ok))
    if no_gar:
        md += ["", "## Anim-bearing objects with no MM3D actor GAR", "",
               ", ".join("`%s`" % r.obj for r in no_gar), ""]
    zero = [r for r in results if r.gar and not r.clips]
    if zero:
        md += ["", "## Resolved to a GAR that contains NO CSAB clips", "",
               "These objects' MM3D archive holds only models/textures. Where a same-family"
               " archive does carry clips it is listed as a HINT -- it is NOT used for matching"
               " (another actor's clips could mean a wrong-skeleton animation).", "",
               "| object | GAR | symbols | same-family hint |", "| --- | --- | --- | --- |"]
        for r in sorted(zero, key=lambda r: -len(r.matches)):
            md.append("| %s | %s | %d | %s |"
                      % (r.obj, r.gar, len(r.matches), r.alt_gar_hint or "-"))
        md.append("")
    return "\n".join(md) + "\n", js


def main(argv=None) -> int:
    _load_env()
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--only", help="restrict to a single N64 object dir (e.g. object_dog)")
    ap.add_argument("--min-confidence", type=float, default=ACCEPT,
                    help="acceptance threshold (default %.2f)" % ACCEPT)
    ap.add_argument("--out", default=os.path.join("scratch", "mm_animmap.inc"),
                    help="C entries output (default scratch/mm_animmap.inc)")
    ap.add_argument("--report", default=os.path.join("scratch", "mm_animmap_report"),
                    help="report path stem; writes <stem>.md and <stem>.json")
    args = ap.parse_args(argv)

    results, meta = build(args.only, args.min_confidence)
    inc = emit_inc(results, meta)
    md, js = build_report(results, meta)

    out = args.out if os.path.isabs(args.out) else os.path.join(REPO, args.out)
    stem = args.report if os.path.isabs(args.report) else os.path.join(REPO, args.report)
    for p in (out, stem + ".md", stem + ".json"):
        os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(out, "w") as fh:
        fh.write(inc)
    with open(stem + ".md", "w") as fh:
        fh.write(md)
    with open(stem + ".json", "w") as fh:
        json.dump(js, fh, indent=2)

    m = js["meta"]
    print("objects with anims: %d  (resolved to a GAR: %d, no GAR: %d)"
          % (m["objects_with_anims"], m["objects_with_gar"], m["objects_without_gar"]))
    print("symbols: %d  matched: %d (%.1f%%)  unmatched: %d"
          % (m["symbols_total"], m["symbols_matched"],
             100.0 * m["symbols_matched"] / max(1, m["symbols_total"]), m["symbols_unmatched"]))
    for k, v in sorted(js["unmatched_reasons"].items(), key=lambda kv: -kv[1]):
        print("  %5d  %s" % (v, k))
    print("wrote %s, %s.md, %s.json"
          % (os.path.relpath(out, REPO), os.path.relpath(stem, REPO),
             os.path.relpath(stem, REPO)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
