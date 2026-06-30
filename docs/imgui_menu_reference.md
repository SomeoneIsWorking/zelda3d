# SoH3D ImGui Menu & Dev-Tooling Reference

A faithful catalogue of the legacy Ship-of-Harkinian **ImGui** menu (the "Port Menu" /
`SohMenu`) and every registered dev/debug **GuiWindow**, captured so the owner can decide
what to re-implement in the new **RmlUi** menu before the ImGui code is deleted.

> Status note: in SoH3D the ImGui menu is already **force-hidden at boot**
> (`SohGui::SetupMenu()` calls `mSohMenu->Hide()`), and `ShowEscMenu()` is a no-op — the
> RmlUi menu is the in-game menu now. The structure below is what the ImGui menu *contained*,
> i.e. the candidate set for migration.

## Source map

| Concern | File |
|---|---|
| Window/menu registration, delegates | `Shipwright/soh/soh/SohGui/SohGui.cpp` |
| Menu shell, sidebar/widget builders | `Shipwright/soh/soh/SohGui/SohMenu.cpp`, `SohMenu.h` |
| Widget/sidebar data types | `Shipwright/soh/soh/SohGui/MenuTypes.h` |
| **Settings** tab | `Shipwright/soh/soh/SohGui/SohMenuSettings.cpp` |
| **Enhancements** tab (incl. Cheats) | `Shipwright/soh/soh/SohGui/SohMenuEnhancements.cpp` |
| **Randomizer** tab | `Shipwright/soh/soh/SohGui/SohMenuRandomizer.cpp` |
| **Network** tab | `Shipwright/soh/soh/SohGui/SohMenuNetwork.cpp` |
| **Dev Tools** tab | `Shipwright/soh/soh/SohGui/SohMenuDevTools.cpp` |
| Generic widget layer (UIWidgets) | `Shipwright/soh/soh/SohGui/UIWidgets.cpp/.hpp` |
| Menu base (sidebars/header draw) | `Shipwright/soh/soh/SohGui/Menu.cpp/.h` |
| Resolution editor (Advanced) | `Shipwright/soh/soh/SohGui/ResolutionEditor.cpp` |
| Modal/popup system | `Shipwright/soh/soh/SohGui/SohModals.cpp/.h` |

The menu is a **header-tabs + left-sidebar** layout (not a classic menu bar). Top tabs are
"sections"; each section has named **sidebars**; each sidebar lays widgets into 1-3 columns.
Order of tabs is fixed by `SohMenu::AddMenuElements()`: **Settings, Enhancements, Randomizer,
Network, Dev Tools**.

---

## MIGRATION NOTES (read first)

For a 3DS-asset port (SoH3D), the overwhelming majority of this menu is OoT-gameplay /
randomizer / streamer tooling that is **droppable**. The handful plausibly worth porting to
RmlUi:

**Likely worth migrating**
- **Settings → Graphics**: Internal Resolution, MSAA, Texture Filter, Vsync, Windowed
  Fullscreen, Interpolation/Current FPS, Match Refresh Rate, Renderer API. Core PC-window
  knobs. (Advanced Resolution editor in `ResolutionEditor.cpp` is optional/extra.)
- **Settings → Audio**: master/music/SFX volume sliders + Audio API. Basic and useful.
- **Settings → General**: Menu Theme/scaling, Cursor Always Visible, Boot Sequence,
  Language/Translate Title — a few are genuinely useful; most are ImGui-menu chrome.
- **Enhancements → Graphics**: the *rendering-relevant* toggles for a 3D-asset port —
  Disable LOD, Disable Bomb Billboarding, Disable Grotto Fixed Rotation, Disable Link's
  Sword Trail, Enable 3D Dropped Items/Projectiles, Disable 2D Pre-Rendered Scenes / Disable
  Fixed Camera, Show Gauntlets in First-Person, N64 Mode, Increase Actor Draw Distance,
  Widescreen Actor Culling. These intersect directly with 3DS-model work.
