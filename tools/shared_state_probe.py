#!/usr/bin/env python3
"""Which libultraship DATA do BOTH game cores touch? -- C050's falsifier, measured.

    tools/shared_state_probe.py

C050 says one binary running both OoT and MM is a linking problem that RTLD_LOCAL solves: two .so
files may each define Play_Init because neither exports it globally. Its stated falsifier is DATA
that must genuinely be SHARED across the dlopen boundary -- a single libultraship-owned global
written by both cores -- which RTLD_LOCAL does NOT resolve, because RTLD_LOCAL's whole job is to
give each .so its own private view.

This measures that set. It asks: of the DATA objects libultraship defines, how many are referenced
(undefined-and-needed) by BOTH game cores? Those are exactly the globals that must resolve to ONE
instance no matter how the cores are loaded -- which means libultraship cannot be statically linked
into each core .so, it has to be a shared library both link against.

A function shared by both cores is harmless: two copies of the same code behave identically. A
DATA object shared by both is not: two copies means two windows, two renderers, two resource
managers, and the second game silently talks to the wrong one. So the split below is the point.

REFUSES rather than reporting an empty set: it takes its inputs from `ninja -t inputs`, and exits
non-zero if the build is missing, if either side has no objects, or if libultraship defines no data
at all -- because "0 shared globals" and "I never looked" must not print the same thing.
"""
import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(REPO, "Shipwright", "build-cmake")

# nm type letters. Uppercase = global (external linkage); lowercase = local, invisible across the
# dlopen boundary and therefore irrelevant here.
DATA_TYPES = set("DBGRS")   # initialised, bss, small-object, read-only, other data
FUNC_TYPES = set("TWi")     # text, weak-text, indirect


def ninja_objects(target, keep_prefix=None, drop_prefix=None):
    try:
        r = subprocess.run(["ninja", "-t", "inputs", target], cwd=BUILD,
                           capture_output=True, text=True, timeout=300)
    except FileNotFoundError:
        sys.exit("ninja not found -- cannot determine what is actually linked. REFUSING to guess.")
    if r.returncode != 0:
        sys.exit(f"`ninja -t inputs {target}` failed in {BUILD}: {r.stderr.strip()}")
    objs = [ln.strip() for ln in r.stdout.splitlines() if ln.strip().endswith(".o")]
    if keep_prefix:
        objs = [o for o in objs if o.startswith(keep_prefix)]
    if drop_prefix:
        objs = [o for o in objs if not any(o.startswith(d) for d in drop_prefix)]
    return [os.path.join(BUILD, o) for o in objs]


def nm_symbols(objs, label):
    """-> (defined: {name: type}, undefined: {names})"""
    defined, undefined = {}, set()
    CHUNK = 300
    for i in range(0, len(objs), CHUNK):
        r = subprocess.run(["nm", *objs[i:i + CHUNK]], capture_output=True, text=True, timeout=600)
        for line in r.stdout.splitlines():
            parts = line.split()
            if len(parts) == 2 and parts[0] == "U":
                undefined.add(parts[1])
            elif len(parts) == 3:
                _, t, name = parts
                if t.isupper():
                    defined[name] = t
    print(f"  {label}: {len(objs)} objects -> {len(defined)} global definitions, "
          f"{len(undefined)} undefined references", file=sys.stderr)
    return defined, undefined


