# Oracle horse-under-rider attribution — port spec skeleton (2026-07-10)

Measurement-only follow-up to `debug_journal/2026-07-10-moon-epona-fade-attribution.md` §2
("Epona missing at az=200 — no horse actor exists at all"). That finding established SoH has
**no horse entity**; it relocates Link's own `Player` actor along the rider path. This session
answers the four questions needed to turn that gap into a concrete port task. No code changed,
nothing built — live harness reads (`titleactors a|b`, `compare firstdiv`) plus doc/grep
cross-checks against `oot3d-decomp/docs/` and `Shipwright/soh/src/zelda3d/`.

## 1. What the oracle uses for the horse — model/objectId/zar

**Not a live `Actor`-table entry with an `id`/`objectId` pair** (title isn't in the normal
`actorCtx` linked-list path the harness's `actors` command reads — `playstate`/`actors` both
return "not populated" at title, confirmed live below). Instead, the oracle's horse is one of
**two statically-pre-allocated 25-bone SkelAnime pose tables** in `.data`, already RE'd in
`oot3d-decomp/docs/title_gamestate.md` and `title_rider_driver.md` §3 and exposed by the
harness as `titleactors a` (tag `epona`, base VA `0x005642D0`) / `titleactors b` (tag
`sibling`, base VA `0x005A54D8`).

Live read at az=200 (harness: `loadstate scratch/title_settled.state` → `soh_boot` → `run 200`
→ `titleactors a`):

```
ok titleactors epona 25
   0 @ 0x005642d0  pos=(   0.00, 5789.00, -2296.10)  rot=(0,0,0)  scale=(1,1,1)
   1 @ 0x005642f4  pos=( -54.86,   34.07,   965.56)  rot=(-4.399,-1.513,1.238)
   ... (25 entries total, 36-byte stride {Vec3f pos, Vec3f rot(rad), Vec3f scale})
```

`compare firstdiv` at the same point confirms Az is title-active (`az_at_title=yes
soh_at_title=no`) and shows entry-0 as the root/pelvis-shaped joint.

**Identity: table A = Epona, resolved via SoH3D's own asset-loader log, not an ActorProfile
objectId lookup** (`oot3d-decomp/docs/title_gamestate.md:453`, `title_rider_port_spec.md:30`):

```
[Zelda3D] auto-loaded model 2010 (/actor/zelda_horse.zar): cmb 'Model/epona.cmb' of 1,
  height=9239.5, bones=25 (skinned=SkelAnime retarget), 5 groups, 3 textures
```

- **zar**: `/actor/zelda_horse.zar` (`OBJECT_HORSE` = N64/SoH object id `0x001A`, per
  `Shipwright/soh/src/zelda3d/zelda3d_object_zars.inc:33`).
- **cmb**: `Model/epona.cmb`, 25 bones — the bone count is the anchor: it matches table A's
  25-entry stride exactly, which is why the RE docs identify table A as Epona (not a
  coincidence — cross-checked against the N64-side `object_horse` naming too).
  `title_actor_world_pos.md` independently derived this is the same shape referenced by SoH's
  auto-load log.
