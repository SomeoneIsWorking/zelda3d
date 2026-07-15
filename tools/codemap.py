#!/usr/bin/env python3
"""codemap.py — survey a codebase into a tree, scaffold a codemap, and check it
for drift. Dependency-free (Python 3.8+ stdlib). Project-agnostic; run from the
project root.

    codemap.py tree [roots...] [--depth N] [--files] [--min-lines N]
        Print an annotated source TREE: each directory with its recursive
        (lines, files) and per-file lines with --files. This is the tree-like
        structure a codemap is built on. Roots default to auto-detected source
        dirs (src, lib, app, pkg, internal, cmd, ...) else the top-level dirs.

    codemap.py scaffold [roots...] [--status-legend ...]
        Emit a starter docs/codemap.md to stdout: layers blurb + a per-subsystem
        status table (one row per top-level source dir) + the tree in a fenced
        block + a Where-index skeleton. Redirect into docs/codemap.md and edit.

    codemap.py check [--map docs/codemap.md] [roots...]
        Governance: cross-check the map against the tree. Reports (a) source
        subsystems/dirs NOT mentioned in the map (coverage gaps) and (b) file
        paths the map references that no longer exist on disk (stale entries).
        Exit code 1 if any drift is found (usable in CI / a pre-commit check).

Storage-agnostic: the codemap doc is plain Markdown you own; this tool builds
and audits it, it does not lock you into a format.
"""
import argparse
import os
import re
import sys

SKIP_DIRS = {
    ".git", ".hg", ".svn", "build", "dist", "out", "bin", "obj", "target",
    "node_modules", "vendor", "third_party", "thirdparty", "scratch",
    ".venv", "venv", "env", "__pycache__", ".mypy_cache", ".pytest_cache",
    ".tox", ".idea", ".vscode", ".cache", "coverage", ".next", ".claude",
}
CODE_EXT = {
    ".c", ".h", ".cc", ".hh", ".cpp", ".hpp", ".cxx", ".py", ".rs", ".go",
    ".js", ".jsx", ".ts", ".tsx", ".java", ".kt", ".swift", ".rb", ".php",
    ".cs", ".m", ".mm", ".scala", ".lua", ".sh", ".zig", ".ml", ".ex", ".exs",
}
SRC_ROOT_HINTS = ["src", "lib", "app", "apps", "pkg", "internal", "cmd",
                  "source", "core", "packages"]


def _is_code(name):
    return os.path.splitext(name)[1].lower() in CODE_EXT


def _count_lines(path):
    try:
        with open(path, "rb") as f:
            return sum(1 for _ in f)
    except OSError:
        return 0


def _walk(root):
    """Yield (dirpath, code_files[]) for root, pruning SKIP_DIRS. dirpath is
    relative to cwd."""
    for dp, dirs, files in os.walk(root):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS and not d.startswith("."))
        cf = sorted(f for f in files if _is_code(f))
        yield dp, cf


def _dir_stats(root):
    """{dirpath: (lines, nfiles)} aggregated RECURSIVELY, plus per-file lines."""
    per_file = {}
    for dp, files in _walk(root):
        for f in files:
            p = os.path.join(dp, f)
            per_file[p] = _count_lines(p)
    agg = {}
    for p, n in per_file.items():
        d = os.path.dirname(p)
        while True:
            l, c = agg.get(d, (0, 0))
            agg[d] = (l + n, c + 1)
            if os.path.normpath(d) == os.path.normpath(root) or not d:
                break
            nd = os.path.dirname(d)
            if nd == d:
                break
            d = nd
    return agg, per_file


def _auto_roots():
    hints = [d for d in SRC_ROOT_HINTS if os.path.isdir(d)]
    if hints:
        return hints
    return sorted(d for d in os.listdir(".")
                  if os.path.isdir(d) and d not in SKIP_DIRS and not d.startswith("."))


def _exts_in(per_file, dirpath):
    e = {}
    for p in per_file:
        if os.path.normpath(p).startswith(os.path.normpath(dirpath) + os.sep):
            e[os.path.splitext(p)[1]] = e.get(os.path.splitext(p)[1], 0) + 1
    return " ".join(k for k, _ in sorted(e.items(), key=lambda x: -x[1])[:4])


def cmd_tree(args):
    roots = args.roots or _auto_roots()
    if not roots:
        print("(no source roots found)")
        return
    total_l = total_f = 0
    for root in roots:
        if not os.path.isdir(root):
            print(f"{root}  (not a directory)")
            continue
        agg, per_file = _dir_stats(root)
        rl, rf = agg.get(root, (0, 0))
        total_l += rl
        total_f += rf
        print(f"{root}/  —  {rl:,} lines, {rf} files")
        # print subdirs as a tree up to depth
        subdirs = sorted(d for d in agg if d != root
                         and os.path.dirname(d) and _depth(d, root) <= args.depth)
        subdirs.sort()
        for d in subdirs:
            depth = _depth(d, root)
            l, c = agg[d]
            if l < args.min_lines:
                continue
            name = os.path.basename(d)
            exts = _exts_in(per_file, d)
            print(f"{'│  ' * (depth - 1)}├─ {name}/  {l:,} lines  {c} files"
                  + (f"  [{exts}]" if exts and depth == 1 else ""))
            if args.files:
                for p in sorted(per_file):
                    if os.path.dirname(p) == d:
                        print(f"{'│  ' * depth}   {os.path.basename(p)}  {per_file[p]:,}")
    print(f"\nTOTAL: {total_l:,} lines across {total_f} files in {len(roots)} root(s)")


