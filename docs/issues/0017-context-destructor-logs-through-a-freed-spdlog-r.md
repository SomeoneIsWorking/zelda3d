---
id: 17
title: "~Context() logs through a freed spdlog registry when a core calls exit() -- heap-use-after-free"
status: fixed
symptom: AddressSanitizer reports a heap-use-after-free in spdlog::logger::should_log, read from Ship::Context::~Context() running at __cxa_finalize. Reproduced 3/3. Off the sanitizer it is silent -- the read lands in freed-but-mapped memory.
tags: n3,launcher,teardown,lifetime,asan,spdlog
created: 2026-08-12
updated: 2026-08-12
---

## The finding

Found by the ASAN build that [issue 0009](0009-mm-solo-teardown-corrupts-the-heap-after-the-cor.md) names as its next step.
It is **not** 0009's corruption (see "What this is NOT" below) -- it is a separate, cleanly located
bug that the same instrument turned up on the way.

```
ERROR: AddressSanitizer: heap-use-after-free  READ of size 4
  #1 spdlog::logger::should_log(...)            /usr/include/spdlog/logger.h:263
  #3 Ship::Context::~Context()                  libultraship/src/ship/Context.cpp:136
  #6 __cxa_finalize
  #7 __do_global_dtors_aux

freed by thread T0 here:
  #1 spdlog::details::registry::~registry()
  #2 __run_exit_handlers
  #3 exit
  #4 OTRGlobals::RunExtract(int, char**)        2ship/2s2h/BenPort.cpp:583
  #5 InitOTR                                    2ship/2s2h/BenPort.cpp:1005

previously allocated by thread T0 here:
  #1 spdlog::stderr_color_mt<...>(...)
  #2 Ship::Context::EarlyLogToStderr()          libultraship/src/ship/Context.cpp:188
  #3 Ship::Context::CreateUninitializedInstance libultraship/src/ship/Context.cpp:206
```

Read top to bottom, the whole bug is in the three frames:

1. `RunExtract`'s render loop hits `if (!WindowIsRunning()) { exit(0); }` (`BenPort.cpp:583`).
2. `exit()` runs the exit handlers, and spdlog's registry destructor **frees every logger**.
3. `__cxa_finalize` then destroys `Context`'s static `unique_ptr`, and `~Context()` opens with
   `SPDLOG_TRACE("destruct context")` -- reading the logger that step 2 just freed.

`~Context()`'s own comment says "Explicitly destructing everything so that logging is done last".
Logging last is exactly what makes it unsafe here: it is the one thing guaranteed to outlive its
dependency when the destructor runs from static teardown.

## Why this is a HOLE in 0009's first fix, not a new class

Issue 0009 records, as the first of two defects it fixed: *"Teardown ran at `__cxa_finalize`. The
launcher left `Context`'s static `unique_ptr` to static destruction... Fixed by calling
`Ship::Context::DestroyInstance()` explicitly from the launcher once the core returns."*

That fix is correct **for cores that return**. A core that calls `exit()` never returns, the launcher
never regains control, `DestroyInstance()` is never reached, and `Context` is destroyed at
`__cxa_finalize` again -- the precise situation the fix exists to prevent. MM's `RunExtract` calls
`exit(0)` directly, so the extraction path reintroduces it.

So the invariant to state is stronger than "the launcher destroys the Context": **no code path may
leave `Context` to static destruction**, and any `exit()` inside a core is such a path.

## Reproduction (cheap)

Any interruption during MM's asset extraction takes the `exit(0)` branch:

1. Build with ASAN (`docs/issues/0009` has the command).
2. Ensure the ASAN build dir has **no** `mm.o2r` so `RunExtract` actually runs its loop.
3. Start MM, then kill it while it is extracting.

Reproduced on three consecutive runs (pids 383853, 385324, 401807), identical stack each time.

## What this is NOT

**It is not 0009's "corrupted size vs. prev_size".** That fires on a NORMAL quit, in
`GameSession::End()` -> `Config::Save()` -> nlohmann json destroy. This one fires on the
`exit()`-from-`RunExtract` path, which a normal quit never takes. The two are plausibly related --
a use-after-free during teardown is exactly the kind of write that leaves a damaged chunk for a
later `free()` to trip over -- but nothing here demonstrates that, and 0009 should not be marked
fixed on the strength of it.

## FIXED 2026-08-12 -- and the first two attempts are the reason the third is right

Three defects on one path, each exposed by fixing the one before it. That progression IS the
evidence for where the real fix belongs:

1. **Removed `SPDLOG_TRACE` from `~Context()`.** ASAN promptly reported the identical
   use-after-free from `Ship::ControlDeck::~ControlDeck()` (`ControlDeck.cpp:46`). That is what
   proved the defect is the OWNERSHIP of the default logger and not any particular log statement --
   patching destructors one at a time is unbounded, exactly like the run-scoped-state arc in 0016.
2. **Gave the default logger an owner that never releases it** (`EarlyLogToStderr`). `stderr_color_mt`
   + `set_default_logger` left the REGISTRY as sole owner, so anything that destroys the registry
   frees it and every later log dangles. A deliberately leaked `shared_ptr` costs one logger for the
   process lifetime and makes the default logger outlive every destructor that might use it, on every
   path -- without depending on "no core ever calls `exit()`", which a dozen extraction-failure
   popups in both games already violate.
3. **Guarded `~Context()` against PARTIAL construction.** With the UAF gone, ASAN reported a plain
   null dereference one line later: `mLogger->flush()` where `InitLogging` had never run, because
   this path tears down a Context that never finished being built. `mLogger` and `GetWindow()` are
   both guarded now; anything else this destructor touches deserves the same reading.

