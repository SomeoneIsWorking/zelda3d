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
- **the A button / do-action prompt** — this is the residual black-bar stack still visible left of the
  item buttons. (An earlier revision of this note guessed "C-Up / Navi and the start button, same
  shared-tile cause". Both halves were wrong; see the identification below.)
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


## Follow-up, same day — identifying the residual black bars (they are the A button)

The user re-reported the bar stack and asked what it is. Guessing twice was not acceptable, so it was
measured.

**It is screen-anchored, not world geometry.** Orbiting the camera 0/60/120/180 degrees
(`camorbit 60` x3) leaves the stack in exactly the same screen position while the whole world rotates
behind it (`scratch/screenshots/orbit_sheet.png`, `cup_sheet.png`). My first instinct on seeing the
user's crop — a ladder, because the crop shows bars over a tree trunk — was wrong.

**It is the A button (the do-action prompt), and only with the HD disc.** Decisive A/B via the live
REPL toggle `hudtex 0|1` (`scratch/screenshots/abtn_ab.png`): with the crisp HUD textures OFF the A
button draws as a clean solid disc; with them ON the same spot is the bar stack, with the "PutAway"
label over it.

**Mechanism.** `Interface_DrawActionButton` is NOT a texrect — it is a flip-animated 3D quad
(`Matrix_RotateX(interfaceCtx->unk_1F4 / 10000)`) over `interfaceCtx->actionVtx[0..3]` with texcoords
baked for a 32-texel tile. The #31 HD substitution keeps that quad and rescales the baked s10.5
texcoords by `gw/32`:

    tcFarS = (1024 - 16) * sButW / 32

With the OoT3D texture pack present (`[Zelda3D] texpack: 2143 textures indexed`) the disc is far
larger than 32 texels, and that ratio-rescale is a magic-constant patch over a tile the N64 path
cannot describe — the row stride comes out wrong and the quad samples repeating rows, which is why
the corruption is HORIZONTAL banding specifically. Same family as the C-button shared-resident-tile
bug, different path.

**Fix is the same architectural one, and it is NOT a texcoord repair.** The A button converts to the
native path as a group with the do-action label (they share the flip and the label draws over the
disc, so converting one inverts the layering). Two things it needs that the texrect elements did not:
the label is IA4 (`gDPLoadTextureBlock_4b`, 48x16 from `doActionSegment`) so it needs an IA4->RGBA
decode, and both quads are placed through the HUD's ortho MATRIX stack rather than as screen rects,
so their pixel rect has to be derived from that projection instead of read off the call. The X-flip
reduces exactly to a vertical scale by `cos(unk_1F4 / 10000)` under an orthographic projection.


## Pass 2 — the A button + do-action label converted

The group identified above is now on the native path, so the black bars are gone.

**Placement had to be DERIVED, not read off the call.** Unlike every element in pass 1, these two are
not texrects: they are quads placed through the HUD's ortho matrix stack. The translate puts the
disc's centre at ortho `(-137 + x, 97 - y)` and the HUD ortho maps ortho->virtual as
`(160 + ox, 120 - oy)`, giving a virtual centre of `(x + 23, y + 23)`. The label's translate uses
`-138` (vanilla OoT authors the two one unit apart) and its Y is pre-flipped as `98 - R_A_ICON_Y`, so
its centre is `(rAIconX + 22, 120 - rAIconY)` — one unit from the disc's, which is why the label sits
concentric on the button.

That derivation was checked against the game rather than trusted: predicted disc centre (249, 32);
measured on a `hudtex 0` vanilla reference frame (247.5, 30.0) and on the native frame (248.8, 30.2).
Native vs vanilla agree to 1.3 virtual units in X and 0.2 in Y — the residual is the HD disc art's
ring versus the flat vanilla disc, since both numbers come from the same saturated-blue interior
mask. `scratch/screenshots/abtn_native_ab.png`.

**The flip animation survives exactly.** `Matrix_RotateX` about the quad's own centre under an
orthographic projection is a vertical scale by `cos(angle)` — not an approximation — so the native
quad just scales its height by `|cos(unk_1F4 / 10000)|` about the centre. A fully edge-on quad has
zero height and is dropped by the `w <= 0 || h <= 0` guard in `record()`, which is the correct
result.

**IA4.** The label (`doActionSegment`, 48x16, `gDPLoadTextureBlock_4b`) is the one HUD source that is
neither RGBA32 nor one of our own buffers, so the module decodes it (3 bits intensity -> rgb, 1 bit
alpha). Its N64 combine — `(PRIM-ENV)*TEXEL0+ENV` with `ENV = 0`, alpha `TEXEL0*PRIM` — is then a
plain modulate. The decode cache is keyed by **content hash, not by pointer**: the label rewrites the
same buffer whenever the prompt changes, so a pointer-keyed cache would pin whichever label was shown
first, and the GPU-side upload cache (which keys on the buffer address) would happily reuse a stale
upload. One buffer per distinct hash fixes both. Verified live: the label reads "Drop" while carrying
and updates as the prompt changes (`scratch/screenshots/label_native_zoom.png`).

`Interface_DrawActionButton` gained a `Color_RGB8 prim` parameter — it is called exactly once, and
passing the colour is cleaner than shadowing the N64 PRIM register.

Remaining on the interpreter after this pass: health meter, magic bar, rupee/small-key counters,
timers, the C-Up "Navi" label, and the minimap. None of them are corrupted today.


## Pass 3 — health meter, magic meter, rupee/small-key counters

Three more groups off the interpreter. Notes worth keeping:

**The heart row** needed the PRIM/ENV lerp mode added in pass 1 (unused until now). Double-defense
hearts use the SWAPPED combine `(ENV-PRIM)*TEXEL0+PRIM`, which is the same operation with the colours
exchanged — but alpha is `TEXEL0*PRIM` in both cases, so the swap must carry the real PRIM alpha (env
alpha is a constant 255 and would defeat the fade). The native path reads its colours back off
`curColorSet` rather than duplicating the eight prim/env branches.

**The magic meter** needed three new capabilities: IA8/I4 decoding (folded into one
`Zelda3D_HudDecode` covering IA4/IA8/I4/I8), a repeat sampler for its tiled middle section, and a
third combine mode whose alpha comes from PRIM alone.

**THE SOURCE RECT IS THE THING TO GET RIGHT, and it bit twice.** An N64 texrect with dsdx/dtdy 1:1
samples ONE TEXEL PER PIXEL rather than stretching its texture over the rect.
1. The magic fill's 7-pixel-tall rect takes the first 7 of its texture's 16 rows; stretching all 16
   into 7 rendered a thin green sliver over black.
2. That recovered rect is in the ORIGINAL texture's texel space, so it must be rescaled into the
   resolution of whatever crisp substitute is actually drawn. Without that, the rupee icon sampled a
   16x16 window of a 64x64 replacement — the top-left quarter, which is empty, so the gem vanished
   while its digits (which happen to pass a full-texture rect) looked fine.

**New instrument: REPL `nativehud 0|1`.** Comparing two separately-captured frames does not work for
this — the world behind the HUD moves between captures, and any colour mask picks that up (a mask
over the rupee area reported a bogus mismatch that was grass). The toggle flips every converted
element back to its display list in the SAME scene, so a real A/B is one command.

Using it, the expected residual between the two paths is **edge-only**: a diff heatmap of the rupee
counter shows thin outlines around the gem and each digit with identical interiors — the native
linear sampler versus the texrect path's filtering. Solid interiors and unchanged bounding boxes are
the pass criterion; anything filled-in means a real difference. Control measurement (two captures at
the same setting) is ~250 changed pixels in the heart region from scene motion alone, so compare
against that, not against zero.

Remaining on the interpreter: timers, the HBA score digits, the C-Up "Navi" label, the minimap.


## Pass 5 — the minimap, and a correction to this document's own ordering rule

Converted first: the C-Up/"Navi" prompt (its disc was a THIRD victim of the shared-resident-tile
corruption), the event timer, and the HBA score digits. New primitive `navicall <0|1>` — the Navi
prompt only appears when the game decides Navi has something to say, and a one-shot poke at
`naviCalling` does not survive because `Interface_Update` clears it before the draw, so it is a
persistent override re-applied at the top of `Interface_Draw`.

That leaves the minimap, and it does not convert the way the others did.

**The compass icons are 3D MESHES, not texrects.** `Minimap_DrawCompassIcons` draws the player-position
and last-entrance arrows with `gSPDisplayList(gCompassArrowDL)` — an untextured OTR display list under
a full transform (`Matrix_Scale` + `Matrix_RotateX(-1.6)` + `Matrix_RotateY(heading)`), with
`G_CC_PRIMITIVE` for a solid colour. A textured quad cannot represent that. Substituting a rotated
arrow SPRITE would look approximately right and would be exactly the thing this project bans: a clone
standing in for the real mechanism, losing the arrow's actual geometry while looking finished.

**And the group rule this document has been asserting is WRONG — or rather, it is an artifact, not a
property.** Every pass so far has said "elements must convert as a group, because the native pass runs
after the whole interpreter frame and lands on top of everything". The first half is true; the
conclusion is not. The HUD quads are appended by `AppendZelda3DHudDraw` as `OP_DRAW` records into the
SDL3-GPU backend's **same deferred op list as the N64 triangles**, replayed in order. They land on top
only because `Zelda3D_HudFrame()` batches the whole frame's quads and flushes them once, from
`Gui::EndFrame`, after everything else has been recorded.

Flush at the point of RECORDING instead and the ops interleave in the correct place — which means:

- elements convert INDIVIDUALLY, with no group rule at all;
- the minimap's map image can go native while its 3D arrows stay on the interpreter and still draw
  over it, with no approximation of the arrows anywhere.

What that needs: `Begin`/`End` currently assume one cycle per frame — `End` uploads into
`g.rings[ringIdx]` and advances, so with `kRingFrames == 3` only three flushes fit before wrapping
onto a slot that may still be in flight. Multi-flush needs the ring to accumulate an offset within a
frame (or to size itself to the frame's quad count) rather than one slot per flush.

That is the next step, and it is worth doing before the minimap: it removes a constraint every future
element would otherwise inherit, and it is why the minimap is NOT being converted with a sprite arrow
in the meantime.


## Pass 6 — the flush marker, and the minimap

Built the mechanism the last two passes kept circling, then used it.

**`G_ZELDA3D_HUDFLUSH` (0x4c) + `gSPZelda3DHudFlush`**, modelled on the existing
`G_ZELDA3D_CLEARDEPTH`: no operands, handler calls `gfx->Flush()` then hands off. A converted element
emits the marker where it would have drawn, and its quads composite AT THAT POINT of the interpreter's
execution instead of after the whole frame. `Zelda3DHudRenderer::Flush()` split out of `End()` — each
flush takes its own ring slot, so a frame consumes one per converted element (`kRingFrames` 3 -> 24).

**The first attempt silently did nothing, and the verification caught it.** The marker fired, but
`Flush()` early-returns unless `g.active`, and the batch was only opened inside `Zelda3D_HudFrame()`
at end of frame — so every marker was a no-op and the minimap still composited last, burying the
compass arrow. The A/B showed the map correct and the red arrow simply absent; without the
`nativehud` toggle that would have read as "the arrow is broken" rather than "the marker did nothing".

Fixed by pushing quads in INSTALMENTS: the module keeps a cursor into its recorded list, each marker
pushes everything recorded up to it (opening the renderer batch lazily, since that needs a live frame
recording that only exists once the interpreter is running), and `Zelda3D_HudFrame` pushes the
remainder. Verified: red compass-arrow pixels 65 native / 65 interpreter, identical.

**So the group rule is lifted** — elements can now convert alone, and the minimap image is native while
its compass arrows stay where they belong.

## Where #205 stands

Native: item buttons, do-action/A button, heart row, magic meter, rupee + small-key counters, event
timer, HBA score, C-Up/Navi, minimap image.

Still interpreter-drawn, and correctly so: the minimap's compass/position arrows and the map-mark
icons. These are not HUD texrects — `gCompassArrowDL` is untextured 3D geometry under a scale +
RotateX + RotateY. Drawing them natively means a mesh path, not a quad path; substituting a rotated
sprite would be a clone standing in for the real mechanism. They now layer correctly over the native
map, so there is no visual debt — only a scope boundary worth stating plainly.
