# 0020 — every shader compile leaks ~200 KB into glslang's pool allocator

status: open — cause identified and measured; fix not applied
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

## Candidate fix (not applied)

Bracket each compile with its own pool:

    glslang::TPoolAllocator* prev = &glslang::GetThreadPoolAllocator();
    glslang::TPoolAllocator local;
    glslang::SetThreadPoolAllocator(&local);
    ... TShader / TProgram / GlslangToSpv ...
    glslang::SetThreadPoolAllocator(prev);

`outSpirv` is a `std::vector<uint32_t>` on the default allocator, so the SPIR-V survives the pool.
Not applied here because it changes shader compilation at the tail of a long session and wants its own
verification pass — the gates render and reach scenes, so they would catch gross breakage, but "the
shaders still compile" is not the same as "every combiner still renders correctly".

`glslang::FinalizeProcess()` at engine shutdown is a smaller, separate improvement: it reclaims the
builtin tables once, but does nothing about the per-compile growth.

## Instrument added

`Fast::Sdl3GpuShaderCompileCount()`, printed by `Ship::Context::BeginRun` as
`"Run start: N shader(s) compiled so far this process."` The cumulative form is deliberate: the
DELTA between consecutive runs is the answer, and a per-run counter reset at run start could not
distinguish "compiled nothing" from "the counter was reset".