- Table A is **NOT** reachable through the general OoT3D object-id→filename table derived in
  `oot3d-decomp/docs/title_scene_spot99.md` §6.1 (that table resolves spot99's *scene* object
  list — `zelda_mo.zar`/`zelda_wm2.zar`/`zelda_box.zar` — and the title logo's objectId 330;
  it was not re-run against the horse/rider node specifically, since the rider's identity was
  already nailed down independently via the CMB bone-count match above). No objectId value for
  the title-demo horse specifically has been derived — only the zar/cmb name, which is
  sufficient to specify the port (SoH3D's auto-replace path already keys off the same zar).

## 2. One actor or two — and does a separate Player/Link exist at title?

**Two pose tables, not one actor with rider params — and they map to two DISTINCT things, not
"horse (with rider baked in)" vs "nothing."**

- Table A (`0x005642D0`, tag `epona`) = **Epona's own 25-bone SkelAnime pose**.
- Table B (`0x005A54D8`, tag `sibling`) = **Link's own 25-bone SkelAnime pose**, confirmed by
  `title_rider_driver.md` §3: entry 1's anomalously high `pos.y=7875.24` (live-read below,
  matches doc's cited 7875 exactly) vs Epona's own entry-0 `pos.y=5789.00` — consistent with a
  mount-attachment root joint sitting above the horse's back, i.e. a MOUNTED Link skeleton, not
  a standing one.

Live read confirms the doc's cited value exactly:

```
ok titleactors sibling 25
   0 @ 0x005a54d8  pos=(   0.00,    0.00,    0.00)  rot=(0,0,0)
   1 @ 0x005a54fc  pos=( -18.37, 7875.24, -623.57)  rot=(0,0,0)
   ... (25 entries)
```

So the oracle's rider is **structurally two skeletons, driven by two independent actors**, per
the deeper decomp in `title_rider_driver.md` §§1-3 (not re-verified live this session, static
Ghidra decomp only there):

- **Epona is a real `Actor`** whose world position is written by the rider-cue integrator
  (`title_actor_world_pos.md`'s `0x005AFFB0` Vec3f slot — the single cued world-pos both mounts
  visually share; "Link is drawn attached via Epona's SkelAnime").
- **Link is the real `Player` actor**, running its **ordinary mounted-on-horse state machine**
  — `stateFlags1 & 0x1000000` (`PLAYER_STATE1_ON_HORSE`, same bit position as N64) gates
  animID 8 (riding) via `Player_UpdateCommon` (`FUN_00250ad0`), exactly like any other
  in-game Epona mount. `title_rider_driver.md` §1 is explicit: **"The title-demo rider is not a
  bespoke title-only pose — it is Link's ordinary mounted-Player state machine, running exactly
  as it would during regular Epona-riding gameplay."** No bespoke title-only actor kind exists
  on the oracle side.

**Does Az's Player/Link actor exist separately from Epona at title?** Yes — it's the SAME
`Player` actor structure normal gameplay uses (title runs a real `Play_Main` tick per
`title_gamestate_v2.md`, not a bespoke title loop), just with the horse-mount flag set. It is
not merged into a single "horse+rider" node; it's two actors linked via the standard
`rideActor`/`actor.parent` mount mechanism (mount-flag setter not yet located per
`title_rider_driver.md` §1's open item — only the flag *readers* are pinned; not needed for
this port spec since the target-side port reuses SoH's own existing mount code, not the
oracle's setter).

**On SoH's side** (read-only, contrast): `Zelda3D_ActorPostUpdate` currently overwrites ONLY
`actor->id == ACTOR_PLAYER`'s `world.pos`/`rot.y`; `ACTOR_EN_HORSE` is referenced zero times
under `Shipwright/soh/src/zelda3d/` (grep confirmed). So today there is exactly one entity in
motion at SoH's title (Link's bare Player actor, unmounted, teleported along the path) versus
the oracle's two (Epona actor + mounted Player).

## 3. Does the oracle horse animate (galloping)?

**Not measured live this session — flagged as unknown per the task's own scope guard ("if the
harness can read it cheaply... if not, note as unknown, do not build tooling for it").** The
`titleactors` command dumps the SkelAnime **pose** (resolved per-limb pos/rot/scale), which
already reflects whatever animation frame is currently evaluated — so implicitly, if a diff is
taken between two `titleactors a` reads at different `az` steps, the joints move and that IS
evidence of animation playing (not a frozen T-pose). This session did not take that A/B diff
(would need a second `run <n>` + second `titleactors a` capture, then per-limb delta) — doing so
is a small, in-scope harness read (no new tooling), just not exercised here since the four
questions as posed only required the identity/count answers above. The *decomp* side already
answers "does it animate, and with what": `title_rider_driver.md` §3 states table A/B are driven
by the standard SkelAnime-Update chain (`FUN_003204a4 → FUN_00347550 → FUN_002bb1cc →
FUN_0036b4ec` for Epona, `FUN_002bd9ec → FUN_00347550` for Link) — i.e. yes, actively animated
every frame via the normal animation-evaluation pipeline, not a static pose. The still-open
question (§3 of that doc) is **byte-exact CSAB clip identity** for animID 8 — deferred there as
"not required for a faithful port," resolved to "very likely the same `gEponaGallopingAnim` /
`hl_anim_fastrun2_30`-family clip SoH3D's own `zelda3d_animmap.inc` already uses for Epona,"
pending a live CSAB-index cross-check that was never run (no harness build available in that
session; a harness build now exists but this session didn't spend it on that specific
cross-check since it's explicitly non-blocking for the port).

## 4. SoH3D-side infrastructure already available for a horse port (read-only audit)

Everything a horse port needs already exists in-tree, just never wired to the title path:

- **Model/asset mapping**: `Shipwright/soh/src/zelda3d/zelda3d_object_zars.inc:33` —
  `OBJECT_HORSE (0x001A) → "/actor/zelda_horse.zar"`. Already resolves and loads correctly per
  the auto-load log in §1 (SoH3D's generic object-id → zar auto-replace path, not title-specific
  code).
- **CSAB anim map for gallop and friends**: `Shipwright/soh/src/zelda3d/zelda3d_animmap.inc:
  416-425`, the full `object_horse` table:
  - `gEponaGallopingAnim → hl_anim_fastrun2_30` (the gallop clip a title port would force-select)
  - `gEponaTrottingAnim → hl_anim_slowrun2_30`
  - `gEponaWalkingAnim → hl_anim_walk2_30`
  - `gEponaIdleAnim → hl_anim_wait2`, `gEponaWhinnyAnim`, `gEponaRefuseAnim`,
    `gEponaRearingAnim`, `gEponaJumpingAnim`, `gEponaJumpingHighAnim` also mapped.
  These are marked `// OVERRIDE` entries already tuned for Epona specifically — not a generic
  fallback — so they're production-ready for a title-rider port with zero new anim-mapping work.
- **Native mount mechanism** (full `En_Horse`/`Player` mount code vendored and unused by title):
  `Shipwright/soh/src/overlays/actors/ovl_En_Horse/z_en_horse.c` (the complete N64 `En_Horse`
  actor logic — AI, mount/dismount, gallop state machine) and
  `Shipwright/soh/src/overlays/actors/ovl_player_actor/z_player.c` (~line 7158-7193: the
  mount-on-A-press path — `PLAYER_STATE1_ON_HORSE`, `rideActor`, `Actor_MountHorse`,
  `actor.parent` wiring). `oot3d-decomp/docs/title_rider_port_spec.md` (already written, ready
  to apply, explicitly out of scope for the session that wrote it since `zelda3d.c` was
  off-limits there) lays out the exact 4-step port using this existing machinery:
  1. Retarget the rider-cue integrator's target from `ACTOR_PLAYER` to the title's `EN_HORSE`
     instance (`Zelda3D_ActorPostUpdate`, `zelda3d.c` ~line 441).
  2. Mount Link onto that Epona actor once per title-demo entry via the existing
     `Actor_MountHorse` call (same code real Epona-mounting gameplay uses).
  3. Drop the direct `ACTOR_PLAYER` world.pos/rot writes once (1)+(2) land — Link's transform
     then falls out of the existing mounted-rider Player code.
  4. Wire the already-parsed-but-dropped `RiderCue::action` field (values 0x40/0x41/0x24) to
     force-select Epona's gallop/idle/trot CSAB per cue segment (candidate mapping given, one
     open confirmation: 0x24 = trot vs. a shot-transition marker).
- **What's still missing / open** (per that spec's own "still open" list, unchanged by this
  session): confirming exactly one `EN_HORSE` instance spawns at the title entrance (needs a
  harness `actorscan 0x14` — cheap addition if not already present as a command; not checked
  this session since it wasn't one of the four questions asked), and the byte-exact CSAB-index
  cross-check from §3 above (explicitly non-blocking).

## Port task, stated plainly

Given §1-4: the port is **not** "invent a horse" — the model, textures, skeleton, and the full
CSAB gallop/trot/idle anim table are already loading and mapped correctly (confirmed by SoH's
own log line in §1); the gap is purely that SoH3D's title path drives Link's bare `Player` actor
directly instead of spawning+mounting the native `EN_HORSE` actor the way regular gameplay does.
`oot3d-decomp/docs/title_rider_port_spec.md`'s 4-step plan is the concrete spec; this session's
job was to confirm the oracle-side ground truth it depends on (two independent SkelAnime-driven
actors, not one fused node) is still accurate — it is, both by static decomp cross-check and by
a fresh live harness read at az=200 that reproduces the doc's cited numbers exactly (Link table-B
entry-1 `pos.y=7875.24` matches to 2 decimal places).

