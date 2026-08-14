# En_Vb_Ball graphics and the unified generic-TEV gap

## Ground truth

OoT3D actor-table entry `0xAD` resolves to profile `0x0052FDB4` (object `0x9C`,
`zelda_fd.zar`). `oot3d-decomp/docs/boss_fd2.md` records the recovered init/update/draw functions:

- init loads model index 8 (`valbasia_death_body.cmb`) and index 7
  (`valbasia_attack_stone.cmb`);
- params `>= 200` draw the detached death body, lower params draw the attack stone;
- params `100` additionally draw keep model index `0x53`,
  `shadow/model/shadow_model.cmb`, at `(actor.x, 100, actor.z)`;
- the shadow scale is `actorScale * 68 * fade`, with fade `.1 + .025/draw` clamped to 1, and
  material-0 constant slot 4 supplies black plus the recovered alpha.

The port lives in `behaviors/actor/en_vb_ball.cpp`. It consumes typed native actor transform,
timer, shadow-size, and shadow-opacity fields. It never consumes an N64 display list, animation
identity/phase, joint table, morph state, or procedural history.

## False diagnosis and discriminator

Early screenshots showed black radial spokes around the attack stone. The source CMB parser and the
shipping CMB parser both reported bounded geometry (attack stone: 68 triangles, maximum world extent
about 113; shadow: exactly 6 vertices). Two contaminated tests made the problem look geometric:

1. params-101 was called "no shadow", but older frozen params-100 actors were still alive and drawing
   their shadows;
2. immediate captures after teleporting a diagnostic actor from `(5000,*,5000)` retained the previous
   transform in the frame interpolator.

The decisive A/B held one settled params-100 actor and toggled only `unified 0/1`. Legacy rendered
cleanly; unified turned the bounded keep shadow into a large textured quad. `sgdump 2025` showed why:
the shadow material is a blended, depth-write-off, three-stage generic PICA TEV material with a
per-draw constant override, while the unified CMB route reduced every textured group to its one-stage
N64-style combiner and left the mirrored TEV UBO fields unused.

## Root fix and evidence

The unified renderer now has a structural `kGenericTev` variant. It receives the already-verified
legacy CMB material packer's six stage words, RGBA8 constants after per-actor overrides, all three
texture bindings, coordinator-1/2 transforms, combiner-buffer latch semantics, stage scales, and
final-alpha comparison. The TEV evaluator itself is injected from the shared
`zelda3d_tev_glsl.h` source into both legacy and unified shader templates, so the new route does not
carry a second implementation that can drift. No model-name or Volvagia special case exists.

The first shared-source build failed the all-variant self-test because the unified Prism template
used the legacy Inja insertion delimiter (`{{ ... }}`), leaving literal braces in its final GLSL.
Changing that boundary to Prism's `@{...}` insertion made every unified variant compile; switching
live to `unified 0` then compiled and rendered the legacy template from the same shared evaluator
without a shader/template error.

Evidence:

- `ZELDA3D_UNIFIED_SHADER_SELFTEST=1`: every variant, including GenericTev, compiles to SPIR-V;
- live unified screenshot: `scratch/screenshots/boss_fd_attack_shadow_unified_tev_fixed.png`;
- live detached rib: `scratch/screenshots/boss_fd_detached_bone_settled_final.png`;
- attack-stone and shadow geomscan: extents 113 and 46 respectively, with no En_Vb_Ball huge/NaN draw;
- isolated shadow-on/off masks: unified mean darkening `31.709`, legacy `31.702`, mask IoU `0.9904`
  (`23,954` intersecting pixels / `24,186` union).

Negative-design lesson: actor diagnostics that can leave prior instances alive must not use
"params X has no branch Y" as a discriminator without first proving the scene contains exactly one
matching actor. A first-frame capture after an explicit teleport is also not settled evidence.

## Collision-producer completion

The “unrecovered attack-impact effects” were not another asset. Ghidra had removed most of
En_Vb_Ball's params-100/101 collision branch as unreachable after misclassifying the ordinary
`Actor_Kill` call as terminating. Direct ARM disassembly recovered the continuation and two producer
callees: type-1 debris (`FUN_00335814`) and type-3 smoke (`FUN_0036442C`), both rendered by the
already-ported `vb_particle_group.cmb`. `FUN_0036FCA8` is only camera quake setup.

The En_Vb_Ball module now supplies the exact 3DS producer state while the N64 overlay retains its
gameplay collision/child-spawn path: ordinary stones emit 2 debris, params 100/101 emit 6 debris + 4
smoke, detached ribs emit 4 smoke and use centered range 50 for post-bounce angular velocity, and the
params-100 shadow approaches 255 so `1-shadow/255` reaches zero. No N64 animation identity, phase,
joints, morph, or history enters any branch.

Live frozen-step discriminators on the shipping update path:

- large split: `debris=6 smoke=4`;
- ordinary impact: `debris=2 smoke=0`; same path with Zelda3D disabled: N64 control `debris=5`;
- rib bounce: `debris=0 smoke=4`, `rotVel=(-14.51,-22.65)` (inside recovered ±50);
- seven held updates of params 100: `shadow=255.0`;
- screenshot: `scratch/screenshots/boss_fd_rib_impact_3ds.png`.

## Flying-body cadence A/B

The natural OoT3D Fire Temple intro (`entrance 0x305`) exposed a separate ordinary-flight defect:
the oracle title-card flight has a long, populated body trail, while the native port compressed the
body around the head and fire mane. The controlling discriminator was state, not a subjective image
judgement: native `fdinfo` advanced `bodyLead` by only one after `step 60`.

The cause was ownership of time. `BossFdBehavior::tryDrawModel()` both sampled the recovered
150-entry body / 45-entry mane histories and incremented the four authored CSAB playheads. OoT3D's
recovered producer `FUN_003C724C` runs from the actor update tail. Consequently, visibility, render
cadence, and deterministic multi-step control changed the 3DS object's procedural history and clip
phase even though no N64 history or animation state entered it.

`ActorBehavior::postUpdate()` is now the generic typed update-cadence seam, dispatched from
`Zelda3D_ActorPostUpdate`. Boss_Fd records history and advances its four 3DS-owned playheads there;
draw only consumes the resulting state. The negative/positive discriminator is exact: baseline
`bodyLead=118`, then `step 120`, produced `bodyLead=88`, equal to `(118 + 120) % 150` rather than the
old draw-count result. Natural shipping captures now show the long articulated body during the same
intro sequence (`scratch/screenshots/volvagia_native_updatecadence_live.png` and
`volvagia_native_updatecadence_montage.png`); the oracle reference is
`scratch/screenshots/volvagia_oracle_cutscene360.png`.

The embedded paired harness initially could not take the follow-up capture: its first `soh_step 1`
stalled in `Audio_StopSfxByBank`. An all-thread watchdog stack proved pacing was not involved.
`SohBootInternal()` had bypassed `Zelda3D_CoreRunBegin`, leaving the run epoch at zero; audio's
zero-initialized once-per-run latch therefore skipped `Audio_InitSound`, and the sound-bank sentinel
lists remained unseeded. The harness now enters the same lifecycle as `Zelda3D_CoreRun`, disables the
desktop-only game picker, and passes `soh_boot` plus `soh_step 1` under the default five-second
watchdog. That run also exercised ROM provisioning through the repository `.env` wrapper.
