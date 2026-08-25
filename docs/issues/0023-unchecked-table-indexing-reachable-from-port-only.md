# 0023 — unchecked table indexing reachable from port-only callers (audit)

status: OPEN — 9 of 22 fixed 2026-08-13; the rest catalogued here, unfixed
found by: a 33-agent audit run after the SAME bug was found twice in one day by accident

## The category

Code that is provably safe while the ONLY caller is retail game code that never constructs an
out-of-range value, and which becomes a NULL/OOB dereference once this port adds a caller the
original never had. Two crashes on 2026-08-12 were this, in unrelated files:

- `z_camera.c` `Camera_Init` gated its register-table fill on `static s32 sInitRegs`. Correct on a
  console (one game per boot); wrong in a launcher that runs several cores in one process. Run 2 ran
  the camera on an all-zero OREG table -> `1.0f / rUpdateRateInv` = inf -> NULL floor poly deref.
- `z_scene_table.c` `Entrance_GetTableEntry` indexed `sSceneEntranceTable[entrance >> 9]` with no
  check. 22 of 110 scene slots are `SCENE_ENTRANCE_NONE()` = `{ 0, NULL, NULL }`. REPL `warp 0x1000`
  dereferenced NULL. The struct even HAD a `tableCount` field, commented "unused".

**The port-only callers that make these reachable:** the REPL command handlers (they parse user
text), the multi-core launcher (any `static` "already initialised" latch), the randomizer,
savestates, enhancement/CVar code, the ImGui developer tools, and hand-edited saves.

## Audit, 2026-08-12

Six scanners over slices of the vendored decomp, each finding verified by a separate ADVERSARY
agent told to refute it and to default to refuted when uncertain. **2197 files examined; 27
findings verified; 22 confirmed, 5 refuted.** Seven further findings were reported but NOT verified
(the run capped verification at 6 per slice) — they are not listed here because they carry no
adversarial check, and an unverified finding in a catalogue reads as a confirmed one.

Denominators per slice: mm-core 31 files, oot-core 154, mm-overlays-port 1911, oot-port-layer 31,
zelda3d-layer 42, libultraship 28.

### Fixed already

- `Flags_{Get,Set,Unset}EventChkInf` (`z_actor.c`) — `eventChkInf` is `u16[14]`, flags 0..223,
  indexed by `flag >> 4` unchecked. REPL `eventflag` parses `%i`, so `eventflag 224` wrote past it
  and `eventflag -1` wrote BELOW it (arithmetic shift: `-1 >> 4 == -1`). SILENT SaveContext
  corruption — worse than a crash, because every later measurement in the session is quietly wrong.
  Guarded in the engine and again at the REPL; a second, unreachable copy of the handler deleted.
  Verified on all four boundaries: 224 and -1 refused, 0x40 and 223 still work.
- `Entrance_GetTableEntry` / REPL `warp` (commit 050942b1, see above).
- `Actor_LoadOverlay` (`2ship/src/code/z_actor.c`) — `&gActorOverlayTable[index]` with a raw s16.
  The bound existed as `ACTOR_ID_MAX` (0x2B2) all along and was read only by the fault handler —
  the `tableCount` shape again. Reachable from the developer console `spawn` (std::stoi, catching
  only std::invalid_argument) and from ActorViewer's unclamped s16 ImGui field.
- `Actor_Spawn` (`Shipwright/soh/src/code/z_actor.c`) — **the audit overstated this one and the
  correction matters.** `ActorDB::RetrieveEntry` DOES bound-check and returns a default-constructed
  `invalid` entry, so there is no wild read. The real defect is that the guard was
  `assert(dbEntry->valid)` and this build defines `NDEBUG`, so it compiled to nothing: an invalid id
  proceeded into a zero-`instanceSize` arena allocation and a null init call. Replaced with a
  runtime check. Verified live on both classes: `spawn 0x00B` still OK, `spawn 20000` and
  `spawn -1` refused with the log line `z_actor.c:3380 Actor_Spawn: actor id N is not a valid
  ActorDB entry -- REFUSED` proving the new check is what fired, process alive throughout.
- OoT REPL `warp` — the counterpart of the MM entrance crash. `gEntranceTable` is read UNBOUNDED at
  `z_play.c:880` (`[nextEntranceIndex + sceneLayer]`) and `z_play.c:546`
  (`[entranceIndex + sceneSetupIndex]`). The handler's long comment fixed the STATE a warp is issued
  in (cutsceneIndex, gameMode) but never the index. Bounded on the SUM with 19 frames of headroom,
  because sceneSetupIndex reaches `4 + (cutsceneIndex & 0xF)`.