- **Dev viewers genuinely useful here**: **ActorViewer**, **Collision Viewer**,
  **Display List Viewer**, **GfxDebugger**, **Stats**, **Console**, and the **Debug Mode /
  Frame Advance / Warp** controls. These help inspect the 3DS render path.
- **Cosmetics Editor / Audio Editor**: maybe, if asset re-skinning is wanted; otherwise skip.

**Almost certainly droppable (SoH/randomizer/streamer cruft)**
- The entire **Randomizer** tab (General/Locations/Tricks/Plandomizer/Item-Check-Entrance
  trackers), the **Network** tab (Sail/Crowd Control/Anchor), **Cheats**, **Difficulty**,
  **Minigames**, **Extra Modes**, **Skips & Speed-ups**, **Quality of Life**, **Items**,
  **Fixes/Restorations**, **Time Splits**, **Gameplay Stats**, **Timers**, **Notifications**,
  **Input Viewer**, **Mod Menu**, **Save Editor**, **Value/Message/Hook viewers**.
- Note: SoH3D already force-enables the genuine bug-fix enhancements at boot
  (`ForceBugFixesOn()` / `kSoh3dForcedFixes[]` in `SohGui.cpp`) and deliberately does **not**
  surface them in the RmlUi menu — so the Enhancements→Fixes group does not need migrating.

---

## CVar prefix legend

Macros expand to string prefixes (see `soh/cvar_prefixes.h`):

| Macro in tables | Expands to |
|---|---|
| `CVAR_SETTING("X")` | `gSettings.X` |
| `CVAR_ENHANCEMENT("X")` | `gEnhancements.X` |
| `CVAR_CHEAT("X")` | `gCheats.X` |
| `CVAR_DEVELOPER_TOOLS("X")` | `gDeveloperTools.X` |
| `CVAR_RANDOMIZER_SETTING("X")` | `gRandoSettings.X` |
| `CVAR_RANDOMIZER_ENHANCEMENT("X")` | `gRandoEnhancements.X` |
| `CVAR_WINDOW("X")` | `gWindows.X` (window open/visible state) |
| `CVAR_REMOTE_SAIL`/`CVAR_REMOTE_CROWD_CONTROL` | network remote cvars |
| `CVAR_TIME_DISPLAY("X")` | additional-timers cvars |

Control-type abbreviations: **chk** = checkbox, **cmb** = combobox, **sld** = slider
(int/float), **btn** = button, **txt** = text/label, **win** = window-toggle button,
**sel** = button-combo selector, **custom** = bespoke ImGui draw.

---

# 1. MENU BAR STRUCTURE

## 1.1 Settings  (`SohMenuSettings.cpp`)

### Sidebar: General — col 1
| Label | Type | CVar | Notes |
|---|---|---|---|
| Menu Theme | cmb | `gSettings.Menu.Theme` | Widget color theme |
| Menu Controller Navigation | chk | `CVAR_IMGUI_CONTROLLER_NAV` | D-pad navigate menu (non-Switch/WiiU) |
| Allow background inputs | chk | `CVAR_ALLOW_BACKGROUND_INPUTS` | Inputs when window unfocused |
| Menu Background Opacity | sld f | `gSettings.Menu.BackgroundOpacity` | |
| Cursor Always Visible | chk | `gSettings.CursorVisibility` | |
| Search In Sidebar | chk | `gSettings.Menu.SidebarSearch` | Move search box into sidebar |
| Search Input Autofocus | chk | `gSettings.Menu.SearchAutofocus` | |
| Reset Button Combination | sel | `gSettings.ResetBtn` | |
| Open App Files Folder | btn | — | Opens save/mods folder |
| Boot Sequence | cmb | `gSettings.BootSequence` | Default/Authentic/File Select/Debug Warp/Warp Point |
| Translate Title Screen | chk | `gSettings.TitleScreenTranslation` | |
| Language | cmb | `gSettings.Languages` | Eng/Ger/Fra/Jpn |
| Text to Speech | chk | `gSettings.A11yTTS` | Win/Mac/espeak only |
| Disable Idle Camera Re-Centering | chk | `gSettings.A11yDisableIdleCam` | |
| Disable Screen Flash for Finishing Blow | chk | `gSettings.A11yNoScreenFlashForFinishingBlow` | |
| Disable Jabu Wobble | chk | `gSettings.A11yNoJabuWobble` | |
| Disable Heat Haze | chk | `gSettings.A11yNoHeatHaze` | |
| ImGui Menu Scaling | cmb | `gSettings.ImGuiScale` | Small/Normal/Large/X-Large (EXPERIMENTAL) |

