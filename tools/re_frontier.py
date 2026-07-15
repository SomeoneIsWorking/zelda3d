#!/usr/bin/env python3
"""re_frontier.py — the RE-frontier progress tracker for OpenBL2.

The codemap (docs/codemap.md) answers "what subsystem is where + coarse status".
The issue-catalog answers "did we hit this symptom before". Neither answers the
question this project keeps tripping on: **for the ordered chain of RE steps
toward a faithful behaviour (the main menu first), which step is real
reverse-engineering vs a render/behaviour HACK that jumped ahead of the RE?**

This tool tracks exactly that. It operates over a greppable markdown roadmap
(docs/re-frontier.md) — one entry per RE step, each with a status, its
dependencies (the RE that must land first), where the ground-truth evidence
lives, and the honest gap. It is zero-dependency (stdlib only).

Statuses (the core axis — real RE vs jumped-ahead hack):
  re-verified   RE'd from ground truth (exe / cooked data) + implemented + VERIFIED on real data
  re-partial    real RE, but a documented honest gap remains
  in-progress   actively being RE'd/implemented, not yet verified
  hack          a shortcut standing in for absent RE -- DEBT. Must be removed and
                replaced with the real mechanism (no-hacks / no-fallbacks hard rule).
  blocked       cannot start: a dependency's RE isn't done (usually COMPUTED, not stored)
  todo          not started
  skip-by-design deliberately not implemented (e.g. Bink startup movies)

Commands:
  list [--area A] [--status S]   table of entries
  show <id>                      full entry
  next [--area A]                steps ready to work (all deps satisfied) + hacks to replace
  hacks                          every hack entry -- the debt list (no-hacks rule)
  blocked                        steps whose deps' RE isn't done yet
  tree [--area A]                dependency tree per area
  stats                          counts by status
  check                          integrity: unknown deps, cycles, missing fields; exit 1 on drift
  add <id> --title T --area A [--status S] [--deps a,b] [--evidence E] [--where W] [--gap G] [--notes N]
  set <id> field=value ...       update fields (status/deps/evidence/where/gap/notes/title/area)
"""
import argparse
import os
import re
import sys

# The roadmap lives at <repo>/docs/re-frontier.md by default (this file at
# <repo>/tools/re_frontier.py). Override with $RE_FRONTIER_ROADMAP so the same
# generic tool can run in-place from a global skill dir against any project.
ROADMAP = os.environ.get(
    "RE_FRONTIER_ROADMAP",
    os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                 "docs", "re-frontier.md"))

STATUS_EMOJI = {
    "re-verified": "✅",
    "re-partial": "🟡",
    "in-progress": "🔬",
    "hack": "⛔",
    "todo": "⬜",
    "skip-by-design": "➖",
    "blocked": "⏸",  # computed
}
# Statuses that count as "the RE this step depends on is done enough to build on".
SATISFIED = {"re-verified", "re-partial", "skip-by-design"}
FIELDS = ["status", "area", "deps", "evidence", "where", "gap", "notes"]
VALID_STATUS = set(STATUS_EMOJI) - {"blocked"}

HEADER = """# RE Frontier — the ordered RE dependency chain toward a faithful BL2

Tracked by `tools/re_frontier.py` (consult it FIRST; update it in the SAME commit
that changes a step). This is the fine-grained companion to `docs/codemap.md`:
the codemap says *what subsystem exists*, this says *which ordered RE step is
real reverse-engineering vs a hack that jumped ahead*.

**Hard rule (no hacks / no fallbacks):** a `⛔ hack` status is DEBT, never an
acceptable resting state. It marks a shortcut standing in for absent RE and MUST
be removed as its real mechanism lands. `re_frontier.py hacks` is the debt list;
`re_frontier.py next` tells you the next RE-ready step.

**`re-verified` MEANS FAITHFUL to the real target — not "the mechanism runs."** A
step is `re-verified` only when its OUTPUT matches the real game/binary (look /
sound / behavior) on real data. An internal trace ("bytecode reached the call
site", "N rows attached") is a mechanism check, NOT faithfulness — if it runs but
the result doesn't match the real target, it is `re-partial` with the
faithfulness gap named. The user observes the running system; that observation
overrides any internal trace.

**Fail fast & loud:** a failure must surface loudly, never silently fall back —
unless the fallback IS intended behavior of the real target being reproduced.

Statuses: ✅ re-verified · 🟡 re-partial (honest gap) · 🔬 in-progress ·
⛔ hack (debt, must remove) · ⬜ todo · ➖ skip-by-design · ⏸ blocked (computed).

<!-- Machine-edited by tools/re_frontier.py add/set. Format: `## <area>` sections;
     each entry is `### <id> — <title>` followed by `- <field>: <value>` lines. -->
"""


