---
id: 10
title: OoT-after-MM crashes in ImGui::NewFrame -> SetCurrentFont (regression from restoring real ImGui)
status: resolved
symptom: `tools/zelda3d_sequence.sh mm,oot` runs MM, returns 0, attaches OoT with all four per-game subsystems fresh -- then OoT dies on its first drawn frame in ImGui::SetCurrentFont, called from ImGui::NewFrame <- Ship::Gui::StartDraw.
tags: n3,imgui,launcher,regression
created: 2026-08-06
updated: 2026-08-06
---

## Status

**A regression introduced by restoring real Dear ImGui.** Before that change the sequence gate exited
0 with no crashes. Both SOLO gates still pass (`solo oot` EXIT=0 / 122 live lines, `solo mm` EXIT=0),
so this is specific to a second game attaching.

## What is established

- `Gui::Init()` runs **once**, for MM only. Verified by log: `"ImGui SDL3-GPU renderer backend up on
  the Fast3D device"` appears exactly once, and the `"reusing the existing ImGui context"` line added
  as a guard for the second-init theory **never fires**. So the crash is NOT a second ImGui context.
  (That guard is still correct and is kept -- the ImGui context is engine lifetime, like the window.)
- The session handoff itself is healthy: `"Ending game session \"2 Ship 2 Harkinian\""` is logged, and
  OoT installs FRESH Configuration / ConsoleVariables / ControlDeck / ResourceManager, inheriting no
  per-game state.
- `GameOverlay::Init()` -- the one Gui-owned thing that DOES run per game, via
  `Gui::RegisterResourceFactories` from `Context::InitResourceManager` -- does not touch ImGui's font
  atlas. It stores `Font` resources in `mFonts` and deliberately keeps no atlas handle.

So: one context, one backend init, fonts not obviously rebuilt -- and yet the atlas state ImGui reads
at `NewFrame` is bad by the time OoT draws.

## Next places to look

1. **`Context::BeginGameSession` calls `RemoveAllGuiWindows()`.** The windows are dropped but the
   ImGui context keeps per-window state keyed by name, and the fonts pushed by those windows are not
   obviously unwound. Check what ImGui state survives a full window-registry clear.
2. **The font atlas versus the ResourceManager.** The atlas was built from `fontawesome_compressed_
   data_base85` (static) plus the default font, so it should not depend on the outgoing game's
   ResourceManager -- confirm that, since a font whose backing memory belonged to MM's resources
   would produce exactly this.
3. **`ImGui_ImplSDLGPU3` across the handoff.** The renderer backend is initialised once and never
   re-initialised; check whether anything in MM's teardown destroys the font texture it owns
   (`ImGui_ImplSDLGPU3_DestroyFontsTexture`) without the context being told.

## Related

- [issue 0009](0009-mm-solo-teardown-corrupts-the-heap-after-the-cor.md) -- MM solo teardown heap
  corruption, still open and **intermittent**: after this ImGui work `solo mm` exited 0 twice and 134
  once, with the same "corrupted size vs. prev_size". Restoring ImGui changed the heap layout; it did
  not fix the corruption. Do not read a green `solo mm` as evidence 0009 is closed -- run it several
  times.

### Resolution (2026-08-06)
ROOT CAUSE: the ImGui font atlas is ENGINE lifetime but every game registers its own fonts into it, and adding a font marks the atlas not-built. The SDL3-GPU backend only builds the font texture when it has none (imgui_impl_sdlgpu3.cpp: 'if (!bd->FontTexture) CreateFontsTexture()'), so once MM had built it, OoT's fonts were never rasterised; ImGui::NewFrame's 'Font Atlas not built!' assert is compiled out under NDEBUG, so instead of a message it walked an unbuilt font and died in SetCurrentFont. Compounding it, OTRGlobals/BenPort passed the ResourceManager's TTF buffer with FontDataOwnedByAtlas=false -- a raw pointer into PER-GAME memory held by an ENGINE-lifetime atlas, dangling the moment the outgoing game's ResourceManager destructed. FIX: Fast3dGui::ImGuiBackendNewFrame drops the font texture when io.Fonts is not built so the backend rebuilds it; both games now copy the TTF bytes with IM_ALLOC and leave FontDataOwnedByAtlas at its default. VERIFIED: tools/zelda3d_sequence.sh mm,oot exits 0 with no crash markers; log shows 'font atlas has unbuilt fonts ... dropping the font texture so the backend rebuilds it' followed by OoT drawing real frames (sgDraw #19/#20, uma_anim_fastrun / hl_anim_fastrun2_30) and an orderly shutdown.