### Sidebar: General — col 2 (About)
Static text: "Ship Of Harkinian", build/branch/commit, and one line per loaded game
version (`GetGameVersionString`). Informational only.

### Sidebar: Audio
Master / Main Music / Sub Music / Fanfare / Sound Effects volume sliders
(`gSettings.Volume.Master|MainMusic|SubMusic|Fanfare|SFX`, 0-100), plus **Audio API**
backend combo (`WIDGET_AUDIO_BACKEND`, needs reload).

### Sidebar: Graphics
| Label | Type | CVar |
|---|---|---|
| Toggle Fullscreen | btn | — |
| Internal Resolution | sld f | `CVAR_INTERNAL_RESOLUTION` |
| Anti-aliasing (MSAA) | sld i | `CVAR_MSAA_VALUE` |
| Current FPS (interpolation) | sld i | `gSettings.InterpolationFPS` |
| Match Refresh Rate | chk | `gSettings.MatchRefreshRate` |
| Renderer API (Needs reload) | video-backend | — |
| Enable Vsync | chk | `CVAR_VSYNC_ENABLED` |
| Windowed Fullscreen | chk | `CVAR_SDL_WINDOWED_FULLSCREEN` |
| Allow multi-windows | chk | `CVAR_ENABLE_MULTI_VIEWPORTS` |
| Texture Filter (Needs reload) | cmb | `CVAR_TEXTURE_FILTER` |
| Advanced Graphics Options (col 2) | separator | — | Advanced Resolution editor injected via `ResolutionEditor.cpp` |

### Sidebar: Controls
Clear Devices (btn, wipes `gSettings.Controllers`); **Popout Bindings Window** (win →
"Configure Controller", `gWindows.ControllerConfiguration`).

### Sidebar: Input Viewer
Toggle Input Viewer (win → `gWindows.InputViewer`); Popout Input Viewer Settings (win →
`gWindows.InputViewerSettings`).

### Sidebar: Notifications
Position (cmb `gSettings.Notifications.Position`), Duration (sld f
`...Notifications.Duration`), Background Opacity (`...Notifications.BgOpacity`), Size
(`...Notifications.Size`), Test Notification (btn), Mute Notification Sound (chk
`...Notifications.Mute`).

### Sidebar: Mod Menu
Popout Mod Menu Window (win → `gWindows.ModMenu`).

---

## 1.2 Enhancements  (`SohMenuEnhancements.cpp`)

Largest tab. Sidebars: **Quality of Life, Skips & Speed-ups, Graphics, Items, Fixes,
Difficulty, Minigames, Extra Modes, Cheats, Cosmetics Editor, Audio Editor, Gameplay Stats,
Time Splits, Timers**.

