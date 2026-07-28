# libultraship (LUS) architecture — the "two trees" and the input path

Investigated 2026-06-21. Record so no agent has to re-derive it. The LUS submodule lives at
`Shipwright/libultraship/` (the only checkout — there is NO top-level `libultraship/`; remote =
`SomeoneIsWorking/libultraship`, our fork). The project CLAUDE.md's "libultraship/ → fork/soh3d"
refers to this submodule.

## There are NOT two duplicate LUS trees — it's framework + N64 implementation

`Shipwright/libultraship/src/` (and `include/`) contains two namespaces by DESIGN, not duplication:

| dir | namespace | role | size |
|-----|-----------|------|------|
| `src/ship/`, `include/ship/` | `Ship::` | **generic, game-agnostic LUS framework** — abstract base classes + ALL device/mapping code | ~109 files |
| `src/libultraship/`, `include/libultraship/` | `LUS::` | **concrete N64 implementation** — subclasses that produce the N64 `OSContPad` | ~26 files |

The 3 filenames that appear in BOTH trees (`ControlDeck.cpp`, `Controller.cpp`,
`ControllerDefaultMappings.cpp`) are **base class (Ship) vs N64 subclass (LUS)** — same name,
different layer, related by inheritance. Nothing to delete; deleting either breaks the build.

- `Ship::ControlDeck` — abstract, `virtual void WriteToPad(void*) = 0`. Header comment: "Subclass
  ControlDeck to implement WriteToPad() for a specific game's pad layout."
  - `LUS::ControlDeck : public Ship::ControlDeck` — overrides `WriteToPad` to fill the N64 OSContPad.
- `Ship::Controller : public ControlDevice` — abstract, `virtual void ReadToPad(void*) = 0`; owns the
  per-N64-button `ControllerButton`s and their mappings.
  - `LUS::Controller : public Ship::Controller` — overrides `ReadToPad`/`ReadToOSContPad` (N64 pad).

So `Ship::` = "the framework", `LUS::` = "this game is an N64". Both compile; both are used together
(an `LUS::ControlDeck` IS-A `Ship::ControlDeck`).

## The per-frame input path (physical button -> N64 pad bit)

```
libultra os.cpp:  Ship::Context::GetRawInstance()->GetControlDeck()->WriteToPad(pad)
  -> LUS::ControlDeck::WriteToPad(pad)                 // src/libultraship/.../controldeck/ControlDeck.cpp
       for each port: controller->ReadToPad(&pad[i])
  -> LUS::Controller::ReadToOSContPad(OSContPad*)      // src/libultraship/.../controller/Controller.cpp
       for (bitmask, button : mButtons)                //   one ControllerButton per N64 bit (A,B,CUP,...)
           button->UpdatePad(padToBuffer.button)
  -> Ship::ControllerButton::UpdatePad(CONTROLLERBUTTONS_T&)   // src/ship/.../controller/ControllerButton.cpp
       for (mapping : mButtonMappings) mapping->UpdatePad(padButtons)
  -> e.g. Ship::SDLButtonToButtonMapping::UpdatePad(...)       // reads SDL button, ORs mBitmask into padButtons
```

`padToBuffer.button` is the assembled **N64 button bitmask** (libultra `controller.h`): A=0x8000,
B=0x4000, Z=0x2000, START=0x1000, D-pad U/D/L/R=0x0800/0400/0200/0100, L=0x0020, R=0x0010,
C U/D/L/R=0x0008/0004/0002/0001.

## Where things live (edit here)

- **Button mapping classes** (one physical input -> one N64 bit): `src/ship/controller/controldevice/
  controller/mapping/` — `sdl/SDLButtonToButtonMapping`, `keyboard/KeyboardKeyToButtonMapping`,
  `mouse/MouseButtonToButtonMapping`, etc. Each: ctor(portIndex, bitmask, physicalInput), `UpdatePad`,
  `GetButtonMappingId`, `SaveToConfig`/`EraseFromConfig`, `GetMappingType`.
- **Factory** (create from config string / from a freshly-pressed physical input):
  `mapping/factories/ButtonMappingFactory.cpp` — `CreateButtonMappingFromConfig` switches on the
  `…ButtonMappingClass` CVar string; `CreateButtonMappingFromSDLInput` builds one from a held button.
- **Config**: each mapping serializes under `CVAR_PREFIX_CONTROLLERS ".ButtonMappings.<id>.*"`; the
  owning `ControllerButton` stores its mapping-id list under
  `…Port%d.Buttons.<ConfigName>ButtonMappingIds`.