## Files referenced (read-only)

- `oot3d-decomp/docs/title_rider_port_spec.md` — the existing ready-to-apply 4-step port spec.
- `oot3d-decomp/docs/title_rider_driver.md` §§1-3 — mount-flag mechanism, cue-driven motion,
  pose-table ownership.
- `oot3d-decomp/docs/title_actor_world_pos.md` — the shared world-pos slot (`0x005AFFB0`) both
  mounts read.
- `oot3d-decomp/docs/title_gamestate.md`, `title_gamestate_v2.md` — title-is-a-real-Play-tick
  finding table A/B pose tables were first surfaced under.
- `oot3d-decomp/docs/title_scene_spot99.md` §6.1 — the general object-id→filename table (not
  used to identify the horse; identity came from the CMB bone-count/log match instead).
- `Shipwright/soh/src/zelda3d/zelda3d_object_zars.inc:33` (OBJECT_HORSE zar mapping),
  `Shipwright/soh/src/zelda3d/zelda3d_animmap.inc:416-425` (Epona CSAB map).
- `Shipwright/soh/src/overlays/actors/ovl_En_Horse/z_en_horse.c`,
  `Shipwright/soh/src/overlays/actors/ovl_player_actor/z_player.c` (native mount mechanism).
- `Shipwright/soh/src/zelda3d/zelda3d.c` (`Zelda3D_ActorPostUpdate` ~line 441 — where the
  retarget from §4 step 1 lands).
- `tools/soh3d_harness/main.cpp` (`HandleTitleActors` ~line 1492, `TITLE_POSE_TABLE_VA`/
  `TITLE_POSE_TABLE_B_VA` constants ~line 1148-1158) — the harness command used for the live
  reads in §§1-2.
- Live harness session this run: `scratch/logs/horse_probe.log` (stderr), savestate
  `scratch/title_settled.state`, sequence `soh_boot` → `run 200` (chunked 100+100) →
  `titleactors a` / `titleactors b` / `playstate` / `scene` / `actors` / `compare firstdiv`.