### Sidebar: Quality of Life
Saving: Autosave (`gEnhancements.Autosave`), Notification on Autosave
(`AutosaveNotification`), Remember Save Location (`RememberSaveLocation`).
Containers: Containers Match Contents (`ChestSizeAndTextureMatchContents`), Containers of
Agony (`ChestSizeDependsStoneOfAgony`).
Time of Day: Nighttime GS Always Spawn (`NightGSAlwaysSpawn`), Pull Grave During the Day
(`DayGravePull`), Dampe Appears All Night (`DampeAllNight`), Exit Market at Night
(`MarketSneak`), Shops and Games Always Open (`OpenAllHours`).
Pause Menu: Cursor on Any Slot (cmb `PauseAnyCursor`), Pause Warp (`PauseWarp`).
Controls: Answer Navi Prompt with L (`NaviOnL`), Don't Require Input for Credits
(`NoInputForCredits`), Include Held Inputs at Start of Pause Buffer
(`IncludeHeldInputsBufferWindow`), Pause Buffer Input Window (sld `PauseBufferWindow`),
Simulated Input Lag (sld `CVAR_SIMULATED_INPUT_LAG`), Reworked Targeting
(`ReworkedTargeting.Enabled`) + Target Switch combo (`ReworkedTargeting.Btn`).
Item Count Messages: Gold Skulltula / Pieces of Heart / Heart Containers
(`InjectItemCounts.*`).
Misc: Disable Crit Wiggle (`DisableCritWiggle`), Better Owl (`BetterOwl`).
Convenience: Quit Fishing at Door (`QuitFishingAtDoor`), Instant Putaway
(`InstantPutaway`), Navi Timer Resets on Scene Change (`ResetNaviTimer`), Link's Cow in
Both Time Periods (`CowOfTime`), Play Zelda's Lullaby to Open Waterfall (cmb
`TimeSavers.SleepingWaterfall`), Skip Feeding Jabu-Jabu (`TimeSavers.SkipJabuJabuFish`).

### Sidebar: Skips & Speed-ups
Cutscenes: All/None buttons; per-toggle: Skip Intro, Skip Entrance, Skip Story, Skip Song,
Skip Boss Intros, Quick Boss Deaths, Skip One Point, Skip Owl, Skip Misc, Disable Title
Card, Exclude Glitch-Aiding (`gEnhancements.TimeSavers.SkipCutscene.*` /
`SkipOwlInteractions` / `SkipMiscInteractions` / `DisableTitleCard`).
Text: Skip Bottle Pickup (`FastBottles`), Skip Consumable Pickup (`FastDrops`), Skip Forced
Dialog (cmb `TimeSavers.SkipForcedDialog`), Skip Text (`SkipText`), Text Speed (sld
`TextSpeed`), Slow Text Speed (sld `SlowTextSpeed`).
Animations: Faster Heavy Block Lift, Faster Shadow Ship, Fast Chests, Skip Water Take Breath
Anim, Empty Bottles Faster, Vine/Ladder Climb Speed, Block Pushing Speed, Crawl Speed,
Exclude Glitch-Aiding Crawlspaces, King Zora Speed, Faster Pause Menu
(`FasterHeavyBlockLift`, `FasterShadowShip`, `FastChests`, `SkipSwimDeepEndAnim`,
`FasterBottleEmpty`, `ClimbSpeed`, `FasterBlockPush`, `CrawlSpeed`,
`GlitchAidingCrawlspaces`, `MweepSpeed`, `FasterPauseMenu`).
Misc: Skip Child Stealth, Skip Tower Escape, Skip Scarecrow's Song, Faster Rupee
Accumulator, No Skulltula Freeze, Skip Save Confirmation, Link as Default File Name, Spawn
Bean Skulltula Faster, Biggoron Forge Time.

