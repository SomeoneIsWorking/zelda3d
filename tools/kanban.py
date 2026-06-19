#!/usr/bin/env python3
"""kanban.py — thin wrapper over GitHub Issues so the SoH3D backlog is a kanban board
that ANY agent (local sessions, cloud subagents, another PC) can read/drive via the
GitHub API, not just a file in one branch.

Design (why GitHub Issues, not a local file or a TUI tool):
  - The board must be reachable by other agents/machines -> a shared remote store wins
    over a markdown file living in one checkout. GitHub Issues on the (private) repo are
    that store; `gh` is the well-known CLI; this script just removes the per-move friction.
  - Columns are LABELS (`status:<col>`); an issue carries exactly one status label.
    `done` is the CLOSED state (no label) so the board and GitHub's own UI agree.
  - `render` pulls the live issues into a compact local KANBAN.md so an OFFLINE / fresh
    session still gets the whole board at a glance without hitting the network.

Columns (left -> right):  todo  in-progress  in-review  needs-confirmation  reopened  blocked  [done=closed]

Usage:
  tools/kanban.py ls [--status todo] [--label type:stairs] [--all]
  tools/kanban.py show <issue#>
  tools/kanban.py add --title "..." [--status todo] [--labels type:stairs,type:render]
                      [--body "..." | --body-file f] [--legacy 5]
  tools/kanban.py mv <issue#> <status>      # status one of the columns, or `done` to close
  tools/kanban.py reopen <issue#> [<status>]
  tools/kanban.py render [--out KANBAN.md]  # regenerate the local board mirror
  tools/kanban.py stats
"""
import argparse, json, subprocess, sys, os, datetime

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
COLUMNS = ["todo", "in-progress", "in-review", "needs-confirmation", "reopened", "blocked"]
STATUS_LABELS = [f"status:{c}" for c in COLUMNS]
COL_TITLE = {
    "todo": "📋 To Do",
    "in-progress": "🔨 In Progress",
    "in-review": "🔍 In Review (agent verifying)",
    "needs-confirmation": "🙋 Needs User Confirmation",
    "reopened": "♻️ Reopened (was done, found broken)",
    "blocked": "⛔ Blocked (needs live/user input)",
    "done": "✅ Done (recently closed)",
}


def gh(args, capture=True, check=True):
    r = subprocess.run(["gh"] + args, cwd=REPO_ROOT,
                       capture_output=capture, text=True)
    if check and r.returncode != 0:
        sys.stderr.write(r.stderr or r.stdout or "")
        sys.exit(r.returncode)
    return r.stdout if capture else ""


def status_of(labels):
    for l in labels:
        n = l["name"] if isinstance(l, dict) else l
        if n.startswith("status:"):
            return n[len("status:"):]
    return None


def list_issues(state="open", extra=None):
    args = ["issue", "list", "--state", state, "--limit", "300",
            "--json", "number,title,labels,updatedAt,state"]
    if extra:
        args += extra
    return json.loads(gh(args) or "[]")


def cmd_ls(a):
    state = "all" if a.all else "open"
    issues = list_issues(state)
    if a.status:
        issues = [i for i in issues if status_of(i["labels"]) == a.status]
    if a.label:
        issues = [i for i in issues if any((l["name"] == a.label) for l in i["labels"])]
    # order by column then number
    order = {c: i for i, c in enumerate(COLUMNS)}
    def key(i):
        st = status_of(i["labels"]) if i["state"] == "OPEN" else "done"
        return (order.get(st, 99), i["number"])
    for i in sorted(issues, key=key):
        st = status_of(i["labels"]) if i["state"] == "OPEN" else "closed"
        types = ",".join(l["name"][5:] for l in i["labels"] if l["name"].startswith("type:"))
        print(f"#{i['number']:>3} [{st or '-':<18}] {i['title']}" + (f"  ({types})" if types else ""))


def cmd_show(a):
    print(gh(["issue", "view", str(a.issue), "--comments"]))