- REPL `boots` — `currentBoots` indexes `sBootData[PLAYER_BOOTS_MAX][17]`, and Player_SetBootData
  copies eight s16 from that row into REG(19)/REG(30)/REG(32)/REG(34..38): Link's speed, friction and
  jump regs. `boots 100` read row 99 and fed garbage into his movement — it does not crash and does
  not look like a bad command, it looks like a PHYSICS BUG. Bounded to the three EQUIPPABLE boots
  (1..3), deliberately NOT to PLAYER_BOOTS_MAX (6): rows 3..5 are internal states Player_SetBootData
  selects itself, so bounding to 6 would be memory-safe and still wrong.
- REPL `wingmap` — each source axis is used as `dd[src]` in Zelda3D_ApplyProcOverride, which tested
  only `src >= 0`. A value of 3+ read past the three-float source and the result went into a bone
  rotation: silently wrong, not a crash.
- `SubS_GetPathByIndex` (`2ship/src/code/z_sub_s.c`) — the ROOT of five separate overlay findings
  (`EnIk_Draw`, `EnPst_FollowSchedule`, `EnGirlA_InitObjIndex`, `BgCtowerRot_Draw`, `EnBoom_Draw`),
  since ~40 callers reach it with `pathIndex` from actor params. Two guards, both load-bearing:
  `play->setupPathList` is `nullptr` in any scene with no path list, and `&NULL[pathIndex]` is a
  small NON-NULL address that passes the caller's own `path == NULL` test and is then dereferenced
  as a `Path`. The bound needed a length that PlayState did not record — `SetPathwaysMM` carries
  `numPaths` and `Scene_CommandPathList` kept only the pointer. `setupPathCount` is now appended
  PAST the N64 struct so no documented offset shifts, and the dead N64 handler zeroes it so a revival
  FAILS SAFE (paths visibly stop working) rather than bounding against a stale count.
  **Verification is partial and that is stated deliberately:** two scenes of normal play (Clock Town,
  Termina Field) produce ZERO refusals, so the bound is not too tight and path NPCs still work — but
  the refusing branch has NOT been observed firing, because MM's REPL has no `spawn` command and the
  developer-console path is ImGui-only. Someone should drive a bad params value through it before
  treating this guard as proven.
- `EnIk_Draw` (`2ship/src/overlays/actors/ovl_En_Ik/z_en_ik.c`) — scene data encodes Iron Knuckle
  armor as 1..3, which init converts to the 0..2 row in `sIronKnuckleArmorType`. A port-only spawn
  can supply any params: zero became -1 and any low byte above 3 became an out-of-range draw-table
  index. Init now refuses invalid armor types before setting up actor state; valid scene values retain
  their exact old mapping.

### Confirmed, NOT yet fixed

