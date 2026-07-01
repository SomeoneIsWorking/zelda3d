#ifndef SHIP_SCRIPTED_INPUT_H
#define SHIP_SCRIPTED_INPUT_H

#include <stdint.h>

// Scripted / synthetic controller input — a GAME-AGNOSTIC seam living in the shared
// libultraship (serves both the OoT (zelda3d) and MM (2s2h) games, which link the exact
// same library). It holds a synthetic N64 pad state (held buttons + analog stick) that is
// OR-mixed into the real per-frame OSContPad at LUS::ControlDeck::WriteToOSContPad — the
// game's genuine input path, so scripted input is indistinguishable from a physical pad.
//
// It is DISABLED by default: with no caller enabling it, MixInto() is a no-op and physical
// input is untouched. This is what keeps the live OoT game unaffected. Enable it (from a
// headless FIFO poller, a per-game REPL, or a test) to drive Link without a real controller.
//
// The C API is callable from both C game code (the decomps) and C++ (the ControlDeck seam),
// and is thread-safe (the game thread reads via MixInto while a poller thread may write).

#ifdef __cplusplus
extern "C" {
#endif

// Master gate. Off (0) by default -> MixInto() is a no-op; physical input passes through
// untouched. Set non-zero to inject the synthetic state below.
void Ship_ScriptedInput_SetEnabled(int enabled);
int Ship_ScriptedInput_IsEnabled(void);

// Held button mask (N64 BTN_* bits from libultra controller.h), OR'd into the pad each
// frame until changed. Set to 0 to release all synthetic buttons.
void Ship_ScriptedInput_SetButtons(uint16_t buttonMask);
uint16_t Ship_ScriptedInput_GetButtons(void);

// Analog stick override, each axis in N64 range [-128, 127]. Merged per-axis into the pad
// (the larger-magnitude axis wins), so scripted motion drives a headless run yet a human
// pushing harder still wins on a live shared pad. (0, 0) leaves the physical stick alone.
void Ship_ScriptedInput_SetStick(int8_t x, int8_t y);

// Merge the synthetic state into an OSContPad (typically port 0). No-op when disabled.
// `osContPad` is an OSContPad* (void* to keep this header free of libultra type deps).
void Ship_ScriptedInput_MixInto(void* osContPad);

#ifdef __cplusplus
}
#endif

#endif // SHIP_SCRIPTED_INPUT_H
