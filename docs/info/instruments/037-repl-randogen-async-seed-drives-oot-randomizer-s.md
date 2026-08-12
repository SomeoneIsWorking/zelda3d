---
id: I037
kind: instrument
status: trusted
created: 2026-08-12
---

## Instrument

REPL randogen [async] [seed] (drives OoT randomizer seed generation headlessly; ZELDA3D_SEQ_CMDS / ZELDA3D_DEEP_CMDS run it per core)

## Validated by

Validated in both directions on 2026-08-12, and the FIRST direction was misleading, which is the lesson. POSITIVE (blocking form): replies 'generated=1 hash=... seedString=...' with different hashes per run, and it immediately caught a real alloc-dealloc-mismatch in Rando::Logic::NewSaveContext that had never been exercised. NEGATIVE is expressible and observed: MM replies 'err unknown-command' (rando is OoT-only) and the sequence ECHOES that rather than swallowing it. CAUTION -- the blocking form CANNOT test teardown-during-generation: with it, removing DeinitOTR's JoinRandoGenerationThread call changed nothing and read as 'the join is not load-bearing'. Use 'randogen async' for that case: it starts generation and returns, and without the join oot,oot then FAILS with run 2 never reaching a scene (a boot hang, NO ASAN report -- the gate that catches it is the one requiring each core to answer posinfo). JoinRandoGenerationThread logs IN-FLIGHT vs already-finished vs nothing-to-join, so a clean async run shows which case it actually covered.

## Known failure modes

(none recorded yet)
