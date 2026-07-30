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

### [CONFIRMED] * `Shipwright/cmb3d/asset/csab.cpp:115` — user-visible, affected 2443, refuted 0/1
  On the 2ship3d/MM3D branch every skinned actor that plays its own 3DS CSAB (mm3d_model.cpp 'STAGE 4 — CSAB anim', live and wired) renders in the bind pose forever: 100% of 138820 tracks in 2443 clips are silently discarded, with no error log and a correct-looking duration. Actors are frozen statues while the N64 playhead advances.

## PLAYER

### [CONFIRMED] * `Shipwright/zelda3d_shared/player/link_midmask.cpp:53` — user-visible, affected 4, refuted 0/2
  With the sword drawn and no shield on the back, Link's empty sheath/strap disappears instead of staying on his back. Child with the Deku shield and sword drawn loses the sheath ring. Child holding the Mirror shield shows a HYLIAN shield on his back (line 308 lumps MIRROR into `hylian`; the table's child MIRROR row is 14 = sword only).
### [CONFIRMED] * `Shipwright/zelda3d_shared/player/link_midmask.cpp:36` — user-visible, affected 3, refuted 0/1
  Adult Link holding the hookshot/longshot shows a flat open palm with the chain emerging from nothing; playing the Ocarina of Time shows an open palm instead of the ocarina grip. PLAYER_MODELTYPE_RH_OOT (0x0E) is not even in the switch, so it takes the same default.
### [FIXED] * `Shipwright/zelda3d_shared/player/link_midmask.cpp:32` — user-visible, affected 2, refuted 0/2
  Adult Link raising the Mirror Shield displays a HYLIAN shield (wrong texture, wrong silhouette) — the Mirror Shield is visually absent from the whole game. Separately, adult Link in a shield-holding pose with no shield equipped shows an open palm instead of a fist.
### [CONFIRMED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:326` — user-visible, affected 2, refuted 0/1
  Child Link carrying the Hylian or Mirror shield and raising it shows a DEKU shield strapped to his arm. With no shield equipped he shows an open palm where OoT3D (and N64) show a fist.
### [CONFIRMED] * `Shipwright/zelda3d_shared/player/link_midmask.cpp:23` — user-visible, affected 2, refuted 0/1
  Adult Link swinging the Megaton Hammer holds nothing — the hammer is entirely missing. Bottle-holding uses a clenched fist rather than the cupped hand the bottle model is posed against.
### [CONFIRMED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:311` — user-visible, affected 2, refuted 0/1
  Link's hands stay flat-open whenever he runs — in every locomotion state, both ages, which is the most-seen pose in the game.
### [CONFIRMED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:306` — user-visible, affected 2, refuted 0/2
  Drawing the bow/slingshot does not bring up its waist piece (adult mid 43, p_tex26 on bone 23; child mid 22, p_tex27 on bone 23). Small, but it is on screen for the whole of every bow shot.
### [CONFIRMED] * `Shipwright/zelda3d_shared/player/link_midmask.cpp:12` — internal, affected 2, refuted 0/1
  Effectively none, which is the useful half of this result: I compared posed vertex sets and mid 47 contributes only 2 positions not already in mid 46 (6 of its 8 unique points coincide), and child mid 25 shares 80 of its 97 unique points with mid 24 — both are co-located low-poly overlays that would z-fight rather than add silhouette. Do NOT spend a session "fixing" this; do fix the false rationale in the comment/doc, because it is the sort of guess that gets reused.
### [DISPUTED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:314` — user-visible, affected 1, refuted 0/2
  Child Link's Kokiri sword renders as the adult-length Master Sword — a blade nearly as long as he is tall, in every child sword swing, block and idle-with-sword-drawn.
### [FIXED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:329` — user-visible, affected 1, refuted 0/1
  Child Link aiming/firing the slingshot holds an Ocarina of Time in his right hand; the slingshot never appears.
### [CONFIRMED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:315` — user-visible, affected 1, refuted 0/1
  Child Link holding the boomerang shows an object OoT3D never draws (and the real fist+boomerang mesh never appears).
### [CONFIRMED] * `Shipwright/zelda3d_shared/player/link_midmask.cpp:34` — user-visible, affected 1, refuted 0/2
  Adult Link with the bow drawn in third person renders the first-person bow-arm mesh instead of the third-person one (different arm/bow geometry, authored to be seen only from the camera-inside-the-arm angle).
### [CONFIRMED] * `Shipwright/soh/src/zelda3d/player/zelda3d_link.cpp:754` — internal, affected 1, refuted 1/2
  No player impact; a workflow/consistency defect in the file most likely to be instrumented next.

## BEHAVIORS

### [CONFIRMED] * `Shipwright/soh/src/zelda3d/tables/zelda3d_object_zars.inc:49` — user-visible, affected 4, refuted 0/1
  The Deku Tree's mouth — one of the most-looked-at objects in the early game — renders the N64 mesh in an otherwise-3DS Kokiri Forest, and cannot be fixed by the generator alone.
### [CONFIRMED] * `Shipwright/soh/src/zelda3d/render/zelda3d_render.cpp:454` — user-visible, affected 3, refuted 0/1
  Same-object actors that differ in size all render at one size: large Hyrule Field/Kakariko bushes and trees render at the small variant's size; En_Ishi's silver boulder renders at the liftable rock's scale; the En_Ishi rocks and Obj_Hana rock-debris sit 5.8 and 32 world units too low (sunk into the ground).
### [CONFIRMED] * `Shipwright/soh/src/zelda3d/zelda3d.h:355` — user-visible, affected 2, refuted 0/1
  En_Ishi's silver/large rock renders roughly a third of its correct size; the Hyrule Field flower (Obj_Hana type 0) renders far oversized — plausibly taller than Link.
### [CONFIRMED] * `Shipwright/soh/src/zelda3d/render/zelda3d_render.cpp:892` — user-visible, affected 1, refuted 0/2
  The forced-CMB path is unusable for any static prop: the wooden-torch entry can never draw (Obj_Syokudai params>>12==2 silently falls back to the N64 model), and any future non-skinned forced entry will silently do the same. This is the mechanism that would otherwise fix finding #1, so it matters more than the one torch.
### [CONFIRMED] * `Shipwright/soh/src/zelda3d/render/zelda3d_render.cpp:610` — internal, affected 0, refuted 0/2
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
