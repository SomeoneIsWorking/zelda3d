# MM (2S2H) z_player.c — action-function behavioral-RE reference (NOT applied)

Behavioral RE of the numbered `Player_Action_NN` player state-machine action functions in
`2ship/src/overlays/actors/ovl_player_actor/z_player.c` — one row per function: what it does, its
`Player_SetAction(this, <fn>, ...)` install context, the OoT (soh) `z_player.c` structural twin, and a
**proposed** descriptive name. Adversarially verified per function.

> **STATUS: reference only — these names are NOT applied to the code, and should not be batch-applied.**
> Per `docs/re-frontier.md` `mm.action-func-naming`, renaming all 83 as a standalone sweep was
> deliberately ruled out: `2ship` is a vendored fork, the OoT side is itself address-named (no
> descriptive Rosetta stone), and non-canonical names diverge from the eventual upstream zeldaret
> canonical names → permanent merge churn. The established pattern keeps the action funcs **numbered**
> and names only the `Zelda3D_PlayerForce*` wrappers. This table exists to **seed instrumental naming**:
> when `mm.force-hook-layer` (or a future dedicated pass) needs to intercept a specific action func, use
> the behavioral evidence here to identify it — and reconcile the final name against the canonical
> reference, treating the "proposed" column as a hypothesis, not an answer.

The proposed names below survived adversarial verification against the function body + install context;
treat the OoT-equiv column (itself address-named, e.g. `Player_Action_80840450`) as the structural
match, and the evidence column as the checkable behavioral basis.


## Proposed names (verified behavior, non-canonical — do not batch-apply)