- **Default mappings**: `mapping/ControllerDefaultMappings.cpp` (exists in both trees — Ship base +
  LUS N64 defaults).

## Chords / modifier bindings (#32) — the design

SoH maps ONE physical input -> ONE N64 bit; there is no native modifier/chord support. Two facts drive
the design:
1. The user's `R1 + A/B/X/Y` item-slot chords need **physical** input — X/Y have no N64 equivalent, so
   a chord can't be expressed in N64-bitmask space. It must be a **physical-device button mapping** that
   reads TWO physical inputs (modifier + trigger) and sets the N64 bit only when BOTH are held. Model it
   on `SDLButtonToButtonMapping` (add a second SDL button + a "modifier" SDL button).
2. **Suppression** (R1+A must NOT also fire R1's own action) cannot live in a single mapping — a mapping
   can only OR bits, not clear another mapping's. Do it at the assembly chokepoint
   `LUS::Controller::ReadToOSContPad`, AFTER the `mButtons` UpdatePad loop: track which physical
   modifiers are "consumed" by an active chord this frame and clear the modifier's N64 bit.

So a complete chord feature = (a) a `Ship::SDLButtonChordToButtonMapping` (+ keyboard variant) in the
mapping framework, (b) factory + config serialization for it, (c) chokepoint suppression in
`LUS::Controller`, (d) ImGui rebinding UI in SoH's controller-config menu. Default Xbox scheme: RB as
the modifier; RB+A/B/X/Y -> the four C-buttons; D-pad holds item slots (SoH `DpadEquips` CVar).

## The PC-native keyboard scheme, and why the HUD badge must READ it (#203)

The default keyboard table is `LUS::ControllerDefaultMappings::SetDefaultKeyboardKeyToButtonMappings`.
It is **versioned**: `kZelda3dInputSchemeVersion` in `src/ship/controller/controldeck/ControlDeck.cpp`.
Bump it whenever the table changes, or existing users keep their old bindings forever — `Init` only
re-derives the keyboard defaults when the stored version is behind (gamepad/mouse bindings are left
alone).

**Scheme v3** (2026-07-28). The three C buttons ARE OoT's item slots, so they get the PC item-bar
keys; C-Up is the first-person look / Navi call, which is not an item slot:

| N64 | Key | | N64 | Key |
|---|---|---|---|---|
| `BTN_A` | Space | | `BTN_CLEFT` | `1` |
| `BTN_B` | F | | `BTN_CDOWN` | `2` |
| `BTN_Z` | Q | | `BTN_CRIGHT` | `3` |
| `BTN_R` | Left Ctrl | | `BTN_CUP` | `C` |
| `BTN_L` | Left Shift | | `BTN_D*` | I / K / J / L |
| `BTN_START` | Enter | | stick | W / A / S / D |

The **arrow keys are deliberately unbound** — they are where the camera goes in the mouse-look pass,
and leaving them on the C buttons as a second binding would make "which key does the HUD show?"
arbitrary.

### The HUD badge is a view of the bindings, never a copy of them

`soh/src/zelda3d/input/zelda3d_keymap.cpp` is the only place that answers "which key is bound to this
N64 button". It walks `ControlDeck → GetControllerByPort(0) → GetButton(bitmask) →
GetAllButtonMappings()`, keeps the keyboard mappings, and asks LUS for the name
(`KeyboardKeyToAnyMapping::GetPhysicalInputName` → `Window::GetKeyName` → `SDL_GetScancodeName`).
The HUD (`z_parameter.c` `Zelda3D_DrawHudBadges`) then composites that label onto a keycap
(`Zelda3D_KeyCapTex`).

Two things to know before touching this:

- **`GetAllButtonMappings()` returns an `unordered_map`.** "The first keyboard mapping" is not a
  stable notion. When a button has several keyboard bindings the badge takes the **lowest scancode**;
  without a deterministic tie-break it flickers between keys frame to frame.
- **Do not bake key letters into artwork.** That is exactly how this broke: the badges used to be four
  PNGs with "C"/"←"/"↓"/"→" drawn into the SVGs, and the B badge still read "C" long after `BTN_B`
  moved to F. Anything that displays a binding must read it at draw time.

Inspect it live with the REPL: `keycap` prints the four resolved labels, `keycap <label>` dumps the
composited cap to `scratch/raw/keycap.rgba` (that is how the widened multi-character cap is checked,
since the default scheme binds only single-character keys to badged buttons).
