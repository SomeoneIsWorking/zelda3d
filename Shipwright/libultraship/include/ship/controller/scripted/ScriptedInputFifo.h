#ifndef SHIP_SCRIPTED_INPUT_FIFO_H
#define SHIP_SCRIPTED_INPUT_FIFO_H

// Game-agnostic scripted-input FIFO poller — a shared libultraship seam (serves BOTH the OoT
// (zelda3d) and MM (2s2h) games, which link the exact same library). A background thread reads
// newline-delimited commands from a named pipe and drives the synthetic pad via the
// Ship_ScriptedInput_* C API (see ScriptedInput.h). This turns the fixed input demos into
// interactive headless control without either game having to grow its own input transport.
//
// OFF by default: it starts ONLY when env SHIP_SCRIPTED_FIFO=<path> is set. With the env unset,
// Ship_ScriptedInputFifo_StartFromEnv() is a no-op and physical input is completely untouched
// (the ScriptedInput seam itself also stays disabled until an `enable 1` command arrives), so
// the live OoT game is unaffected.
//
// Command grammar (one per line, replies written to "<path>.out"):
//   enable <0|1>        master gate for synthetic input (Ship_ScriptedInput_SetEnabled)
//   btn <mask>          held N64 button bitmask, e.g. `btn 0x8000` (A). strtol base 0.
//   stick <x> <y>       analog stick, each axis clamped to [-128, 127]
//   reset               release everything and disable the synthetic pad
//   ping                reply "pong" (handshake / readiness probe)

#ifdef __cplusplus
extern "C" {
#endif

// Start the poller if env SHIP_SCRIPTED_FIFO=<path> is set; otherwise no-op. Idempotent — a
// second call while already running is ignored. Safe to call from libultraship startup.
void Ship_ScriptedInputFifo_StartFromEnv(void);

// Stop the poller thread and join it. Safe to call if never started.
void Ship_ScriptedInputFifo_Stop(void);

#ifdef __cplusplus
}
#endif

#endif // SHIP_SCRIPTED_INPUT_FIFO_H