| old | new | OoT equiv | evidence (abridged) |
|---|---|---|---|
| `Player_Action_1` | `Player_Action_ReturnToSolidGround` | — | Installed at z_player.c:6640 when this->floorProperty == FLOOR_PROPERTY_13 (and again at 9463), setting PLAYER_STATE1_20000000. Body (14881) runs a full-screen fade (R_PLAY_FILL_SC |
| `Player_Action_2` | `Player_Action_TargetEnemyStand` | `Player_Action_80840450` | Installed by func_80836888 (the Player_CheckHostileLockOn branch of func_80836988/func_808369F4) at z_player.c:6989, and by func_8083B23C. Body (14953) is a near-identical twin of  |
| `Player_Action_3` | `Player_Action_TargetNeutralStand` | `Player_Action_808407CC` | Installed by func_8083692C (the Player_FriendlyLockOnOrParallel branch of func_80836988/func_808369F4) at z_player.c:7005. Body (15016) matches OoT Player_Action_808407CC (z_player |
| `Player_Action_5` | `Player_Action_TargetSidewalk` | `Player_Action_80840DE4` | Installed by func_8083AF30 (z_player.c:9179) which plays D_8085BE84[PLAYER_ANIMGROUP_walk]. Body (15146) is a twin of OoT Player_Action_80840DE4 (z_player.c:8781): sets ANIMMODE_LO |
| `Player_Action_6` | `Player_Action_TargetBackwalk` | `Player_Action_808414F8` | Installed by func_8083AECC (z_player.c:9171). Body (15229) matches OoT Player_Action_808414F8 (z_player.c:8937): opens with func_8083EE60 (=OoT func_80841138, the back_walk->back_r |
| `Player_Action_7` | `Player_Action_TargetBackwalkBrake` | `Player_Action_8084170C` | Installed by func_8083F144 (z_player.c:10648) which plays gPlayerAnim_link_normal_back_brake -- exactly OoT func_8084140C which installs Player_Action_8084170C with the same gPlaye |
| `Player_Action_8` | `Player_Action_WaitForBrakeEnd` | `Player_Action_808417FC` | Body is identical to OoT Player_Action_808417FC: PlayerAnimation_Update, Player_TryActionHandlerList(sActionHandlerList4, true), then if anim finished call the return-to-standing t |
| `Player_Action_9` | `Player_Action_ZTargetSidewalk` | `Player_Action_8084193C` | Body matches OoT Player_Action_8084193C one-to-one: side-walk blend setup (MM func_8083F27C == OoT func_80841860), TryActionHandlerList(sActionHandlerList5), non-target handoff (fu |
| `Player_Action_11` | `Player_Action_BremenMarch` | — | Installed by MM func_80839978: sets itemAction=PLAYER_IA_OCARINA, Player_SetAction_PreserveItemAction(...,Player_Action_11,0), Player_Anim_PlayLoopAdjusted(gPlayerAnim_clink_normal |
| `Player_Action_12` | `Player_Action_KamaroDance` | — | Installed by MM func_80839A10: Player_SetAction_PreserveItemAction(...,Player_Action_12,0), Player_Anim_PlayLoopAdjusted(gPlayerAnim_alink_dance_loop), sets PLAYER_STATE2_2000000,  |
| `Player_Action_13` | `Player_Action_Run` | `Player_Action_80842180` | Body is identical to OoT Player_Action_80842180: sets PLAYER_STATE2_20 (==PLAYER_STATE2_DISABLE_ROTATION_Z_TARGET), func_8083F57C (==func_80841EE4), TryActionHandlerList(sActionHan |
| `Player_Action_14` | `Player_Action_RunTargeting` | `Player_Action_8084227C` | Body is identical to OoT Player_Action_8084227C: PLAYER_STATE2_20, func_8083F57C(==func_80841EE4), TryActionHandlerList(sActionHandlerList9), non-target handoff func_8083A794(==fun |
| `Player_Action_15` | `Player_Action_ZTargetBackpedal` | `Player_Action_808423EC` | Installed by MM func_8083AF8C (Player_SetAction(...Player_Action_15,1) + PlayerAnimation_Change to gPlayerAnim_link_anchor_back_walk, speedXZ=8.0, this->yaw=yaw), the exact paralle |
| `Player_Action_16` | `Player_Action_ZTargetBackpedalEnd` | `Player_Action_8084251C` | Installed by MM func_8083B090 (Player_SetAction(...Player_Action_16,1) + PlayerAnimation_PlayOnceSetSpeed(gPlayerAnim_link_anchor_back_brake, 6/3)), the parallel of OoT func_8083CD |
| `Player_Action_17` | `Player_Action_PlantBean` | `Player_Action_8084279C` | Install (z_player.c:7985-7992): exchangeItemAction==PLAYER_IA_MAGIC_BEANS path does Inventory_ChangeAmmo(ITEM_MAGIC_BEANS,-1) then Player_SetAction_PreserveItemAction(...Player_Act |
| `Player_Action_18` | `Player_Action_Shielding` | `Player_Action_80843188` | Already carries the comment // Player_Action_Shielding. Body: Player_DecelerateToZero, PLAYER_ANIMGROUP_defense_wait loop, sets PLAYER_STATE1_400000 (MM's un-renamed SHIELDING bit) |
| `Player_Action_20` | `Player_Action_Damage` | `Player_Action_8084370C` | Body matches OoT Player_Action_8084370C (z_player.c:9762): Player_DecelerateToZero, Player_TryActionInterrupt(...,16.0f), on anim-done or interrupt>=MOVE calls func_80836988 (OoT f |
| `Player_Action_21` | `Player_Action_KnockbackDown` | `Player_Action_8084377C` | Installed by the damage/knockback path (z_player.c:5946) when Link is hit and thrown (arg2==1/2, not on ground, or in special states). Body is 1:1 with OoT Player_Action_8084377C ( |
| `Player_Action_22` | `Player_Action_KnockbackWake` | `Player_Action_80843954` | Installed only by Action_21 on landing (z_player.c:15793). Body matches OoT Player_Action_80843954 (z_player.c:9822) exactly: disable-rotation flags, Player_DecelerateToZero, and w |
| `Player_Action_23` | `Player_Action_KnockbackGetUp` | `Player_Action_80843A38` | Installed by Action_22 (z_player.c:15819). Body matches OoT Player_Action_80843A38 (z_player.c:9848): sets PLAYER_STATE2_20, in-cutscene just updates anim else Player_TryActionInte |
| `Player_Action_24` | `Player_Action_DeathAndRebirth` | `Player_Action_80843CEC` | Body matches OoT Player_Action_80843CEC (z_player.c:9915): hot-room/floor-type burn check via func_808344C0 (==OoT func_8083821C), Player_DecelerateToZero, and on anim finish (for  |
| `Player_Action_25` | `Player_Action_Airborne` | `Player_Action_8084411C` | The general jump/freefall state: installed from jump/fall paths (z_player.c:5857, 6321, 7639/7656). Body matches OoT Player_Action_8084411C (z_player.c:10031): CheckHostileLockOn g |
| `Player_Action_26` | `Player_Action_Roll` | `Player_Action_Roll` | Already commented '// Player_Action_Rolling' in MM (z_player.c:16012) and installed by the roll setup func_80836B3C non-Goron branch (z_player.c:7070, 8731) and 11230. Body matches |
| `Player_Action_27` | `Player_Action_DiveFall` | `Player_Action_80844A44` | Installed at z_player.c:7682 after func_80834DB8(&gPlayerAnim_link_normal_run_jump_water_fall,...) in the 'dive over water' branch (VB_LINK_DIVE_OVER_WATER). Body plays gPlayerAnim |
| `Player_Action_29` | `Player_Action_JumpSlash` | `Player_Action_80844AF4` | Body reads sMeleeAttackAnimInfo[meleeWeaponAnimation], sets gravity, calls func_8083FCF0(...,6.0f,...) in air and on ground bumps meleeWeaponAnimation+=3, func_80833864, sets melee |
| `Player_Action_30` | `Player_Action_SpinAttackCharge` | `Player_Action_80844E68` | Installed via func_80840EC0 (z_player.c:11291) which mirrors OoT func_80844DC8. Sets PLAYER_STATE1_CHARGING_SPIN_ATTACK, on anim end does Player_Anim_ResetMove + Player_SetParallel |
| `Player_Action_31` | `Player_Action_SpinAttackChargeMove` | `Player_Action_80845000` | Installed via func_80840DEC (z_player.c:11276) — the OoT func_80844CF8 counterpart, reached when func_8083E7F8>0. Sets CHARGING_SPIN_ATTACK, computes yaw-vs-shape.rot delta to sign |
| `Player_Action_35` | `Player_Action_DoorTransition` | `Player_Action_80845CA4` | Body matches OoT Player_Action_80845CA4: opens with Player_ActionHandler_13, uses func_808411D4 movement, R_PLAY_FILL_SCREEN fade, walks player through play->transitionActors.list[ |
| `Player_Action_36` | `Player_Action_OpenDoor` | `Player_Action_80845EF8` | Body matches OoT Player_Action_80845EF8: DECR(doorTimer), on anim finish adjusts skelAnime.endFrame then StopCutscene + Room_FinishRoomChange + Camera_SetFinishedFlag + Play_SetupR |
| `Player_Action_37` | `Player_Action_LiftActor` | `Player_Action_80846050` | Body matches OoT Player_Action_80846050: Player_DecelerateToZero, at anim frame 4.0 grabs this->interactRangeActor (func_808313A8/OoT func_80835644 guard), sets heldActor/actor.chi |
| `Player_Action_38` | `Player_Action_LiftSilverRock` | `Player_Action_80846260` | Body matches OoT Player_Action_80846260 (NOT 80846120): uses gPlayerAnim_link_silver_wait loop on finish, frame 27 attaches interactRangeActor as heldActor, frame 25 plays NA_SE_VO |
| `Player_Action_40` | `Player_Action_FailToLift` | `Player_Action_80846408` | Installed in func_808379C0 when interactRangeActor is EN_BOMBF/EN_KUSA/EN_KUSA2/OBJ_GRASS_CARRY AND Player_GetStrength()<=PLAYER_STRENGTH_DEKU (too weak to lift), playing gPlayerAn |
| `Player_Action_41` | `Player_Action_PutDownActor` | `Player_Action_808464B0` | Installed by func_808309F8 (put-down path, gated by !Player_CanThrowCarriedActor at 8830) and at 10067. Body: Player_DecelerateToZero; on PlayerAnimation_OnFrame frame 4.0 drops he |
| `Player_Action_43` | `Player_Action_FirstPerson` | `Player_Action_8084B1D8` | Body switches on this->unk_AA5 first-person sub-mode (PLAYER_UNKAA5_0..5), handles PLAYER_STATE1_8000000 (in-water) via func_808475B4/func_8084748C, checks pictobox flag, exits via |
| `Player_Action_45` | `Player_Action_GrabWall` | `Player_Action_8084B78C` | Installed by func_80837BF8 (Player_SetAction Player_Action_45). Body sets PLAYER_STATE2_1\|40\|100 (grab flags), func_8083DEE4, on anim-done calls Player_GetMovementSpeedAndYaw(SPE |
| `Player_Action_46` | `Player_Action_Push` | `Player_Action_8084B898` | Body plays gPlayerAnim_link_normal_pushing (Player_Anim_PlayLoopOnceFinished), NA_SE_VO_LI_PUSH voice at frame 11.0, SLIP anim-sfx (D_8085D650), func_8083DEE4; func_8083E758 <0 ->  |
| `Player_Action_47` | `Player_Action_Pull` | `Player_Action_8084B9E4` | Body uses PLAYER_ANIMGROUP_pulling/pull_end and PLAYER_STATE2_10; installed by func_8083E28C via pull_start anim, and transitions into pushing (installs Player_Action_46 push_start |
| `Player_Action_48` | `Player_Action_HangOffLedge` | `Player_Action_8084BBE4` | Installed by Zelda3D_PlayerForceHang mimicking func_80837CEC + PLAYER_ANIMGROUP_jump_climb_hold and setting PLAYER_STATE1_2000 (OoT PLAYER_STATE1_HANGING_OFF_LEDGE). Body plays jum |
| `Player_Action_50` | `Player_Action_Climb` | `Player_Action_8084BF1C` | Installed by func_80837C20 (velocity.y=0). Body is stick-driven wall/ladder/vine climbing using ageProperties climb tables (unk_B4/unk_C4/unk_D4), func_8083DD1C, func_8083E354, gPl |
| `Player_Action_52` | `Player_Action_RideHorse` | `Player_Action_8084CC98` | rideActor=(EnHorse*)this->rideActor; uses gPlayerAnim_link_uma_wait_1/2/3, D_8085D688 uma anim table, Player_SetCameraHorseSetting, EN_HORSE_CHECK_2/3, riderPos positioning, lash s |
| `Player_Action_53` | `Player_Action_DismountHorse` | `Player_Action_8084D3E4` | Body is a byte-for-byte analog of OoT Player_Action_8084D3E4 (z_player.c:14228): both call func_80847E2C/func_8084CBF4(this,1.0f,10.0f) horse-Y settle, on PlayerAnimation_Update co |
| `Player_Action_54` | `Player_Action_SwimIdle` | `Player_Action_8084D610` | Matches OoT Player_Action_8084D610 (z_player.c:14278): loops gPlayerAnim_link_swimer_swim_wait (tread), sets the swim state flag, gates on Player_IsTalking/Player_TryActionHandlerL |
| `Player_Action_55` | `Player_Action_EnterWater` | `Player_Action_8084D7C4` | Matches OoT Player_Action_8084D7C4 (z_player.c:14321): gated by Player_ActionHandler_13, sets swim state flag, func_808477D0(=OoT func_8084B158), func_808475B4(=OoT func_8084B000), |
| `Player_Action_57` | `Player_Action_Swim` | `Player_Action_8084D84C` | Matches OoT Player_Action_8084D84C (z_player.c:14334): sets swim state flag, func_808477D0(sPlayerControlInput)(=OoT func_8084B158), func_808475B4, handler-list/func_8083B3B4 gate, |
| `Player_Action_58` | `Player_Action_SwimZTarget` | `Player_Action_8084DAB4` | Matches OoT Player_Action_8084DAB4 (z_player.c:14397): func_808477D0(sPlayerControlInput), func_808475B4, handler-list/func_8083B3B4 gate, Player_GetMovementSpeedAndYaw, then if sp |
| `Player_Action_59` | `Player_Action_SwimUnderwater` | `Player_Action_8084DC48` | Body drives the underwater deep-dive swim: gPlayerAnim_link_swimer_swim_wait/_swim loop, depthInWater vs ageProperties->unk_30 buoyancy, PLAYER_BOOTS_ZORA_UNDERWATER branch, func_8 |
| `Player_Action_60` | `Player_Action_SwimToSurface` | `Player_Action_8084E1EC` | Structural clone of OoT Player_Action_8084E1EC (soh:14643): NA_SE_VO_LI_BREATH_DRINK on frame 5, PLAYER_STATE1_400/GETTING_ITEM branch with func_808482E0 (=OoT func_8084DFF4) on fr |
| `Player_Action_61` | `Player_Action_SwimHit` | `Player_Action_8084E30C` | Installed by the damage handler specifically in the PLAYER_STATE1_8000000 (IN_WATER) branch (z_player.c:5930) and with gPlayerAnim_link_swimer_swim_hit (z_player.c:11225). OoT's da |
| `Player_Action_62` | `Player_Action_DeathInWater` | `Player_Action_8084E368` | Installed by func_80831F34 as Player_SetAction(this, func_801242B4(this) ? Player_Action_62 : Player_Action_24) while playing gPlayerAnim_link_derth_rebirth with endFrame 84 (z_pla |
| `Player_Action_63` | `Player_Action_PlayOcarina` | `Player_Action_8084E3C4` | The ocarina/instrument-playing action: installed via Player_SetAction_PreserveItemAction from the ocarina-start path (z_player.c:8056), calls Message_DisplayOcarinaStaff(OCARINA_AC |
| `Player_Action_64` | `Player_Action_ThrowDekuNut` | `Player_Action_8084E604` | Byte-for-byte behavioral match to OoT Player_Action_8084E604 (soh:14750): PlayerAnimation_Update -> func_80836A98(gPlayerAnim_link_normal_light_bom_end); on frame 3 spawns ACTOR_EN |
| `Player_Action_65` | `Player_Action_GetItem` | `Player_Action_8084E6D4` | Body plays gPlayerAnim_link_demo_get_itemA/B (Deku pn_getA/B), sets csId=playerCsIds[PLAYER_CS_ID_ITEM_GET], handles getItemId/getItemDrawIdPlusOne incl GID_REMAINS_ODOLWA/GOHT/GYO |
| `Player_Action_67` | `Player_Action_DrinkFromBottle` | `Player_Action_8084EAC0` | Plays gPlayerAnim_link_bottle_drink_demo_wait/end (Deku pn_drink/pn_drinkend), fills gSaveContext.healthAccumulator / Magic_Add per D_8085D790 potion table, handles PLAYER_IA_BOTTL |
| `Player_Action_68` | `Player_Action_SwingBottle` | `Player_Action_SwingBottle` | Swings bottle to catch: iterates catch table D_8085D798 against interactRangeActor (En_Elf fairy, En_Fish, En_Insect, En_Zoraegg, En_Poh...), plays NA_SE_IT_SCOOP_UP_WATER, Message |
| `Player_Action_69` | `Player_Action_ReleaseFairy` | `Player_Action_8084EED8` | On anim frame 37 plays NA_SE_EV_BOTTLE_CAP_OPEN + NA_SE_VO_LI_AUTO_JUMP and calls Player_SpawnFairy(leftHandWorld.pos, FAIRY_TYPE_8/1); if PLAYER_IA_BOTTLE_FAIRY empties bottle and |
| `Player_Action_70` | `Player_Action_DropBottledActor` | `Player_Action_8084EFC0` | Empties a bottled creature: on anim frame 76 Actor_Spawn's D_8085D80C[GET_BOTTLE_FROM_IA(itemAction)-1] (En_Fish, Obj_Aqua spring water, En_Zoraegg, En_Dnp Deku princess, En_Ot sea |
| `Player_Action_72` | `Player_Action_GrabbedByEnemy` | `Player_Action_8084F308` | Loops gPlayerAnim_link_normal_re_dead_attack_wait, mashes BTN_R (func_8082F164) except in SCENE_SEA_BS, and on func_8082DE88(this,0,0x64) escape-struggle threshold calls func_80836 |
| `Player_Action_77` | `Player_Action_Death` | `Player_Action_8084F88C` | Body is a near-exact structural match to OoT Player_Action_8084F88C: `av2.actionVar2++ >= threshold` gate, then branches on `av1.actionVar1` between a voidout/respawn path (respawn |
| `Player_Action_80` | `Player_Action_RideSwampBoat` | — | MM-only. The file's own comment at line 13928 labels `this->actionFunc == Player_Action_80` as "Riding swamp boat (non-archery)". Gated entirely on MM-specific state: play->bButton |
| `Player_Action_81` | `Player_Action_BowMinigame` | — | MM-only. The file's own comment at line 13929 labels `this->actionFunc == Player_Action_81` as "Bow minigames". Body is the first-person aim/shoot update: sets unk_AA5, func_808386 |
| `Player_Action_82` | `Player_Action_Frozen` | `Player_Action_8084FB10` | Installed by the damage-reaction dispatcher (line 5908) for arg2==3 with anim gPlayerAnim_link_normal_ice_down, NA_SE_PL_FREEZE_S / NA_SE_VO_LI_FREEZE — the ice-trap/frozen reactio |
| `Player_Action_83` | `Player_Action_Electrocuted` | `Player_Action_8084FBF4` | Installed by the damage-reaction dispatcher (line 5918) for arg2==4 with anim gPlayerAnim_link_normal_electric_shock. Body matches OoT Player_Action_8084FBF4 line-for-line: PlayerA |
| `Player_Action_84` | `Player_Action_MeleeAttack` | `Player_Action_808502D0` | Installed by func_80833864 (the melee-slash setup) via Player_SetAction(...,Player_Action_84,0); reads AttackAnimInfo* = &sMeleeAttackAnimInfo[this->meleeWeaponAnimation]. Body mat |
| `Player_Action_85` | `Player_Action_MeleeWeaponRebound` | `Player_Action_808505DC` | MM body is byte-identical to OoT Player_Action_808505DC: PlayerAnimation_Update + Player_DecelerateToZero + `if(curFrame>=6.0f) func_80836988(this,play)` (OoT calls func_80839FFC a |
| `Player_Action_86` | `Player_Action_MaskTransformation` | — | Installed by func_808388B8 (Player_SetAction_PreserveItemAction, line 7783) which plays D_8085D160[transformation] maskoffstart/cl_setmask anims and sets gSaveContext.save.playerFo |
| `Player_Action_87` | `Player_Action_FinishMaskTransformation` | — | Installed at line 11698 inside the post-transformation reload path (right after Player_InitCommon with gPlayerSkeletons[transformation], new ageProperties, prevBoots). Body un-fill |
| `Player_Action_88` | `Player_Action_ElegyOfEmptiness` | — | Installed at line 18245 only in the OCARINA_MODE_EVENT + lastPlayedSong==OCARINA_SONG_ELEGY branch. Body counts actionVar2; at frame 10 calls func_80848640 which spawns/repositions |
| `Player_Action_89` | `Player_Action_PutOnGiantsMask` | — | Installed by func_808389BC (line 7794) playing gPlayerAnim_cl_setmask with PLAYER_STATE1_100. Body: func_80855218 (setmask SFX/anim driver), then on !(stateFlags1 & PLAYER_STATE1_1 |
| `Player_Action_90` | `Player_Action_TakeOffGiantsMask` | — | Installed by func_80838A20 (line 7801) which plays gPlayerAnim_cl_maskoff, sets currentMask=PLAYER_MASK_NONE, PLAYER_STATE1_100, and Magic_Reset (Giant's Mask is the magic-draining |
| `Player_Action_91` | `Player_Action_WarpTagArrive` | — | Installed only by Player_StartMode_WarpTag (z_player.c:11476), which sets unk_ABC=-10000 and spins shape.rot.y down onto a warp pad. Body calls func_808323C0(this, play->playerCsId |
| `Player_Action_93` | `Player_Action_DekuFlowerLaunch` | — | File comment 'Deku Flower related'. State machine sinks Link into a Deku flower (unk_ABC stepping to -3900, actor.scale.y shrinking), charges while BTN_A held (av2.actionVar2), the |
| `Player_Action_94` | `Player_Action_DekuFlowerFly` | — | File comment 'Flying as Deku?'. Installed by Player_Action_93's flower launch (z_player.c:19795, also 19891). Uses Deku glide anims gPlayerAnim_pn_kakku/pn_batabata/pn_kakkufinish/ |
| `Player_Action_95` | `Player_Action_DekuSpinAttack` | — | Installed by func_80839A84 gated on this->transformation==PLAYER_FORM_DEKU (z_player.c:8367). Body sets Player_SetCylinderForAttack(DMG_DEKU_SPIN), spins actor.shape.rot.y via unk_ |
| `Player_Action_96` | `Player_Action_GoronRoll` | — | Installed by func_80836AD8 (z_player.c:7046). Comment 'Goron rolling related'. Body is the Goron curled-ball roll: NA_SE_PL_GORON_BALLJUMP/GORON_SLIP/GORON_PUNCH/GORON_BALL_CHARGE_ |

## Extra-low-confidence (flag before trusting even as a hypothesis)

**Duplicate-name pairs** — two distinct funcs each RE'd to the same name; their OoT equivalents
are themselves still numbered in soh, so the names were behavior-inferred. Need disambiguation before
applying (a duplicate symbol would not compile):

- `Player_Action_33` → proposed `Player_Action_ClimbOntoLedge` (oot `Player_Action_80845668`): Body byte-matches OoT Player_Action_80845668: same anims gPlayerAnim_link_normal_250jump_start / link_swimer_swim_15step_up (frame 30) / normal_150ste
- `Player_Action_39` → proposed `Player_Action_ThrowActor` (oot `Player_Action_80846358`): Body matches OoT Player_Action_80846358: on anim frame 6.0 sets heldActor->world.rot.y = shape.rot.y, speed=10.0f, velocity.y=20.0f, func_808309CC (Oo
- `Player_Action_42` → proposed `Player_Action_ThrowActor` (oot `Player_Action_80846578`): Installed by func_808308A0 (throw path, selected over put-down by Player_CanThrowCarriedActor at 8741) and at 10038. Body: DecelerateToZero; finishes 
- `Player_Action_49` → proposed `Player_Action_ClimbOntoLedge` (oot `Player_Action_8084BDFC`): Installed by func_808381A0 (PlayerAnimation_PlayOnceSetSpeed 1.3f). Body: Player_AnimReplace_SetupLedgeClimb, NA_SE_PL_CLIMB_CLIFF + NA_SE_VO_LI_CLIMB

**Low / unverified confidence** — proposal recorded, not applied:

- `Player_Action_19` → `Player_Action_StartShielding` [unverified/high]: Body is a line-for-line match of OoT Player_Action_808435C4 (z_player.c:9736): Player_DecelerateToZero; if av1==0 update upper body and (upp
- `Player_Action_28` → `Player_Action_ZoraDive` [revise/medium]: Verified body (16096) + install (9301): Zora-specific airborne ballistic arc after leaping out of water (NA_SE_EV_JUMP_OUT_WATER, speedXZ=cos(unk_AAA)*sp24, velocity.y=-sin(unk_AAA)*sp24), gravit
- `Player_Action_32` → `Player_Action_SpinAttackChargeSideWalk` [revise/medium]: Body is a verbatim port of OoT Player_Action_80845308 (mm_specific=false is correct): identical speedXZ/unk_B4C rotation logic into func_8083EA44 (OoT func_8084029C), identical D_
- `Player_Action_51` → `Player_Action_ClimbOffLadder` [revise/medium]: Body genuinely corresponds to OoT Player_Action_8084C5F8 (Player_TryActionInterrupt 4.0f, clears CLIMBING_LADDER/0x200000, ladder-walk AnimSfx, actionVar2 top/bottom raycast+landing sfx), a
- `Player_Action_56` → `Player_Action_ZoraFastSwim` [revise/medium]: Body verified: Player_Action_56 is genuinely the MM Zora-form fast/boost swim. It uses gPlayerAnim_pz_fishswim / pz_swimtowait / pz_waterroll, gates on currentBoots==PLAYER_BOOTS_ZORA_LAND, p
