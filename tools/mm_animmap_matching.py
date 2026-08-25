"""Token scoring and evidence-gated animation matching."""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Dict, List, Optional, Tuple

from mm_animmap_inventory import NON_SKEL_TOKENS, symbol_of

# =============================================================== (c) matcher

SYNONYM_CLASSES: List[Tuple[str, ...]] = [
    (
        "wait",
        "idle",
        "stand",
        "matsu",
        "matteru",
        "machi",
        "mati",
        "kihon",
        "tachi",
        "tati",
        "neutral",
    ),
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
    ("jmp",),
    ("dam",),
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
            base = t[: -len(suf)]
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
    return (
        len(inter) / float(len(sa | sb)),
        "partial overlap %s (%d/%d)"
        % ("+".join(sorted(inter)), len(inter), len(sa | sb)),
    )


ACCEPT = 0.90
TIE_MARGIN = 0.05

# ---------------------------------------------- externally VERIFIED mappings (evidence required)
# Mappings established by measurement rather than by a rule that generalises. Each entry must cite
# the evidence, and `--verify-overrides` re-derives it from the ROM so a stale entry FAILS instead
# of quietly persisting. This is the sanctioned way to carry a proven fact -- hand-editing the
# generated .inc is not, because the next regeneration silently drops it.
#
# gameplay_keep doors. Closed system: exactly 8 N64 door symbols, exactly 8 door clips in
# zelda2_keep (door/anim/). Resolved by two INDEPENDENT chains, neither of which is frame counts
# alone -- durations 66 and 85 each recur across forms, so bipartite matching on duration admits
# many assignments and settles nothing by itself:
#
#   1. FORM, from archive directory names. zelda2_link_new.gar.lzs files the same door animations
#      under form-named directories: child/anim/clink_demo_door*, goron/anim/pg_door*,
#      nuts/anim/pn_door*, zora/anim/pz_door*. So clink=Human(child), pg=Goron, pn=Deku(nuts),
#      pz=Zora -- named, not inferred. zelda2_keep has NO pz and instead has `link`, and the one
#      N64 symbol covering two adult-height forms is FierceDeityZora: the door needs 4 height
#      classes where the player rig needs 5. Hence link = FierceDeityZora.
#   2. SIDE, from exact duration equality WITHIN each already-pinned form, where the two clips
#      always differ (measured N64 frameCount u16@0x44 vs MM3D csab duration u32@0x34):
#         Human  L 88 / R 85   clink_demo_doorA 88 / B 85
#         FD+Zora L 66 / R 74  link_demo_doorA  66 / B 74
#         Goron  L 66 / R 85   pg_doorA 66 / pg_doorB 85
#         Deku   L 81 / R 85   pn_doorA 81 / pn_doorB 85
#      Four independent confirmations of the single bit A=Left, B=Right.
VERIFIED_OVERRIDES: Dict[str, Dict[str, str]] = {
    "gameplay_keep": {
        "gDoorHumanOpenLeftAnim": "clink_demo_doorA_door",
        "gDoorHumanOpenRightAnim": "clink_demo_doorB_door",
        "gDoorFierceDeityZoraOpenLeftAnim": "link_demo_doorA_door",
        "gDoorFierceDeityZoraOpenRightAnim": "link_demo_doorB_door",
        "gDoorGoronOpenLeftAnim": "pg_doorA",
        "gDoorGoronOpenRightAnim": "pg_doorB",
        "gDoorDekuOpenLeftAnim": "pn_doorA",
        "gDoorDekuOpenRightAnim": "pn_doorB",
    },
    # object_delf: the decomp XML annotates TWO symbols with the same original name, which is an
    # upstream copy-paste error rather than a genuine alias -- and it produced a real wrong mapping,
    # two N64 animations sharing one clip while elf_attack_2b went unclaimed.
    #   0x4FF4 elf_attack_1a   0x53A4 elf_attack_1b   0x5B68 elf_attack_2a   0x6328 "elf_attack_1b"
    # MEASURED, not argued from the offset ordering (which an adversarial pass rejected as a general
    # corroborator, 8 of 141 objects being non-monotone): the N64 frameCounts are 24, 24, 56, 56 and
    # the GAR durations are elf_attack_1a 23, 1b 23, 2a 55, 2b 55. Symbol 0x6328 is 56 frames, so it
    # belongs to the 2-series and is 33 frames away from the elf_attack_1b it is annotated with.
    # Within that series 2a is already claimed by its own correctly-annotated symbol and 2b is the
    # only unclaimed clip, so the assignment is forced. The duration evidence establishes the SERIES;
    # it cannot separate 2a from 2b on its own, since both are 55.
    "object_delf": {
        "object_delf_Anim_006328": "elf_attack_2b",
    },
}


# ---------------------------------------------- authoritative XML "Original name" annotations
# The 2ship decomp XMLs annotate most animations with the asset's ORIGINAL (romaji) name, which is
# exactly what the MM3D GAR names its CSAB clip:
#     <Animation Name="gDogBarkAnim" Offset="0x998" /> <!-- Original name is "dog_bark" -->
# 1555 of 1746 Animation entries carry it. This is an AUTHORITATIVE mapping written by the decomp
# authors, so it beats any lexical guess — and it is the only thing that bridges the romaji gap
# (MM3D clips are Japanese: an_hokiwalk, dnt_iyaiyaTOmuun), which pure name matching cannot do.


def match_anims(
    symbols: List[str],
    clip_names: List[str],
    object_name: str = "",
    accept: float = ACCEPT,
    orig: Optional[Dict[str, Optional[str]]] = None,
    texanims: Optional[set] = None,
) -> List[Match]:
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
    texanims = texanims or set()
    clipset = set(clip_names)
    out: List[Match] = []
    for s, toks in zip(symbols, sym_toks):
        sym = symbol_of(s)
        ov = VERIFIED_OVERRIDES.get(object_name, {}).get(sym)
        if ov:
            # Refuse rather than silently fall through: an override naming a clip this GAR does not
            # have means the evidence behind it no longer describes the asset.
            if ov not in clipset:
                raise SystemExit(
                    "VERIFIED_OVERRIDES[%r][%r] names %r, which is not a clip in this GAR (%d "
                    "clips). The recorded evidence no longer holds -- re-derive it, do not delete "
                    "the guard." % (object_name, sym, ov, len(clip_names))
                )
            out.append(Match(s, ov, 1.0, "verified override (see VERIFIED_OVERRIDES)"))
            continue
        if sym in texanims:
            out.append(
                Match(
                    s, None, 0.0, "non-skeletal: declared <TextureAnimation> in the XML"
                )
            )
            continue
        # AUTHORITATIVE: the decomp XML's annotation, when it names a clip this GAR actually has.
        if sym in orig:
            o = orig[sym]
            if o is None:
                # The annotator checked and said it is gone. Without this the heuristics were free
                # to hand the symbol whatever clip scored best, which is a fabricated mapping.
                out.append(
                    Match(
                        s, None, 0.0, "XML annotates this animation as absent from MM3D"
                    )
                )
                continue
            if o in clipset:
                out.append(Match(s, o, 1.0, "xml annotation names an existing clip"))
                continue
            # The annotated name is absent but exactly one numbered clip extends it (last_dam ->
            # last_dam01). Gated on the exact name being absent AND the variant being unique; with
            # either guard dropped this rule starts stealing clips from their real owners.
            vs = sorted(
                c for c in clip_names if re.fullmatch(re.escape(o) + r"\d{1,2}", c)
            )
            if len(vs) == 1:
                out.append(
                    Match(
                        s,
                        vs[0],
                        0.95,
                        "xml annotation %r + its unique numeric variant" % o,
                    )
                )
                continue
            # Otherwise fall through to the heuristics: an annotation naming no clip here is a
            # dead end, not evidence of absence (that is what the negative dialect is for).
        if any(t in NON_SKEL_TOKENS for t in toks):
            out.append(Match(s, None, 0.0, "non-skeletal symbol (%s)" % "+".join(toks)))
            continue
        scored = []
        for c, ct, cn in zip(clip_names, clip_toks, clip_num):
            sc, why = _score(toks, ct)
            if sc > 0:
                scored.append((sc, cn, c, why))
        if not scored:
            out.append(
                Match(
                    s,
                    None,
                    0.0,
                    "no candidate shares a token with [%s]" % " ".join(toks),
                )
            )
            continue
        scored.sort(key=lambda x: (-x[0], x[1], x[2]))
        top_sc, top_num, top_c, top_why = scored[0]
        if top_sc < accept:
            out.append(
                Match(
                    s, None, round(top_sc, 2), "best %s too weak: %s" % (top_c, top_why)
                )
            )
            continue
        rivals = [
            c
            for sc, num, c, _w in scored[1:]
            if sc >= top_sc - TIE_MARGIN and (num == top_num or top_num)
        ]
        if rivals:
            out.append(
                Match(
                    s,
                    None,
                    round(top_sc, 2),
                    "ambiguous: %s vs %s" % (top_c, ",".join(rivals[:3])),
                )
            )
            continue
        why = top_why
        if any(sc >= top_sc - TIE_MARGIN for sc, _n, _c, _w in scored[1:]):
            why += "; preferred over numbered variant"
        out.append(Match(s, top_c, round(top_sc, 2), why))
    return out
