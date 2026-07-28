# 2026-07-28 — Taking the HUD off the Fast3D interpreter (#205, pass 1)

User request, with a screenshot of the item-button area rendering as a column of black bars:

> I want to bring your attention to N64 HUD, it's glitched, I don't want you to fix the glitch, you
> should make it so HUD is not driven by interpreter, you can also use 3DS HUD

So: architecture, not a patch. This pass builds the native HUD path end-to-end and moves the
**item-button cluster** — the part in the photo — onto it.

## What the glitch actually was (context, not the deliverable)

Worth writing down because it is the argument for the architecture change. The crisp HD button disc
is substituted inside `Gfx_TextureIA8`, which the B button calls once; the three C-button texrects
then **reuse that resident tile** and fudge their `dsdx`/`dtdy` by `bgScale = discW/32`, because the
texcoords were authored for the N64 32x32 tile. A HUD element whose correctness depends on another
element having left the right tile resident, at the right size, is exactly the kind of coupling the
emulated pipeline forces and a real UI layer does not have. With native quads there is no shared
tile and no `bgScale` — each quad carries its own texture and UVs.

## Design: record here, draw natively later — the engine keeps its layout

The N64 HUD code still computes every rect the way it always did (the 320x240-based,
widescreen-extended space from `OTRGetDimensionFrom{Left,Right}Edge`, the `interfaceCtx` fade alphas,
the cosmetic CVars, the visibility rules). Only the final "emit a texrect" step changes: the site
calls `Zelda3D_HudQuad*`, which records the resolved quad; `Gui::EndFrame` then calls
`Zelda3D_HudFrame()`, which maps the rects to framebuffer pixels and draws them through the SDL3-GPU
quad renderer as ordinary ops in the one unified render pass.

Re-authoring the layout in a new module was the alternative, and it is exactly what the **deleted**
custom HUD did (#202) — which is also why it was rejected: a redesigned item bar that suppressed the
native C-button cluster and clobbered `gSaveContext.equips.buttonItems`. Keeping the engine's layout
and moving only the draw cannot silently lose a HUD feature or fight native state.

**Ordering consequence, and why elements convert as a GROUP:** the native pass runs after the whole
interpreter frame, so anything recorded lands on top of everything the interpreter drew. An element
must be converted with its entire stack (disc → icon → ammo → badge) or the layering inverts. That is
what `Zelda3D_HudOwns()` gates; unconverted elements keep their display-list emission untouched.

## Pieces

- **Renderer.** The SDL3-GPU HUD quad renderer deleted by #202 is restored from `c6daa4d4^`
  (`zelda3d_hud_sdl3gpu.cpp`, `Zelda3DHudRenderer`, the `Op::DRAW_HUD` class, `AppendZelda3DHudDraw`,
  the `Gui::EndFrame` call). It was deleted for the LAYOUT it served, not for the plumbing, and it is
  known-good code that shipped. Extended here with a per-vertex **ENV colour + combine mode** so the
  two combiners the N64 HUD actually uses both work in one pipeline:
  mode 0 `TEXEL0 * PRIM` (also covers `MODULATEIA_PRIM`, since our HUD textures store intensity in
  rgb and coverage in a) and mode 1 `(PRIM-ENV)*TEXEL0+ENV` (z_lifemeter.c's hearts). Carrying the
  mode per-vertex rather than per-pipeline keeps quads coalescing by texture alone.
- **`soh/src/zelda3d/hud/zelda3d_hud.{h,cpp}`** — the record/draw seam and the virtual→pixel map.
- **`z_parameter.c`** — the item-button discs, item icons, ammo counts and key badges each grew a
  native branch beside their display-list emission.

## The one non-obvious thing that bit: HUD textures are OTR PATHS, not pixels

The first native item-icon draw came out blank — empty discs. SoH stores most HUD textures as OTR
**path strings** (`"__OTR__textures/icon_item_static/gItemIconDekuStickTex"`), and the Fast3D
interpreter resolves them to pixels when it executes `gDPLoadTextureBlock`. A native HUD gets no such
step. The fix is in `record()`: `ResourceMgr_OTRSigCheck` + `ResourceMgr_GetResourceDataByNameHandlingMQ`,
so any recorded texture — ours or the engine's — resolves the same way. Our own runtime-built
textures (disc, digits, keycaps, hearts) are already raw RGBA and pass straight through, which is why
those three drew correctly while the icons did not.

## Verified live

`scratch/screenshots/hud_native_after{,_zoom}.png` vs `hud_before_native.png`, same scene and camera:
the four item buttons are round discs again (B green, C orange) with their item icons, ammo counts
(30 green, 10 white) and `F`/`1`/`2`/`3` keycap badges — all drawn by the native renderer. The
black-bar corruption on those discs is gone. Hearts, magic bar, rupee counter and minimap are
unchanged (still interpreter). Build clean, no warnings in the changed files.

## Still on the interpreter — the honest remainder

Not done in this pass, and NOT hidden behind a "fixed" claim:

- health meter (`z_lifemeter.c`) — the mode-1 lerp combine exists for it, it just is not converted;
- magic bar, rupee/small-key counters, timers, the do-action label and A button;
- **C-Up / Navi and the start button** — the residual black-bar stack still visible left of the item
  buttons in the after shot is this, same shared-tile cause;
- the minimap (`z_map_exp.c`).

Each converts the same way: give the site a native branch, add its element to `Zelda3D_HudOwns`, and
convert its whole draw stack together.

## On "you can also use 3DS HUD"

Read as permission, not a mandate, and not acted on beyond the art already in use. OoT3D's HUD is a
DUAL-SCREEN design — hearts and magic on the top screen, the item buttons on the touch screen — so
transplanting its layout onto one screen is a redesign, not a port, and the current single-screen
layout is the one the user restored and verified in #202. 3DS art keeps being used where we already
have it (the texture-pack disc and counter icons). If a 3DS-styled layout is wanted, that is a
separate decision worth making explicitly.
