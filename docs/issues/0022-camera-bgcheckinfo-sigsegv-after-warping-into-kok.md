# 0022 — `Camera_BGCheckInfo` SIGSEGVs a few seconds after warping into Kokiri Forest spawn 2

status: OPEN — found 2026-08-12, not yet diagnosed
found by: `tools/zelda3d_deep_check.sh` after issue 0021 made the warp tour land where it was aimed
severity: crashes the core; currently makes the deep check RED

## What happens

Sanitizer build, `oot,oot`, tour = `randogen; warp 0xEE; …; warp 0x209; …`. The warp to `0x209`
(`ENTR_KOKIRI_FOREST_OUTSIDE_DEKU_TREE`, Kokiri Forest spawn 2) succeeds — the previous warp's
`posinfo` confirms Kokiri Forest — and the core dies during the following settle:

    Camera_BGCheckInfo
    func_80045508
    func_80046E20
    Camera_Normal1
    Camera_Update
    Play_Update

`RAX = 0` on the faulting instruction. Signal 11.

## What is and is not known

- **Not the warp itself.** `warp 0x209` completes and the scene loads; the crash is a few seconds
  later, from the camera's per-frame background check.
- **Not reproduced on the release build yet.** A release run of `warp 0xEE → 0x209 → 0x109` with 15 s
  settles survived all three. The sanitizer run sits in the scene far longer (30 s settle + 30 s
  dwell), so "release is fine" is NOT established — it may simply never have run that long there.
  Do not report this as sanitizer-only without running release with the same dwell.
- **No AddressSanitizer report exists for it**, which is a second problem: our own crash handler
  installs a SIGSEGV handler and `_exit`s, so on a sanitizer build it pre-empts ASAN's handler and
  the precise report (what address, what allocation, what shadow state) is lost. The
  async-signal-safe backtrace above is all there is. That is worth fixing before this is chased --
  the report would probably name the cause outright.

## Next step

Reproduce with the crash handler out of the way (so ASAN reports), on both builds, with the same
dwell. Then read `Camera_BGCheckInfo` for what it dereferences that can be NULL at that spawn --
`RAX = 0` suggests a null collision/bgcheck pointer rather than a wild one.
