---
id: 0029
title: Entrance_EnableFW always returns early, so entrance rando can never enable Farore's Wind
status: open
kind: finding
found: 2026-10-04
found_by: clang-analyzer-deadcode.DeadStores, while bounding the randomizer's save-JSON indexes (issue 0023)
tags: randomizer,entrance,farores-wind,dead-code,gameplay
---

# `Entrance_EnableFW` is a complete no-op

## Symptom

`Shipwright/soh/soh/Enhancements/randomizer/randomizer_entrance.c:557`:

```c
void Entrance_EnableFW(void) {
    Player* player = GET_PLAYER(gPlayState);
    // Leave restriction in Tower Collapse Interior, Castle Collapse, Treasure Box Shop, ...
    if (!false /* farores wind anywhere */ || gPlayState->sceneNum == SCENE_GANONS_TOWER_COLLAPSE_INTERIOR ||
        ...
    ) {
        return;
    }

    for (size_t i = 1; i < ARRAY_COUNT(gSaveContext.equips.buttonItems); i++) {
        if (gSaveContext.equips.buttonItems[i] == ITEM_FARORES_WIND) {
            gSaveContext.buttonStatus[i] = BTN_ENABLED;
        }
    }
}
```

`!false` is `true`, so the `||` short-circuits on the very first operand. The condition is
unconditionally true, the function returns on every call, and the loop that re-enables the
Farore's Wind button **never runs for any input, in any scene, in any player state**. Every
other term in that expression — nine scene checks, `eventInf[0] & 0x1`, four `stateFlags1`
bits, `stateFlags2_CRAWLING` — is dead, because nothing can reach it.

## Why the intended behaviour is not a guess

The comment immediately above the `if` states it: *"Leave restriction in Tower Collapse
Interior, Castle Collapse, Treasure Box Shop, Tower Collapse Exterior, Grottos area, Fishing
Pond, Ganon Battle and for states that disable buttons."* That is a description of the scene
and state list, which is exactly what the remaining terms test. The two `!false` markers read
as notes about the conditions being considered ("farores wind anywhere", "shuffled chest mini
game"), not as a decision to disable the feature.

So the intent is unambiguous and the code does not implement it. This is a bug, not a style
question — which matters, because the linter's suggested fix and the correct fix happen to be
the same edit here and that coincidence should not be the reason for making it.

## How it was found

`clang-analyzer-deadcode.DeadStores` flagged the `player` initialisation as never read. The
real defect is the short-circuit one line below it: the analyzer reports the *consequence*
(`player->stateFlags1` unreachable) rather than the *cause* (`!false`), so the diagnostic
understates the bug. It was surfaced while bounding issue 0023's randomizer save-JSON indexes,
where this file was already being touched.

## Player-visible effect

Entrance rando can place Farore's Wind into the starting inventory or behind an entrance, but
the button that uses it stays whatever the game left it as. In practice the item is
unusable — not crashy, not visibly wrong, just permanently inert, which is the worst shape for
a randomizer feature because a player cannot tell it from "this seed did not give me FW".

## The decision that is actually needed

Deleting the two `!false` terms restores the documented intent: enable FW everywhere except
the listed scenes and player states. That is a **gameplay change** and belongs to whoever owns
entrance randomizer, not to a bounds-check or lint commit — a randomizer's item placement
balance is tuned against the current behaviour, and enabling FW that was never enabled can
change seeds' difficulty.

The alternative reading — that `!false` is a deliberate stub meaning "FW is available
everywhere, so this whole restriction function is obsolete" — is possible but unsupported: it
would make the function entirely dead code, and the nine scene checks and five state checks
inside it would have no reason to exist.

## Verification when fixed

* `Entrance_EnableFW` reaches its loop: with FW in `equips.buttonItems`, `buttonStatus` for
  that slot becomes `BTN_ENABLED` in a normal scene, and stays disabled in each listed scene
  and for each listed player state.
* `clang-tidy` no longer reports `DeadStores` at that line.
* An entrance-rando seed that places FW is playable: the button works.

## Note on the linter's advice

Do not "fix" this by suppressing the `DeadStores` finding. The finding is correct; the code
is what is wrong.