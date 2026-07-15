# 2026-07-15 — Keyboard/game input dead (title + in-game) — KeyboardGameInputBlocked regression

## Symptom (user, headed Wayland/GTK session)
Physical key presses do nothing: START at title won't reach file select; after picking a file,
in-game input is dead too. User: "GTK is stealing my inputs again" — recurred; fixed once before
(history lost in the repo flatten 69f27a2f), and the sibling `../psxport` hit + fixed the same
class.

## Why my earlier "input fixed" (37c3475a) did NOT cover this
That fix was the title `gameMode` clobber (real, separate). It was verified HEADLESS by injecting
inputs through the REPL (`key`/`btnhold`), which bypass the real keyboard→SDL→window→ControlDeck
path. So it proved the gameMode logic, never the physical-input path. This bug is in that path.

## Root cause
`ControlDeck::KeyboardGameInputBlocked()` (Shipwright/libultraship/src/ship/controller/controldeck/
ControlDeck.cpp) blocked keyboard game input when
`ImGui ActiveIdWindow != main-game-window`. Its own comment admitted this "altered" the standard
`ImGui::GetIO().WantCaptureKeyboard`. In the real windowed build a lingering ImGui ActiveId (even
on a hidden window) makes that true → ALL keyboard game input blocked. The legacy ImGui menu is
force-hidden in this fork (RmlUi is the menu, and it already registers a game-input blocker via
`BlockGameInput` while open — covered by `AllGameInputBlocked()`), so the ActiveId heuristic can
now only produce false-positives. Headless didn't repro (ImGui not fully realized without a real
window), which is why REPL injection worked.

## Fix
`return AllGameInputBlocked() || ImGui::GetIO().WantCaptureKeyboard;` — block only when the RmlUi
menu is open (AllGameInputBlocked) or a visible ImGui widget is genuinely being typed into
(WantCaptureKeyboard, false in normal play). Mirrors psxport's principle: gate on the overlay
actually wanting keyboard (`runtime/recomp/pad_input.cpp` → `rml_overlay.wantsKeyboard()`), not on
stale focus state.

## Verification status
Built clean. NOT headless-verifiable (headed-only path). Awaiting user headed confirmation:
`./run.sh`, at title press START/Enter → should reach file select promptly once the wordmark is
up; after loading a file, in-game input should respond. If still dead, next step is an env-gated
per-frame log of `AllGameInputBlocked()` / `WantCaptureKeyboard` / RmlUi `mVisible` to pinpoint
which condition is stuck.