class Entry:
    def __init__(self, eid, title, area):
        self.id = eid
        self.title = title
        self.area = area
        self.status = "todo"
        self.deps = []
        self.evidence = ""
        self.where = ""
        self.gap = ""
        self.notes = ""

    def serialize(self):
        out = [f"### {self.id} — {self.title}"]
        out.append(f"- status: {self.status}")
        out.append(f"- deps: {', '.join(self.deps)}")
        for f in ("evidence", "where", "gap", "notes"):
            out.append(f"- {f}: {getattr(self, f)}")
        return "\n".join(out)


def load():
    """Parse docs/re-frontier.md into {id: Entry}, preserving area order."""
    if not os.path.exists(ROADMAP):
        return {}, []
    entries = {}
    order = []
    area = "misc"
    cur = None
    with open(ROADMAP, encoding="utf-8") as fh:
        for line in fh:
            line = line.rstrip("\n")
            m = re.match(r"^## +(.+)$", line)
            if m and not line.startswith("### "):
                area = m.group(1).strip()
                continue
            m = re.match(r"^### +(\S+) +— +(.+)$", line)
            if not m:
                m = re.match(r"^### +(\S+) +- +(.+)$", line)
            if m:
                cur = Entry(m.group(1).strip(), m.group(2).strip(), area)
                entries[cur.id] = cur
                order.append(cur.id)
                continue
            m = re.match(r"^- +(\w+): ?(.*)$", line)
            if m and cur:
                key, val = m.group(1), m.group(2).strip()
                if key == "deps":
                    cur.deps = [d.strip() for d in val.split(",") if d.strip()]
                elif key in ("status", "evidence", "where", "gap", "notes"):
                    setattr(cur, key, val)
    return entries, order


def save(entries, order):
    areas = []
    for eid in order:
        a = entries[eid].area
        if a not in areas:
            areas.append(a)
    with open(ROADMAP, "w", encoding="utf-8") as fh:
        fh.write(HEADER)
        for a in areas:
            fh.write(f"\n## {a}\n\n")
            for eid in order:
                if entries[eid].area == a:
                    fh.write(entries[eid].serialize() + "\n\n")


def effective_status(e, entries):
    """A todo/in-progress step whose deps aren't all satisfied is BLOCKED."""
    if e.status in ("todo", "in-progress"):
        for d in e.deps:
            dep = entries.get(d)
            if dep is None or dep.status not in SATISFIED:
                return "blocked"
    return e.status


def emoji(status):
    return STATUS_EMOJI.get(status, "?")


def cmd_list(entries, order, args):
    for eid in order:
        e = entries[eid]
        if args.area and e.area != args.area:
            continue
        eff = effective_status(e, entries)
        if args.status and eff != args.status and e.status != args.status:
            continue
        print(f"{emoji(eff)} {e.status:<14} {eid:<34} {e.title}")


def cmd_show(entries, order, args):
    e = entries.get(args.id)
    if not e:
        print(f"no such entry: {args.id}", file=sys.stderr)
        return 1
    eff = effective_status(e, entries)
    print(f"### {e.id} — {e.title}")
    print(f"  area:     {e.area}")
    print(f"  status:   {emoji(e.status)} {e.status}" +
          (f"  (effective: {emoji(eff)} {eff})" if eff != e.status else ""))
    print(f"  deps:     {', '.join(e.deps) or '—'}")
    for d in e.deps:
        dep = entries.get(d)
        tag = f"{emoji(dep.status)} {dep.status}" if dep else "‼ UNKNOWN"
        print(f"              {d}: {tag}")
    print(f"  evidence: {e.evidence or '—'}")
    print(f"  where:    {e.where or '—'}")
    print(f"  gap:      {e.gap or '—'}")
    print(f"  notes:    {e.notes or '—'}")
    return 0