def cmd_add(a):
    labels = []
    if a.status and a.status != "done":
        if a.status not in COLUMNS:
            sys.exit(f"bad status '{a.status}', pick one of {COLUMNS + ['done']}")
        labels.append(f"status:{a.status}")
    if a.labels:
        labels += [x.strip() for x in a.labels.split(",") if x.strip()]
    body = a.body or ""
    if a.body_file:
        with open(a.body_file) as f:
            body = f.read()
    if a.legacy:
        body = f"_Legacy backlog id: #{a.legacy}_\n\n" + body
    args = ["issue", "create", "--title", a.title, "--body", body or "(no description)"]
    for l in labels:
        args += ["--label", l]
    out = gh(args)
    print(out.strip())
    if a.status == "done":
        num = out.strip().rstrip("/").split("/")[-1]
        gh(["issue", "close", num, "--reason", "completed"])
        print(f"  (closed #{num} as done)")


def cmd_mv(a):
    st = a.status
    if st == "done":
        gh(["issue", "edit", str(a.issue)] +
           sum([["--remove-label", l] for l in STATUS_LABELS], []), check=False)
        gh(["issue", "close", str(a.issue)])
        print(f"#{a.issue} -> done (closed)")
        return
    if st not in COLUMNS:
        sys.exit(f"bad status '{st}', pick one of {COLUMNS + ['done']}")
    # reopen if closed, swap status labels
    gh(["issue", "reopen", str(a.issue)], check=False)
    rm = sum([["--remove-label", l] for l in STATUS_LABELS if l != f"status:{st}"], [])
    gh(["issue", "edit", str(a.issue), "--add-label", f"status:{st}"] + rm, check=False)
    print(f"#{a.issue} -> {st}")


def cmd_reopen(a):
    a.status = a.status or "reopened"
    cmd_mv(a)


def cmd_render(a):
    open_issues = list_issues("open")
    closed = list_issues("closed")
    # group open by status
    cols = {c: [] for c in COLUMNS}
    for i in open_issues:
        st = status_of(i["labels"]) or "todo"
        cols.setdefault(st, []).append(i)
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    lines = [
        "# SoH3D Kanban",
        "",
        f"> **Generated mirror — do not hand-edit.** Source of truth = GitHub Issues on",
        f"> `SomeoneIsWorking/soh3d` (private). Regenerate with `tools/kanban.py render`.",
        f"> Move a card with `tools/kanban.py mv <#> <column>`; add with `tools/kanban.py add`.",
        f"> Columns: {' · '.join(COLUMNS)} · done(=closed).  Updated {now}.",
        "",
    ]
    counts = " | ".join(f"{c}:{len(cols.get(c, []))}" for c in COLUMNS)
    lines += [f"**Counts:** {counts} | done:{len(closed)}", ""]
    for c in COLUMNS:
        items = sorted(cols.get(c, []), key=lambda i: i["number"])
        lines.append(f"## {COL_TITLE[c]}  ({len(items)})")
        if not items:
            lines.append("_none_")
        for i in items:
            types = ",".join(l["name"][5:] for l in i["labels"] if l["name"].startswith("type:"))
            tag = f" _({types})_" if types else ""
            lines.append(f"- [#{i['number']}](../../issues/{i['number']}) {i['title']}{tag}")
        lines.append("")
    # recently closed (cap)
    closed_sorted = sorted(closed, key=lambda i: i["updatedAt"], reverse=True)[:30]
    lines.append(f"## {COL_TITLE['done']}  (showing {len(closed_sorted)} of {len(closed)})")
    for i in closed_sorted:
        lines.append(f"- [#{i['number']}](../../issues/{i['number']}) {i['title']}")
    lines.append("")
    out = a.out or os.path.join(REPO_ROOT, "KANBAN.md")
    with open(out, "w") as f:
        f.write("\n".join(lines))
    print(f"wrote {out} ({len(open_issues)} open, {len(closed)} closed)")


def _repo_meta():
    """(owner/repo, default-branch) from gh, cached."""
    if not hasattr(_repo_meta, "v"):
        out = gh(["repo", "view", "--json", "nameWithOwner,defaultBranchRef"])
        d = json.loads(out)
        _repo_meta.v = (d["nameWithOwner"], d["defaultBranchRef"]["name"])
    return _repo_meta.v


