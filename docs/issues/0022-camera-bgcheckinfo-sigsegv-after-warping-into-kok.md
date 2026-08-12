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

## Progress 2026-08-12

**The crash handler no longer pre-empts the sanitizer.** `CrashHandler`'s constructor now skips
SIGILL/SIGABRT/SIGFPE/SIGSEGV when the translation unit was built with AddressSanitizer
(`__SANITIZE_ADDRESS__` / `__has_feature(address_sanitizer)`), so a fault becomes an ASAN report
instead of a symbol-only backtrace and an `_exit`. It says so on stderr at startup, because a
sanitizer run with no crash-handler output would otherwise look like the handler failing. Release
builds are unchanged — this is a compile-time branch on a build that exists to be diagnosed, not a
runtime toggle. **Not yet observed doing its job**: no fault has occurred since, so "ASAN now reports
it" is a design claim, not a measurement, until 0022 next fires.

**It is INTERMITTENT.** Two targeted reproductions failed to trigger it on the sanitizer build:

    sleep:30; warp 0x209; sleep:60; posinfo                                    -> survived
    sleep:30; randogen; warp 0xEE; sleep:30; posinfo; warp 0x209; sleep:45     -> survived

The second is the deep check's own tour up to and past the point it died, on the same build, with the
same ASAN options. So the trigger is not simply "reach Kokiri Forest spawn 2 and wait" — either it
needs something later in the tour (the `warp 0x109` step, or the dwell), or it is timing-dependent.
Anyone chasing this should NOT conclude from a single clean run that it is fixed.

## Next step

Run the full `tools/zelda3d_deep_check.sh` and let it fire on its own; the sanitizer report will now
survive. Then read `Camera_BGCheckInfo` for what it dereferences that can be NULL --  `RAX = 0`
suggests a null collision/bgcheck pointer rather than a wild one.
