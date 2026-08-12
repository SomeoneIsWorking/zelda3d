# 0020 — every shader compile leaks ~200 KB into glslang's pool allocator

status: partly addressed 2026-08-12 — reclaimed at shutdown; per-compile growth DURING a session remains
found: 2026-08-12, measuring the per-run leak left after issue 0016
severity: ~5 MB per MM run, ~0 per OoT run after the first; bounded, not a crash

## The measurement

With `LSAN_OPTIONS=use_globals=0` (instrument I035), the per-run leak differential:

    oot     80,711,147 bytes / 36,803 allocs        oot,oot  80,714,652 / 36,885   -> +3,505 per run
    mm      42,224,894 bytes / 18,295 allocs        mm,mm    47,356,003 / 25,928   -> +5,131,109 per run

MM's 5.1 MB is almost entirely glslang: `SetupBuiltinSymbolTable`, `TFunction::clone`,
`TType::clone`, `pool_allocator<TParameter>` — all under `glslang::TPoolAllocator::allocate`.

## What it is NOT (each checked, not assumed)

- **Not a shader cache that fails to survive the run boundary.** That was the first hypothesis and it
  is wrong. `mShaderProgramPool` is keyed on `(shaderId0, shaderId1)` — pure content, nothing
  run-scoped — and `ClearShaderCache()` has no caller in either game.
- **Not runaway recompilation.** The new per-run counter (below) reads 0 / 20 / 44 across `mm,mm,mm`:
  run 1 compiles 20 shaders, run 2 compiles 24 more. Those are genuinely new colour-combiner variants
  reached by a second run, lazily compiled — which is the design. A broken cache would be thousands.

## What it is

glslang allocates everything a compile touches from a thread pool allocator, and that memory is
reclaimed only when the pool is popped or `glslang::FinalizeProcess()` runs. `CompileGlslToSpirv` in
`gfx_sdl3gpu.cpp` calls `glslang::InitializeProcess()` once and never brackets an individual compile,
so each compile's ~200 KB stays for the life of the process. 24 compiles ≈ 5 MB, which is the number.

OoT shows ~0 because its second run happens to reach no new combiner variants, not because it behaves
differently.

## The obvious fix is NOT AVAILABLE in this build — tried, does not compile

Attempted, so the next session does not spend the same pass on it:

    glslang::TPoolAllocator* prev = &glslang::GetThreadPoolAllocator();
    glslang::TPoolAllocator local;                 // <- error: incomplete type
    glslang::SetThreadPoolAllocator(&local);       // <- error: not a member of 'glslang'

Fedora's `glslang-devel` installs `glslang/Public/ShaderLang.h`, which **forward-declares**
`class TPoolAllocator` (line 420) and exposes nothing to construct or swap one. `PoolAlloc.h` lives in
`glslang/MachineIndependent/` upstream and is not shipped — the installed
`/usr/include/glslang/MachineIndependent/` contains only `Versions.h`.

The entire public pool-related surface is:

    GLSLANG_EXPORT bool InitializeProcess();   // "exactly once per process"
    GLSLANG_EXPORT void FinalizeProcess();     // "once per process to tear down everything"

So the per-compile bracketing needs either a vendored glslang with its internal headers, or a
different backend. What CAN be done with the public API, and their costs:

- **`FinalizeProcess()` at engine shutdown.** Correct hygiene, reclaims the builtin tables once, and
  does nothing about the per-compile growth this issue is about.
- **`Finalize`/`Initialize` around each compile.** Would reclaim everything — and would rebuild the
  builtin symbol table on every compile, which is precisely the 5 MB of tables being measured. That
  trades a bounded leak for a large per-compile CPU cost on the rendering path. Not worth it.

## Superseded: the fix as originally written (kept for the record)

Bracket each compile with its own pool:

    glslang::TPoolAllocator* prev = &glslang::GetThreadPoolAllocator();
    glslang::TPoolAllocator local;
    glslang::SetThreadPoolAllocator(&local);
    ... TShader / TProgram / GlslangToSpv ...
    glslang::SetThreadPoolAllocator(prev);

`outSpirv` is a `std::vector<uint32_t>` on the default allocator, so the SPIR-V survives the pool.
Not applied — it does not compile here, per the section above. The verification approach WAS set up
before that was discovered and is worth reusing when this is retried: `tools/zelda3d_repl.py shot`
captures a frame and `isolate <a> <b>` diffs two, so a pixel-exact before/after across a couple of
frozen scenes settles "every combiner still renders correctly", which the sequence gates cannot.



## Instrument added

`Fast::Sdl3GpuShaderCompileCount()`, printed by `Ship::Context::BeginRun` as
`"Run start: N shader(s) compiled so far this process."` The cumulative form is deliberate: the
DELTA between consecutive runs is the answer, and a per-run counter reset at run start could not
distinguish "compiled nothing" from "the counter was reset".


## Partly addressed: `FinalizeProcess()` at engine shutdown (2026-08-12)

`Fast::Sdl3GpuFinalizeShaderCompiler()` is now called at the end of `Ship::Context::DestroyInstance()`
— last, after the window and renderer are gone so no further compile can happen. It is guarded on the
shader-compile counter rather than a separate flag: `FinalizeProcess` without a preceding
`InitializeProcess` is undefined, and the counter is incremented in the very function that does the
`call_once` init, so a non-zero count is proof the process was initialized. It logs which branch it
took, so "nothing to finalize" cannot be confused with "never ran".

Measured with the same differential (`use_globals=0`):

    before   mm 42,224,894 / 18,295 allocs   mm,mm 47,356,003 / 25,928   -> 5,131,109 per run
    after    mm 28,356,180 /  9,210 allocs   mm,mm 28,898,481 / 13,969   ->   542,301 per run

**What that does and does not mean.** These numbers are memory alive at process EXIT, so what improved
is the leak at shutdown: glslang's pools — the builtin tables and every compile's ~200 KB — are now
given back, about 14 MB off the baseline and a 10x cut in the per-run figure.

**The thing this issue is actually about is unchanged:** during a session, each compile still adds
~200 KB of live memory that nothing reclaims until the process ends. A long session that reaches many
colour-combiner variants still grows. Fixing THAT still needs the per-compile pool bracketing, which
still needs glslang headers this build does not ship.