def _git(args, check=True):
    r = subprocess.run(["git"] + args, cwd=REPO_ROOT, capture_output=True, text=True)
    if check and r.returncode != 0:
        sys.stderr.write(r.stderr or r.stdout or "")
        sys.exit(r.returncode)
    return r.stdout


def cmd_evidence(a):
    """Attach screenshot(s) to an issue: commit them to kanban/evidence/<#>/, push, then
    embed by raw URL in a comment (default) or appended to the issue body (--to-body).
    This is how agents post bug evidence AND fix-verification proof — gh can't upload
    images, so a tracked repo dir + raw blob URL is the reproducible path for a private repo."""
    owner, branch = _repo_meta()
    dest_dir = os.path.join(REPO_ROOT, "kanban", "evidence", str(a.issue))
    os.makedirs(dest_dir, exist_ok=True)
    rels = []
    for f in a.files:
        if not os.path.isfile(f):
            sys.exit(f"no such file: {f}")
        name = os.path.basename(f)
        dst = os.path.join(dest_dir, name)
        if os.path.abspath(f) != os.path.abspath(dst):
            with open(f, "rb") as src, open(dst, "wb") as out:
                out.write(src.read())
        rels.append(f"kanban/evidence/{a.issue}/{name}")
    if not a.no_push:
        _git(["add"] + [os.path.join(REPO_ROOT, r) for r in rels])
        cap = a.caption or f"evidence for #{a.issue}"
        _git(["commit", "-q", "-m", f"evidence #{a.issue}: {cap}"])
        _git(["push", "origin", "HEAD"])
    # build markdown
    lines = []
    if a.caption:
        lines.append(a.caption)
        lines.append("")
    for r in rels:
        name = os.path.basename(r)
        url = f"https://github.com/{owner}/blob/{branch}/{r}?raw=true"
        lines.append(f"![{name}]({url})")
    block = "\n".join(lines)
    if a.to_body:
        cur = gh(["issue", "view", str(a.issue), "--json", "body", "-q", ".body"]).rstrip()
        new = cur + "\n\n## Evidence\n\n" + block + "\n"
        gh(["issue", "edit", str(a.issue), "--body", new])
        print(f"appended {len(rels)} image(s) to #{a.issue} body")
    else:
        gh(["issue", "comment", str(a.issue), "--body", block])
        print(f"commented {len(rels)} image(s) on #{a.issue}")


def cmd_stats(a):
    open_issues = list_issues("open")
    cols = {c: 0 for c in COLUMNS}
    for i in open_issues:
        st = status_of(i["labels"]) or "todo"
        cols[st] = cols.get(st, 0) + 1
    for c in COLUMNS:
        print(f"{c:<20} {cols.get(c,0)}")
    print(f"{'done(closed)':<20} {len(list_issues('closed'))}")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("ls"); s.add_argument("--status"); s.add_argument("--label"); s.add_argument("--all", action="store_true"); s.set_defaults(fn=cmd_ls)
    s = sub.add_parser("show"); s.add_argument("issue", type=int); s.set_defaults(fn=cmd_show)
    s = sub.add_parser("add")
    s.add_argument("--title", required=True); s.add_argument("--status", default="todo")
    s.add_argument("--labels"); s.add_argument("--body"); s.add_argument("--body-file"); s.add_argument("--legacy")
    s.set_defaults(fn=cmd_add)
    s = sub.add_parser("mv"); s.add_argument("issue", type=int); s.add_argument("status"); s.set_defaults(fn=cmd_mv)
    s = sub.add_parser("reopen"); s.add_argument("issue", type=int); s.add_argument("status", nargs="?"); s.set_defaults(fn=cmd_reopen)
    s = sub.add_parser("evidence", help="attach screenshot(s) to an issue (commit+push, embed by raw URL)")
    s.add_argument("issue", type=int); s.add_argument("files", nargs="+")
    s.add_argument("--caption"); s.add_argument("--to-body", action="store_true")
    s.add_argument("--no-push", action="store_true"); s.set_defaults(fn=cmd_evidence)
    s = sub.add_parser("render"); s.add_argument("--out"); s.set_defaults(fn=cmd_render)
    s = sub.add_parser("stats"); s.set_defaults(fn=cmd_stats)

    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
