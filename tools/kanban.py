#!/usr/bin/env python3
"""kanban.py — thin wrapper over GitHub Issues so the Zelda3D backlog is a kanban board
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
    # `gh issue view --comments` prints ONLY the comments (not the body/metadata), so an
    # issue with 0 comments printed nothing at all. Show the body view first, then comments.
    print(gh(["issue", "view", str(a.issue)]))
    comments = gh(["issue", "view", str(a.issue), "--comments"]).strip()
    if comments:
        print("\n--- comments ---\n")
        print(comments)


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
        "# Zelda3D Kanban",
        "",
        f"> **Generated mirror — do not hand-edit.** Source of truth = GitHub Issues on",
        f"> `SomeoneIsWorking/zelda3d` (private). Regenerate with `tools/kanban.py render`.",
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


EVIDENCE_TAG = "evidence-assets"


def _ensure_release():
    if subprocess.run(["gh", "release", "view", EVIDENCE_TAG], cwd=REPO_ROOT,
                      capture_output=True, text=True).returncode != 0:
        gh(["release", "create", EVIDENCE_TAG, "--title", "Evidence assets (kanban screenshots)",
            "--notes", "Off-tree storage for kanban bug/verification screenshots. Uploaded via "
            "`gh release upload`, embedded in issues by URL. Not source — safe to prune old assets."])


def cmd_evidence(a):
    """Attach screenshot(s) to an issue. Uploads each image as a RELEASE ASSET (off the source
    tree, on GitHub's CDN — gh can't push browser-style user-attachments, but `gh release upload`
    is supported), then embeds it by URL in a comment (default) or the issue body (--to-body).
    This is how agents post bug evidence AND fix-verification proof for the user to review."""
    owner, _ = _repo_meta()
    _ensure_release()
    tmp = os.path.join(REPO_ROOT, "scratch", "evidence_upload")
    os.makedirs(tmp, exist_ok=True)
    urls = []
    for f in a.files:
        if not os.path.isfile(f):
            sys.exit(f"no such file: {f}")
        # namespace the asset name by issue so the same basename across issues doesn't collide
        asset = f"i{a.issue}-{os.path.basename(f)}"
        staged = os.path.join(tmp, asset)
        with open(f, "rb") as src, open(staged, "wb") as out:
            out.write(src.read())
        gh(["release", "upload", EVIDENCE_TAG, staged, "--clobber"])
        urls.append((asset, f"https://github.com/{owner}/releases/download/{EVIDENCE_TAG}/{asset}"))
    # build markdown
    lines = []
    if a.caption:
        lines.append(a.caption)
        lines.append("")
    for asset, url in urls:
        if asset.lower().endswith((".mp4", ".mov", ".webm")):
            # GitHub renders an inline <video> player for a BARE video URL on its own line;
            # the ![]() image syntax would render a broken image for video.
            lines.append(url)
        else:
            lines.append(f"![{asset}]({url})")
        lines.append("")
    block = "\n".join(lines)
    if a.to_body:
        cur = gh(["issue", "view", str(a.issue), "--json", "body", "-q", ".body"]).rstrip()
        new = cur + "\n\n## Evidence\n\n" + block + "\n"
        gh(["issue", "edit", str(a.issue), "--body", new])
        print(f"appended {len(urls)} image(s) to #{a.issue} body")
    else:
        gh(["issue", "comment", str(a.issue), "--body", block])
        print(f"commented {len(urls)} image(s) on #{a.issue}")


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
    s = sub.add_parser("evidence", help="attach screenshot(s) to an issue (upload as release asset, embed by URL)")
    s.add_argument("issue", type=int); s.add_argument("files", nargs="+")
    s.add_argument("--caption"); s.add_argument("--to-body", action="store_true")
    s.set_defaults(fn=cmd_evidence)
    s = sub.add_parser("render"); s.add_argument("--out"); s.set_defaults(fn=cmd_render)
    s = sub.add_parser("stats"); s.set_defaults(fn=cmd_stats)

    a = p.parse_args()
    a.fn(a)


if __name__ == "__main__":
    main()
