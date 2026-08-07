---
id: 15
title: Launcher row unclickable: an invisible oversized decoration overlapped it, and RmlUi hit-tests irrespective of opacity
status: resolved
symptom: In the OoT/MM chooser, the Ocarina of Time 'Start game' row did nothing when CLICKED. Keyboard navigation onto it plus Enter worked, and every Majora's Mask row was clickable. Hover highlighting on the OoT row worked too.
tags: launcher,rmlui,input,ui
created: 2026-08-07
updated: 2026-08-07
---

## Root cause

`.launcher__background-wrapper` — the Majora's Mask background artwork in the RIGHT half — is deliberately oversized so its 25s slide-in reads: `left: -70vw; right: -100%; top: -55vw; bottom: -50vw`. Nothing clips it (no `overflow: hidden` on the split), so it extends across the LEFT half as well, and it comes AFTER the left half in DOM order.

RmlUi's own `Context::GetElementAtPoint` docs say it plainly: *"Interaction is determined irrespective of background and opacity."* So a decoration at `opacity: 0.1` hit-tests exactly like a solid panel. The OoT row was underneath it and never received the click. MM's rows were unaffected because they come after the wrapper in DOM order.

## Why the symptom was so misleading

Three separate things made this read as an input-plumbing bug rather than a layout one:

- Keyboard worked, because focus navigation walks the DOM's tab order and never consults geometry.
- HOVER highlighting worked on the OoT row. That looks like proof the element is reachable — it is not. The `:hover` styling comes from the sheet's `.menu-list-item:hover` rule, and what RmlUi propagates as hover state does not imply the same element wins the click dispatch.
- The click handlers were correct and are attached to every `action=` row (`AttachLauncherClickHandlers`). Reading that code finds no bug, because there isn't one.

## The instrument that settled it

`menuclick` (inject a click at a pixel) could not distinguish "the row is covered", "the click path is broken" and "I clicked the wrong pixel" — all three look like nothing happening. Added REPL `menuhit` (instrument I029): for every actionable row it prints the row's box and which element `GetElementAtPoint` actually returns at its centre, classifying it REACHABLE (the row or a descendant — a click on a child still bubbles to the row's listener) or OCCLUDED, naming the occluder.

    launcher hit-test: 5 actionable row(s), 4 reachable by mouse, 1 OCCLUDED
      start_oot   box=(32,380 336x68) centre=(200,414) -> OCCLUDED by  svg class="launcher__background-mm"

## Fix

`pointer-events: none` on `.launcher__background-wrapper` (RmlUi supports it; it inherits, so the child `<svg>` is covered by the one declaration). Applied to the SCSS source and the generated `recomp.rcss`.

Note on the stylesheet: `recomp.rcss` is generated from `scss/main.scss`, but no pinned sass version reproduces the committed file byte-for-byte — 1.75 differs only in colour notation (`whitesmoke` vs `rgb(244.6,...)`, 46 lines) while the latest reorders declarations (260 lines). So the one declaration was applied to both files by hand after checking it matched sass's own output for that block exactly. A wholesale regen would bury a one-line fix in unrelated churn.

## After

`5 actionable row(s), 5 reachable by mouse, 0 OCCLUDED`, and `menuclick` at the reported centre starts the game (title sequence in the run log).

## Generalisation worth remembering

Any absolutely-positioned decorative element that bleeds outside its container is a click-eater in RmlUi, and it is invisible to code review of the input path. If a UI element responds to the keyboard but not the mouse, hit-test it before reading any handler code.
