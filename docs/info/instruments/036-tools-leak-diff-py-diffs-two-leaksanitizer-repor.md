---
id: I036
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

tools/leak_diff.py (diffs two LeakSanitizer reports by allocation SITE, not by total)

## Validated by

Validated in both directions on 2026-08-12. POSITIVE: given oot vs oot,oot before the cameraStrings fix it named the two real sites (strdup <- OTRGlobals::Initialize, +1,161 B/+74 objects; operator new <- InitOTR, +600 B/+3) whose sum matched the total delta, and those two fixes took the delta to 0. NEGATIVE is expressible and was run: same file on both sides prints '0 differing' out of 152 non-zero buckets, and a missing report exits non-zero saying it searched NOTHING rather than printing no-differences. CAUTION: X11/SDL locale allocations bucket unstably between two processes (the same allocations appear with and without their _Xlc*/_Xrm* intermediate frames), producing large paired +/- entries that net to zero -- read the sum, and filter for frames that are actually ours before concluding a site is real.

## Known failure modes

(none recorded yet)