**Evidence:** the repro below reported the use-after-free on 3/3 runs before, and **0/3 after** (plus
the two intermediate reports above, each of which named the next defect). Verified on the release
build too via `tools/zelda3d_switch_test.sh`, since `Context.cpp` is shared by both games.

## Candidate fixes, in order of how well they match the defect

1. **Do not log from `~Context()`.** The narrow fix: the destructor of a static object cannot rely on
   another library's static registry still existing. Removes this UAF outright.
2. **Do not `exit()` from inside a core.** `RunExtract` should request exit and let the frame loop
   unwind to `run()`, so the launcher's `DestroyInstance()` runs -- the same shape as the `_exit(0)`
   removals in the one-binary consolidation, and it fixes the class rather than this instance.
3. Both. (1) is defensive and cheap; (2) is the actual design rule.

What shipped is a fourth option that neither of the above named: fix the LOGGER'S OWNERSHIP so the
default logger cannot be freed while destructors still run. Option 2 ("do not `exit()` from inside a
core") remains the right design rule and is still worth doing -- an `exit()` mid-run also skips
`GameSession::End`, so config and save state are lost on those paths -- but it is a behaviour change
across a dozen call sites in both games, and it is no longer load-bearing for this crash.

## Sizing for "do not `exit()` from inside a core" (measured 2026-08-12, NOT done)

Option 2 above is still the right design rule, and here is what it actually costs, so the next
session does not start by counting.

**30 call sites**, and they are not scattered: 15 in `Shipwright/soh/soh/OTRGlobals.cpp` and 15 in
`2ship/2s2h/BenPort.cpp`, mirroring each other nearly line for line (both games' `InitOTR` share an
ancestor). Every one is an asset-setup failure -- no ROM found, wrong ROM, extraction failed,
user declined -- raised either directly or from an "OK" popup callback.

**They all fire BEFORE the frame loop exists**, inside `InitOTR`, which is why option 2 as written
("request exit and let the frame loop unwind to `run()`") does not fit them: there is no loop to
unwind. The right shape is for the core's entry point to RETURN A FAILURE to the launcher, which
already knows how to regain control, so a game that cannot start puts the user back at the chooser
instead of killing the whole application. That is a signature change on the core entry plus a
launcher-side branch -- bounded, but a real behaviour change in two games.

**Practical impact today is low**, which is why it stays deferred: these paths only run when the
`.o2r` archives are missing or unreadable, so a normal launch never reaches them. What they cost is
paid on a bad install -- the launcher dies instead of reporting -- and, per this issue, they are the
one remaining way to leave `Context` to static destruction.

## Instrument notes (so the next session does not re-lose the time)

- ASAN aborts at startup on an **odr-violation for StormLib's `DistBits`**, which is linked into both
  the launcher and `libultraship.so`. Run with `detect_odr_violation=0`. Worth asking separately why
  the launcher links StormLib at all when it dlopens the engine.
- **Copy `mm.o2r` and `2ship.o2r` into the ASAN build dir** from the release build. Without them MM
  re-extracts the ROM while drawing a progress frame per step through software Vulkan: 40 minutes of
  CPU with the log not advancing one line. Two separate theories about that stall (shader compile,
  then a deadlock) were both wrong; `eu-stack -p <pid>` named it in one call and was available the
  whole time.
- The sequence gate's boot budget is `ZELDA3D_SEQ_BOOT_WAIT` (default 120s, nowhere near enough for
  a sanitizer build).


## A SECOND instance, found 2026-08-12 -- the fix covered one logger, not all of them

ASAN on an `oot` sequence run (once [issue 0018](0018-animation-resources-point-into-a-resource-file-b.md)
stopped killing the run earlier):

```
spdlog::logger::should_log                                    /usr/include/spdlog/logger.h:263
InputViewerSettingsWindow::~InputViewerSettingsWindow         soh/Enhancements/controls/InputViewer.cpp:462
std::_Sp_counted_base<...>::_M_release                        (a static shared_ptr to the window)
__run_exit_handlers  <-  exit
```

Same shape as the original: a destructor running at static-teardown time logs through a logger the
exit handlers have already freed. The fix recorded above gave the **early stderr logger** an owner
that never releases it, which is why `~Context()` and `~ControlDeck()` are safe. This is a different
logger -- the one `InitLogging` installs -- still owned solely by spdlog's registry and freed when
that registry is destroyed.

So the invariant stated above ("no code path may leave `Context` to static destruction") is still too
narrow. The general form is: **no logger may be owned solely by spdlog's registry if anything logs
from a destructor**.

**FIXED the same day, at the class rather than the instance.** `InitLogging` now pushes its logger
into a deliberately leaked keep-alive list, exactly as `EarlyLogToStderr` does. One subtlety that
would have made a narrower fix wrong: in RELEASE builds `mLogger` is an `async_logger` holding
`mLogThreadPool`, which is a **Context member** -- keeping only the logger alive would have left it
posting to a pool that had been joined and destroyed, the same use-after-free one indirection
further out. Both are kept. (The pool exists only in non-`_DEBUG` builds, which the sanitizer build
is not -- the first attempt failed to compile there, and the ASAN run that followed silently used a
stale binary. Its "clean" result was discarded rather than reported.)

**Evidence:** the ASAN `oot` run that reported this now exits 0 with **no report file at all**.
`oot`, `mm`, `mm,mm`, `mm,oot` and the switch test all exit 0 on the release build.