def _depth(dirpath, root):
    rel = os.path.relpath(dirpath, root)
    return 0 if rel == "." else rel.count(os.sep) + 1


def cmd_scaffold(args):
    roots = args.roots or _auto_roots()
    print("# Codemap\n")
    print("The single-page answer to **what is where, what's done, what's missing.**")
    print("Update the relevant row in the SAME commit that changes a subsystem.\n")
    print("Legend: ✅ done (verified on real data) · 🟡 partial (documented gap) · "
          "🔬 built/parsed but not wired · ⬜ missing.\n")
    print("## Subsystems\n")
    print("| Subsystem | Status | Where | Gap / next |")
    print("|---|---|---|---|")
    for root in roots:
        if not os.path.isdir(root):
            continue
        agg, per_file = _dir_stats(root)
        subs = sorted(d for d in agg if _depth(d, root) == 1)
        if not subs:
            l, _ = agg.get(root, (0, 0))
            print(f"| {root} | ⬜ | `{root}/` | _{l:,} lines — describe_ |")
        for d in subs:
            l, c = agg[d]
            print(f"| {os.path.basename(d)} | ⬜ | `{d}/` | _{l:,} lines, {c} files — describe_ |")
    print("\n## Source tree\n\n```")
    # reuse tree rendering
    ns = argparse.Namespace(roots=roots, depth=2, files=False, min_lines=0)
    _capture(cmd_tree, ns)
    print("```\n")
    print("## Where is X? (quick index)\n")
    print("- **<capability>** → `<file>:<entry function>`  _(add the lookups you do most)_")


def _capture(fn, ns):
    import io
    buf = io.StringIO()
    old = sys.stdout
    sys.stdout = buf
    try:
        fn(ns)
    finally:
        sys.stdout = old
    sys.stdout.write(buf.getvalue())


# paths referenced in the map: backticked `foo/bar.ext` or `foo/bar/`
_PATH_RE = re.compile(r"`([./A-Za-z0-9_\-]+\.[A-Za-z0-9]+|[./A-Za-z0-9_\-]+/)`")


def cmd_check(args):
    if not os.path.isfile(args.map):
        sys.exit(f"no codemap at {args.map} (run: codemap.py scaffold > {args.map})")
    with open(args.map, encoding="utf-8") as f:
        text = f.read()
    referenced = set()
    for m in _PATH_RE.finditer(text):
        referenced.add(m.group(1).rstrip("/"))
    roots = args.roots or _auto_roots()
    # (a) coverage: top-level subsystem dirs not mentioned anywhere in the map
    gaps = []
    for root in roots:
        if not os.path.isdir(root):
            continue
        agg, _ = _dir_stats(root)
        subs = [d for d in agg if _depth(d, root) == 1] or [root]
        for d in subs:
            nd = os.path.normpath(d)
            if not any(nd == os.path.normpath(r) or r.startswith(nd) for r in referenced):
                gaps.append(d)
    # (b) stale: referenced PATHS (multi-segment, containing '/') that no longer
    # exist. Bare filenames (e.g. `interpreter.cpp` cited in prose without a
    # directory) are NOT path assertions, so they're not checked -- only things
    # written as real relative paths are.
    stale = []
    for r in sorted(referenced):
        if "/" in r and not os.path.exists(r):
            stale.append(r)
    if gaps:
        print("COVERAGE GAPS — source subsystems not mentioned in the codemap:")
        for g in sorted(gaps):
            print(f"  ⬜ {g}/")
    if stale:
        print("STALE — paths the codemap references that don't exist on disk:")
        for s in stale:
            print(f"  ❌ {s}")
    if not gaps and not stale:
        print("codemap OK: every source subsystem is mentioned and no referenced path is missing.")
        return
    sys.exit(1)


def main(argv=None):
    p = argparse.ArgumentParser(description="survey / scaffold / check a codebase codemap")
    sub = p.add_subparsers(dest="cmd", required=True)

    t = sub.add_parser("tree", help="print an annotated source tree")
    t.add_argument("roots", nargs="*")
    t.add_argument("--depth", type=int, default=2)
    t.add_argument("--files", action="store_true", help="list files under each dir")
    t.add_argument("--min-lines", type=int, default=0, help="hide dirs below this line count")
    t.set_defaults(func=cmd_tree)

    s = sub.add_parser("scaffold", help="emit a starter codemap.md")
    s.add_argument("roots", nargs="*")
    s.set_defaults(func=cmd_scaffold)

    c = sub.add_parser("check", help="report coverage gaps + stale paths vs the tree")
    c.add_argument("--map", default="docs/codemap.md")
    c.add_argument("roots", nargs="*")
    c.set_defaults(func=cmd_check)

    args = p.parse_args(argv)
    args.func(args)


if __name__ == "__main__":
    main()
