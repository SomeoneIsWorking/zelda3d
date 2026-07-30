# Audit round 2 — FINAL confirmed set (22 findings, adversarially verified)

Full run: 56 assumptions tested, 32 falsified raw, **22 confirmed** after adversarial refutation,
18 of them user-visible, 21 HOLDS, 5 left UNVERIFIED because their refuters died.

Refuter votes are shown as `refuted/cast`. 28 of 69 agents errored (API 500/529 and stalls), so
some findings carry only ONE refuter vote (0/1) rather than two — weaker, and marked as such.

## STATUS KEY
* FIXED — landed and verified by the operator this session
* CONFIRMED — verified by agents, not yet acted on
* DISPUTED — agent-confirmed but contradicted by my own first-hand check


## SCENE

### [FIXED] * `Shipwright/soh/src/zelda3d/model/zelda3d_model.cpp:1136` — user-visible, affected 232, refuted 0/1
  In any Master Quest dungeon, zelda3d renders AND collides the vanilla 3DS room geometry while the N64 side runs MQ rooms/actors — wrong walls, wrong floors, missing/extra rooms, silently. The correct selection is a `_dd` suffix on both the room path and the collision path when ResourceMgr_IsGameMasterQuest(). Caveat on user-visibility: only reachable for a player who supplies an MQ ROM and enters an MQ dungeon; I did not run the game to confirm the build exposes MQ.
