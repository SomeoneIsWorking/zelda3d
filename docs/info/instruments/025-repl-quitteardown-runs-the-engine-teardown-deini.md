---
id: I025
kind: instrument
status: trusted
created: 2026-08-05
---

## Instrument

REPL quitteardown (runs the engine teardown DeinitOTR's _exit(0) skips, to re-test C057 on current drivers)

## Validated by

Validated in both directions on 2026-08-05, which mattered because its two outcomes are 'crashes' and 'does not crash' and only one had ever been assumed. NEGATIVE (teardown survives) is observable: it prints 'ZELDA3D TEARDOWN: Context destroyed WITHOUT crashing' and the launcher then prints 'core returned N'; both were actually produced on the first run, when a core-held shared_ptr deferred the window's destruction past ~Context. POSITIVE (teardown crashes) is observable as a gdb backtrace ending in SDL3 VULKAN_DestroyDevice, which is what the corrected run produces. CAUTION learned from that first run: the log lines alone are NOT sufficient -- they reported success while the process still exited 139, because the object whose destructor crashes can outlive the Context. Always check the process exit code AND take a backtrace; treat a clean pair of log lines with a non-zero exit as the interesting case, not a contradiction to explain away. This is the one-command re-test C057 asks for when drivers or the render backend change.

## Known failure modes

(none recorded yet)
