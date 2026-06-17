#!/usr/bin/env python3
"""Generate charcompare_index.inc — the character index the charcompare tool's cascading
selectors read (TYPE -> thing -> ANIMATION).

Source of truth: tools/skeldata/animmap.json (keyed by 3DS ZAR path, with the N64 object,
each N64 anim's OTR path + frame count, and the best-matched 3DS CSAB). We add a curated
TYPE/category per object (CATEGORY below; unknowns fall to "other"). The N64 *skeleton*
symbol is NOT here — the tool derives it at runtime by listing the object's resources.

Emits an .inc of IndexAnim[]/IndexEntry[] arrays (structs declared in cc_index.h).
Run:  python3 tools/gen_charcompare_index.py
"""
import os

import animmap_source

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "Shipwright/soh/charcompare/charcompare_index.inc")

# Curated TYPE per N64 object. Anything unlisted -> "other". Editable; regenerate after edits.
# Categories: character (named/humanoid NPCs), monster (enemies/bosses), creature (animals),
# item (pickups/props), object (misc).
CATEGORY = {
    # --- characters (NPCs, humanoid) ---
    "object_ge1": "character", "object_geldb": "character", "object_gla": "character",
    "object_daiku": "character", "object_toryo": "character", "object_du": "character",
    "object_im": "character", "object_in": "character", "object_ka": "character",
    "object_ru1": "character", "object_ru2": "character", "object_sa": "character",
    "object_zl1": "character", "object_zl4": "character", "object_ma1": "character",
    "object_ma2": "character", "object_mastergolon": "character", "object_masterzoora": "character",
    "object_kz": "character", "object_nb": "character", "object_zo": "character",
    "object_zf": "character", "object_hni": "character", "object_oF1d_map": "character",
    "object_md": "character", "object_mu": "character", "object_fu": "character",
    "object_ahg": "character", "object_aob": "character", "object_bji": "character",
    "object_boj": "character", "object_cne": "character", "object_os": "character",
    "object_rl": "character",
    # --- monsters / enemies / bosses ---
    "object_am": "monster", "object_anubice": "monster", "object_Bb": "monster",
    "object_dekubaba": "monster", "object_dekunuts": "monster", "object_dh": "monster",
    "object_dodongo": "monster", "object_dodojr": "monster", "object_ei": "monster",
    "object_fd": "monster", "object_fw": "monster", "object_goma": "monster",
    "object_gnd": "monster", "object_gndd": "monster", "object_ganon2": "monster",
    "object_kingdodongo": "monster", "object_mb": "monster", "object_rd": "monster",
    "object_sb": "monster", "object_skb": "monster", "object_sk2": "monster",
    "object_st": "monster", "object_sst": "monster", "object_tr": "monster",
    "object_tw": "monster", "object_wf": "monster", "object_wallmaster": "monster",
    "object_tite": "monster", "object_reeba": "monster", "object_peehat": "monster",
    "object_bigokuta": "monster", "object_okuta": "monster", "object_ik": "monster",
    "object_fhg": "monster", "object_bv": "monster", "object_bw": "monster",
    "object_brob": "monster", "object_bl": "monster", "object_bg": "monster",
    "object_mm": "monster", "object_ms": "monster", "object_ph": "monster",
    "object_ssh": "monster", "object_vali": "monster", "object_dns": "monster",
    "object_dnk": "monster", "object_hintnuts": "monster", "object_shopnuts": "monster",
    "object_skj": "monster", "object_cs": "monster",
    # --- creatures (animals) ---
    "object_cow": "creature", "object_dog": "creature", "object_horse": "creature",
    "object_horse_normal": "creature", "object_horse_ganon": "creature",
    "object_horse_zelda": "creature", "object_horse_link_child": "creature",
    "object_crow": "creature", "object_niw": "creature", "object_firefly": "creature",
    "object_owl": "creature", "object_po_field": "creature",
    # --- poe / spirit ---
    "object_poh": "monster", "object_po_composer": "monster", "object_po_sisters": "monster",
    # --- items / props / objects ---
    "object_box": "item", "object_dy_obj": "item", "object_warp1": "item",
    "object_hata": "item", "object_gr": "item", "object_hs": "object",
    "object_js": "object", "object_ps": "object", "object_ts": "object",
    "object_vm": "object", "object_mk": "object", "object_sd": "object",
    "object_ds": "object", "object_ds2": "object", "object_gm": "object",
    "object_jj": "object", "object_rs": "object", "object_ec": "object",
    "object_ta": "object", "object_tk": "character", "object_fr": "creature",
}


def cident(s):
    return "".join(c if c.isalnum() else "_" for c in s)


def main():
    src, n_ov = animmap_source.load()
    # Stable order: by category then display name.
    entries = []
    for d in src:
        zar = d["zar"]
        obj = d["object"]
        name = zar.replace("/actor/zelda_", "").replace(".zar", "")
        cat = CATEGORY.get(obj, "other")
        anims = [(r["n64"], r["otr"], r["frameCount"], r["csab"]) for r in d["rows"]]
        entries.append((cat, name, zar, obj, anims))
    entries.sort(key=lambda e: (e[0], e[1]))

    lines = []
    lines.append("// GENERATED by tools/gen_charcompare_index.py — do not edit by hand.")
    lines.append("// charcompare cascading-selector index: TYPE -> thing -> ANIMATION.")
    lines.append("// Structs (IndexAnim / IndexEntry) are declared in cc_index.h.")
    lines.append(f"// {len(entries)} characters from tools/skeldata/animmap.json.")
    lines.append("")
    # Per-entry anim arrays.
    for cat, name, zar, obj, anims in entries:
        arr = "ccAnims_" + cident(name)
        lines.append(f"static const IndexAnim {arr}[] = {{")
        for n64, otr, fc, csab in anims:
            lines.append(f'    {{ "{n64}", "{otr}", {fc}, "{csab}" }},')
        lines.append("};")
    lines.append("")
    lines.append("static const IndexEntry kCcEntries[] = {")
    for cat, name, zar, obj, anims in entries:
        arr = "ccAnims_" + cident(name)
        lines.append(f'    {{ "{cat}", "{name}", "{zar}", "{obj}", {arr}, {len(anims)} }},')
    lines.append("};")
    lines.append(f"static const int kCcEntryCount = {len(entries)};")
    lines.append("")

    with open(OUT, "w") as f:
        f.write("\n".join(lines))
    cats = {}
    for cat, *_ in entries:
        cats[cat] = cats.get(cat, 0) + 1
    print(f"wrote {OUT}: {len(entries)} entries ({n_ov} csab overrides applied); categories: " +
          ", ".join(f"{k}={v}" for k, v in sorted(cats.items())))


if __name__ == "__main__":
    main()