def cmd_next(entries, order, args):
    ready = []
    for eid in order:
        e = entries[eid]
        if args.area and e.area != args.area:
            continue
        if e.status in ("todo", "in-progress") and effective_status(e, entries) != "blocked":
            ready.append(e)
    print("== RE-ready steps (all deps satisfied) ==")
    if not ready:
        print("  (none — every unblocked step is done, or blocked on upstream RE)")
    for e in ready:
        print(f"  {emoji(e.status)} {e.id:<34} {e.title}")
        if e.gap:
            print(f"      gap: {e.gap}")
    hacks = [entries[i] for i in order if entries[i].status == "hack"
             and (not args.area or entries[i].area == args.area)]
    if hacks:
        print("\n== ⛔ hacks to REPLACE with real RE (no-hacks rule) ==")
        for e in hacks:
            print(f"  {e.id:<34} {e.title}")
            if e.gap:
                print(f"      real mechanism: {e.gap}")


def cmd_hacks(entries, order, args):
    hacks = [entries[i] for i in order if entries[i].status == "hack"]
    if not hacks:
        print("No hacks tracked. (Good — no-hacks rule holds.)")
        return 0
    print(f"⛔ {len(hacks)} hack(s) — DEBT standing in for real RE, must be removed:\n")
    for e in hacks:
        print(f"  {e.id:<34} [{e.area}] {e.title}")
        if e.where:
            print(f"      where: {e.where}")
        if e.gap:
            print(f"      real mechanism: {e.gap}")
    return 0


def cmd_blocked(entries, order, args):
    for eid in order:
        e = entries[eid]
        if effective_status(e, entries) == "blocked":
            unmet = [d for d in e.deps
                     if d not in entries or entries[d].status not in SATISFIED]
            print(f"⏸ {eid:<34} {e.title}")
            print(f"      waiting on: {', '.join(unmet)}")


def cmd_tree(entries, order, args):
    children = {eid: [] for eid in order}
    roots = []
    for eid in order:
        deps = [d for d in entries[eid].deps if d in entries]
        if not deps:
            roots.append(eid)
        for d in deps:
            children[d].append(eid)

    printed = set()

    def walk(eid, depth):
        if args.area and entries[eid].area != args.area:
            return
        e = entries[eid]
        eff = effective_status(e, entries)
        mark = " (seen)" if eid in printed else ""
        print(f"{'  ' * depth}{emoji(eff)} {eid}{mark}")
        if eid in printed:
            return
        printed.add(eid)
        for c in children[eid]:
            walk(c, depth + 1)

    for r in roots:
        walk(r, 0)


def cmd_stats(entries, order, args):
    counts = {}
    for eid in order:
        eff = effective_status(entries[eid], entries)
        counts[eff] = counts.get(eff, 0) + 1
    total = len(order)
    print(f"{total} step(s) tracked:")
    for st in ["re-verified", "re-partial", "in-progress", "blocked", "todo", "hack", "skip-by-design"]:
        if counts.get(st):
            print(f"  {emoji(st)} {st:<14} {counts[st]}")


def cmd_check(entries, order, args):
    problems = 0
    for eid in order:
        e = entries[eid]
        if e.status not in VALID_STATUS:
            print(f"‼ {eid}: invalid status '{e.status}'", file=sys.stderr)
            problems += 1
        for d in e.deps:
            if d not in entries:
                print(f"‼ {eid}: unknown dependency '{d}'", file=sys.stderr)
                problems += 1
        if e.status == "re-verified" and not e.evidence:
            print(f"‼ {eid}: re-verified but no evidence cited (RE must name ground truth)",
                  file=sys.stderr)
            problems += 1
    # cycle detection
    WHITE, GRAY, BLACK = 0, 1, 2
    color = {eid: WHITE for eid in order}

    def dfs(eid, stack):
        color[eid] = GRAY
        for d in entries[eid].deps:
            if d not in entries:
                continue
            if color[d] == GRAY:
                print(f"‼ dependency cycle: {' -> '.join(stack + [d])}", file=sys.stderr)
                return True
            if color[d] == WHITE and dfs(d, stack + [d]):
                return True
        color[eid] = BLACK
        return False

    for eid in order:
        if color[eid] == WHITE and dfs(eid, [eid]):
            problems += 1
    hacks = sum(1 for i in order if entries[i].status == "hack")
    if hacks:
        print(f"⛔ {hacks} hack(s) present — debt, run `re_frontier.py hacks` (not a check failure, "
              f"but must be burned down).", file=sys.stderr)
    if problems:
        print(f"\n{problems} problem(s) found.", file=sys.stderr)
        return 1
    print("re-frontier OK: no unknown deps, no cycles, every re-verified step cites evidence.")
    return 0


