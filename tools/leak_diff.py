#!/usr/bin/env python3
"""Diff two LeakSanitizer reports by allocation SITE, not by total.

The totals line ("N byte(s) leaked in M allocation(s)") tells you a second run leaked more; it never
tells you where. This buckets every leak record by its stack -- function names only, so the ASLR'd
addresses in two different processes collapse onto the same key -- and prints the sites whose bytes
or object count DIFFER between the two reports.

A negative result here is a real answer and says so: it prints how many records each side had, how
many buckets matched, and that a zero diff means every site leaked identically -- not that nothing
was parsed. Feeding it two copies of the same file must print "0 differing sites" out of a non-zero
bucket count; if the bucket count is 0 the parse failed and it exits non-zero.

  usage: tools/leak_diff.py <before.asan> <after.asan> [--top N] [--frames N]
"""
import argparse
import collections
import os
import re
import sys

REC = re.compile(r"^(Direct|Indirect) leak of (\d+) byte\(s\) in (\d+) object\(s\)")
FRAME = re.compile(r"^\s+#\d+ 0x[0-9a-f]+ in (.+?) (?:/|\()")


def parse(path):
    """-> {stack_key: [bytes, objects]}, plus the number of records seen."""
    if not os.path.exists(path):
        sys.exit(f"leak_diff: no such report: {path} -- searched NOTHING, this is not 'no leaks'")
    buckets = collections.defaultdict(lambda: [0, 0])
    records = 0
    cur = None
    frames = []
    with open(path, errors="replace") as fh:
        for line in fh:
            m = REC.match(line)
            if m:
                if cur:
                    key = tuple(frames)
                    buckets[key][0] += cur[0]
                    buckets[key][1] += cur[1]
                cur = (int(m.group(2)), int(m.group(3)))
                frames = []
                records += 1
                continue
            if cur is not None:
                f = FRAME.match(line)
                if f:
                    frames.append(f.group(1).strip())
                elif not line.strip():
                    key = tuple(frames)
                    buckets[key][0] += cur[0]
                    buckets[key][1] += cur[1]
                    cur = None
                    frames = []
    if cur:
        key = tuple(frames)
        buckets[key][0] += cur[0]
        buckets[key][1] += cur[1]
    return buckets, records


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("before")
    ap.add_argument("after")
    ap.add_argument("--top", type=int, default=25, help="how many differing sites to print")
    ap.add_argument("--frames", type=int, default=6, help="stack frames to show per site")
    args = ap.parse_args()

    a, a_recs = parse(args.before)
    b, b_recs = parse(args.after)
    if not a and not b:
        sys.exit("leak_diff: parsed 0 leak records from BOTH reports -- the parse failed, not the leak")

    keys = set(a) | set(b)
    diffs = []
    for k in keys:
        ab, ao = a.get(k, (0, 0))
        bb, bo = b.get(k, (0, 0))
        if ab != bb or ao != bo:
            diffs.append((bb - ab, bo - ao, k))
    diffs.sort(key=lambda d: -abs(d[0]))

    print(f"before: {a_recs} records in {len(a)} distinct sites   ({args.before})")
    print(f"after : {b_recs} records in {len(b)} distinct sites   ({args.after})")
    print(f"buckets compared: {len(keys)}   differing: {len(diffs)}")
    total = sum(d[0] for d in diffs)
    print(f"net byte change across differing sites: {total:+,}\n")
    if not diffs:
        print("no site differs -- every bucket leaked the same bytes and objects in both reports.")
        print("(that is a measurement: the two reports parsed to identical per-site totals.)")
        return
    for db, do, k in diffs[: args.top]:
        print(f"=== {db:+,} bytes  {do:+} objects")
        for fr in k[: args.frames]:
            print(f"      {fr}")
        print()
    if len(diffs) > args.top:
        print(f"... {len(diffs) - args.top} more differing sites not shown (raise --top)")


if __name__ == "__main__":
    main()
