#!/usr/bin/env python3
"""Report STB_GNU_UNIQUE symbols that two game cores share -- the one class RTLD_LOCAL cannot privatise.

The launcher dlopen's each core with RTLD_LOCAL so OoT's and MM's colliding decomp symbols stay
private. That holds for every ordinary symbol and not at all for STB_GNU_UNIQUE ones: their defining
property is that the dynamic linker binds them to a SINGLE instance process-wide, whatever the scope.
GCC gives that binding to function-local statics inside inline functions -- which is precisely how
both games write their header-defined registries.

It is not hypothetical. `MenuInit::GetInitFuncs()` holds a static vector of menu init callbacks in
BOTH games under the same global-namespace name, so with MM loaded first, OoT's menu setup iterated
MM's list and jumped into libmm_core.so. The cores are now built -fno-gnu-unique, and this script is
what says whether that is still true -- a build-system flag is exactly the kind of thing a refactor
drops without noticing.

    tools/unique_symbol_collisions.py [--build DIR] [--all]

Exit status is the point: 0 when no core-owned unique symbol is shared, 1 when any is. Library
statics (std::, nlohmann::, __gnu_cxx::) are listed under --all but never fail the check -- they are
read-only tables and RTTI tags whose sharing is harmless, and failing on them would make the check
noise that gets ignored. A run where a core binary is MISSING is a hard error, not "no collisions":
a check that passes because it looked at nothing is worse than no check.
"""

import argparse
import pathlib
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

# (label, path relative to the build dir)
CORES = [
    ("soh", "soh/libsoh_core.so"),
    ("mm", "mm/libmm_core.so"),
]

# Namespaces whose statics are library-internal: read-only tables, cached powers, RTTI tags. Sharing
# them across cores is harmless, so they are reported but never fail.
LIBRARY_PREFIXES = ("std::", "__gnu_cxx::", "nlohmann::", "Rml::", "ImGui", "pfd::")


def unique_symbols(path: pathlib.Path) -> dict[str, str]:
    """Mangled -> demangled, for every STB_GNU_UNIQUE ('u') dynamic symbol defined by `path`."""
    out = subprocess.run(
        ["nm", "-D", "--defined-only", str(path)], capture_output=True, text=True, check=True
    ).stdout
    mangled = [line.split()[2] for line in out.splitlines() if len(line.split()) == 3 and line.split()[1] == "u"]
    if not mangled:
        return {}
    demangled = subprocess.run(
        ["c++filt"], input="\n".join(mangled), capture_output=True, text=True, check=True
    ).stdout.splitlines()
    return dict(zip(mangled, demangled))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--build", default=str(REPO / "Shipwright" / "build-cmake"), help="build directory")
    ap.add_argument("--all", action="store_true", help="also list library-internal shared statics")
    args = ap.parse_args()

    build = pathlib.Path(args.build)
    tables = {}
    for label, rel in CORES:
        path = build / rel
        if not path.exists():
            print(f"ERROR: {label} core not built at {path} -- this check inspected NOTHING.", file=sys.stderr)
            print("Build targets soh and mm first; a missing binary is not a clean result.", file=sys.stderr)
            return 2
        tables[label] = unique_symbols(path)
        print(f"{label:4s} {path.name}: {len(tables[label])} STB_GNU_UNIQUE symbols")

    (a_label, a), (b_label, b) = tables.items()
    shared = sorted(set(a) & set(b))
    core_owned = [s for s in shared if not a[s].lstrip("_").startswith(LIBRARY_PREFIXES)
                  and not any(p in a[s] for p in LIBRARY_PREFIXES)]
    library = [s for s in shared if s not in core_owned]

    print(f"\nshared between {a_label} and {b_label}: {len(shared)} "
          f"({len(core_owned)} core-owned, {len(library)} library-internal)")

    if args.all and library:
        print("\nlibrary-internal (shared, but harmless -- read-only tables and RTTI tags):")
        for s in library:
            print(f"  {a[s]}")

    if core_owned:
        print("\nCORE-OWNED unique symbols shared across both games -- each is one game's state that the"
              "\nother will silently use. This is what -fno-gnu-unique exists to prevent:")
        for s in core_owned:
            print(f"  {a[s]}\n    {s}")
        return 1

    print("\nOK: no core-owned unique symbol is shared. Both cores keep their own registries.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