| site | symbol | severity | confidence | reachability |
| --- | --- | --- | --- | --- |
| `2ship/src/overlays/actors/ovl_En_Pst/z_en_pst.c:643` | EnPst_FollowSchedule | crash | high | ActorViewer / DebugConsole `spawn` of ACTOR_EN_PST with any Params outside 0..4. |
| `2ship/src/overlays/actors/ovl_En_GirlA/z_en_girla.c:166` | EnGirlA_InitObjIndex | crash | high | ActorViewer / DebugConsole `spawn` of ACTOR_EN_GIRLA with arbitrary Params; also any shop-actor param supplied by a mod or hand-edited scene. |
| `2ship/src/overlays/actors/ovl_Bg_Ctower_Rot/z_bg_ctower_rot.c:145` | BgCtowerRot_Draw | crash | high | ActorViewer / DebugConsole `spawn` of ACTOR_BG_CTOWER_ROT with Params>=3 in the Clock Tower scene. |
| `2ship/src/overlays/actors/ovl_En_Boom/z_en_boom.c:337` | EnBoom_Draw | crash | high | ActorViewer / DebugConsole `spawn` of ACTOR_EN_BOOM with Params>=2 (gameplay_keep object, so it is resident in every scene). |
| `2ship/src/overlays/actors/ovl_Boss_06/z_boss_06.c:168` | Boss06_Init | wrong-behaviour | high | ActorViewer / DebugConsole `spawn` of ACTOR_BOSS_06 with Params>=2 in Majora's/Twinmold's lair. |
| `Shipwright/soh/soh/Enhancements/randomizer/SeedContext.cpp:554` | Rando::Context::GetSeedTexture | crash | medium | Port-only file-select seed-hash display. soh/src/overlays/gamestates/ovl_file_choose/z_file_choose.c:327-328 calls `GetSeedTexture(Save_GetSaveMetaInfo(this->selectedFileIndex)->seedHash[i])` — every  |
| `Shipwright/soh/soh/Enhancements/randomizer/randomizer_entrance.c:244` | Entrance_Init | corruption | high | `entranceOverrides` is `Randomizer_GetEntranceOverrides()`, i.e. `entranceCtx->entranceOverrides`, which SaveManager.cpp:195-204 fills field-by-field from the save JSON: `LoadData("index", entranceCtx |
| `Shipwright/soh/soh/Enhancements/randomizer/dungeon.cpp:199` | Dungeons::GetDungeon | corruption | medium | SaveManager.cpp:243-247 — `LoadArray("masterQuestDungeons", mqDungeonCount, [&](size_t i) { size_t dungeonId; LoadData("", dungeonId); randoContext->GetDungeon(dungeonId)->SetMQ(); })`. Both `dungeonI |
| `Shipwright/soh/soh/Enhancements/randomizer/trial.cpp:47` | Trials::GetTrial | corruption | high | SaveManager.cpp:250-254 — `LoadArray("requiredTrials", randoContext->GetOption(RSK_TRIAL_COUNT).Get(), [&](size_t i) { size_t trialId; LoadData("", trialId); randoContext->GetTrial(trialId)->SetAsRequ |
| `Shipwright/soh/soh/Enhancements/randomizer/item_list.cpp:476` | Rando::StaticData::RetrieveItem | crash | high | Console command `give_item randomizer <n>` — debugconsole.cpp:396: `getItemEntry = Rando::StaticData::RetrieveItem((RandomizerGet)std::stoi(args[2])).GetGIEntry_Copy();`. `args[2]` is user-typed text  |
| `Shipwright/soh/src/zelda3d/repl/commands/save_player_state.cpp` | `HandleEventFlag` — `eventflag` command | corruption | high | REPL command handler (a port-only caller): `eventflag 0x1000` from tools/zelda3d_repl.py. The focused owner now rejects values outside the engine's 0..223 event-check range before calling the shipping flag API. |
| `Shipwright/soh/src/zelda3d/repl/commands/item_equipment.cpp` | `Zelda3D_ReplItemEquipmentCommand` — `boots` command | wrong-behaviour | high | REPL command handler (a port-only caller): the old `boots 100` path set currentBoots = 99, so the next Player_SetBootData read outside sBootData. The focused owner now bounds the three equippable boot values before mutation. |
| `Shipwright/libultraship/src/ship/resource/ResourceLoader.cpp:278` | Ship::ResourceLoader::ReadResourceInitDataXml | crash | medium | Port-only caller: MODS / custom asset archives. ResourceLoader::ReadResourceInitDataLegacy (same file, line 88) routes any resource file whose first byte is '<' into the XML branch, and both cores reg |
| `Shipwright/libultraship/src/fast/resource/factory/DisplayListFactory.cpp:234` | Fast::ResourceFactoryXMLDisplayListV0::ReadResource | crash | high | Same port-only caller as above: a mod-supplied XML DisplayList asset. ResourceFactoryXMLDisplayListV0 is registered for RESOURCE_FORMAT_XML by both cores (OTRGlobals.cpp:1033, BenPort.cpp:695) and is  |

### Refuted by the adversary (do NOT re-report these)

| site | symbol | guard found | why it is safe |
| --- | --- | --- | --- |
| `Shipwright/soh/src/code/z_scene.c` | Object_Spawn | Shipwright/soh/src/boot/z_std_dma.c:437 (DmaMgr_SendRequest1 | The claim's stated failure mode is falsified by the port's own code. (1) The "sooner in practice" half — "walk status[num].segment past spaceEnd so th |
| `Shipwright/soh/src/code/z_scene_table.c` | Scene_SetTransitionForNextEntrance |  | Refuted on reachability (criterion 3), not on a guard — the read at z_scene_table.c:107 is genuinely unchecked. But: (a) the claimed reachability is f |
| `Shipwright/soh/soh/Enhancements/randomizer/randomizer_entrance.c` | Entrance_SceneAndSpawnAre | Shipwright/soh/src/overlays/actors/ovl_player_actor/z_player | The function genuinely lacks a bounds check, but the claim's reachability argument is refuted on two independent grounds. (1) Its stated mechanism is  |
| `Shipwright/soh/src/zelda3d/player/player_repl.cpp` + `player_draw.cpp` | linkjointdump command/capture | Shipwright/soh/include/z64player.h:382 (LIMB_BUF_COUNT ALIGN | Premise is wrong: Player's jointTable is NOT arena-allocated. z_player.c:11175 calls SkelAnime_InitLink(..., 9, this->jointTable, this->morphTable, PL |
| `Shipwright/libultraship/src/fast/resource/factory/VertexFactory.cpp` | Fast::ResourceFactoryXMLVertexV0::ReadResource | Shipwright/libultraship/src/ship/resource/ResourceLoader.cpp | Refuted on structure + scope, though not on absolute impossibility.

(1) STRUCTURAL GUARANTEE ON THE PATH THAT ACTUALLY EXISTS. The only route by whic |

## How to work this list

Fix the ones reachable from OUR OWN tooling first: a REPL command that corrupts state silently
makes every measurement taken after it untrustworthy, which is a workflow-integrity problem and not
just a bug. Fix BOTH ends each time — the engine function (one place, covers randomizer and mods)
and the command handler (so the reply cannot claim success after a refused write).

Do not "fix" a site by clamping the index to the array bound. That converts an OOB write into a
wrong-but-plausible write, which is the harder bug. Refuse, log the value and the valid range, and
change nothing.
