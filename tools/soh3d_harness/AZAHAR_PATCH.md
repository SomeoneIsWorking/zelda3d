# Azahar patch for the writer-PC watchhook primitive

`Azahar/` is gitignored in this repo (as documented in `CLAUDE.md`), so
the Azahar-side wiring for the write-hook cannot be committed here.
This file records the exact patch to re-apply after a fresh Azahar
clone.

## What it does

Wires `soh3d_harness/watchhook.cpp`'s `Soh3d_OnMemoryWrite` receiver
into Azahar's `MemorySystem::Write<T>` at the `PageType::MemoryWatchpoint`
case. When the harness calls `MemorySystem::RegisterWatchpoint(process,
addr, size)`, the page containing `addr` gets marked as
`MemoryWatchpoint`; subsequent guest writes into that page fall out of
the fast path (page pointer is nulled by the watchpoint registration),
hit the switch, land in the `MemoryWatchpoint` case, and the harness
hook is called with the current guest write context.

The hook queries `Core::System::GetRunningCore()` for the current ARM
`PC` and `LR` (r14), and records a `(vaddr, size, data, pc, lr, cycles)`
tuple into a per-address ring buffer that the harness REPL exposes via
`watch` / `hits` / `unwatch` / `watches` / `hitclear`.

Weak linkage: the forward declaration in Azahar carries
`__attribute__((weak))`, so a plain Azahar build with no harness linked
resolves the symbol to `nullptr` and the hook stays a no-op.

## The patch

Two hunks in `Azahar/src/core/memory.cpp`.

### Hunk 1 — forward decl at global scope near the top of the file

Insert just before `SERIALIZE_EXPORT_IMPL(...)`:

```cpp
// soh3d_harness write-hook forward decl. Defined in tools/soh3d_harness/
// watchhook.cpp when the harness executable is linked. Weak linkage
// keeps this a no-op in a plain Azahar build.
extern "C" void Soh3d_OnMemoryWrite(u32 vaddr, u32 size, u64 data)
    __attribute__((weak));
```

### Hunk 2 — call site in `MemorySystem::Write<T>` (~line 727)

Inside the `case PageType::MemoryWatchpoint:` branch, right after the
existing `memcpy` and `#ifdef ENABLE_GDBSTUB` block, insert:

```cpp
        // soh3d_harness write-hook: notify external hook on every write
        // that lands in a MemoryWatchpoint page. See tools/soh3d_harness/
        // watchhook.{h,cpp} for the harness-side receiver.
        if (&::Soh3d_OnMemoryWrite) {
            u64 wd = 0;
            std::memcpy(&wd, &data, std::min(sizeof(T), sizeof(u64)));
            ::Soh3d_OnMemoryWrite(vaddr, sizeof(T), wd);
        }
```

## How to re-apply

1. Fresh clone of Azahar into `Azahar/`.
2. Open `Azahar/src/core/memory.cpp`.
3. Apply Hunk 1 near the top of the file (before `SERIALIZE_EXPORT_IMPL`).
4. Apply Hunk 2 inside `case PageType::MemoryWatchpoint:` in
   `MemorySystem::Write<T>` (search for `MemoryWatchpoint` — there's one
   in the `Read<T>` block and one in the `Write<T>` block; edit the
   Write one).
5. Rebuild the harness: `tools/soh3d_harness.sh` (which triggers
   `ninja -C Azahar/build-libretro soh3d_harness`).

## Related files

- `tools/soh3d_harness/watchhook.cpp` — the harness-side hook receiver
  and per-range ring buffer.
- `tools/soh3d_harness/main.cpp` — REPL commands `watch`, `unwatch`,
  `watches`, `hits`, `hitclear`.
- `scratch/watch_bgflags.py` — smoke test that watches
  `Actor+0x0090` (bgCheckFlags) during walk-into-wall and prints the
  captured writer PCs. Verified 2026-07-03 to capture the wall-bit-set
  writes at guest PCs `0x0032f328` and `0x00376420` on OoT3D USA.

## Verification signature

At Link's House with matched forward walk (Az reaches wall around game
frame 20-23), `hits 0x<Actor+0x0090>` should return records with:

- `data=0x0000000000000081` (rest state) at frames where Az isn't wall-
  touching.
- `data=0x0000000000000289` (wall state) at frames where Az is against
  the north wall (Z ≈ 135.5).
- The transition write to `0x0289` fires at guest PC `0x0032f328`.

If the hook returns 0 hits despite Az reaching the wall, check:

1. `watches` — is the range registered?
2. That the Azahar patch was applied to `memory.cpp` (grep for
   `Soh3d_OnMemoryWrite` in the file).
3. That the harness rebuild picked up the patched `memory.cpp`
   (`ninja -C Azahar/build-libretro citra_core` should show
   `memory.cpp.o` recompiling).