### [FIXED] * `Shipwright/cmb3d/asset/zcol.cpp:70` — user-visible, affected 114, refuted 0/2
  Gameplay collision (default ON, zelda3d_collision.cpp builds SoH's CollisionHeader from this). Two effects: (1) the LAST real polygon of EVERY scene is never loaded — 114 missing triangles game-wide: 82 walls, 25 floors, 7 ceilings (scratch/audit2/impact.py). (2) The parser fabricates a record 0 out of vertex-array tail bytes; zcol.cpp:110's index check rejects it in 107 scenes but ACCEPTS it in 7 — ganontika, ganontika_dd, hakaana2, hylia_labo, k_home4, kakusiana, spot18 — yielding a phantom triangle over real scene vertices with a non-unit normal (|n| = 128..1659 instead of 32767, so ny ~ 0.
### [CONFIRMED] * `Shipwright/cmb3d/asset/zsi.cpp:36` — internal, affected 114, refuted 0/1
  NOT user-visible today: `Zsi::envSettings()` has ZERO consumers anywhere in Shipwright/soh, cmb3d or libultraship (grep for envSettings/ZsiEnvSetting hits only zsi.cpp/zsi.h) — runtime lighting comes from the generated .inc, which is correct. It is a latent trap (dead parser whose header comments contradict the correct doc, ready to be believed by the next session) plus the array-bounds check at zsi.cpp:37 silently truncates. Either delete it (no-tombstones) or re-point it at offset+0x10 with the .inc's record layout. Note: world lighting is a CLOSED parity-map row, so this is a parser/doc cor

## ANIMATION

### [FIXED] * `Shipwright/cmb3d/asset/csab.cpp:115` — user-visible, affected 2443, refuted 0/1
  On the 2ship3d/MM3D branch every skinned actor that plays its own 3DS CSAB (mm3d_model.cpp 'STAGE 4 — CSAB anim', live and wired) renders in the bind pose forever: 100% of 138820 tracks in 2443 clips are silently discarded, with no error log and a correct-looking duration. Actors are frozen statues while the N64 playhead advances.

## PLAYER

### [PARTLY FIXED] * `Shipwright/zelda3d_shared/player/link_midmask.cpp:53` — user-visible, affected 4, refuted 0/2
  With the sword drawn and no shield on the back, Link's empty sheath/strap disappears instead of staying on his back. Child with the Deku shield and sword drawn loses the sheath ring. Child holding the Mirror shield shows a HYLIAN shield on his back (line 308 lumps MIRROR into `hylian`; the table's child MIRROR row is 14 = sword only).
### [FIXED] * `Shipwright/zelda3d_shared/player/link_midmask.cpp:36` — user-visible, affected 3, refuted 0/1
  Adult Link holding the hookshot/longshot shows a flat open palm with the chain emerging from nothing; playing the Ocarina of Time shows an open palm instead of the ocarina grip. PLAYER_MODELTYPE_RH_OOT (0x0E) is not even in the switch, so it takes the same default.
### [FIXED] * `Shipwright/zelda3d_shared/player/link_midmask.cpp:32` — user-visible, affected 2, refuted 0/2
  Adult Link raising the Mirror Shield displays a HYLIAN shield (wrong texture, wrong silhouette) — the Mirror Shield is visually absent from the whole game. Separately, adult Link in a shield-holding pose with no shield equipped shows an open palm instead of a fist.
### [FIXED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:326` — user-visible, affected 2, refuted 0/1
  Child Link carrying the Hylian or Mirror shield and raising it shows a DEKU shield strapped to his arm. With no shield equipped he shows an open palm where OoT3D (and N64) show a fist.
### [FIXED] * `Shipwright/zelda3d_shared/player/link_midmask.cpp:23` — user-visible, affected 2, refuted 0/1
  Adult Link swinging the Megaton Hammer holds nothing — the hammer is entirely missing. Bottle-holding uses a clenched fist rather than the cupped hand the bottle model is posed against.
### [CONFIRMED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:311` — user-visible, affected 2, refuted 0/1
  Link's hands stay flat-open whenever he runs — in every locomotion state, both ages, which is the most-seen pose in the game.
### [PREMISE FALSIFIED — NOT ACTIONABLE] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:306` — user-visible, affected 2, refuted 0/2
  Drawing the bow/slingshot does not bring up its waist piece (adult mid 43, p_tex26 on bone 23; child mid 22, p_tex27 on bone 23). Small, but it is on screen for the whole of every bow shot.
### [FIXED] * `Shipwright/zelda3d_shared/player/link_midmask.cpp:12` — internal, affected 2, refuted 0/1
  Effectively none, which is the useful half of this result: I compared posed vertex sets and mid 47 contributes only 2 positions not already in mid 46 (6 of its 8 unique points coincide), and child mid 25 shares 80 of its 97 unique points with mid 24 — both are co-located low-poly overlays that would z-fight rather than add silhouette. Do NOT spend a session "fixing" this; do fix the false rationale in the comment/doc, because it is the sort of guess that gets reused.
### [FIXED — DISPUTE RESOLVED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:314` — user-visible, affected 1, refuted 0/2
  Child Link's Kokiri sword renders as the adult-length Master Sword — a blade nearly as long as he is tall, in every child sword swing, block and idle-with-sword-drawn.
### [FIXED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:329` — user-visible, affected 1, refuted 0/1
  Child Link aiming/firing the slingshot holds an Ocarina of Time in his right hand; the slingshot never appears.
### [FIXED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:315` — user-visible, affected 1, refuted 0/1
  Child Link holding the boomerang shows an object OoT3D never draws (and the real fist+boomerang mesh never appears).
### [FIXED] * `Shipwright/zelda3d_shared/player/link_midmask.cpp:34` — user-visible, affected 1, refuted 0/2
  Adult Link with the bow drawn in third person renders the first-person bow-arm mesh instead of the third-person one (different arm/bow geometry, authored to be seen only from the camera-inside-the-arm angle).
### [CONFIRMED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:754` — internal, affected 1, refuted 1/2
  No player impact; a workflow/consistency defect in the file most likely to be instrumented next.

## BEHAVIORS

### [FIXED] * `Shipwright/soh/src/zelda3d/tables/zelda3d_object_zars.inc:49` — user-visible, affected 4, refuted 0/1
  The Deku Tree's mouth — one of the most-looked-at objects in the early game — renders the N64 mesh in an otherwise-3DS Kokiri Forest, and cannot be fixed by the generator alone.
### [FIXED] * `Shipwright/soh/src/zelda3d/render/zelda3d_render.cpp:454` — user-visible, affected 3, refuted 0/1
  Same-object actors that differ in size all render at one size: large Hyrule Field/Kakariko bushes and trees render at the small variant's size; En_Ishi's silver boulder renders at the liftable rock's scale; the En_Ishi rocks and Obj_Hana rock-debris sit 5.8 and 32 world units too low (sunk into the ground).
### [FIXED] * `Shipwright/soh/src/zelda3d/zelda3d.h:355` — user-visible, affected 2, refuted 0/1
  En_Ishi's silver/large rock renders roughly a third of its correct size; the Hyrule Field flower (Obj_Hana type 0) renders far oversized — plausibly taller than Link.
### [FIXED] * `Shipwright/soh/src/zelda3d/render/zelda3d_render.cpp:892` — user-visible, affected 1, refuted 0/2
  The forced-CMB path is unusable for any static prop: the wooden-torch entry can never draw (Obj_Syokudai params>>12==2 silently falls back to the N64 model), and any future non-skinned forced entry will silently do the same. This is the mechanism that would otherwise fix finding #1, so it matters more than the one torch.
### [FIXED] * `Shipwright/soh/src/zelda3d/render/zelda3d_render.cpp:610` — internal, affected 0, refuted 0/2
  None at runtime. Costs the next reader a wrong mental model of the scale derivation (a diagonal/height mix-up is exactly the sort of ~1.5-2x systematic scale error that would be hunted for elsewhere).

## UNVERIFIED (refuters died — NOT dismissed)

* `Shipwright/zelda3d_shared/player/link_midmask.cpp:20` — Adult `SwordTwoHand` (LH_BGS) maps to mid 37 unconditionally.
* `Shipwright/soh/src/zelda3d/player/link_mesh_id_map.md:1` — The texture/label map is authoritative: adult "p_tex28 = bow (wood)", "9 = bow on back; 10 = Hylian shield + bow; 12 = bow + sword", "32 = HOOKSHOT-ish item (p_tex10 claw)", "p_tex10 = hookshot claw";
* `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:466` — "all Link clips are authored in the BOY rig's translation space, and the engine scales the anim-provided root translation per age (child *= 0.64f)" — restated in csab.cpp sampleLocalTRS as "Link's CSA
* `Shipwright/soh/src/zelda3d/model/zelda3d_model.cpp:918` — "One OoT3D CMB per N64 object id is enough — the CMB with the most vertices is the main body" (the AUTO pick heuristic; sAuto[] is indexed by object id, so the whole game gets exactly one model per ob
* `Shipwright/soh/src/zelda3d/core/zelda3d.c:757` — Zelda3D_ActorHasReplacement "Mirrors the lookups in Zelda3D_TryDrawActor / Zelda3D_TryAuto without side effects" (comment at zelda3d.c:729-731).

## HOLDS (checked and TRUE — do not re-audit these)

* Shipwright/zelda3d_shared/player/link_midmask.cpp:81 — Iron boots = mids {35,36}, hover boots = mids {15,22}, derived from a 3DS table at 0x0053c74c read as base + boots*8 at -8/-4.
* Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:293 — `g.boots = player->currentBoots` is safe to compare against the literals 1 and 2 (the field never holds the extended PLAYER_BOOTS_IND
* Shipwright/libultraship/src/fast/zelda3d_sdl3gpu.cpp:2092 — A 64-bit mid mask is wide enough for Link, and meshes with id >= 64 being unconditionally drawn (`if (grp.meshId >= 0 && grp.meshI
* Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:356 — The #201-e "child with no Kokiri sword" back-geometry rule is ported correctly: child-only, `currentShield < HYLIAN` for SHEATH_18/19
* Shipwright/soh/src/zelda3d/player/link_mesh_id_map.md:108 — The waist / long scabbard meshes (adult 44 on bone 24, child 23 on bone 24) are "left OFF" — an unexplained omission versus N64, w
* Shipwright/cmb3d/asset/csab.cpp:39 — "STRIDE IS UNVERIFIED: every one of the 2951 tracks in this ROM has nkf == 1, so no asset here can distinguish a 4-byte record from any other size ... FA
* Shipwright/cmb3d/asset/csab.cpp:82 — "CONSTANT / unknown -> no track (fall back to rest TRS)" — track type 0 and unknown types either do not occur or carry nothing worth sampling; and a pres
* Shipwright/cmb3d/asset/csab.cpp:275 — The HERMITE loop-seam segment length is `(first.time - last.time) + track.timeEnd`, i.e. the TRACK's own timeEnd is the right wrap period (rather than t
* Shipwright/cmb3d/asset/csab.cpp:102 — "isRotInt16 applies only to the rotation slots (3,4,5 = rX/rY/rZ); translation/scale tracks stay float even in an int16 anod."
* Shipwright/cmb3d/asset/csab.cpp:142 — nodeForBone maps a CMB bone by using the bone's `id` as a direct index into the CSAB's boneToAnim table — i.e. CMB bone ids are dense 0..n-1 and share a
* Shipwright/soh/src/zelda3d/anim/zelda3d_anim.cpp:548 — The `deltaCount` / `postCount` bounds that csab.cpp checks as `id < deltaCount` are BONE counts, not float counts — a mismatch would le
* Shipwright/soh/src/zelda3d/tables/zelda3d_animmap.inc:26 — Every mapping table entry names a CSAB that actually exists in the target ZAR — a stale/typo'd name silently falls back to the mode
* Shipwright/cmb3d/asset/zsi.h:12 — "every one of the game's 390 room files has exactly one `cmb ` blob" — both the count and the exactly-one claim
* Shipwright/soh/src/zelda3d/model/zelda3d_model.cpp:1136 — the 3DS room index equals the N64 room number, and every N64 room has a matching 3DS room file
* Shipwright/cmb3d/asset/zcol.cpp:108 — `if (p.type >= nSurf) p.type = 0;` and `if (vA/vB/vC >= nVtx) continue;` are defensive no-ops on real data
* Shipwright/soh/src/zelda3d/core/zelda3d.c:1135 — kZelda3dSceneNames is indexed by SceneID and covers the whole scene table; the 9 NULL rows are only unusable test scenes
* Shipwright/soh/src/zelda3d/model/zelda3d_model.cpp:154 — the ctxb atlas cache is safe: cached RGBA pointers stay valid, and "these menu files each carry a single atlas at index 0"
* Shipwright/soh/src/zelda3d/model/zelda3d_model.cpp:836 — Every forced-CMB key in the codebase ("<zar>|<cmbSubstr>", incl. SKY:/BILLBOARD: variants) actually resolves to a CMB — a typo would 
* Shipwright/soh/src/zelda3d/model/zelda3d_model.cpp:1177 — `if (e->modelId == 0) { e->modelId = Zelda3D_AutoModelId(...); if (e->modelId < 0) state = 3; }` — 0 is a safe "unresolved" sentinel
* Shipwright/soh/src/zelda3d/core/zelda3d.c:1103 — The single global `sPendingMeasureKey` slot is safe even though many actors draw per frame — a second actor opening a bracket could orphan th
* tools/gen_object_zars.py:26 — Every ALIAS row "was confirmed by dumping the zar's main CMB" and resolves to a real /actor path.

---

## DISPUTED: child mid 16 / the Kokiri-sword claim

The agents confirmed `zelda3d_link.cpp:314` (child LH_SWORD -> mid 16 draws an adult-length blade) with
0/2 refuters, on TABLE evidence from code.bin. My own first-hand check disagrees: isolating child mid 16
in game (`linkmid only 16`) renders a sword at Link's left hand whose blade looks proportionate for a
child, not "nearly as long as he is tall" (`scratch/screenshots/mid_compare.png`).

Both cannot be right. Possible reconciliations, none tested:
1. mid 16 is the correct child sword and the finding misread the table;
2. mid 16 is correct in the IDLE pose I captured but the sword-drawn state selects something else;
3. the difference between the Kokiri and Master blade is smaller than I can judge by eye at that camera,
   and my "proportionate" read is simply wrong.

Do NOT act on this one until it is settled — a proportional measurement against the adult blade, or a
texture ID (Kokiri vs Master use different blade textures), would decide it. Recording the conflict is
the point: an agent verdict and an operator observation disagreeing is information, not something to
resolve by picking whichever is more convenient.

---

## Session addendum (2026-07-30) — two findings closed, and what the closures taught

### link_midmask.cpp:53 — PARTLY FIXED (adult half)
Adult `EmptySheathNoShield` and `ShieldOnBackSwordDrawn`-with-no-shield now draw mid 42, the empty
sheath strap. Both previously drew NOTHING, so with the sword out and no shield Link's sheath simply
vanished off his back. Tables read byte-exact out of `code.bin` rather than trusted from the report —
see claim C023 for the addresses and the visual confirmation of mid 42.

**The CHILD half is deliberately still open.** The table says child SHEATH_17 = 21, and the audit
called child 21 "the empty sheath guard/ring" — but an earlier isolation sweep of mine read child mid
21 as "sword hilt only on back". Those disagree and a sheath ring could plausibly look like a hilt at
that camera distance. Wiring it on either reading would be a guess; it needs its own identification
pass. Recorded here so the next session does not assume the whole finding was closed.

### render/zelda3d_render.cpp:892 — FIXED, and it was an INSTRUMENT failure as much as a code one
The forced-CMB path could never complete a measurement for a non-skinned entry: the measure bracket
is keyed, `Zelda3D_MeasureResult` routes a bare object id into `sAuto[objId]`, but a forced slot works
out of `forced->entry`. So `measuredH` was **unreachable by construction** — state stuck at 1, tries
climbing to the 8-try cap, then permanent state=3 / N64 fallback. Fixed with a reserved key range
(`ZELDA3D_MEASKEY_FORCED_BASE + slot index`) instead of a fourth hand-written sentinel.

Two reasons this survived so long, both worth remembering because they are general:
1. **Skinned entries mask it.** They skip the measure pass entirely (scale comes from bone lengths),
   so the EN_TG couple worked fine and the table looked healthy. A bug that only affects the code
   path your one working example does not take is nearly invisible.
2. **Neither instrument could have shown it.** `autostate` dumped only `sAuto[]`, so forced slots were
   not merely wrong in the dump — they were absent from it. And the AUTO resolution log printed the
   bare zar, so a forced slot and the default per-object slot logged *identically*. Both are now
   fixed (I014, I015) and both were validated by showing the other answer, not just the expected one.

The lesson matching the "can the instrument show the OTHER answer" rule: introspection that silently
omits a whole table is worse than introspection that prints a wrong number, because the wrong number
prompts a question and the omission reads as "nothing to see".

### Note on this session's two agent losses
Both delegated investigations (the player DL table family, and the same-rig constant-translation
measurement) died on API/stream errors rather than on the work, and were relaunched. No findings
were lost, but nothing from them is recorded here yet — do not read their absence as a negative
result.

### render.cpp:454 + zelda3d.h:355 + render.cpp:610 — all three FIXED in one change

They were one root cause: Obj_Hana and En_Ishi serve several differently-sized props from ONE object
slot keyed by params, so they cannot use `sAuto[objId]` and were drawn with hand-written macros —
two of which were commented UNCALIBRATED and copied verbatim from the small-rock value. Now
self-calibrated per variant (measured 0.38139 for the silver rock, 0.00952 for the flower; see C025).
They also passed `groundOffset 0`, which is the "sunk into the ground" half of the report — now
base-anchored with `-AutoModelMinY()` like the auto path.

**The audit's prediction and my measurement agree, by different methods.** The audit reasoned from
the code that the rock was "roughly a third of its correct size" and the flower "far oversized,
plausibly taller than Link". I measured 0.12/0.38139 = 0.315 (a third) and a flower CMB ~861 local
units tall, which at 0.12 renders 103 world units against Link's ~60 (taller than Link). Two
independent routes to the same numbers is why these are trustworthy rather than merely plausible.

render.cpp:610 fell out of the same work: the comment claimed the interpreter reports a bbox DIAGONAL
and that scale divides by a model diagonal. It reports HEIGHT, and the code divides by
`Zelda3D_AutoModelHeight` — the opcode in libultraship `interpreter.cpp` says so explicitly and
explains why (a diagonal carries an aspect-ratio bias). The code was always right. I had to settle
this before I could trust my own measurement, which is exactly the cost the audit predicted: a wrong
comment about a ~1.5-2x systematic error is expensive even when the code is correct.

### NEW OPEN QUESTION — do not treat the measurement as fully validated (C026)
With self-calibration on for all five variants, the two genuinely CALIBRATED controls measured 17%
and 31% off their tuned values (rock 0.09998 vs 0.12000; bush 0.65595 vs 0.50000). The mechanism is
self-consistent (the two independent small-rock slots measured 0.09998 identically) and dimensionally
sound (height/height). So EITHER the hand calibrations were eyeballed wrong, OR the measurement
mis-scopes something — per-instance `actor->scale` is the obvious suspect, since one measured value
then cannot serve every instance, and the small rock was originally calibrated in Kokiri Forest while
I measured in Hyrule Field.

I did NOT resolve this, so self-calibration is scoped to the two variants whose seeds were guesses.
The cheap next experiment: measure the small rock in Kokiri Forest. Reproducing 0.12 there means the
measurement is scene/instance-dependent (a real limitation of the mechanism); reproducing 0.09998
means the hand value was simply wrong and the calibrated slots should switch over too.

### link_midmask.cpp:23, :34, :36 — FIXED by porting the table instead of the three cases

All three were the same root cause: hand-written mesh-id cases. Rather than patch them individually
I found and ported the OoT3D table they were all guessing at — `sPlayerDLists[21]` @0x0053c698
(claim C027, full decode in `oot3d-decomp/docs/player_dl_tables.md`). See the commit for the five
corrected values and the per-mid isolation measurements.

Two things worth carrying forward:

**The Ghidra dead end was a wrong diagnosis, not a hard limit.** The received explanation for
Ghidra's zero xrefs was ARM `movw`/`movt` immediates it doesn't materialize. That is false: the whole
4.6 MB binary holds only 3 ARM and 49 Thumb-2 MOVW encodings — nowhere near enough to be how
addresses are formed. Scanning for 4-byte-aligned words whose value equals a known table VA (I017)
found the master pointer table and the reading function's literal pool in one pass, no Ghidra at all.
When a tool reports nothing, check the denominator of the negative before accepting the explanation
for it.

**Independent agreement is what made these safe to ship.** The audit read the code and said the
third-person bow renders the first-person arm mesh; the ROM table says 29 where we had 30. Neither
input knew about the other. Same pattern as the rock/flower scales earlier today. Where I could not
get that agreement — what the three shield-variant groups distinguish — I left it unclaimed and left
the verified shield/sheath code alone rather than "completing" the port on a guess.

### Unrelated observation, not investigated
The regression screenshot (`scratch/screenshots/dl_normal.png`, Kokiri Forest, adult Link) shows a
large magenta/purple blob in the upper right near a wooden sign. It cannot be caused by this change
(which only alters Link's hand mesh ids), and Link himself renders correctly, so it did not block the
commit — but it looks like a real artifact rather than an intended effect and is worth a look. Noting
it rather than silently ignoring it; NOT filed as a card since no user reported it.

### zelda3d_link.cpp:314 — the DISPUTE is resolved, and the audit was right

Child Link's Kokiri sword really did render as the adult-length Master Sword. Two refuters failed to
confirm it and I recorded it DISPUTED with "do not act", because my own visual read of the proportions
looked fine. The ROM table settles it without needing either judgement: `LhSword`'s child value is 2
and `LhBgs`'s is 16, they are separate meshes, and we were using 16 for both. Isolating them shows 2
is a short blade and 16 a long blue-hilted one.

**What to take from this:** the DISPUTED verdict came from three subjective looks at a rendered pose
(two refuters plus me), and all three were wrong in the same direction. Proportion judgements on a
posed character are a weak instrument. The finding was resolved the moment a data source with an
independent origin was available. When a dispute is between two eyeball readings, the answer is to go
find data, not to hold a tie-breaker vote.

### The ocarina work from the earlier session was VINDICATED, not overturned
That session isolated mid 18, saw "a hand holding a BLUE instrument with finger holes", and concluded
the mesh map's "slingshot" label was wrong. Both halves were right — 18 is an ocarina, and specifically
the Ocarina of TIME (blue), which is exactly `RhOot`'s child value. It was simply being used for every
right-hand item. That session also left the slingshot pointing at 18 on the grounds that mid 19
"renders a straight brown shaft with red ends, not a forked slingshot frame"; 19 is the slingshot and
the red is its elastic. A careful negative observation ("19 is not a slingshot") was the one thing that
turned out wrong, and it was wrong because recognising an unfamiliar object from a small isolated crop
is harder than it feels.

### Measurement note — why this commit quotes no pixel counts
I isolated each child mesh id and differenced against an empty-mask frame, as with the adult sheath.
This time the numbers were garbage: every mid produced a nearly identical bounding box
(`x[279:432]`, y from 70) because particles/Navi keep moving through `afreeze` + `animlive 0`, so the
diff was dominated by background. The tell was the bboxes agreeing across meshes that plainly differ.
The visual crops answered the actual question, so those are the evidence and the counts were dropped
rather than reported. Same class of error as the four mask/background mistakes earlier in this
session — the method is only sound when the reference frame is genuinely static, and in this scene it
is not.

### zelda3d_link.cpp:306 (bow waist piece) — PREMISE FALSIFIED, do not "fix" this

The finding says drawing the bow should bring up a waist piece, naming adult mid 43 (p_tex26, bone 23)
and child mid 22 (p_tex27, bone 23). Those mesh ids came from our own `link_mesh_id_map.md`, which
the audit itself lists under UNVERIFIED.

Two results kill the premise:

1. `PLAYER_MODELTYPE_WAIST` (slot 0x14 of `sPlayerDLists`, table @0x0053c608) is **(-1, -1)** —
   OoT3D's waist model type draws NOTHING. On N64 `sPlayerWaistDLs` holds real display lists, so this
   is a genuine 3DS divergence, not a mis-read slot.
2. No table anywhere in the binary pairs adult 43 with child 22. Scanned all 1,141,756 4-byte-aligned
   positions in the 4.6 MB image for a row shaped `(43, 22, 43, 22)` and for the bare pair `(43, 22)`:
   **zero** of each. **Control:** the identical search for `(42, 21, 42, 21)` — the known sheath rows —
   returns 2, so the search fires and the zero is a real absence rather than a broken instrument.

What this does NOT prove: that OoT3D has no bow quiver at all. It could be baked into the body or back
geometry, or driven by something other than `sPlayerDLists`. What it does prove is that the specific
fix implied by this finding — map the waist model type to 43/22 — has no support in the ROM, and
implementing it would be inventing a mapping. Left alone deliberately. If someone wants to pursue the
quiver, the question to answer first is "does OoT3D draw one at all", not "which mesh id is it".

### link_midmask.cpp:12 — FIXED (the rationale, which was the actual defect)
The always-on set omits mid 47, which is correct, but the comment justified it with "plausibly the
far-LOD body, which we would double-draw". That is wrong: 47 is a co-located low-poly OVERLAY of 46,
contributing only two posed vertex positions not already in 46 (six of its eight unique points
coincide); child 25 likewise shares 80 of 97 unique points with 24. Corrected in both files, keeping
the wrong guess visible and labelled rather than silently deleting it — a plausible-sounding rationale
is exactly the kind of thing that gets reused, and the correction also records that there is nothing
to gain from an identification pass here.

### object_zars.inc:49 (Deku Tree mouth renders N64) — TWO ROUTES RULED OUT, still open

Established what the object actually is and why the generator cannot fix it:

* `Bg_Treemouth` uses **`OBJECT_SPOT04_OBJECTS`**, which is entry `0x002A` in
  `zelda3d_object_zars.inc` and is `NULL` — hence the N64 fallback. Confirmed from
  `z_bg_treemouth.c`'s init struct, not guessed.
* **There is no `/actor/zelda_spot04_objects.zar`.** Enumerated all 461 `.zar` files in the ROM: the
  only spot04 archive is `/scene/spot04.zar`, and the generator only searches `/actor/`. That is
  exactly why the audit said this "cannot be fixed by the generator alone" — no alias can help,
  because the actor archive does not exist.
* **`/scene/spot04.zar` contains NO CMB.** All 13 entries are one `cmab`, a `qdb`, and eleven `ctxb`
  textures. So the mouth geometry is not there either.

So the two obvious routes are both dead, and adding a `dk_`-style alias — which is what the earlier
note about "3 missing dk_ ZAR aliases" implied for this — cannot work here. (The eight real `dk_`
archives are `board, floater, lightbox, pu_box, spia, stonebridge, trap, vase`; none is a tree mouth.)

**The live hypothesis, NOT yet checked:** *(REFUTED later the same session — see "I GOT THIS WRONG"
below. The mouth mesh MOVES, so it cannot be baked into a static room mesh, and draw suppression is
NOT the fix. Do not act on this paragraph.)* OoT3D bakes the tree mouth into the spot04 ROOM mesh
(room CMBs live in the `.zsi`, one per room) rather than shipping it as an actor. If that is true the
fix is to SUPPRESS the N64 `Bg_Treemouth` draw, not to find a CMB for it — and getting that wrong
leaves a hole in the world, so it must be confirmed by inspecting the room mesh first, not assumed.

Next step is therefore a specific question with a specific answer: does the spot04 room CMB already
contain the tree-mouth geometry? Do not attempt a mesh hunt or an alias before answering it.

**PARTIALLY ANSWERED (2026-07-30, later the same session) — see the addendum at the end.**

### Tooling fix found while doing this
`tools/zelda3d_skel_export.py:39` read `Shipwright/soh/src/zelda3d/zelda3d_object_zars.inc`, a path
that no longer exists — the table moved into `tables/`. Same class of stale-path breakage as the six
generators fixed earlier in this session. Repointed at the real location.

### Deku Tree mouth, addendum — the area IS 3DS geometry, and the actor IS N64

Confirmed both halves, and built the observation recipe that was missing.

`actorsnear 4000` at the site reports `id=0x3E p=0xFFFF cat=1 d=133 --N64--`, so `Bg_Treemouth` is
definitively drawing the N64 mesh. And the framed shot (`scratch/screenshots/tm_pair.png`, top panel)
shows the surrounding Deku Tree — trunk, roots, bark, the arched mouth region — rendering as detailed
3DS room geometry. So this is a genuine single-N64-actor island in a 3DS scene, exactly as reported.

**THE OBSERVATION RECIPE, which is the reusable part.** Three attempts failed before one worked, and
the failures are worth recording because each produced a confident-looking but worthless frame:

1. `roomwarp 1` (force-load the room holding the actor) — `asel 0x3E` succeeds, but the camera ends up
   *inside* geometry and every frame is flat yellow. The actor being selectable made this look like a
   working setup.
2. `warp 0xee` (Kokiri Forest) then `tp` to the actor's coordinates — Link lands outside the resident
   room with no floor under him; the whole frame is flat yellow with Link and his shadow floating in
   it, and `asel 0x3E` finds nothing.
3. `warp 0x209` *after* that `tp` — the transition never completes (Link has no floor), so the frame
   is byte-identical to the previous one. A warp that silently does nothing is the worst of the three,
   because the screenshot looks like a real answer.
4. **WORKS:** restart the game, then `warp 0x209` (`ENTR_KOKIRI_FOREST_OUTSIDE_DEKU_TREE`, spawn 1) as
   the FIRST action. Room 1 loads properly, `asel 0x3E` resolves, and the frame has real content.

Cheap validity check that catches all three failure modes at once: the luminance standard deviation of
the frame. The flat-yellow void reads near zero; the good frame read 33.8. Worth applying to any
"warp somewhere and screenshot" step, because all three bad frames were superficially plausible.

**STILL OPEN — the actual fix.** The area is 3DS and the actor is N64, but that alone does not say
whether the room mesh already contains the mouth (making the N64 actor redundant and the fix a draw
suppression) or whether the mouth is genuinely absent from the room mesh and the actor is filling a
real hole. Deciding that needs the actor's draw hidden for one frame and the same view captured — a
small addition, since there is currently no REPL primitive to suppress a single selected actor's draw.
That primitive is the next step, and it is generic enough to be worth having anyway.

Do NOT "fix" this by pointing the object at some plausible ZAR: there is no actor archive and no CMB
in the scene archive (ruled out above), so any mapping would be invented.

### Deku Tree mouth — I GOT THIS WRONG LAST TICK. Corrected. (C028 falsified -> C029)

Last tick I concluded from the `ahide` capture that OoT3D bakes the mouth into the room mesh and the
N64 actor draws a redundant lip, so the fix was probably draw suppression. **The observation was
right and the inference was wrong.**

Reading `z_bg_treemouth.c` settles it without another experiment:

* `BgTreemouth_Draw` emits exactly ONE display list, `gDekuTreeMouthDL`, in every state. Only an
  env-colour alpha varies. So there is no separate closed-mouth mesh, and my `ahide` capture did show
  this actor's complete visual contribution.
* Lines 226-228 drive the actor's position as a LERP: closed `(4029, 136, -1255)` -> open
  `(3869, -263, -1163)`. The open endpoint is EXACTLY the position I measured with `asel`. So I
  observed the mouth fully OPEN, in the one configuration where the room mesh happens to cover the
  area the actor occupies.

A mesh that MOVES between two positions cannot be baked into a static room mesh in both states.
Suppressing the draw would therefore make the CLOSED mouth invisible and leave the entrance looking
open before Link is allowed in — the exact regression I flagged as the claim's falsifier, and it fired
one tick later.

**What this means for the fix:** OoT3D must ship this geometry somewhere; my ZAR/CMB search simply has
not found it. The searches that came up empty (`/actor/zelda_spot04_objects.zar` absent, no CMB in
`/scene/spot04.zar`) remain valid but no longer support the "baked" conclusion. The next place to look
is the spot04 ROOM CMB's own mesh list — a room CMB can hold several meshes, and OoT3D could keep the
mouth as one of them and move it, which would satisfy both "no separate archive" and "it moves".

**The lesson worth keeping:** a single-state observation cannot distinguish "this geometry is
redundant" from "this geometry is somewhere else right now". I had the right instrument and read one
configuration of a two-configuration mechanism. The cheap guard I skipped was reading the actor's
update function before interpreting a screenshot of it — the lerp is four lines long and would have
told me my capture was of an endpoint, not of the general case.

### Deku Tree mouth — FIXED. The blocker was a ONE-CHARACTER name difference.

`OBJECT_SPOT04_OBJECTS` mapped to NULL because OoT3D drops the "t": the archive is
`zelda_spo04_objects.zar`, and it holds `spot04_kuchi_model.cmb` — *kuchi* (口) is Japanese for MOUTH.

**How I missed it twice, which is the transferable part.** I had enumerated all 461 ROM `.zar` files —
correctly — and then grepped that list for the substring `"spot04"`. `spo04` does not contain `spot04`,
so the search returned only `/scene/spot04.zar` and I concluded no actor archive existed. The
enumeration was sound; the question I asked of it was not. What actually found it was sweeping every
CMB NAME in the ROM (1387 across 461 archives) and grepping those for `kuchi`/`mouth`/`deku` — i.e.
searching for the THING rather than for a guessed container name. Prefer that shape of search: an
asset's own name is chosen by the artist and survives, while an archive name is a naming convention
you are guessing at.

Also worth noting: this is the second time in this session that a stated dead end turned out to be a
wrong diagnosis rather than a real limit (the first was Ghidra's "movw/movt" explanation). Both times
the fix was to check the denominator or re-ask the question, not to accept the negative.

Two further pieces were needed:
* **Explicit CMB routing** — the archive holds ELEVEN CMBs (mouth + `Y_*`/`ousei_*` cutscene models),
  so AUTO's largest-CMB pick would hand Bg_Treemouth a cutscene mesh. And because Bg_Treemouth is
  non-skinned, this only works at all thanks to this session's forced-slot measure-key fix.
* **`noBaseAnchor`** — with the usual `goff = -AutoModelMinY` the mouth rendered as a huge bark slab
  covering the entrance, lifted by its own height. The tell for that class is a measured scale of
  exactly 1.0 (n64h 415.0 / modelh 415.0): Grezzo re-authored the asset in place rather than
  re-originating it, so it belongs at the actor origin. Recorded as claim C030.

**Checked that it DRAWS rather than silently vanishing** — a mesh drawing nothing would look fine in
this open-mouth view and would quietly reintroduce exactly the regression the earlier analysis warned
about. new vs actor-hidden 9337 px; new vs N64 8331 px; actor-hidden vs N64 9257 px. The 3DS footprint
(9337) matches the N64 one (9257), i.e. same footprint, different appearance.

And it resolves C029's worry the right way round: because the mesh is REPLACED and not suppressed, it
still moves with the closed->open lerp, so the closed mouth keeps its geometry. The suppression I was
tempted by two ticks ago would have broken exactly that.

**Two similar name matches deliberately NOT applied.** The same sweep flagged `OBJECT_FD2` ->
`zelda_fd.zar` and `OBJECT_NWC` -> `zelda_nw.zar` as one-character differences. `zelda_fd.zar` holds
*valbasia* (Volvagia) models, so that alias would draw the boss's body for the rubble actor, and
`zelda_nw.zar` is cucco geometry that `niw` already aliases to. Name proximity is not evidence — the
spot04 one was applied because a CMB inside it is literally named "mouth".

### Swept the whole object table with the CMB-name technique — mostly a NEGATIVE, which is the result

Applied the search shape that found the Deku Tree mouth to all 67 NULL object rows. Worth recording
the negative properly, with its denominator, because "there must be more missing models" is an easy
assumption:

* Of 67 NULL rows, almost all are **correctly** NULL: `OA1`-`OA11`, `OB1`-`OB4`, `OE1`-`OE12`,
  `O_ANIME`/`OE_ANIME`/`OS_ANIME`/`ZL2_ANIME*`/`GANON_ANIME*` are animation-only objects with no
  models at all; `GAMEPLAY_KEEP`/`FIELD_KEEP`/`DANGEON_KEEP` and `LINK_BOY`/`LINK_CHILD` are reached
  by other paths; `MJIN_*` (6), `MORI_TEX`, `MEDAL`, `B_HEART`, `FIRE`, `WARP2` are texture- or
  effect-only.
* 348 `/actor/` archives exist, 313 are referenced by the table, so only **35** are unreferenced — and
  most of those are legitimately non-object (`zelda_link_*` ×5, `zelda_keep*` ×2, `gi_*` ×11,
  `zelda_hintstone` is a 3DS-only Sheikah Stone with no N64 object).
* Intersecting those two sets leaves exactly **three** real candidates: VASE, TRAP, PU_BOX.

So the object table was already in much better shape than the Deku Tree case suggested. That one was
not the tip of an iceberg; it was a single one-character naming accident.

Two mapped (VASE verified in game, TRAP structurally confirmed but NOT verified — its N64 draw never
rendered from a bare `spawn`, so the measure pass never ran). PU_BOX deliberately left alone: its
three CMBs are size variants and two actors share the object, so mapping it would recreate the
En_Ishi bug on puzzle geometry.

**Also checked and rejected two tempting one-character matches** (`OBJECT_FD2` -> `zelda_fd.zar`,
`OBJECT_NWC` -> `zelda_nw.zar`). The first holds *valbasia* (Volvagia) models — that alias would draw
the boss's body for the rubble actor. The second is cucco geometry already aliased from `niw`. Name
proximity is not evidence; the spot04 alias earned it by containing a CMB literally named "mouth".

### csab.cpp:115 — FIXED. The largest defect in the audit, and it was a comment asserting a shared layout.

MM3D animation was 100% dead: every skinned 2ship3d actor stood in bind pose while the playhead
advanced. The cause was the claim in the code that OoT3D (subversion 3) and MM3D (subversion 5) differ
only in header offsets and share the anod/track layout. Half of that is true — the anod layout IS
shared, verified with 0 magic mismatches across all 58474 MM3D anods — but the track RECORD is
completely different, so MM3D's type byte does not land where OoT3D's `u32` type does and every one of
168803 tracks fell into the CONSTANT/unknown branch.

Full layout, claim C029... see C031 and the commit. Derived by measurement over the whole ROM, not by
guessing: `u8` flags / `u8` type / `u16` sampleCount / `f32` scale / `f32` offset / `s16` samples one
per frame, `align4(12 + 2n)`.

**The step worth reusing: solving a record size from inter-field gaps.** I did not have to guess the
stride. Taking the byte gap from each track to the next track in the same anod and bucketing by
sampleCount gave 16,16,20,20,24,24,28,28 for n=1..8 — +4 bytes per TWO samples — which fits
`align4(12 + 2n)` and nothing else. That turns a guess into an arithmetic identity, and it is
applicable to any packed table where sibling offsets are available.

Result, measured through the real C++ parser (`tools/csab_anim_check`, not a python twin):
```
MM3D  617 clips  ANIMATES=585  FROZEN=32  unparsed=0    (was 0/109)
OoT3D 189 clips  ANIMATES=183  FROZEN=6   unparsed=0    <- control, unregressed
```
The residual frozen fraction is the same ~3-5% on BOTH branches, which is what genuinely static
single-pose clips look like rather than a lingering parse failure. Having the control in the same units
is what makes that judgement possible at all.

**Two instrument problems found and fixed in the harness itself:**
* Its documented build recipe wrote the binary to `/tmp`, which is a small RAM tmpfs here. Now
  `scratch/bin`.
* It prints `archives=0 clips=0 ANIMATES=0 FROZEN=0` cleanly when no path resolves — indistinguishable
  from "nothing animates". I hit exactly that with an empty argument list and read it as a regression
  for a moment. The caveat is now at the top of the file: check `archives=` against the paths you
  passed before believing any other column. This is the third silent-zero instrument this session
  (after the shared-cap op dump and the misaligned LOD grid).

### MM3D CSAB — verification status, and a CORRECTION to the finding's severity

The decode fix is verified **offline at scale, through the real C++ parser** (`tools/csab_anim_check`,
built against `cmb3d/asset/csab.cpp`, not a python twin): MM3D 585/617 clips animate where 0/109 did
before, with the OoT3D control unregressed at 183/189. That is solid evidence the record layout is
right — the parser now produces motion from data it previously discarded entirely.

It is **NOT yet verified on the live path**, and I am not marking it done on the offline result alone.
Attempting that surfaced two things:

**1. The mm target did not build at all** (fixed, separate commit) — a pre-existing libultraship
layering violation plus my own `AppendCmbTextures` signature regression. So the offline harness was the
only thing that could have caught this defect, and it did.

**2. SEVERITY CORRECTION: the skinned MM3D draw path is behind `ZELDA3D_MM_SKINNED`, default OFF**
(`mm3d_model.cpp:352`). The audit described this as "every skinned actor that plays its own 3DS CSAB
(live and wired)" rendering as a frozen statue. With the gate off — the shipped default — no skinned
MM archive is accepted at all; they fall through to N64. So the user-visible impact was smaller than
the finding stated: the bug was real and total *within* the gated path, not across the shipped game.
The finding's affected-count of 2443 clips is a count of DATA, not of live actors.

Worth noting the gate itself sits awkwardly against the project's no-opt-out-gates rule. It reads as a
deliberate staging flag for in-progress work rather than an N64-original toggle, so I have left it
alone, but it should not survive the skinned path being finished.

**What live verification needs, concretely:** a scene containing an actor whose object maps to a GAR
with a skinned CMB, run with `ZELDA3D_MM_SKINNED=1`. Clock Town South (scene 111) is not one — with
the gate ON it produced no `[MM3D] skinned obj=` and no `[MM3D] skip ... skinned` lines at all, i.e.
no skinned archive was even probed; its six mapped models are all static props (clock tower, doors,
steps, turret). The instruments to watch once in the right scene are those two log lines plus
`[MM3D-ANIM]` (emitted once per unmapped N64 anim OTR) and `ZELDA3D_MM_DBG_SKIN=1` (one line per
skinned emit).