### Sidebar: Graphics  ← most SoH3D-relevant
| Label | CVar | Group |
|---|---|---|
| Disable Bomb Billboarding | `DisableBombBillboarding` | Mods |
| Disable Grotto Fixed Rotation | `DisableGrottoRotation` | Mods |
| Disable Link's Sword Trail | `DisableLinkSwordTrail` | Mods |
| Disable 2D Pre-Rendered Scenes | `3DSceneRender` | Mods |
| Disable Fixed Camera | `DisableFixedCamera` | Mods |
| Ingame Text Spacing | `TextSpacing` (sld) | Mods |
| Disable LOD | `DisableLOD` | Models & Textures |
| Enemy Health Bars | `EnemyHealthBar` | Models & Textures |
| Enable 3D Dropped Items/Projectiles | `NewDrops` | Models & Textures |
| Animated Link in Pause Menu | `PauseMenuAnimatedLink` | Models & Textures |
| Show Age-Dependent Equipment | `EquipmentAlwaysVisible` | Models & Textures |
| Scale Adult Equipment as Child | `ScaleAdultEquipmentAsChild` | Models & Textures |
| Show Gauntlets in First-Person | `FirstPersonGauntlets` | (OoT3D-like) |
| Show Chains on Both Sides of Locked Doors | `ShowDoorLocksOnBothSides` | |
| Color Temple of Time's Medallions | `ToTMedallionsColors` | |
| Minimal UI | `MinimalUI` | UI |
| Disable Hot/Underwater Warning Text | `DisableTunicWarningText` | UI |
| Remember Minimap State Between Areas | `RememberMapToggleState` | UI |
| Visual Stone of Agony | `VisualAgony` | UI |
| Disable HUD Heart Animations | `NoHUDHeartAnimation` | UI |
| Glitch Line-up Tick | `DrawLineupTick` | UI |
| Disable Black Bar Letterboxes | `DisableBlackBars` | UI |
| Dynamic Wallet Icon | `DynamicWalletIcon` | UI |
| Always Show Dungeon Entrances | `AlwaysShowDungeonMinimapIcon` | UI |
| More Info in File Select | `FileSelectMoreInfo` | UI |
| Better Ammo Rendering in Pause Menu | `BetterAmmoRendering` | UI |
| Enable Passage of Time on File Select | `TimeFlowFileSelect` | UI |
| N64 Mode | `CVAR_LOW_RES_MODE` | Misc (4:3 240p) |
| Remove Spin Attack Darkness | `RemoveSpinAttackDarkness` | Misc |
| Disable Link Spinning With Goron Pot | `DisableLinkSpinWithGoronPot` | Misc |
| Increase Actor Draw Distance | `DisableDrawDistance` (sld) | Draw Distance |
| Disable Kokiri Fade | `DisableKokiriDrawDistance` | Draw Distance |
| Widescreen Actor Culling | `WidescreenActorCulling` | Draw Distance |
| Cull Glitch Useful Actors | `ExtendedCullingExcludeGlitchActors` | Draw Distance |

### Sidebar: Items
Equipment (Dpad Equips, ItemUnequip, AssignableTunicsAndBoots, EquipmentCanBeRemoved,
ToggleStrength, UnsheatheWithoutSlashing, SwordToggle, AskToEquip); Ocarina
(DpadNoDropOcarinaInput, FastOcarinaPlayback, TimeTravel); Masks (MMBunnyHood, AdultMasks,
PersistentMasks, HideBunnyHood, MaskSelect); Explosives (RemoteBombchu, NutsExplodeBombs,
RemoveExplosiveLimit, StaticExplosionRadius, DisableFirstPersonChus, BetterBombchuShopping);
Bow/Slingshot (SeparateArrows, SkipArrowAnimation, BlueFireArrows, SunlightArrows,
BowSlingshotAmmoFix, BowReticle, BowArrowCycle); Hookshot (HookshotableReticle); Boomerang
(FastBoomerang, BoomerangFirstPerson, BoomerangReticle); Magic (BetterFarore, FastFarores);
Bottles (RebottleBlueFire). All under `gEnhancements.*`.

### Sidebar: Fixes
Item-related (CrouchStabHammerFix, CrouchStabFix), Graphical (`SceneSpecificDirtPathFix`
"Fix Vanishing Paths" combo), Graphical Restorations (RedGanonBlood, GSCutscene,
PulsateBossIcon, SariaGestureFriendsForever), Glitch Restorations (HoverFishing,
N64WeirdFrames, BombchusOOB, QuickPutaway, QuickBongoKill, EarlyEyeballFrog), Misc
Restorations (NGCKaleidoSwitcher, WideShutterDoorRange, GraveHoles). **Note:** the genuine
bug-fixes are force-on at boot in SoH3D (see `kSoh3dForcedFixes`), so most of this sidebar is
already handled and not migrated.

