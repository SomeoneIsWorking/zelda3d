#!/usr/bin/env python3
"""codequery — answer structured questions about this codebase without paging through it.

WHY THIS EXISTS. Reading source is the single largest consumer of an agent session's context here,
and most of it is waste: `sed -n '4020,4060p' z_parameter.c` guesses at a range, usually overshoots,
and gets re-issued three times because the function turned out to start earlier. z_parameter.c is
7,200 lines; the HUD arc paged through it about fifteen times. Every one of those reads could have
been "give me Interface_DrawItemButtons" or "where is gClockIconTex drawn".

So: ask for a SYMBOL, get exactly that symbol. Ask for callers, get one line each. The point is not
speed, it is that what comes back is bounded by the answer rather than by how well you guessed the
line numbers.

    tools/codequery.py outline <file>              function/struct index with line ranges
    tools/codequery.py slice  <file> <symbol>      just that function's body
    tools/codequery.py def    <symbol> [path...]   where it is DEFINED (not every mention)
    tools/codequery.py callers <symbol> [path...]  call sites, one compact line each
    tools/codequery.py find   <regex> [path...]    matches with no surrounding context

Defaults to the C/C++ sources under Shipwright/ + 2ship/ when no path is given. `--body` on `def`
prints the definition body too; `-n N` caps results (default 60) so a wide query degrades into a
truncated list rather than a context dump.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_ROOTS = ["Shipwright/soh/src", "Shipwright/soh/soh", "Shipwright/libultraship/src",
                 "Shipwright/libultraship/include", "Shipwright/cmb3d", "2ship/2s2h", "2ship/src"]
CODE_EXT = {".c", ".cpp", ".h", ".hpp", ".inc"}

# A definition looks like `type name(args) {` at column 0-ish, possibly with qualifiers. Deliberately
# loose: this is a locator, not a parser — it points you at a line so you can slice it.
DEF_RE = r'^[A-Za-z_][A-Za-z0-9_ \t\*&:<>,"]*\b{sym}\s*\('


def roots(paths: list[str]) -> list[str]:
    return [str(REPO / p) for p in (paths or DEFAULT_ROOTS) if (REPO / p).exists()]


def rg(pattern: str, paths: list[str], extra: list[str] | None = None) -> list[str]:
    """Prefer ripgrep; fall back to grep -rn so this works on a bare machine."""
    exts = [a for e in sorted(CODE_EXT) for a in ("-g", f"*{e}")]
    for cmd in ([["rg", "--no-heading", "-n", *exts, *(extra or []), pattern, *paths]],
                [["grep", "-rn", "-E", *(extra or []), pattern, *paths]]):
        try:
            out = subprocess.run(cmd[0], capture_output=True, text=True, timeout=120)
        except FileNotFoundError:
            continue
        if out.returncode in (0, 1):
            return [ln for ln in out.stdout.splitlines() if ln.strip()]
    return []


def rel(line: str) -> str:
    """Strip the repo prefix so output is short and pasteable as file:line."""
    return line[len(str(REPO)) + 1:] if line.startswith(str(REPO)) else line


def brace_range(lines: list[str], start: int) -> int:
    """End line (0-based, inclusive) of the brace block opening at/after `start`."""
    depth, seen = 0, False
    for i in range(start, len(lines)):
        for ch in lines[i]:
            if ch == "{":
                depth += 1
                seen = True
            elif ch == "}":
                depth -= 1
                if seen and depth == 0:
                    return i
        if seen and depth <= 0 and i > start:
            return i
    return min(start + 400, len(lines) - 1)


def cmd_outline(args) -> int:
    path = REPO / args.file if not Path(args.file).is_absolute() else Path(args.file)
    lines = path.read_text(errors="replace").splitlines()
    pat = re.compile(r'^([A-Za-z_][A-Za-z0-9_ \t\*&:<>,"]*?)\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;]*$')
    hits = 0
    for i, ln in enumerate(lines):
        if ln.startswith((" ", "\t", "#", "//", "*", "}")) or "=" in ln.split("(")[0]:
            continue
        m = pat.match(ln)
        if not m or m.group(2) in ("if", "for", "while", "switch", "return", "sizeof"):
            continue
        end = brace_range(lines, i)
        print(f"{i + 1:6d}..{end + 1:<6d} {m.group(2)}")
        hits += 1
        if hits >= args.max:
            print(f"... (capped at {args.max}; raise with -n)")
            break
    if not hits:
        print("no function-like definitions found (wrong file, or an unusual style)")
    return 0


def cmd_slice(args) -> int:
    path = REPO / args.file if not Path(args.file).is_absolute() else Path(args.file)
    lines = path.read_text(errors="replace").splitlines()
    pat = re.compile(DEF_RE.format(sym=re.escape(args.symbol)))
    for i, ln in enumerate(lines):
        if pat.match(ln) and not ln.rstrip().endswith(";"):
            end = brace_range(lines, i)
            if end - i > args.max:
                end = i + args.max
                trunc = True
            else:
                trunc = False
            for j in range(i, end + 1):
                print(f"{j + 1:6d}  {lines[j]}")
            if trunc:
                print(f"... (truncated at {args.max} lines; raise with -n)")
            return 0
    print(f"no definition of {args.symbol} in {args.file} "
          f"(it may be a macro, or declared elsewhere — try `def`)")
    return 1


def cmd_def(args) -> int:
    hits = [rel(h) for h in rg(DEF_RE.format(sym=re.escape(args.symbol)), roots(args.paths))]
    hits = [h for h in hits if not h.split(":", 2)[-1].rstrip().endswith(";")]
    if not hits:
        print(f"no definition found for {args.symbol} (try `find` — it may be a macro or a table entry)")
        return 1
    for h in hits[:args.max]:
        print(h)
    if args.body and hits:
        f, ln, _ = hits[0].split(":", 2)
        print()
        sub = argparse.Namespace(file=f, symbol=args.symbol, max=args.max)
        cmd_slice(sub)
    return 0


def cmd_callers(args) -> int:
    sym = re.escape(args.symbol)
    hits = [rel(h) for h in rg(rf'\b{sym}\s*\(', roots(args.paths))]
    out = []
    for h in hits:
        try:
            f, ln, body = h.split(":", 2)
        except ValueError:
            continue
        st = body.strip()
        if st.startswith(("//", "*", "/*")) or st.endswith(";") and "(" not in st.split(sym)[0]:
            pass
        if re.match(DEF_RE.format(sym=sym), st):  # the definition itself, not a call
            continue
        out.append(f"{f}:{ln}  {st[:110]}")
    for line in out[:args.max]:
        print(line)
    if len(out) > args.max:
        print(f"... {len(out) - args.max} more (raise with -n)")
    if not out:
        print(f"no call sites found for {args.symbol}")
    return 0


def cmd_find(args) -> int:
    hits = [rel(h) for h in rg(args.regex, roots(args.paths))]
    for h in hits[:args.max]:
        f, _, rest = h.partition(":")
        ln, _, body = rest.partition(":")
        print(f"{f}:{ln}  {body.strip()[:110]}")
    if len(hits) > args.max:
        print(f"... {len(hits) - args.max} more (raise with -n)")
    if not hits:
        print("no matches")
    return 0


def main(argv) -> int:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0],
                                formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog="\n".join(__doc__.splitlines()[1:]))
    # -n is accepted on BOTH sides of the subcommand: `codequery -n 8 outline f` and
    # `codequery outline f -n 8` both read naturally, and having only one work is a papercut that
    # costs a retry every time.
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("-n", "--max", type=int, default=60, help="cap results/lines (default 60)")
    p.add_argument("-n", "--max", type=int, default=60, help=argparse.SUPPRESS)
    sub = p.add_subparsers(dest="cmd", required=True, parser_class=lambda **kw: argparse.ArgumentParser(parents=[common], **kw))

    o = sub.add_parser("outline", help="function index with line ranges")
    o.add_argument("file")
    o.set_defaults(func=cmd_outline)

    s = sub.add_parser("slice", help="print one function body")
    s.add_argument("file")
    s.add_argument("symbol")
    s.set_defaults(func=cmd_slice)

    d = sub.add_parser("def", help="where a symbol is DEFINED")
    d.add_argument("symbol")
    d.add_argument("paths", nargs="*")
    d.add_argument("--body", action="store_true", help="also print the definition body")
    d.set_defaults(func=cmd_def)

    c = sub.add_parser("callers", help="call sites, one compact line each")
    c.add_argument("symbol")
    c.add_argument("paths", nargs="*")
    c.set_defaults(func=cmd_callers)

    f = sub.add_parser("find", help="regex matches, no surrounding context")
    f.add_argument("regex")
    f.add_argument("paths", nargs="*")
    f.set_defaults(func=cmd_find)

    args = p.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