def main():
    if not os.path.isfile(os.path.join(BUILD, "build.ninja")):
        sys.exit(f"{BUILD}/build.ninja missing -- SEARCHED NOTHING. Configure and build first.")

    # There is now ONE program binary (zelda3d_app, the launcher) that dlopens either game's code as
    # a separate shared object -- soh_core (libsoh_core.so) / mm_core (libmm_core.so). Those cores,
    # not soh.elf/mm.elf (which no longer exist as build targets), are where each game's code
    # actually lives, so they are what this probe must compare.
    lus = ninja_objects("soh_core", keep_prefix="libultraship/")
    if not lus:
        sys.exit("no libultraship objects among soh_core's inputs -- SEARCHED NOTHING.")
    soh = ninja_objects("soh_core", keep_prefix="soh/")
    mm = ninja_objects("mm_core", keep_prefix="mm/")
    if not soh or not mm:
        sys.exit(f"missing game-core objects (soh={len(soh)}, mm={len(mm)}) -- SEARCHED NOTHING.")

    lus_def, _ = nm_symbols(lus, "libultraship")
    _, soh_u = nm_symbols(soh, "soh core")
    _, mm_u = nm_symbols(mm, "mm core")

    lus_data = {n: t for n, t in lus_def.items() if t in DATA_TYPES}
    lus_func = {n: t for n, t in lus_def.items() if t in FUNC_TYPES}
    if not lus_data:
        sys.exit("libultraship defines NO global data objects at all -- that is implausible and "
                 "means this probe is looking at the wrong symbols. REFUSING to report a clean bill.")

    both = soh_u & mm_u
    shared_data = sorted(n for n in both if n in lus_data)
    shared_func = sorted(n for n in both if n in lus_func)

    print(f"\nlibultraship global definitions: {len(lus_def)} "
          f"({len(lus_data)} DATA, {len(lus_func)} FUNC)")
    print(f"referenced by BOTH cores: {len(shared_data)} DATA, {len(shared_func)} FUNC")
    print(f"\n  -> {len(shared_func)} shared FUNCTIONS are harmless: duplicate code behaves the same.")
    print(f"  -> {len(shared_data)} shared DATA objects are NOT: each duplicate is a second window /")
    print( "     renderer / resource manager that one of the two games would silently talk to.")

    if shared_data:
        print(f"\nthe {len(shared_data)} cross-boundary DATA objects "
              f"(these decide whether libultraship can be static in each core):")
        for n in shared_data[:60]:
            print(f"  {lus_data[n]}  {n}")
        if len(shared_data) > 60:
            print(f"  ... and {len(shared_data) - 60} more")
    else:
        print("\nNo libultraship DATA is referenced by both cores. Note the blind spot before "
              "trusting this: state reached only through a FUNCTION (a Get/Instance accessor over a "
              "file-static) is invisible here, and that is how most singletons in this codebase are "
              "actually written. A zero here does NOT mean there is no shared state.")

    report_upcalls()


def report_upcalls():
    """Which GAME symbols does libultraship itself need? The RTLD_LOCAL blocker list.

    This is the direction that actually blocks one-binary-both-games, and it is the opposite of
    everything above. A core dlopen'd with RTLD_LOCAL is INVISIBLE to the rest of the process by
    definition -- that is the property that lets two cores each define Play_Init. So any symbol
    libultraship names and expects the game to define cannot be resolved that way, no matter how the
    cores are built. Each one has to become a registration (the core hands libultraship a pointer at
    init) rather than a link-time name.
    """
    lus_so = os.path.join(BUILD, "libultraship", "src", "libultraship.so")
    if not os.path.isfile(lus_so):
        print("\n[upcalls] libultraship.so not found -- SKIPPED, and this section reported NOTHING. "
              "It only works once libultraship is built SHARED.")
        return

    def dynsyms(path, undefined):
        flag = "--undefined-only" if undefined else "--defined-only"
        r = subprocess.run(["nm", "-D", flag, path], capture_output=True, text=True, timeout=120)
        out = set()
        for line in r.stdout.splitlines():
            p = line.split()
            if undefined and len(p) >= 2:
                out.add(p[-1])
            elif not undefined and len(p) == 3 and p[1] in "TWDBRV":
                out.add(p[2])
        return out

    need = dynsyms(lus_so, True)
    # Versioned glibc/libstdc++ imports are the C runtime, not game code; they carry an @ tag.
    need = {s for s in need if "@" not in s}
    if not need:
        sys.exit("libultraship.so reports NO undefined symbols -- implausible, this probe is broken.")

    print(f"\n=== upcalls: symbols libultraship NEEDS FROM the game (the RTLD_LOCAL blocker list) ===")
    print(f"libultraship.so undefined symbols, C runtime excluded: {len(need)}")
    found_any = False
    for label, core_so in (("soh", "soh/libsoh_core.so"), ("mm", "mm/libmm_core.so")):
        p = os.path.join(BUILD, core_so)
        if not os.path.isfile(p):
            print(f"  {label}: {p} MISSING -- this side was NOT checked.")
            continue
        hits = sorted(need & dynsyms(p, False))
        found_any = found_any or bool(hits)
        print(f"  {label}: {len(hits)} of them are defined by the game core"
              + (":" if hits else " -- nothing to unpick on this side."))
        for h in hits:
            print(f"      {h}")
    if not found_any:
        print("\n  None. Nothing in libultraship names a game symbol, so RTLD_LOCAL cores have no")
        print("  upcall problem. (Blind spot: this sees LINK-TIME names only. A dlsym() by string")
        print("  literal at runtime would not appear here and would fail exactly the same way.)")


if __name__ == "__main__":
    main()
