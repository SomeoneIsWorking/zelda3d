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
        // oracle_watch_bridge.h + watchhook.cpp for the harness-side receiver.
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
5. Rebuild the harness: `tools/soh3d_harness.py` (which triggers
   `ninja -C Azahar/build-harness soh3d_harness`).

## Related files

- `tools/soh3d_harness/watchhook.cpp` — the harness-side hook receiver
  and per-range ring buffer.
- `tools/soh3d_harness/watch_commands.{h,cpp}` — REPL commands `watch`, `unwatch`,
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
   (`ninja -C Azahar/build-harness citra_core` should show
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
5. Rebuild: `ninja -C Azahar/build-harness soh3d_harness`.

## Verification

Run `scratch/title_draw_log.py`. At settled title it should print 18
unique `(tex, w, h)` groups; the three moon quads correspond to
`0x2090ec80` (64×64, ADD), `0x20906a80` (128×128, ALPHA),
`0x20910e80` (64×64, ADD). Address values will drift session-to-session
(they're the runtime FCRAM allocation), but the WxH and blend
signatures are stable.

# Patch 3 — MemoryFill / DisplayTransfer / VBlank blit log (task #16 atmos RE)

`Azahar/src/video_core/gpu.cpp` — adds an env-gated tap that dumps every
`MemoryFill`, `DisplayTransfer`, and `VBlank` (with LCD framebuffer
config) to a file when `SOH3D_HARNESS_LOG_BLIT=<path>` is set. Used to
locate the visible top-screen scanout source outside the SW-rasterizer
path — see `docs/title_atmos_layer.md`.

### Hunk 1 — includes + statics near top of file

Just after `#include "common/microprofile.h"`:

```cpp
#include <cstdio>
#include <cstdlib>
#include <mutex>
```

Just after the `VADDR_LCD` / `VADDR_GPU` constants:

```cpp
static FILE* g_soh3d_blit_log = nullptr;
static bool g_soh3d_blit_log_tried = false;
static std::mutex g_soh3d_blit_log_mtx;
static u64 g_soh3d_blit_frame_index = 0;

static FILE* Soh3dOpenBlitLog() {
    std::lock_guard<std::mutex> lk(g_soh3d_blit_log_mtx);
    if (!g_soh3d_blit_log_tried) {
        g_soh3d_blit_log_tried = true;
        const char* path = std::getenv("SOH3D_HARNESS_LOG_BLIT");
        if (path && path[0]) {
            g_soh3d_blit_log = std::fopen(path, "w");
            if (g_soh3d_blit_log) {
                std::fprintf(g_soh3d_blit_log,
                             "# soh3d blit log — MemoryFill / DisplayTransfer / VBlank\n");
                std::fflush(g_soh3d_blit_log);
            }
        }
    }
    return g_soh3d_blit_log;
}
```

### Hunk 2 — MemoryFill entry log

Inside `GPU::MemoryFill`, just after the `config.trigger` early-return:

```cpp
if (FILE* fp = Soh3dOpenBlitLog()) {
    std::lock_guard<std::mutex> lk(g_soh3d_blit_log_mtx);
    std::fprintf(fp,
                 "F%llu MemoryFill idx=%u start=0x%08X end=0x%08X value=0x%08X width=%s\n",
                 (unsigned long long)g_soh3d_blit_frame_index, index,
                 config.GetStartAddress(), config.GetEndAddress(), config.value_32bit,
                 config.fill_32bit ? "32" : (config.fill_24bit ? "24" : "16"));
    std::fflush(fp);
}
```

### Hunk 3 — DisplayTransfer entry log

Inside `GPU::MemoryTransfer`, just before `u64 delay{};`:

```cpp
if (FILE* fp = Soh3dOpenBlitLog()) {
    std::lock_guard<std::mutex> lk(g_soh3d_blit_log_mtx);
    const char* kind = config.is_texture_copy ? "TextureCopy" : "DisplayTransfer";
    std::fprintf(
        fp,
        "F%llu %s in=0x%08X out=0x%08X inW=%u inH=%u outW=%u outH=%u ifmt=%u ofmt=%u "
        "flags=0x%08X scale=%u vflip=%u ilin=%u crop=%u tccpy_sz=%u tccpy_iw=%u tccpy_ig=%u "
        "tccpy_ow=%u tccpy_og=%u\n",
        (unsigned long long)g_soh3d_blit_frame_index, kind,
        config.GetPhysicalInputAddress(), config.GetPhysicalOutputAddress(),
        (unsigned)config.input_width.Value(), (unsigned)config.input_height.Value(),
        (unsigned)config.output_width.Value(), (unsigned)config.output_height.Value(),
        (unsigned)config.input_format.Value(), (unsigned)config.output_format.Value(),
        config.flags, (unsigned)config.scaling.Value(),
        (unsigned)config.flip_vertically.Value(), (unsigned)config.input_linear.Value(),
        (unsigned)config.crop_input_lines.Value(),
        config.texture_copy.size, (unsigned)config.texture_copy.input_width.Value(),
        (unsigned)config.texture_copy.input_gap.Value(),
        (unsigned)config.texture_copy.output_width.Value(),
        (unsigned)config.texture_copy.output_gap.Value());
    std::fflush(fp);
}
```

## Patch 4 — inline memlog for narrow-VA writer PC surfacing (title-cs RE, 2026-07-04)

`Azahar/src/core/memory.cpp` — an ALWAYS-ON inline logger placed
directly in the guest write path (both fast path + all page-type
cases), keyed to a small set of target VAs supplied via env vars.
Bypasses the page-granular MemoryWatchpoint mechanism (which nulls
the fast-path pointer for the whole page, adding overhead to every
write, AND misses RasterizerCachedMemory writes unless the page has
been RegisterWatchpoint'd separately).

Trigger via env:
- `SOH3D_MEMLOG_VAS=0x08721a1a` — comma-separated hex list of vaddrs
  to log (byte-granular, matches any sizeof(T) write that overlaps).
  Up to 16 vaddrs.
- `SOH3D_MEMLOG_PATH=/tmp/memlog.out` — output path; default stderr.

On each qualifying write the logger prints one line:
```
MW pc=0x<pc> lr=0x<lr> va=0x<vaddr> sz=<sizeof(T)> data=0x<data> \
   r0=... r1=... r2=... r3=... r4=... r4p8=... r4p10=... r4p14=... sr4=... \
   sr4p0=... sr4p4=... sr4p5c=... sr4p6c=... sr4t14=... sr4t20=... sr4t24=... sp=...
```

### The patch

Two hunks in `Azahar/src/core/memory.cpp`.

**Hunk A — near the top of the file, just after the
`Soh3d_OnMemoryWrite` forward decl:** the `Soh3dMemLog<T>()` template +
`Soh3dMemLogInit()` static + env-parsed target-VA array. See the
current source in-tree for the exact block (starts with
`// Inline, per-VA write logger for RE.`).

**Hunk B — in `MemorySystem::Write<T>`:** call `Soh3dMemLog(vaddr, data)`
right after each `std::memcpy` store site:

- Fast path (line ~765, guarded by `if (page_pointer)`).
- `case PageType::MemoryWatchpoint` after the memcpy.
- `case PageType::RasterizerCachedMemory` after the memcpy.
- `case PageType::RasterizerCachedMemoryWatchpoint` after the memcpy.

Each site guarded by `if (g_soh3d_memlog_n_vas > 0)` so the branch is
predicted-not-taken when the env var isn't set — near-zero fast-path
overhead in production.

### Why an inline logger instead of the watchhook

The existing watchhook only fires on `PageType::MemoryWatchpoint`, and
only after `RegisterWatchpoint` marks the enclosing page. That has
two problems for narrow-target RE:

1. Page-granular fast-path takedown affects performance of every
   write on the watched page (envCtx sits in play struct at
   0x08720000-region, HOT page).
2. Writes on RasterizerCachedMemory pages don't reach the hook at all.

The inline logger has ZERO effect on unrelated writes (guard =
`g_soh3d_memlog_n_vas > 0`) and reaches every write on every page
type once the target VA hits.

## Watchhook — 64-word stack window (boot-dispatch RE, task #16)

`tools/soh3d_harness/watchhook.cpp` records 64 stack words at write-fire
time to walk the return-address chain multiple frames deep. Required by
the boot-dispatch RE arc (docs/boot_dispatch_thread.md) — the writer's
enclosing fn is 4-6 frames below the top-level "opening/title mode
Main", and a shallower window doesn't reach it. If Azahar is re-cloned,
the watchhook's `WriteRecord::stack_words[64]` field + the per-fire loop
that fills it via `Memory::Read32` are self-contained in the harness
tree (no Azahar patch needed for it — just rebuild the harness). The
mirror `WatchRecord::stack_words[64]` in `main.cpp` + the `hits` REPL
printer must stay in sync.

### Hunk 4 — VBlank entry log (LCD FB config)

Inside `GPU::VBlankCallback`, at the very top before `SwapBuffers`:

```cpp
if (FILE* fp = Soh3dOpenBlitLog()) {
    std::lock_guard<std::mutex> lk(g_soh3d_blit_log_mtx);
    auto& top = impl->pica.regs.framebuffer_config[0];
    auto& bot = impl->pica.regs.framebuffer_config[1];
    std::fprintf(fp,
                 "F%llu VBlank topFB1=0x%08X topFB2=0x%08X topStride=%u topW=%u topH=%u "
                 "topFmt=%u topActive=%u botFB1=0x%08X botFB2=0x%08X botStride=%u botFmt=%u "
                 "botActive=%u\n",
                 (unsigned long long)g_soh3d_blit_frame_index,
                 top.address_left1, top.address_left2, top.stride,
                 (unsigned)top.width.Value(), (unsigned)top.height.Value(),
                 (unsigned)top.color_format.Value(),
                 (unsigned)top.second_fb_active.Value(),
                 bot.address_left1, bot.address_left2, bot.stride,
                 (unsigned)bot.color_format.Value(),
                 (unsigned)bot.second_fb_active.Value());
    std::fflush(fp);
    g_soh3d_blit_frame_index++;
}
```

# Patch 5 — per-draw vertex-shader uniform log (title sheen RE, 2026-07-10)

`Azahar/src/video_core/pica/pica_core.cpp` — logs the decoded VS uniform
state relevant to CmbVShader lighting at every draw trigger, so the
harness can read back EXACTLY what the game wrote into
`LightDir0..2 / LightDiffuseColor0..2 / LightAmbientColor0..2` (c80–c88),
`MatDiffuseColor/MatAmbientColor` (c8/c9), `uModelView` (c4-c7),
`TexMtx0`/`TexMtx1` (c10-c16), `TexMappingMethod` (c92), and the `HasColor /
IsVertexLighting / IsFragmentLighting` bools (b5/b9/b10) for a given
frame's draws. This is how the wordmark light-env slot-color ROLES were
pinned (oot3d-decomp `docs/title_logo_actor.md` §6.6): the slot's first
color (WHITE) lands in LightDiffuseColor0 and its second (0.18) in
LightAmbientColor0 — the reverse of the doc's earlier guess.

## The patch

Two hunks in `Azahar/src/video_core/pica/pica_core.cpp`.

### Hunk 1 — globals just above `namespace Pica {`

```cpp
extern "C" char soh3d_vsuni_log_path[256] = "";
extern "C" int soh3d_vsuni_log_active = 0;

#include <cstdio>
```

### Hunk 2 — inside `WriteInternalReg`, at the top of the
`case PICA_REG_INDEX(pipeline.trigger_draw):` block (before `DrawArrays`)

Appends one line per draw:
`draw idx=<indexed> hasCol=<b5> vLit=<b9> fLit=<b10> matDif=(...) matAmb=(...)
dir0=(...) dif0=(...) amb0=(...) dir1=... dif1=... amb1=... dir2=... dif2=...
amb2=... vtxScl0=(...) modelView0=(...) ... texMappingMethod=(...)` — reading
`vs_setup.uniforms.f[4..16,80..92]` via
`.ToFloat32()` and `uniforms.b[5,9,10]`. See the in-tree copy for the exact
block (search `soh3d_vsuni_log_active`).

## Harness side (committed in this repo)

- `tools/soh3d_harness/main.cpp`: extern decls + REPL command
  `vsuni_log <path>` / `vsuni_log off` (same shape as `draw_log`).

## Verification signature

From `scratch/title_settled.state` + `run 1000` (title cs588, sheen t=1),
`vsuni_log` over one frame contains wordmark draws with
`dir0=(0.57735,-0.57735,-0.57735,1) dif0=(1,1,1,1) amb0=(0.18,0.18,0.18,1)
matAmb=(1,1,1,0) hasCol=0` and terrain draws showing the SAME scene
ambient duplicated into amb0 AND amb1 (the ×2 terrain mechanism of
oot3d-decomp `title_env_lighting.md` §10).

# Azahar patch: per-pixel TEV dump (`PIXEL` lines) + `SOH3D_PIXEL_TEX` selector

`sw_rasterizer.cpp` also carries a per-PIXEL dump inside `ProcessTriangle`'s
shading loop (just after `WriteTevConfig`): when `draw_log` is active AND the
draw's tex0 physical address matches a target, it appends up to 200 lines
per target per activation:

```
PIXEL tex0=<pa> xy=(x,y) texcol=(r,g,b,a) tex1col=(r,g,b,a) primary=(r,g,b,a) combined=(r,g,b,a)
```

`primary` is the interpolated PRIMARY_COLOR the TEV actually consumed (the
per-triangle `tri ... c0=/c1=/c2=` vertex-color fields read ZERO for every
CmbVShader draw — do NOT trust them; the PIXEL dump is the reliable probe),
`combined` the TEV output before blending. Targets: three hardcoded moon/fire
addresses (`0x20906a80/0x2090ec80/0x20910e80`, task #16 era) plus ONE generic
runtime-selected address via env `SOH3D_PIXEL_TEX=<hex physaddr>` (added
2026-07-10 for the cloud-vortex RE; see oot3d-decomp `title_cloud_vortex.md`).
Search `SOH3D_PIXEL_TEX` in the in-tree copy for the exact block.

The same block also logs `tex1col`, the sample returned for texture unit 1 at the fragment. This
field is useful when a multi-stage material's TEX0 and PRIMARY probes agree but its secondary
coordinator remains suspect; it is a diagnostic addition and does not alter TEV execution.

# Azahar Patch 6 (2026-07-10, dawn-hue fog RE): `az_fog` + per-draw fog fields + pixel probes

Four blocks, all in the gitignored Azahar tree — re-apply after a fresh clone
(the harness-side `az_fog` command in `tools/soh3d_harness/main.cpp` is committed
and will fail to link without block 1).

## 1. `src/video_core/pica/pica_core.cpp` — `soh3d_fog_dump`

- Near the existing vsuni globals: `static void* soh3d_picacore = nullptr;`
- First line of `PicaCore::PicaCore(...)` body: `soh3d_picacore = this;`
- After the closing `} // namespace Pica`: an `extern "C" int soh3d_fog_dump(char* out,
  int cap)` that prints `mode/flip/color` from `regs.internal.texturing.fog_*`,
  `depthScale/depthOffset/wbuffer` from `regs.internal.rasterizer.viewport_depth_*`
  / `depthmap_enable`, then all 128 `fog.lut[i]` entries as `value/diff` float pairs
  (`ToFloat()/DiffToFloat()`). **Dump BOTH fields** — at title the entire fog action
  lives in entry 127's difference field (−0.7); value-only reads as "no fog".
  Consumed by harness REPL `az_fog` (terminator `ok end`).
  CAVEAT: registers are per-draw command-list state — an end-of-frame dump reflects
  the LAST draw (usually UI, fog off). For per-draw truth use block 2.

## 2. `src/video_core/pica/pica_core.cpp` — vsuni_log extensions

Inside the existing Patch-5 vsuni_log block (trigger_draw):
- names/regs arrays extended with `proj0..proj3` = float uniforms c0..c3 (uProjection —
  recovers the live projection for depth↔distance conversion).
- After the uniform loop, one extra fragment per draw line:
  `fog=<mode>/<flip>(r,g,b) lutS=(lut16,lut48,lut96,lut127 values)` read from
  `regs.internal.texturing` + `fog.lut`.

## 3. `src/video_core/renderer_software/sw_rasterizer.cpp` — `SOH3D_PIXEL_UNTEX`

In the Patch-5 PIXEL block's target check: env `SOH3D_PIXEL_UNTEX=1` also selects
draws with tex0 DISABLED (untextured, e.g. the title horizon fill / dawn glow),
skipping fragments whose combiner RGB is all zero (the black letterbox quads would
otherwise eat the 200-line cap).

## 4. `src/video_core/renderer_software/sw_rasterizer.cpp` — `SOH3D_PIXEL_XY`

Same site: env `SOH3D_PIXEL_XY=<x>,<y>` dumps EVERY draw's fragment landing on that
one framebuffer pixel (pre depth-test), as
`PIXELXY cbuf=<pa> tex0=<pa> xy=(x,y) texcol=... primary=... combined=... blend=<src>,<dst>
depth=<f>` — the full compositing stack at one coordinate. Coordinate space is the
480x400 3D FB (NOT the final 400x240 image): image_x = fb_y, image_y ≈ 240 − fb_x/2;
the display pass at (x,y) samples the 3D FB at (2x, y).

The software-rasterizer records now also carry `draw=<n>`, using the same identity as the
Patch-5 `vsuni_log` records. `pica_core.cpp` emits `vsuni_log` before incrementing the counter,
then increments immediately before `DrawArrays`; the rasterizer therefore records
`soh3d_draw_index - 1`. This joins a fragment's rasterized footprint to the exact material and
uniform state. Texture address alone is not a reliable identity because multiple materials can
share a texture. The counter remains stable until the software rasterizer's scanline workers
finish.

`SOH3D_PIXEL_DRAW=<n>` selects one exact `vsuni_log` draw and emits every generated fragment as
`PIXEL draw=<n> ...`; unlike repeated `SOH3D_PIXEL_XY` guesses, one capture yields the draw's full
raster footprint plus texture samples, PRIMARY, combiner output, and depth. The generic four-million
line diagnostic cap applies, and the path is inert when the variable is absent.

Verification signature: at title `run 1000` (dayTime 0x3197), `az_fog` prints
`color=(0,0,0)` end-of-frame but per-draw `vsuni_log` shows 51 draws `fog=5/0(56,42,40)`,
and the LUT dump has `lut[127] = 0.9819/-0.7338` (75.2% max fog). See oot3d-decomp
`title_env_lighting.md` §12.

---

# Azahar patch — libretro logging never honored the global filter

## Where

`src/common/logging/backend.cpp`, `Impl::Initialize(retro_log_printf_t)` (the
`#ifdef HAVE_LIBRETRO` overload).

## The bug

`FmtLogMessageImpl` only consults the global filter when `logging_initialized`
is true:

```cpp
if (logging_initialized) {
    if (!Impl::Instance().GetFilter().CheckMessage(log_class, log_level)) return;
    Impl::Instance().PushEntry(...);
} else {
    // pre-init: write straight to stderr, UNFILTERED
    PrintMessage(...);
}
```

The file-based `Initialize(std::string_view)` sets `logging_initialized = true`,
but the libretro `Initialize(retro_log_printf_t callback)` overload never did.
So in the harness (which always takes the libretro path) EVERY log message hit
the `else` branch and printed to stderr unfiltered — `SetGlobalFilter()` was a
complete no-op. Per-frame `LOG_DEBUG` spam (`Audio.DSP mixers remaining_dirty`,
`Render.Vulkan`, ...) — thousands of synchronous stderr writes per second —
flooded the harness and throttled the frame loop.

## The patch

Add `logging_initialized = true;` in the libretro `Initialize` overload, right
after the `instance` is constructed (mirroring the file-based overload). Then
the harness's `Common::Log::SetGlobalFilter(Filter(Level::Warning))` (called in
`main()` after `retro_init`) takes effect and the debug flood stops.

# Azahar Patch 7 (2026-07-22, per-draw light-setup RE): draw ISOLATION + draw identity

`pica_core.cpp`, in the same `trigger_draw` case as Patch 5/6.

Two globals:

```cpp
extern "C" int soh3d_draw_index = 0;   // per-frame draw counter (harness resets it per retro_run)
extern "C" int soh3d_draw_skip  = -1;  // -1 = none; otherwise suppress that draw's DrawArrays
```

and at the end of the `trigger_draw` case:

```cpp
if (soh3d_draw_skip >= 0 && soh3d_draw_index == soh3d_draw_skip) { ++soh3d_draw_index; break; }
++soh3d_draw_index;
DrawArrays(is_indexed);
```

Harness: `main.cpp` resets `soh3d_draw_index = 0` before every `retro_run()` in `HandleRun`, and
adds the REPL command `drawskip <n>|off`.

# Azahar Patch 10 (2026-08-30, cache-owned fragment-lighting state)

`pica_core.cpp`, in the same `trigger_draw` case as Patches 5–7, adds the one-shot globals
`soh3d_lighting_capture_path[256]` and `soh3d_lighting_capture_draw`. The harness command
`lighting_capture <draw> <path>` selects one draw after a lightweight `vsuni_log` discovery frame.
When that draw triggers, Azahar writes one JSON object containing:

- raw `config0`, `config1`, global ambient, LUT input/absolute/scale, and light-slot mapping words;
- all eight raw PICA light records, while `max_light_index` plus `slot_mapping` identify the active
  subset and order;
- only the 256-entry lighting LUT tables activated by that configuration, including per-light spot
  and distance attenuation tables when enabled.

The latch clears after one attempt, so later frames cannot silently overwrite the capture. The
committed owner `tools/cmb_fragment_lighting_oracle_probe.py` checks `OracleCache` before spawning,
uses one process for discovery plus capture on a miss, selects and validates a draw through `picaLit=1`
from the authoritative `regs.lighting.disable` register (not the independent CmbVShader `fLit`
boolean), and stores raw artifacts and any structured probe under the complete
savestate/ROM/patch/texture-pack identity. Failed discovery logs are cached too; changing scenes
without retaining that falsifier is forbidden. The current `kokiri-save-overlay` fixture is a
bounded PICA-disabled negative control, not a claim that it reaches Navi or the pause-menu Link model.

**Why**: the Patch-5 uniform log says WHAT lighting state a draw used but not WHICH surface it
painted. Skipping one draw and diffing the frame against the unmodified one yields that draw's
exact screen footprint — the oracle-side draw→material mapping. Driver: `tools/oracle_draw_isolate.py`.

The Patch-5 log line also gained draw IDENTITY, which is what ties an oracle draw to one of our
CMB material groups: `tex0=<paddr>/<w>x<h>/f<fmt> en=<n> nv=<vertexCount>`,
`tev0=src.../mod.../op<c>-<a>/sc<rgb>x<a>/k<const>`, `texEn=<t0>/<t1>/<t2>` and
`tev1..5=<sources>:<colorOp>:<scale>,...`. `nv` alone matched our 21 Zora room groups 1:1 to the
oracle's room draws by vertex count.

## Two protocol gotchas (both cost a full debug cycle; do not re-derive)

1. **A single `retro_run()` after `loadstate` renders a CORRUPT frame** — the Vulkan HW renderer's
   caches/framebuffers are not in the save state, so the first frame is garbage tile blocks. From
   ≥3 frames the output is *bit-exact* reproducible across loadstates (measured: 0 differing pixels).
2. **OoT3D draws one 3D frame per TWO `retro_run` calls** (30 fps logic on the 60 Hz libretro
   cadence) and the captured framebuffer trails the emulated GPU by ~2 frames. `drawskip` therefore
   has *zero* visible effect if you only run 1 or 2 frames with the latch set; it bites at 3+.
   `oracle_draw_isolate.py` uses warm=2, probe=4.

# Azahar Patch 8 (2026-07-22, hi-res texture parity): CustomTexManager stats accessor

## Why

The harness now runs the **same hi-res texture pack on BOTH sides** so an oracle-vs-Zelda3D A/B is
like-for-like (`ZELDA3D_HARNESS_TEXPACK=on|off`, see `main.cpp`'s `SetupTexPack`). "Hi-res is
actually in effect on both sides" must be a *measurement*, not an assumption — our side already
counts hits via `Zelda3D::TexPackGetStats()` (committed, `Shipwright/cmb3d/asset/texpack.h`), so
Azahar needs the same read-only counter. Both are printed by the `texpack` REPL command.

Read-only instrumentation: no behaviour change, no core logic touched.

## The patch

Two hunks, both in `src/video_core/custom_textures/`.

### Hunk 1 — `custom_tex_manager.h`, public section (after `UseNewHash()`)

```cpp
    /// soh3d_harness parity instrumentation (tools/soh3d_harness/AZAHAR_PATCH.md
    /// Patch 8). Read-only: lets the harness PROVE the hi-res pack is actually
    /// in effect on the oracle side instead of assuming it.
    struct Stats {
        std::size_t files;     ///< custom texture files parsed out of the pack
        std::size_t materials; ///< distinct 3DS texture hashes covered
        u64 hits;              ///< lookups that found a replacement
        u64 misses;            ///< lookups with no replacement in the pack
    };
    Stats GetStats() const noexcept {
        return Stats{custom_textures.size(), material_map.size(), lookup_hits, lookup_misses};
    }
```

and in the private data section, after `bool use_new_hash{true};`:

```cpp
    // soh3d_harness parity instrumentation (AZAHAR_PATCH.md Patch 8).
    mutable u64 lookup_hits{0};
    mutable u64 lookup_misses{0};
```

### Hunk 2 — `custom_tex_manager.cpp`, `CustomTexManager::GetMaterial`

```cpp
    const auto it = material_map.find(data_hash);
    if (it == material_map.end()) {
        lookup_misses++; // soh3d_harness instrumentation (AZAHAR_PATCH.md Patch 8)
        LOG_WARNING(Render, "Unable to find replacement for surface with hash {:016X}", data_hash);
        return nullptr;
    }
    lookup_hits++; // soh3d_harness instrumentation (AZAHAR_PATCH.md Patch 8)
    return it->second.get();
```

## No other Azahar change is needed

Everything else is harness-side and committed:

* the pack is **not** copied into Azahar's user dir — `SetupTexPack()` creates a *symlink*
  `<save_dir>/Azahar/load/textures -> <repo>/textures`, which is exactly where
  `CustomTexManager::GetTextures()` looks (`{UserPath::LoadDir}textures/{TITLE_ID}/`);
* `custom_textures` is answered through `RETRO_ENVIRONMENT_GET_VARIABLE`
  (`citra_custom_textures`), because `retro_load_game -> UpdateSettings -> ParseCoreOptions`
  re-reads it and would clobber a direct `Settings::values` assignment.

## Two findings (do not re-derive)

1. **`Settings::values.async_custom_loading = false` CRASHES this Azahar.** The synchronous
   `CustomTexManager::Decode` path calls `upload()` inline from inside the rasterizer's surface
   fill, which re-enters `MemorySystem::RasterizerMarkRegionCached` on an already-cached page and
   aborts on `core/memory.cpp:1130: Unreachable code!` (100% reproducible ~3.5 s into a Kokiri
   warp). Leave `async_custom_loading` at Azahar's default (`true`). Consequence: replacements
   land at ≤8 uploads per rendered frame after a scene load, so step until the `texpack` hit
   counter stops growing before capturing a frame.
2. **Azahar's user dirs must be ABSOLUTE.** `g_system_dir`/`g_save_dir` used to be the relative
   `scratch/harness/{system,save}`; `SohBootInternal()` then `chdir()`s into
   `scratch/harness/soh_cwd`, so every Azahar file access issued after `soh_boot` resolved against
   the wrong cwd. Symptom was `Render <Critical> ... Failed to open custom texture:
   scratch/harness/save/Azahar/load/textures/...`, followed by a zero-extent `VkImage` and
   `vk_texture_runtime.cpp:217: Unreachable code!`. Fixed harness-side in `main()`.

## Verification signature

```
harness: texpack: ON on BOTH sides — root <repo>/textures (az load dir <repo>/scratch/harness/save/Azahar/load)
> texpack
ok texpack mode=on root=<repo>/textures az=2148/2143 az_hits=74/65 soh=2143 soh_hits=67/54
```

Kokiri Forest (entrance 0xEE, dayTime 0x6000, both engines at `sceneNum=0x0055`), hi-res vs
vanilla at a matched schedule: oracle 98.3 % of pixels differ (mean |Δ| 13.8), Zelda3D 94.8 %
(mean |Δ| 7.8) — the pack is demonstrably in effect on both. The az-vs-soh delta is *not* made
worse by turning it on (mean |Δ| 37.98 both-hi-res vs 39.64 both-vanilla; cameras unmatched, so
this is a sanity check, not a parity number).

# Azahar Patch 9 (2026-07-22, per-fragment TEV over a WHOLE draw): PIXEL cap + depth

Two one-line changes in the existing Patch-5 `PIXEL` block
(`src/video_core/renderer_software/sw_rasterizer.cpp`), plus the harness-side
`SOH3D_HARNESS_SW=1` switch in `tools/soh3d_harness/main.cpp` (committed) that
forces `g_use_vulkan = false` — the `PIXEL`/`PIXELXY` probes exist only in the
SOFTWARE rasterizer, so reading the oracle's real per-fragment
texcol / PRIMARY_COLOR / combiner output requires that renderer.

1. The 200-line cap now applies only to the hardcoded moon/fire addresses; the
   generic `SOH3D_PIXEL_TEX` target gets `fc_cap = 4000000`, so a whole draw can
   be dumped. 200 samples land on two scanlines and are not representative of a
   surface — the Kokiri near-terrain RE needed the mean texcol/PRIMARY over the
   draw.
2. Each `PIXEL` line carries `depth=%.6f` (the same `depth` `PIXELXY` already
   printed), so a reader can keep the NEAREST fragment per pixel instead of the
   last-rasterized one.

```
PIXEL tex0=<pa> xy=(x,y) depth=<f> texcol=(r,g,b,a) primary=(r,g,b,a) combined=(r,g,b,a)
```

Verification signature: Kokiri `scratch/drawiso/kokiri/probe.state`, `SOH3D_PIXEL_TEX=180e3600`,
`run 6` then `draw_log` + `run 2` yields ~113k `PIXEL` lines for the near-terrain draw, whose
`combined` mean over d8's exclusive-pixel region lands within 0.4% of the captured frame's mean
there. CAVEAT (unsolved): the framebuffer x/y in these lines could not be mapped per-pixel onto
the captured image — the depth gradient fixes the orientation (`np.rot90(a, 1)`, far at the top)
but every candidate transform correlates at only ~0.10, so treat the dump as a POPULATION of the
draw's fragments, not as an image.

# Azahar Patch 11 (2026-08-31, guest-PC watch for vtable blind spots)

`src/core/arm/dynarmic/arm_dynarmic.cpp` turns the exact armed guest VA into a one-shot JIT
translation breakpoint. Arming invalidates that target block. Dynarmic's code reader substitutes
an ARM or Thumb `BKPT` only while the weak `soh3d_pc_watch_target` is nonzero; the breakpoint
callback records through weak `Soh3d_OnGuestPc`, disarms, invalidates the synthetic block, and
returns to the unchanged original instruction. Boot, settling, and every untargeted block remain on
the normal JIT path. The harness owns both symbols and exposes `pcwatch`, `pchits`, and `pcclear`;
ordinary Azahar builds link neither symbol and retain only the null weak-symbol check during block
translation.

This closes the static-xref blind spot for functions reached only through heap-resident vtables.
OoT3D `FUN_003fa5d0`, the candidate fixed-function fragment-lighting material method, was the first
watch target. The cache-owned probe subsequently watched its preceding `FUN_003f9b5c` material-setup
slot too; the separately keyed 99-draw Save-overlay capture reached neither address. That rules the
candidate renderer out for this fixture before choosing a new retail observation. Each watch records
the PC, LR, r0-r3, SP, and cycle tick without changing untargeted execution.

# Azahar Patch 12 (2026-08-31, PICA-light logger positive control)

The `lighting_selftest <draw>|off` harness command arms one selected PICA draw. At that draw only,
`pica_core.cpp` temporarily clears `regs.internal.lighting.disable`, emits the normal `vsuni_log`
line, runs the draw, and restores the original bit before the next draw. The cache-owned fragment
lighting probe requires this log to contain `picaLit=1` before it accepts a retail `picaLit=0`
observation. The synthetic draw is solely an instrument validation: it is never captured as retail
lighting state or used for host parity.

# Azahar Patch 13 (2026-08-31, PICA command-list provenance)

Every Patch-5 `vsuni_log` line now includes `cmdList=<physical-address>/<word-index>/<word-count>`.
`PicaCore::cmd_list` owns these values while processing a command list, so the field identifies the
exact PICA packet stream that produced a logged draw without exposing a guessed CPU renderer function.
The cache-owned `tools/pica_command_provenance_oracle_probe.py` persists both that draw log and the raw
physical command list. `tools/pica_command_writer_oracle_probe.py` first reuses that cached provenance
(or captures it without memory logging), decodes the selected register packet offline, then launches a
trace harness with the direct `SOH3D_MEMLOG_RANGES` logger armed for only that packet's four-byte guest
writer address. It rejects a traced frame whose command-list provenance or selected packet differs from
discovery. This avoids both the page-watch stale-buffer problem and an arena-wide write log when PICA
storage rotates per frame.

The cache persists only the exact selected writer records, not an arena-wide raw memory log. A source
trace or exact state-field watch may arm one additional bounded range; it stores only the matching
records and the selected list record. The write record includes `r7`--`r10` as well as `r0`--`r4`.
The final list writer identifies its exact staging packet word; a subsequent exact watch of only
that word can identify the `FUN_00371758` copy loop, whose second store uses the four loaded source
values to recover the template word and refuses an ambiguous match. Command-list backing allocation
can rotate between harness processes, so this source-watch path requires the same draw, list length,
packet index, and packet value but does not pretend the physical address is a stable identity. This
keeps the oracle cache reusable without turning repeated RE captures into multi-gigabyte scratch
accumulation. A generic exact watch always preserves its selected records and reports the number that
are the copy loop; the cache retains only records matching the selected PICA value and records both
the total write count and matching count, so unrelated overwrites cannot inflate an RE artifact or be
misclassified as another copy.

Command-list storage may rotate before the next frame. A writer probe must verify that its watched
physical list is reused before associating any hit with a draw; a stale linear alias is a bounded cached
negative, not a renderer identity. The Hut fragment-lighting fixture first demonstrated this rotation.

# Azahar Patch 14 (2026-08-31, synchronous configuration-builder input)

The Patch-13 memory logger now appends `r10b` and a compact set of `r10b+offset` words to each selected
write record. At the recovered direct template store `0x0040cfe4` in `FUN_0040cdd8`, `r10b = r10 -
0x100` is that function's input object. The words cover its independent eight-element loop
(`+0x164..+0x17c`) and every byte source used for output word 6 (`+0x184..+0x190`).

This is explicitly synchronous: transient renderer input objects may be cleared or reused by the end
of a frame, so a post-run memory dump is not evidence of the state which produced a template word.
`pica_command_writer_oracle_probe.py` accepts these fields only at the exact store PC, verifies the
derived base, rejects divergent inputs, and persists the decoded state beside the already-selected
compact memory records. Its optional second exact state-address watch preserves the bounded writes
that construct that input, rather than a post-frame dump. It does not infer an object type or
CMB-field mapping from either snapshot.

## Render-contract cache marker

`AZAHAR_RENDER_CONTRACT` is the oracle cache discriminator. Change its sole marker only when a
patch can change emulated or rendered output; observer-only logging, capture decoding, and this
document do not invalidate a cache. The initial marker deliberately retains the existing `p45` cache
context. Tests require marker changes to rotate the key and documentation-only changes not to.

For the remaining producer boundary, `SOH3D_HARNESS_DISABLE_FASTMEM=1` leaves Dynarmic's page-table
fast path unset for an oracle-only process. This routes guest stores through the normal callbacks, while
normal Azahar and harness launches retain fast memory. The Hut command packet still has no page-watch
record in this mode, proving that its producer is outside both Dynarmic store paths.
Its selected `config0` packet also has no matching `MemorySystem::Write` record under the direct logger,
so the active list is populated by another write path; recover that path before assigning a guest writer.

The exact Hut `config0` write is now observed in interpreter mode at guest PC `0x00466e60`,
with `lr=0x00466e20`. Static ARM disassembly identifies its enclosing function as the packet-copy
loop at `0x00466e0c`: it obtains an output cursor, copies pairs of words from a prepared 24-byte
packet block, and releases the output reservation. Its sole direct caller is the virtual material-pass
dispatcher at `0x004527e8`, after that dispatcher has called its three pass setup slots. The logger
captures both levels at the exact store. The copy loop’s `r4` is its packet descriptor, so `r4p8`,
`r4p10`, and `r4p14` record its source pointer, byte count, and block index. Its prologue saves the
caller’s dispatcher `r4` at `sp+4`; `sr4` and its table/context/packet/visibility fields
(`sr4p0`, `sr4p4`, `sr4p5c`, `sr4p6c`) plus its three virtual setup slots
(`sr4t14`, `sr4t20`, `sr4t24`) identify the owning material pass. Reading these after the frame
completes is invalid because the dispatcher can reset. Capturing them at the store records the
producer ownership rather than guessing a shader from a copied PICA value.

# Azahar Patch 14 (2026-08-31, GSP command-list submitter provenance)

`src/video_core/gpu.cpp` adds a second opt-in, observer-only trace. When
`SOH3D_HARNESS_LOG_GSP_SUBMIT=<path>` is set, both `CommandId::SubmitCmdList`
and direct GPU-MMIO command-buffer triggers record the guest CPU state that submits each list, plus
its resolved physical command-list address and byte size:

```
CMDSUBMIT source=GSP|MMIO pc=0x... lr=0x... listVa=0x... listPa=0x... size=... mmio=0x... r0=0x... r1=0x... r2=0x... r3=0x... sp=0x... s0=0x... ... s16=0x...
```

This is deliberately at the last submission boundary before `GPU::SubmitCmdList` consumes the supplied
list without copying it, so page watching its rotating backing allocation cannot identify its producer.
A probe must join a record to the PICA draw by both exact physical command-list address *and byte size*;
the backing address is reused with several list lengths during one frame. The cached Hut capture joins
`0x2058FA80 / 69648` to the GSP submitter at PC `0x004A0814`, LR `0x002C1970`; this identifies the
guest GSP request context, not yet the routine that populated the list. Never infer provenance from
list order or from an address captured in another frame. Normal Azahar and harness launches do not set
this environment variable and take no logging path.

The 17 `sN` values are non-faulting reads of the guest stack at the submission SVC. They are needed
because the captured `lr` can be a generic service-return stub. A nonzero stack word is not by itself a
return address: validate it against the active ARM callback frame before extending the material-state
RE. They are diagnostic state only and do not alter guest memory or command processing.

# Azahar Patch 15 (2026-08-31, direct guest-pointer provenance)

`src/core/memory.cpp` adds an opt-in trace for direct host-pointer access to a selected guest virtual
range. Set both `SOH3D_PTRLOG_RANGE=<start>:<end>` and `SOH3D_PTRLOG_PATH=<path>` to receive one
record per `MemorySystem::GetPointer` acquisition in that half-open range:

```
PTR pc=0x... lr=0x... va=0x... r0=0x... r1=0x... r2=0x... r3=0x... sp=0x...
```

This complements, rather than replaces, the write logger: the Hut command-list arena has no observed
`MemorySystem::Write` producer even with Dynarmic fast memory disabled. The trace is range-limited,
off unless explicitly enabled, and records no data or mutations. A positive only proves a caller
acquired the pointer; correlate it with the exact same-run PICA list address before treating it as a
candidate list-construction path.

# Azahar Patch 16 (2026-08-31, bulk guest-write provenance)

The existing `SOH3D_MEMLOG_*` range logger now records `MemorySystem::WriteBlockImpl` page chunks before
their `memcpy`, using `MB` records with PC/LR, virtual range start, byte count, argument registers, and
SP. This covers host bulk copies and `CopyBlock`'s destination path, which do not route through templated
`MemorySystem::Write<T>`. The probe joins only records overlapping the exact PICA list interval. The
trace is disabled unless the existing `SOH3D_MEMLOG_RANGES` setting is present and remains observer-only.

The harness REPL exposes `memlogselftest <va>`, which reads 16 bytes and writes those unchanged bytes
back through the production `MemorySystem::WriteBlock` path. It is a reversible positive control for
the `MB` logger, not a renderer or game-state override. The command refuses an unmapped range and emits
its exact VA and byte count; the submitter probe captures this self-test only after preserving the
unmodified production log, so its proof cannot be mistaken for a command-list producer.

# Azahar Patch 17 (2026-08-31, synchronous CMB descriptor snapshot)

`src/core/memory.cpp` now appends seven `r1+offset` words to each selected memory-write record. At
the recovered binder store `PC=0x004c6374` in `FUN_004c6364`, `r1` is the live nested CMB material
descriptor. The offsets `+0x10..+0x28` cover every field this binder reads. The observer is bounded
by the existing exact state-address watch and does not alter emulation or render output.

`pica_command_writer_oracle_probe.py` decodes those words only for that exact PC, requires every
field, and rejects multiple distinct descriptors. Its state-watch trace version is 2, so this single
new observation has a distinct cache key while all prior provenance and render observations remain
cache hits. `AZAHAR_RENDER_CONTRACT` remains unchanged because this is capture-only instrumentation.

# Azahar Patch 18 (2026-08-31, serializable HLE request wakeup callback)

`src/core/hle/kernel/thread.cpp` now serializes `wakeup_callback` exactly once.
A cold-booted title reached a live cutscene counter (`0 -> 25 -> 85`) but
aborted during state serialization because `Thread::serialize` first respected
`SupportsSerialization()` and then unconditionally serialized the callback a
second time. That second write reached an intentionally non-serializable async
HLE request callback and made `title_settle.py` unable to regenerate the
deterministic title fixture.

This corrects save-state serialization only. It does not observe, alter, or
render guest state, so `AZAHAR_RENDER_CONTRACT` remains unchanged.
