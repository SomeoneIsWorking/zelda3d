#!/usr/bin/env python3
"""Generate the committed N64-object-id -> OoT3D actor-ZAR table.

An actor's object dependency id is the ObjectID enum value, and ObjectID is generated
from soh/include/tables/object_table.h via DEFINE_OBJECT in order, so the enum value
equals the row index (the `/* 0xNNNN */` comment). We emit a positional `const char*`
array indexed by object id.

N64 objects are `DEFINE_OBJECT(object_<name>, OBJECT_<ENUM>)`; OoT3D ships the matching
model archive at `/actor/zelda_<name>.zar`. The `<name>` matches 1:1 for ~289 of the
object_* entries (tsubo, kibako2, ge1, kusa, box, bombiwa, ...). We map an id ONLY when
its segment is `object_*` AND that exact ZAR exists in the romfs; everything else (the
keep objects, renamed/absent ZARs, UNSET/NULL slots) maps to NULL so the runtime falls
back to the N64 actor. Output is ZAR PATHS only (no ROM-derived asset bytes) -> safe to
commit. Needs ZELDA3D_OOT3D_ROM to check ZAR existence.

Run: ZELDA3D_OOT3D_ROM=<path.3ds> python3 tools/gen_object_zars.py
"""
import os, re, sys
sys.path.insert(0, os.path.dirname(__file__))
from ctr_romfs import CtrRom

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OBJ_TABLE = os.path.join(REPO, "Shipwright/soh/include/tables/object_table.h")
OUT = os.path.join(REPO, "Shipwright/soh/src/zelda3d/tables/zelda3d_object_zars.inc")

# N64 object name -> OoT3D /actor/zelda_<basename>.zar, for CREATURES whose OoT3D archive was
# RENAMED (so the exact object_<name> match misses). Each was confirmed by dumping the zar's main
# CMB (tools: zar.py + cmb.py): the listed zar's main model is that creature. Only add an entry
# when the model identity is certain (the cmb name says so) — a wrong guess renders the wrong actor.
ALIAS = {
    "niw": "nw",                # Cucco            (nw/chicken.cmb)
    "okuta": "oc2",             # Octorok          (oc2/octarock.cmb)
    "bigokuta": "ocd",          # Big Octo         (ocd/daiocta.cmb)
    "peehat": "ph",             # Peahat           (ph/peehat.cmb)
    "poh": "po",                # Poe              (po/poh.cmb)
    "reeba": "rb",              # Leever           (rb/reeba.cmb)
    "tite": "tt",               # Tektite          (tt/tectite.cmb)
    "wallmaster": "wm2",        # Floor/Wallmaster (wm2/floormaster.cmb)
    "kingdodongo": "kdodongo",  # King Dodongo     (kdodongo/kingdodongo.cmb)
    "firefly": "ff",            # Keese            (ff/keith.cmb)
    "anubice": "av",            # Anubis           (av/anubis.cmb)
    "Bb": "bb",                 # Bubble           (bb/bubble.cmb)
    "geldb": "gelb",            # Gerudo guard     (gelb/geld.cmb)
    "horse_link_child": "hlc",  # child Epona      (hlc/childepona.cmb)
    "fhg": "fantomHG",          # Phantom Ganon's horse (fantomHG/ganonhorse.cmb)
    "door_killer": "killer_door",  # Killer Door   (killer_door/idle.cmb)
    "sk2": "skelton",           # Stalfos (En_Test) (skelton/stalfos.cmb)
    "oF1d_map": "oF1d",         # Goron NPCs (En_Go/En_Go2, Goron City) (oF1d/goronpeople.cmb)
    "fish": "fishing",          # Fishing-pond owner (Fishing actor) (fishing/fishmaster.cmb)
    "bdoor": "boss_door",       # Boss-room door     (boss_door/bossdoor_model.cmb)
    # Dungeon-prop archives (OoT3D groups them under a "dk_" prefix, no zelda_ prefix). Each
    # confirmed by the CMB basename matching the N64 object/actor's single drawn model.
    "lightbox": "dk_lightbox",  # En_Lightbox (Forest Temple light box) (dk_lightbox/lightbox4_model.cmb)
    "shop_dungen": "shop_tana",  # En_Tana shop shelves (shop_tana01/02/03_model.cmb) — #115
    "gol": "kogoma",            # En_Goma baby-Gohma larva (uses gObjectGolSkel egg-hatch) (kogoma/childgoma.cmb)
    # OoT3D drops the "t": zelda_spo04_objects.zar, not zelda_spot04_objects.zar. Holds
    # spot04_kuchi_model.cmb -- "kuchi" is Japanese for MOUTH -- which is Bg_Treemouth's model, the
    # Deku Tree's mouth in Kokiri Forest. It had been rendering the N64 mesh in an otherwise-3DS
    # scene purely because of the one-character name difference. NOTE: this archive holds 11 CMBs
    # (Y_*/ousei_* cutscene models too), so AUTO's largest-CMB heuristic cannot be trusted to pick
    # the mouth -- Bg_Treemouth is routed explicitly by the replacement catalog.
    "spot04_objects": "spo04_objects",
    # Dungeon props whose archives use the bare dk_ prefix. Each confirmed twice: the archive name
    # matches the object, AND the actor that loads the object draws a single model of that name.
    "vase": "dk_vase",   # En_Vase (z_en_vase.c)  -- dk_vase/vase1_obj_o2.cmb, the archive's ONLY cmb
    "trap": "dk_trap",   # En_Trap (z_en_trap.c)  -- dk_trap/trap_model.cmb (+ trap2_center_model)
    # DELIBERATELY NOT ADDED: "pu_box": "dk_pu_box". The archive holds pu_box1/pu_box2/pu_box4_model,
    # which are SIZE VARIANTS, and Bg_Pushbox + En_Pu_box share the object. Mapping it would hand
    # every push block whichever CMB is largest -- precisely the En_Ishi/Obj_Hana bug fixed earlier
    # (differently-sized props all rendering at one size), and push blocks are puzzle geometry where
    # a wrong size is a gameplay error, not just a cosmetic one. Needs per-variant routing first;
    # the CMB names above are the whole remaining input to that.
}

