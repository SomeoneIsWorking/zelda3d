#!/usr/bin/env python3
"""How much of OoT's and MM's game code is ACTUALLY the same, not just the same size.

    tools/core_overlap.py

Claim C051 measured the overlap by symbol SIZE and flagged its own weakness: two different functions
can share a size, so "42.6% identical" was an UPPER BOUND, not a measurement. This settles it by
comparing the code.

Raw bytes cannot be compared directly -- relocations and immediates embed addresses that differ
between the two links even for identical source. So each function is normalised to its sequence of
instruction MNEMONICS (opcode only, operands dropped) and hashed. Two functions with the same
mnemonic sequence are the same code up to register allocation and addressing; different sequences
are genuinely different code.

That normalisation is deliberately generous: it can call two functions identical when they differ
only in operands (a different constant, a different field offset). So the number it reports is STILL
an upper bound -- but a far tighter one than size equality, and being generous is the right bias
here, because the question is "how much COULD be shared".

REFUSES rather than reporting an empty overlap: missing object trees exit non-zero saying what was
searched, because "0% shared" and "I found no objects" are the same output otherwise.
"""
import collections
import hashlib
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(REPO, "Shipwright", "build-cmake")
# Objects belonging to the shared engine layer are not game code and are excluded on both sides.
SHARED = re.compile(r"libultraship|_deps|/ship/|/fast/", re.I)

FUNC_RE = re.compile(r"^[0-9a-f]+ <(.+)>:$")
INSN_RE = re.compile(r"^\s+[0-9a-f]+:\s+(?:[0-9a-f]{2} )+\s*\t([a-z][a-z0-9.]*)")


def objects(root):
    out = []
    for dirpath, _, files in os.walk(root):
        for f in files:
            if f.endswith(".o"):
                p = os.path.join(dirpath, f)
                if not SHARED.search(p):
                    out.append(p)
    return out


def disassemble(objs, label):
    """symbol -> hash of its mnemonic sequence."""
    sigs = {}
    CHUNK = 200
    for i in range(0, len(objs), CHUNK):
        batch = objs[i:i + CHUNK]
        try:
            out = subprocess.run(["objdump", "-d", "--no-show-raw-insn", *batch],
                                 capture_output=True, text=True, timeout=900).stdout
        except Exception as exc:
            print(f"  objdump failed on a {label} batch: {exc}", file=sys.stderr)
            continue
        cur, seq = None, []
        for line in out.splitlines():
            m = FUNC_RE.match(line)
            if m:
                if cur and seq:
                    sigs.setdefault(cur, hashlib.sha1("\n".join(seq).encode()).hexdigest())
                cur, seq = m.group(1), []
                continue
            if cur is None:
                continue
            # with --no-show-raw-insn the mnemonic is the first token after the address
            parts = line.split("\t")
            if len(parts) >= 2:
                mnem = parts[1].strip().split(" ")[0]
                if mnem:
                    seq.append(mnem)
        if cur and seq:
            sigs.setdefault(cur, hashlib.sha1("\n".join(seq).encode()).hexdigest())
        print(f"  {label}: {i + len(batch)}/{len(objs)} objects, {len(sigs)} functions",
              file=sys.stderr, flush=True)
    return sigs


def main():
    soh_root = os.path.join(BUILD, "soh")
    mm_root = os.path.join(BUILD, "mm")
    for r in (soh_root, mm_root):
        if not os.path.isdir(r):
            sys.exit(f"{r} does not exist -- SEARCHED NOTHING. Build both targets first.")
    soh_objs, mm_objs = objects(soh_root), objects(mm_root)
    if not soh_objs or not mm_objs:
        sys.exit(f"no game-code objects found (soh={len(soh_objs)}, mm={len(mm_objs)}) -- SEARCHED NOTHING")
    print(f"soh objects {len(soh_objs)}, mm objects {len(mm_objs)}", file=sys.stderr)

    soh = disassemble(soh_objs, "soh")
    mm = disassemble(mm_objs, "mm")

    common = sorted(set(soh) & set(mm))
    # C symbols only: the mangled C++ ones are engine glue, not decomp game code.
    common = [s for s in common if not s.startswith("_Z")]
    same = [s for s in common if soh[s] == mm[s]]
    diff = [s for s in common if soh[s] != mm[s]]
    n = len(common)
    if n == 0:
        sys.exit("no colliding C functions were disassembled on BOTH sides -- the comparison did not run")

    print(f"\ncolliding C functions disassembled on both sides: {n}")
    print(f"  SAME code (identical mnemonic sequence): {len(same)} ({100*len(same)/n:.1f}%)")
    print(f"  DIFFERENT code                         : {len(diff)} ({100*len(diff)/n:.1f}%)")
    print("\nNOTE: mnemonic-sequence equality ignores operands, so this remains an UPPER BOUND on")
    print("what is genuinely shareable -- generous by design, since the question is what COULD be.")

    by_prefix = collections.Counter(s.split("_")[0] for s in same)
    print("\nlargest SHARED subsystems (by symbol prefix):")
    for p, c in by_prefix.most_common(12):
        print(f"  {p:<20} {c}")
    by_prefix_d = collections.Counter(s.split("_")[0] for s in diff)
    print("\nlargest DIVERGENT subsystems:")
    for p, c in by_prefix_d.most_common(12):
        print(f"  {p:<20} {c}")


if __name__ == "__main__":
    main()