### Sidebar: Difficulty
Health (PermanentHeartLoss, DamageMult, FallDamageMult, VoidDamageMult, BonkDamageMult,
FullHealthSpawn, NoHeartDrops); Drops (NoRandomDrops, EnableBombchuDrops, TreesDropSticks,
DampeDropRate); Miscellaneous (DeleteFileOnDeath, SwitchTimerMultiplier, GoronPot, DampeWin,
AllDogsRichard, CuccoStayDurationMult, CuccosToReturn); Enemies (HyperBosses, HyperEnemies,
GuardVision, LeeverSpawnRate). All `gEnhancements.*`.

### Sidebar: Minigames
Customize-behavior groups for Shooting Gallery, Bombchu Bowling, Horseback Archery, Frogs'
Ocarina Game, Lost Woods Ocarina Game, Forest Temple (Amy's Puzzle), Rupee Diving Game,
Fishing — each gated behind a `Customize*` master checkbox with instant-win / count / weight
sliders. All `gEnhancements.*`.

### Sidebar: Extra Modes
BounceOffWalls, MirroredWorldMode (cmb), IvanCoopModeEnabled, DogFollowsEverywhere,
RupeeDash (+RupeeDashInterval), ShadowTag, HurtContainer, ExtraTraps.Enabled + tiered trap
toggles (`ExtraTraps.Ice|Burn|Shock|Knockback|Speed|Bomb|Void|Ammo|Kill|Teleport`).

### Sidebar: Cheats  (this is SoH's "Cheats" menu — under Enhancements, not a top tab)
Infinite: Money/Health/Ammo/Magic/Nayru/EponaBoost (`gCheats.Infinite*`).
Items: TimelessEquipment, NoRestrictItems, SuperTunic, FireproofDekuShield, ShieldTwoHanded,
DekuStick (cmb), BombTimerMultiplier, HookshotEverything, HookshotReachMultiplier.
Misc: NoClip, ClimbEverything, MoonJumpOnL, NoRedeadFreeze, NoKeeseGuayTarget,
DisableSandstorm, GSTargetable.
Glitch Aids: EasyFrameAdvance, EasyISG, EasyQPA, Clear Cutscene Pointer (btn).
Despawn Timers: DropsDontDie, NoFishDespawn, NoBugsDespawn.
Time of Day: FreezeTime, TimeSync.
Instant Age Change: Change Age (btn → `SwitchAge()`).
Speed Modifier: SpeedToggle, DoesntChangeJump, Value (sld), Btn (combo).
Save States: SaveStatePromise, SaveStatesEnabled (F5 save / F6 slot / F7 load).
Beta Quest: EnableBetaQuest, BetaQuestWorld (resets game). All `gCheats.*`.

### Sidebar: Cosmetics Editor / Audio Editor / Gameplay Stats / Time Splits / Timers
Window-toggle buttons → `gWindows.CosmeticsEditor`, `gWindows.AudioEditor`,
`gWindows.GameplayStats`, `gWindows.TimeSplits`, `gWindows.TimeDisplayEnabled`. The Timers
sidebar also has Font Scale (`CVAR_TIME_DISPLAY("FontScale")`), Hide Background
(`...ShowWindowBG`), and a generated checkbox per timer in `timeDisplayList`.

---

## 1.3 Randomizer  (`SohMenuRandomizer.cpp`)  — droppable for SoH3D
Sidebars: **General** (manual seed entry, Generate/Randomize buttons, spoiler file, rando-
only enhancements like RandoRelevantNavi/CustomKeyModels/ColoredMapsAndCompasses/
SkipGetItemAnimation, plus option groups from `Rando::Settings` — Logic/Dungeons/Shuffles/
Hints&Traps/Starting Items), **Locations** (custom include/exclude tree, `DrawLocationsMenu`),
**Tricks/Glitches** (custom tag/area tree, `DrawTricksMenu`), **Plandomizer** (win →
`gWindows.PlandomizerEditor`), **Item Tracker** (win + settings win), **Entrance Tracker**
(win + settings win), **Check Tracker** (win + settings win). All randomizer-specific.

## 1.4 Network  (`SohMenuNetwork.cpp`)  — droppable for SoH3D
Sidebars: **Sail** (host/port input + enable, remote-control protocol), **Crowd Control**
(host/port + enable + EnemyNameTags / SpawnedEnemiesIgnoredIngame), **Anchor** (empty
sidebar; AnchorRoom window registered separately). Streamer/co-op tooling.

## 1.5 Dev Tools  (`SohMenuDevTools.cpp`)  ← several items useful for SoH3D
### Sidebar: General
| Label | Type | CVar |
|---|---|---|
| Popout Menu | chk | `gSettings.Menu.Popout` |
| Debug Mode | chk | `gDeveloperTools.DebugEnabled` |
| Map Select Button Combination | sel | `gDeveloperTools.MapSelectBtn` |
| No Clip Button Combination | sel | `gDeveloperTools.NoClipBtn` |
| OoT Registry Editor | chk | `gDeveloperTools.RegEditEnabled` |
| Debug Save File Mode | cmb | `gDeveloperTools.DebugSaveFileMode` |
| OoT Skulltula Debug | chk | `gDeveloperTools.SkulltulaDebugEnabled` |
| Resource logging | chk | `gDeveloperTools.ResourceLogging` |
| Frame Advance | chk | (ptr → `gPlayState->frameAdvCtx.enabled`) |
| Advance 1 / Advance (Hold) | btn | `gDeveloperTools.FrameAdvanceTick` |
| Log Level | cmb | `gDeveloperTools.LogLevel` |
| Better Debug Warp Screen | chk | `gDeveloperTools.BetterDebugWarpScreen` |
| Debug Warp Screen Translation | chk | `gDeveloperTools.DebugWarpScreenTranslation` |
| Warp Points | custom | `WarpPointsWidget` |

### Other Dev Tools sidebars (all window-toggle buttons)
Stats (`gWindows.SohStats`), Console (`gWindows.SohConsole`), Save Editor
(`gWindows.SaveEditor`), Hook Debugger (`gWindows.HookDebugger`), Collision Viewer
(`gWindows.CollisionViewer`), Actor Viewer (`gWindows.ActorViewer`), DList Viewer
(`gWindows.DisplayListViewer`), Value Viewer (`gWindows.ValueViewer`), Message Viewer
(`gWindows.MessageViewer`), Gfx Debugger (`gWindows.SohGfxDebugger`).

---

# 2. DEV / DEBUG WINDOWS (registered GuiWindows)

Registered in `SohGui::SetupGuiElements()` / `SetupMenu()` (`SohGui.cpp`). "Embedded"
windows also appear inside the menu via their sidebar's window-toggle button.

| Window (title) | Class / source | Purpose | SoH3D relevance |
|---|---|---|---|
| Console | `SohConsoleWindow` — `Enhancements/debugger/SohConsoleWindow.h` (base `Ship::ConsoleWindow` in libultraship) | Command console / cvar dispatch | **Useful** (generic) |
| GfxDebugger | `SohGfxDebuggerWindow` — `Enhancements/debugger/SohGfxDebuggerWindow.h` (base in libultraship) | Step/inspect the F3D display list | **Useful** for render work |
| Stats | `SohStatsWindow` — `Enhancements/debugger/SohStatsWindow.h` | FPS / perf counters | **Useful** |
| Mod Menu | `ModMenuWindow` — `Enhancements/mod_menu.h` | Lua/mod-script menu surface | Maybe |
| Audio Editor | `AudioEditor` — `Enhancements/audio/AudioEditor.h` | Sequence/SFX swap editor | Maybe (asset) |
| Input Viewer | `InputViewer` — `Enhancements/controls/InputViewer.h` | On-screen controller input overlay | Droppable |
| Input Viewer Settings | `InputViewerSettingsWindow` — same header | Configure the overlay | Droppable |
| Cosmetics Editor | `CosmeticsEditorWindow` — `Enhancements/cosmetics/CosmeticsEditor.h` | Recolor Link/UI/effects | Maybe (asset) |
| Actor Viewer | `ActorViewerWindow` — `Enhancements/debugger/actorViewer.h` | Inspect/spawn/edit live actors | **Useful** |
| Collision Viewer | `ColViewerWindow` — `Enhancements/debugger/colViewer.h` | Render collision meshes | **Useful** |
| Save Editor | `SaveEditorWindow` — `Enhancements/debugger/debugSaveEditor.h` | Edit save/inventory/flags | Droppable (OoT save) |
| Hook Debugger | `HookDebuggerWindow` — `Enhancements/debugger/hookDebugger.h` | GameInteractor hook inspector | Droppable |
| Display List Viewer | `DLViewerWindow` — `Enhancements/debugger/dlViewer.h` | Browse/disassemble display lists | **Useful** for render work |
| Value Viewer | `ValueViewerWindow` — `Enhancements/debugger/valueViewer.h` | Watch arbitrary memory addresses | Maybe |
| Message Viewer | `MessageViewer` — `Enhancements/debugger/MessageViewer.h` | Browse text/message tables | Droppable |
| Gameplay Stats | `GameplayStatsWindow` — `Enhancements/gameplaystatswindow.h` | Run timer / kill / step counters | Droppable |
| Check Tracker | `CheckTracker::CheckTrackerWindow` — `Enhancements/randomizer/randomizer_check_tracker.cpp` | Rando check tracker | Droppable (rando) |
| Check Tracker Settings | `CheckTracker::CheckTrackerSettingsWindow` — same | Tracker config | Droppable (rando) |
| Entrance Tracker | `EntranceTracker::EntranceTrackerWindow` — `Enhancements/randomizer/randomizer_entrance_tracker.cpp` | Rando entrance tracker | Droppable (rando) |
| Entrance Tracker Settings | `EntranceTracker::EntranceTrackerSettingsWindow` — same | Tracker config | Droppable (rando) |
| Item Tracker | `ItemTrackerWindow` — `Enhancements/randomizer/randomizer_item_tracker.h` | Rando item tracker | Droppable (rando) |
| Item Tracker Settings | `ItemTrackerSettingsWindow` — same header | Tracker config | Droppable (rando) |
| Time Splits | `TimeSplitWindow` — `Enhancements/timesplits/TimeSplits.h` | Speedrun split timer | Droppable |
| Plandomizer Editor | `PlandomizerWindow` — `Enhancements/randomizer/Plandomizer.h` | Author rando placements | Droppable (rando) |
| Notifications | `Notification::Window` — `Notification/Notification.h` | Toast/notification overlay (always shown) | Maybe (generic) |
| Additional Timers | `TimeDisplayWindow` — `Enhancements/TimeDisplay/TimeDisplay.h` | Extra on-screen timers | Droppable |
| Anchor Room | `AnchorRoomWindow` — `Network/Anchor/Anchor.h` | Co-op (Anchor) room UI | Droppable (network) |
| Modal Window | `SohModalWindow` — `SohGui/SohModals.h` | Popup/confirm dialogs (always shown) | **Keep** (generic infra) |
| Configure Controller | input-editor window from libultraship (`gWindows.ControllerConfiguration`) | Controller binding UI | **Useful** (generic) |

Total registered windows: **30** (28 in `SetupGuiElements` + the always-on Modal and
Notification windows; the libultraship "Configure Controller" window is created by the engine
and only referenced here).

---

# 3. WIDGET / CONTROL VOCABULARY (for the RmlUi port)

From `MenuTypes.h` `WidgetType` — the control kinds the ImGui menu used, i.e. what RmlUi
equivalents are needed: checkbox, combobox, int/float slider, button-combo selector
(`BTN_SELECTOR`), button, text input, color picker, search box, separator / separator-text,
text label, window-toggle button, audio-backend & video-backend combos (special-cased), and
`WIDGET_CUSTOM` (bespoke ImGui draw — these are the hardest to port: Seed entry, Spoiler
file, Sail/CC host:port, Warp Points, Locations tree, Tricks tree).

Each widget supports: `CVar`, `Options` (per-type), `Callback` (on-change), `PreFunc`
(per-frame hide/disable), `PostFunc`, `RaceDisable`, `HideInSearch`, `Tooltip`,
`DisabledTooltip`, `ValuePointer` (for non-cvar state like Frame Advance).
