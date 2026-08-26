# Renderer template recovery + MM Player animation routing — 2026-08-27

## Renderer-wide black output

The paired BossFd camera first produced a useful falsification: both actors had exact matching
movement/history state and the SoH behavior submitted every expected Volvagia model, yet the SoH
frame and depth readback were both entirely black. Resource and pose hypotheses were downstream of
the real failure. `Zelda3DRenderer::DrawModel` reported `context=0`, and the startup log named why:

```
[Zelda3D_SG] shader template render FAILED: shader template has unresolved token {{ q }}
```

`kVaryings` contains eight `{{ q }}` qualifiers. The shared `ReplaceToken` primitive intentionally
replaces one token, but `BuildSources` incorrectly used it for this repeated token. Its unresolved-
token gate therefore rejected both shaders, `BeginPass` never became valid, and every Zelda3D draw
was discarded. The symptom was renderer-wide, not BossFd-specific.

The fix gives repeated expansion its own `ReplaceAllTokens` authority and leaves one-shot template
holes on the strict one-token primitive. `Zelda3DShaderTemplate.ExpandsEveryRepeatedVaryingQualifier`
calls the shipping builder and requires all eight vertex `out` and fragment `in` declarations. The
backend now creates fixed renderer resources during initialization and refuses startup on failure,
so an invalid shader cannot become a silent black frame or a per-frame retry storm. That startup
responsibility moved into `gfx_sdl3gpu_initialization.cpp` rather than growing the 2,800-line backend.

Executing the real renderer exposed a second tooling issue: the harness's five-second frame watchdog
treated a progressing cold batch of room/model/texture uploads as a hang. A generic optional renderer
progress callback now pulses the watchdog only after a GPU texture or model upload completes and only
while a watchdog frame is active. A genuinely stuck upload still receives no pulse and still exits.

## Paired rendered evidence

The camera hold is grounded in `FUN_002d77dc`: camera eye/at/up copy into the gameplay View and set
its dirty bit. The harness holds the live active Camera values against guest writes, seeds the View,
and applies the same eye/at/FOV through SoH's real View update path. Releasing restores the prior
camera status and removes the guest watchpoint.

At eye `(700,600,300)`, at `(0,500,300)`, FOV 45, the forced BossFd profile reported exact `MATCH`
through the final paired slice. The renderer probe saw 1,074 finite body vertices per submission,
zero invalid bone IDs, weight sums exactly 1, and 918–1,044 vertices inside clip. The evidence pair
is `scratch/screenshots/bossfd_shader_fixed.{az,soh}.png`: both show the full flying neck/head/mane
in the arena. This closes the black/missing-body failure; it does not claim pixel/material parity or
general BossFd action/death parity.

## MM Player animation ownership

Retail `/actors/zelda2_link_new.gar.lzs` contains 847 CSABs across `boy` 455, `goron` 105, `child`
95, `nuts` 93, `zora` 83, and `kafai` 16. There are 113 duplicate basenames (maximum multiplicity
five), so leaf lookup is invalid. Named N64 resources provide an exact identity: strip only
`__OTR__objects/gameplay_keep/gPlayerAnim_`, then require the exact `<form>/anim/<leaf>.csab` member.
Human tries `child` and then the measured shared `boy` fallback; other forms never borrow a directory.
The actor playhead resets its phase and outgoing morph whenever the selected body model changes, so a
form transition cannot carry an old form's exact path around that policy.

The focused Player animation owner loads the shared GAR once, inventories exact paths, and caches the
resolved path per form-specific body model. Player misses do not take the generic default-CSAB path.
Among 675 named resources, exact candidates exist for Fierce Deity 383, Goron 85, Zora 54, Deku 70,
and Human 431 (77 child, 354 boy fallback). Unnamed `Linkanim_*`, live playback phase/retarget, and
equipment visibility remain open; the opt-in gate stays in place.
