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

# Azahar patch 2 — SW rasterizer draw log (task #16)

Adds a per-triangle log to Azahar's software rasterizer so the harness
can enumerate exactly which textures + blend modes make up a given
frame (e.g. the 3-layer moon at title, RE'd in
`oot3d-decomp/docs/title_moon_composition.md`).

## What it does

Two `extern "C"` globals `soh3d_draw_log_path[256]` and
`soh3d_draw_log_active` sit inside
`Azahar/src/video_core/renderer_software/sw_rasterizer.cpp`. When the
harness sets both via the REPL `draw_log <path>` command, every triangle
that ProcessTriangle receives appends one CSV-shaped line to the file:

    tri tex0=<hex> tex1=<hex> tex2=<hex>
        blendRGB=<srcF>,<dstF>,<eq> blendA=<srcF>,<dstF>,<eq>
        sxy=(x0,y0),(x1,y1),(x2,y2)
        w=<texW> h=<texH>

Zero overhead when off — the branch on `soh3d_draw_log_active` is
predicted-untaken and skips the fopen.

## The patch

Two hunks in `Azahar/src/video_core/renderer_software/sw_rasterizer.cpp`.

### Hunk 1 — include + globals near the top of the file

Add `#include <cstdio>` to the include list. Just above the definition
of `RasterizerSoftware::AddTriangle` (or the first Rasterizer method):

```cpp
extern "C" char soh3d_draw_log_path[256] = "";
extern "C" int  soh3d_draw_log_active = 0;
```

### Hunk 2 — draw-log block inside `ProcessTriangle`

Immediately after `vtxpos` is computed (~line 244), before the cull-mode
switch:

Includes per-vertex color (Vec4<f24>), the encoded normal quaternion
(quat.x/y/z), and the master `regs.lighting.disable` bit — enough to
answer "is PICA fragment lighting active for this draw, and what's the
vertex color feeding the combiner?" for any composite-draw RE.

```cpp
    if (soh3d_draw_log_active && soh3d_draw_log_path[0]) {
        FILE* f = std::fopen(soh3d_draw_log_path, "a");
        if (f) {
            const auto texs = regs.texturing.GetTextures();
            const auto out = regs.framebuffer.output_merger;
            const u32 t0 = texs[0].enabled ? texs[0].config.GetPhysicalAddress() : 0u;
            const u32 t1 = texs[1].enabled ? texs[1].config.GetPhysicalAddress() : 0u;
            const u32 t2 = texs[2].enabled ? texs[2].config.GetPhysicalAddress() : 0u;
            const auto& blend = out.alpha_blending;
            std::fprintf(f,
                "tri tex0=%08x tex1=%08x tex2=%08x "
                "blendRGB=%d,%d,%d blendA=%d,%d,%d "
                "sxy=(%.1f,%.1f),(%.1f,%.1f),(%.1f,%.1f) "
                "w=%d h=%d "
                "c0=(%.3f,%.3f,%.3f,%.3f) c1=(%.3f,%.3f,%.3f,%.3f) c2=(%.3f,%.3f,%.3f,%.3f) "
                "n0=(%.3f,%.3f,%.3f) lit_dis=%d\n",
                (unsigned)t0, (unsigned)t1, (unsigned)t2,
                (int)blend.factor_source_rgb.Value(),
                (int)blend.factor_dest_rgb.Value(),
                (int)blend.blend_equation_rgb.Value(),
                (int)blend.factor_source_a.Value(),
                (int)blend.factor_dest_a.Value(),
                (int)blend.blend_equation_a.Value(),
                (double)(u16)vtxpos[0].x / 16.0,
                (double)(u16)vtxpos[0].y / 16.0,
                (double)(u16)vtxpos[1].x / 16.0,
                (double)(u16)vtxpos[1].y / 16.0,
                (double)(u16)vtxpos[2].x / 16.0,
                (double)(u16)vtxpos[2].y / 16.0,
                (int)texs[0].config.width, (int)texs[0].config.height,
                (double)v0.color.r().ToFloat32(), (double)v0.color.g().ToFloat32(),
                (double)v0.color.b().ToFloat32(), (double)v0.color.a().ToFloat32(),
                (double)v1.color.r().ToFloat32(), (double)v1.color.g().ToFloat32(),
                (double)v1.color.b().ToFloat32(), (double)v1.color.a().ToFloat32(),
                (double)v2.color.r().ToFloat32(), (double)v2.color.g().ToFloat32(),
                (double)v2.color.b().ToFloat32(), (double)v2.color.a().ToFloat32(),
                (double)v0.quat.x.ToFloat32(),
                (double)v0.quat.y.ToFloat32(),
                (double)v0.quat.z.ToFloat32(),
                (int)regs.lighting.disable.Value());
            std::fclose(f);
        }
    }
```

## How to re-apply

1. Open `Azahar/src/video_core/renderer_software/sw_rasterizer.cpp`.
2. Add `#include <cstdio>` to the includes.
3. Above `RasterizerSoftware::AddTriangle` add the two `extern "C"`
   globals from Hunk 1.
4. Inside `ProcessTriangle`, just after the `vtxpos` initialisation
   and before the `cull_mode` switch, paste Hunk 2.
5. Rebuild: `ninja -C Azahar/build-libretro soh3d_harness`.

## Verification

Run `scratch/title_draw_log.py`. At settled title it should print 18
unique `(tex, w, h)` groups; the three moon quads correspond to
`0x2090ec80` (64×64, ADD), `0x20906a80` (128×128, ALPHA),
`0x20910e80` (64×64, ADD). Address values will drift session-to-session
(they're the runtime FCRAM allocation), but the WxH and blend
signatures are stable.