# Alias values are OoT3D zar BASENAMES. Resolve to a real /actor path, trying the common
# "zelda_<name>.zar" naming first, then a bare "<name>.zar" (the dk_* dungeon-prop archives).
def alias_path(base, zars):
    for cand in ("/actor/zelda_%s.zar" % base, "/actor/%s.zar" % base):
        if cand in zars:
            return cand
    return None


def main():
    rom = CtrRom(os.environ["ZELDA3D_OOT3D_ROM"])
    zars = {f.path for f in rom.iter_files() if f.path.endswith(".zar")}

    rows = []  # (id, enum, segment_or_None, zar_path_or_None)
    maxid = 0
    for line in open(OBJ_TABLE):
        m = re.search(r"/\*\s*0x([0-9A-Fa-f]+)\s*\*/\s*DEFINE_OBJECT(?:_UNSET|_NULL)?\(", line)
        if not m:
            continue
        oid = int(m.group(1), 16)
        maxid = max(maxid, oid)
        d = re.search(r"DEFINE_OBJECT\(\s*([A-Za-z0-9_]+)\s*,\s*(OBJECT_[A-Z0-9_]+)", line)
        if not d:
            continue  # UNSET/NULL slot -> stays NULL
        seg, enum = d.group(1), d.group(2)
        zar = None
        if seg.startswith("object_"):
            base = seg[len("object_"):]
            cand = "/actor/zelda_" + base + ".zar"
            if cand in zars:
                zar = cand
            elif base in ALIAS:
                zar = alias_path(ALIAS[base], zars)  # renamed-creature / dungeon-prop alias
        rows.append((oid, enum, seg, zar))

    table = [None] * (maxid + 1)
    enums = [""] * (maxid + 1)
    for oid, enum, seg, zar in rows:
        table[oid] = zar
        enums[oid] = enum

    mapped = sum(1 for z in table if z)
    with open(OUT, "w") as o:
        o.write("// GENERATED by tools/gen_object_zars.py — do not edit by hand.\n")
        o.write("// N64 object id (ObjectID enum, == object_table.h row index) -> OoT3D actor\n")
        o.write("// model archive /actor/zelda_<name>.zar (NULL = no OoT3D ZAR; N64 fallback).\n")
        o.write("// Paths only; no ROM-derived asset bytes. Used by the ZELDA3D_AUTO actor path.\n")
        o.write(f"// {mapped}/{maxid + 1} object ids mapped to a ZAR.\n")
        o.write("static const char* const kZelda3dObjectZars[] = {\n")
        for i in range(maxid + 1):
            val = f"\"{table[i]}\"" if table[i] else "NULL"
            tag = enums[i] if enums[i] else "(unset)"
            o.write(f"    /* 0x{i:04X} {tag:<36} */ {val},\n")
        o.write("};\n")
    print(f"wrote {OUT}: {mapped}/{maxid + 1} object ids mapped to a ZAR")


if __name__ == "__main__":
    main()