def cmd_scaffold(entries, order, args):
    """Bootstrap an empty roadmap at $RE_FRONTIER_ROADMAP (or docs/re-frontier.md)."""
    if os.path.exists(ROADMAP):
        print(f"{ROADMAP} already exists — not overwriting.", file=sys.stderr)
        return 1
    os.makedirs(os.path.dirname(ROADMAP), exist_ok=True)
    starter = Entry("area.first-step", "Describe the first RE step in this chain", args.area or "core")
    starter.status = "todo"
    starter.gap = "Fill in real steps; add deps to encode the RE dependency order."
    save({starter.id: starter}, [starter.id])
    print(f"scaffolded {ROADMAP} — edit it, then `re_frontier.py check`.")
    return 0


def cmd_add(entries, order, args):
    if args.id in entries:
        print(f"entry '{args.id}' already exists (use `set`)", file=sys.stderr)
        return 1
    if args.status not in VALID_STATUS:
        print(f"invalid status '{args.status}'", file=sys.stderr)
        return 1
    e = Entry(args.id, args.title, args.area)
    e.status = args.status
    e.deps = [d.strip() for d in (args.deps or "").split(",") if d.strip()]
    e.evidence = args.evidence or ""
    e.where = args.where or ""
    e.gap = args.gap or ""
    e.notes = args.notes or ""
    entries[e.id] = e
    order.append(e.id)
    save(entries, order)
    print(f"added {e.id}")
    return 0


def cmd_set(entries, order, args):
    e = entries.get(args.id)
    if not e:
        print(f"no such entry: {args.id}", file=sys.stderr)
        return 1
    for kv in args.assignments:
        if "=" not in kv:
            print(f"bad assignment '{kv}' (want field=value)", file=sys.stderr)
            return 1
        key, val = kv.split("=", 1)
        key = key.strip()
        if key == "status":
            if val not in VALID_STATUS:
                print(f"invalid status '{val}'", file=sys.stderr)
                return 1
            e.status = val
        elif key == "deps":
            e.deps = [d.strip() for d in val.split(",") if d.strip()]
        elif key in ("evidence", "where", "gap", "notes", "title", "area"):
            setattr(e, key, val)
        else:
            print(f"unknown field '{key}'", file=sys.stderr)
            return 1
    save(entries, order)
    print(f"updated {e.id}")
    return 0


def main():
    p = argparse.ArgumentParser(description="OpenBL2 RE-frontier progress tracker")
    sub = p.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("list"); sp.add_argument("--area"); sp.add_argument("--status")
    sp = sub.add_parser("show"); sp.add_argument("id")
    sp = sub.add_parser("next"); sp.add_argument("--area")
    sub.add_parser("hacks")
    sub.add_parser("blocked")
    sp = sub.add_parser("tree"); sp.add_argument("--area")
    sub.add_parser("stats")
    sub.add_parser("check")
    sp = sub.add_parser("scaffold"); sp.add_argument("--area")
    sp = sub.add_parser("add")
    sp.add_argument("id"); sp.add_argument("--title", required=True)
    sp.add_argument("--area", required=True); sp.add_argument("--status", default="todo")
    sp.add_argument("--deps"); sp.add_argument("--evidence"); sp.add_argument("--where")
    sp.add_argument("--gap"); sp.add_argument("--notes")
    sp = sub.add_parser("set"); sp.add_argument("id")
    sp.add_argument("assignments", nargs="+")

    args = p.parse_args()
    entries, order = load()
    fn = {
        "list": cmd_list, "show": cmd_show, "next": cmd_next, "hacks": cmd_hacks,
        "blocked": cmd_blocked, "tree": cmd_tree, "stats": cmd_stats, "check": cmd_check,
        "scaffold": cmd_scaffold, "add": cmd_add, "set": cmd_set,
    }[args.cmd]
    sys.exit(fn(entries, order, args) or 0)


if __name__ == "__main__":
    main()
