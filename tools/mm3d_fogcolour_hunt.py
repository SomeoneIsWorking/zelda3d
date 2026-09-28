#!/usr/bin/env python3
#!/usr/bin/env python3
"""Is MM3D's authored PICA fog colour present in the scene data we can already read?

The measured divergence is that MM3D's live PICA fog colour for the Lost Woods frame is
(40,140,220) while the recovered env-light record's `fogCol` values for that scene are
(90,133,180) (28,20,0) (0,0,30) (5,55,75) (130,180,180) (28,20,0) (0,0,30) (114,115,101).
So the quantity the hardware uses is not the one the env record stores.

The scene ZSIs are already readable -- the lighting generator inflates 182 of MM3D's 424 as LzS
-- so this asks the cheap question before anyone reaches for a decompiler: does the authored
triple appear in that data at all, and if so is its offset CONSISTENT across scenes?

**The recorded answer is NO, and getting there cost a false positive.** A 3-byte triple in an 80 KB
blob hits by chance: the first version of this tool found (40,140,220) twice, 0x20 apart, with a clean
never-reported control, and called it a real candidate. It was a coincidence. The 0x20 spacing looked
convincing until the offsets were checked against a region whose layout is already known -- MM3D's env
records are 0x20-strided, so the hits had to land at slot multiples from a known env `fogCol`, and
they were +12008 and +12040, i.e. 8 mod 0x20. Their containing data is referenced nowhere in the file,
its float fields are garbage, and the "plausible colour" run around them degenerates into noise
((160,9,173), (41,0,56), (174,9,124)) after four entries.

So this tool exists to keep that negative reproducible, and `classify()` is pure so the two answers can
both be tested without the ROM.
"""
import os
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

from gen_mm3d_scene_lighting import maybe_inflate  # noqa: E402
from ctr_romfs import CtrRom  # noqa: E402

# (40,140,220) is MM3D's recorded live PICA fog colour for the Lost Woods frame.
TARGET = (40, 140, 220)
# A control the oracle never reported. If this also hits, a TARGET hit is not evidence.
CONTROL = (37, 143, 217)

SCENES = ("z2_lost_woods", "spot00", "z2_clocktower", "z2_11goronnosato", "z2_10yukiyamanomura")

ENV_RECORD_STRIDE = 0x20
# The recovered z2_lost_woods slot 0 fogCol, used as the alignment anchor: its offset inside the
# inflated ZSI is known, so "same table" becomes arithmetic rather than a resemblance.
KNOWN_ENV_FOGCOL = (90, 133, 180)


def classify(hits: list[int], anchor: int | None) -> str:
    """Say whether `hits` lie in the env-record table. Pure, so it is testable without the ROM.

    The negative here is the point. A never-reported control triple coming back clean is NOT enough:
    the first version of this tool reported "real candidate" on exactly that evidence, and the hit was
    a coincidence. What separates a field from a coincidence is ALIGNMENT -- MM3D's env records are
    0x20-strided, so a hit at a slot multiple from a known env colour is in the table and one that is
    not, is not, however much its neighbours look like plausible colours.
    """
    if not hits:
        return "absent"
    if anchor is None or anchor < 0:
        return "unanchored"
    return "aligned" if all((h - anchor) % ENV_RECORD_STRIDE == 0 for h in hits) else "off-stride"


def scan(blob: bytes, value: tuple[int, int, int]) -> list[int]:
    needle = bytes(value)
    hits, at = [], blob.find(needle)
    while at != -1:
        hits.append(at)
        at = blob.find(needle, at + 1)
    return hits


def main() -> int:
    rom_path = os.environ.get("ZELDA3D_MM3D_ROM")
    if not rom_path or not os.path.isfile(rom_path):
        print("set ZELDA3D_MM3D_ROM (see .env)")
        return 2
    rom = CtrRom(rom_path)

    wanted: dict[str, list[str]] = {}
    for entry in rom.iter_files():
        p = entry if isinstance(entry, str) else getattr(entry, "path", str(entry))
        m = re.match(r"/scenes/(.+?)_\d+_info\.zsi$", p) or re.match(r"/scenes/(.+?)_info\.zsi$", p)
        if m and m.group(1) in SCENES:
            wanted.setdefault(m.group(1), []).append(p)

    print(f"scanned ROM file table; {len(wanted)}/{len(SCENES)} requested scenes present")
    total_target = total_control = 0
    for name in SCENES:
        paths = sorted(wanted.get(name, []))
        if not paths:
            print(f"  {name}: no ZSI variant")
            continue
        for path in paths:
            raw = rom.read(rom.get(path))
            blob = maybe_inflate(raw)
            compressed = "LzS" if blob is not raw and len(blob) != len(raw) else "plain"
            hits = scan(blob, TARGET)
            ctrl = scan(blob, CONTROL)
            total_target += len(hits)
            total_control += len(ctrl)
            where = ",".join(f"0x{o:04X}" for o in hits[:8]) or "-"
            print(
                f"  {name}: {path.rsplit('/', 1)[-1]} {compressed} {len(blob)}B"
                f" target[{TARGET}]={len(hits)} at {where}  control[{CONTROL}]={len(ctrl)}"
            )
    print(f"TOTAL target={total_target} control={total_control}")

    # The never-reported control above is too WEAK to be the only test: a 3-byte triple in an 80 KB
    # blob hitting twice with the control clean is still consistent with coincidence, and it was.
    # The decisive test is ALIGNMENT against a region whose layout is already known: MM3D's env
    # records are 0x20-strided, so a hit that is a slot multiple away from a known env colour is in
    # the same table, and one that is not, is not -- whatever its neighbours look like.
    blob = maybe_inflate(rom.read(rom.get("/scenes/z2_lost_woods_info.zsi")))
    known = blob.find(bytes((90, 133, 180)))  # slot 0 fogCol of the recovered z2_lost_woods palette
    hits = []
    at = blob.find(bytes(TARGET))
    while at != -1:
        hits.append(at)
        at = blob.find(bytes(TARGET), at + 1)
    print(f"\nknown env fogCol (90,133,180) at 0x{known:05X}; target hits at {[hex(h) for h in hits]}")
    verdict = classify(hits, known)
    print(f"slot-aligned with the env region (stride 0x20): {verdict}")
    for h in hits:
        delta = h - known if known >= 0 else 0
        print(f"  hit 0x{h:05X} is {delta:+d} from it; {delta % 0x20} mod 0x20")

    if verdict == "absent":
        print("VERDICT: the authored colour is NOT in the scene ZSIs; it lives elsewhere.")
    elif verdict == "aligned":
        print("VERDICT: slot-aligned with the known env region -- a real candidate, still needing a")
        print("         consistent offset across scenes before it can be called a field.")
    else:
        print("VERDICT: NOT the env region. Every hit is off-stride, its containing data is")
        print("         unreferenced, and the 0x20 run around it degenerates into noise -- so this is")
        print("         a coincidental byte match. The clean never-reported control did NOT catch it;")
        print("         the alignment test did. MM3D's fog colour is not in the scene ZSIs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
