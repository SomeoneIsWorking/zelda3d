"""N64 object and decomp-XML inventory for MM animation mapping."""

from __future__ import annotations

import os
import re
from typing import Dict, List, Optional

from mm_animmap_paths import OBJ_DIR, REPO

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
    return sorted(
        d
        for d in os.listdir(objects_dir)
        if os.path.isdir(os.path.join(objects_dir, d))
    )


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
            stem = stem[len(pref) :]
            break
    if name.startswith("gameplay_"):
        stem = name[len("gameplay_") :]
    cands = [
        "zelda2_" + stem,
        "zelda_" + stem,
        "zelda2_" + stem + "_new",
        "zelda_" + stem + "_new",
        stem,
        "zelda2_" + name,
        name,
    ]
    ordered, seen = [], set()
    for c in cands:
        if c not in seen:
            seen.add(c)
            ordered.append(c)
    return ordered if known is None else [c for c in ordered if c in known]


XML_DIRS = ("N64_US", "GC_US")
# KNOWN GAP: 11 animation symbols live in assets/overlays/ (e.g. ovl_En_Sth) rather than
# objects/. They are deliberately NOT scanned: an overlay has no MM3D actor GAR of its own
# (its model comes from some object), and that overlay->object association is not recorded
# in the XML, so scanning them would only produce unresolvable entries.
XML_SUBDIRS = ("objects",)
# One entry = the self-closing <Animation/> tag plus EVERY trailing comment on its line. Scanning
# only the FIRST comment (the previous shape of this regex) silently dropped 16 symbols whose
# annotation sits in a second comment, and there is no signal that it dropped them -- the symbol
# just fell through to fuzzy matching. Classification of the comment blob happens in Python below,
# because there are four annotation dialects and encoding them as four regexes made the precedence
# between them invisible.
_ANIM_ENTRY_RE = re.compile(
    r'<Animation\s+Name="([A-Za-z0-9_]+)"[^>]*/>((?:[ \t]*<!--(?:(?!-->).)*?-->)*)'
)

# The MM3D-side name, when the annotator recorded it directly. This OUTRANKS "Original name is":
# it names the clip in the MM3D GAR, which is the thing being matched, whereas "Original name"
# names the N64 asset and only usually coincides. e.g.
#     <!-- MM3D name is "dance_roll", but it was probably renamed. Might have originally been ... -->
# Under the old single-dialect regex these entries carried NO annotation at all and were matched
# by guesswork.
# BOTH alternatives are anchored on "MM3D". An unanchored /Named "X"/ also matches the SPECULATIVE
# phrasing "might have been originally named \"jmp_stop13\"", which is a guess about the N64 name in
# an entry that says the animation is NOT in MM3D at all -- widening it that far made
# gOdolwaJumpDanceAnim fall through to the heuristics and steal dance_jump from the symbol actually
# annotated with it.
_MM3D_NAME_RE = re.compile(
    r'(?:MM3D name is\s+["\']([A-Za-z0-9_]+)'
    r'|Named\s+["\']([A-Za-z0-9_]+)["\']\s+in MM3D)',
    re.I,
)
# "Or\w*nal" and not "Original": the corpus contains "Orignal" and "Orginal". The value is read as a
# maximal identifier rather than as everything-between-quotes, because several annotations close the
# quote in the wrong place ("gg_odoroki (surprise)").
_ORIG_NAME_RE = re.compile(r'Or\w*nal name is\s+["\']([A-Za-z0-9_]+)')
# A NEGATIVE annotation: the annotator checked and the clip is gone. This never adds a mapping; it
# only stops the heuristics from inventing one, which is the failure it exists to prevent -- these
# symbols were previously free to claim whatever clip scored best.
_ABSENT_RE = re.compile(r"Not present in MM3D|removed in MM3D", re.I)

_ANIM_ANY_RE = re.compile(r'<Animation\s+Name="([A-Za-z0-9_]+)"')
# <TextureAnimation> is a UV/material track, not a skeletal clip -- it can never match a CSAB and
# its symbol does not always carry a "tex"/"uv" token, so token-based exclusion misses some.
_TEXANIM_RE = re.compile(r'<TextureAnimation\s+Name="([A-Za-z0-9_]+)"')


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
                    with open(
                        os.path.join(base, fn), encoding="utf-8", errors="ignore"
                    ) as xml_file:
                        txt = xml_file.read()
                except OSError:
                    continue
                for sym in _ANIM_ANY_RE.findall(txt):
                    lst = out.setdefault(obj, [])
                    if sym not in lst:
                        lst.append(sym)
    return out


def _classify_annotation(comments: str) -> Optional[Optional[str]]:
    """The annotated clip name, None for 'annotated as ABSENT', or missing (return ()) for no
    annotation at all. Precedence: MM3D-side name > N64 original name > absent."""
    m = _MM3D_NAME_RE.search(comments)
    if m:
        return m.group(1) or m.group(2)
    # ABSENT outranks "Original name is": when an entry carries both ('Original name is "ka_dance"
    # (this animation was removed in MM3D)') the annotator is telling us the clip is gone, and the
    # N64 name is history rather than a lookup key.
    if _ABSENT_RE.search(comments):
        return None
    m = _ORIG_NAME_RE.search(comments)
    if m:
        return m.group(1)
    return ()  # sentinel: no annotation of any dialect


def xml_original_names(repo: str = REPO) -> Dict[str, Dict[str, Optional[str]]]:
    """{object_name: {n64_symbol: annotated_clip_name_or_None}}, from the 2ship asset XMLs.

    A value of None means the annotator recorded that the animation is ABSENT from MM3D. That is
    information, not a gap: it must suppress fuzzy matching rather than leave the symbol open."""
    out: Dict[str, Dict[str, Optional[str]]] = {}
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
                    with open(
                        os.path.join(base, fn), encoding="utf-8", errors="ignore"
                    ) as xml_file:
                        txt = xml_file.read()
                except OSError:
                    continue
                for sym, comments in _ANIM_ENTRY_RE.findall(txt):
                    if not comments:
                        continue
                    val = _classify_annotation(comments)
                    if val == ():
                        continue
                    # first XML dir wins; don't let a later variant overwrite a known name
                    out.setdefault(obj, {}).setdefault(sym, val)
    return out


def xml_texture_anims(repo: str = REPO) -> Dict[str, set]:
    """{object_name: {symbol,...}} declared as <TextureAnimation> -- never skeletal."""
    out: Dict[str, set] = {}
    for d in XML_DIRS:
        for sub in XML_SUBDIRS:
            base = os.path.join(repo, "2ship", "assets", "xml", d, sub)
            if not os.path.isdir(base):
                continue
            for fn in sorted(os.listdir(base)):
                if not fn.endswith(".xml"):
                    continue
                try:
                    with open(
                        os.path.join(base, fn), encoding="utf-8", errors="ignore"
                    ) as xml_file:
                        txt = xml_file.read()
                except OSError:
                    continue
                for sym in _TEXANIM_RE.findall(txt):
                    out.setdefault(fn[:-4], set()).add(sym)
    return out
