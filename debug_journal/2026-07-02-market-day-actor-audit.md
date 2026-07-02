# 2026-07-02 — Market Day structured actor audit

Post-time-sync + post-EnHy-body-colors + post-door-inc3, a fresh structured sweep of the
Market Day scene (ent 0xB1, dayTime 0x6000, r=5000 from spawn) shows the following state.
Signal source: `actorsnear` output + `[Zelda3D] auto-loaded model …` lines in `scratch/logs/run.log`.

## Sweep

| id | actor | count | render path | note |
|----|-------|-------|-------------|------|
| 0x000 | Player | 1 | N64 | separate track (#117 anim port, Link port pipeline) |
| 0x009 | EN_DOOR | 6 | MODULE(3DS) | door.cpp inc3 → field-keep CMB (per-scene table) |
| 0x018 | EN_ELF (Navi) | 1 | N64 | no OoT3D asset; universal, not market-specific |
| 0x077 | EN_KUSA (tree/foliage) | 1 | AUTO | zelda_wood02.zar |
| 0x0E7 | EN_MA1 (young Malon) | 1 | AUTO skin→skip → N64 | ZAR loads but skinned; needs behavior port |
| 0x112 | EN_WONDER_ITEM | 2 | N64 | invisible trigger, no visual gap |
| 0x125 | (mgr) | many | N64 | env manager, non-drawing |
| 0x16E | EN_HY (townsfolk) | 1+ | AUTO skin + TownsfolkBehavior | head-track + eye anim + body-color (Step 2c) |
| 0x178 | EN_HEISHI4 (guards) | 2 | AUTO skin→skip → N64 | ZAR loads but skinned; needs behavior port |
| 0x19B | EN_DOG | 6 | AUTO skin→skip → N64 | ZAR loads but skinned; needs behavior port |
| 0x1A0 | (mgr) | 4 | N64 | env sound, non-drawing |
| 0x1AC | EN_TG (dancing couple) | 1 | AUTO skin→skip → N64 | AUTO picks WRONG cmb (see below); needs behavior port |
| 0x1AD | EN_MU (haggling townspeople) | 0 | — | not spawned in Market Day at r≤10000 from spawn (user's "En_Mu mis-rendered" in #118 does not reproduce here — check Market Night rooms / other scenes) |

At Market Night (ent 0xB1, dayTime 0xE000, scene 0x21): same actor list minus daytime-only
NPCs, no EN_MU either. EN_MU may live in a different scene entirely (its N64 draws are
"Haggling townspeople"; check where else object_mu is bound).

## Confirmed structural divergences

### 1. EN_TG shares object_mu with EN_MU — AUTO picks the wrong CMB
`zelda_mu.zar` contains two CMBs:
- `Model/couple.cmb` (38400 bytes) — En_Tg dancing couple
- `Model/marketpeople.cmb` (42624 bytes, larger) — En_Mu haggling townspeople

`Zelda3D_AutoModelId` picks by "most vertices in a non-flat non-debris CMB" (largest = main
body), so it picks **marketpeople.cmb** for BOTH actors. En_Tg then would draw as haggling
townspeople if it drew at all — but skinned→skip suppresses that, so the visible symptom is
"N64 fallback", not "wrong OoT3D mesh". Log:

    [Zelda3D] auto-loaded model 2008 (/actor/zelda_mu.zar): cmb 'Model/marketpeople.cmb' of 2, height=7731.2, bones=8 (skinned->skip)

Fix direction: a per-actor CMB override on top of object→zar. Two options:
- (a) When porting En_Tg with a behavior module, use `tryDrawModel()` (door.cpp pattern) and
  call `Zelda3D_AutoModelId("/actor/zelda_mu.zar|couple")` (forced-CMB selector already
  supported at `zelda3d_model.cpp:660`).
- (b) Add a global per-actor CMB override table keyed by (actorId, objId) so AUTO itself
  routes correctly for any actor sharing a multi-CMB ZAR. Cleaner but new infra.

Not visually consequential UNTIL En_Tg is ported. Recording now so the port picks the right
CMB and the log signal is correct.

## Downstream from this audit

Kanban #118 originally listed En_Hy + En_Mu mis-rendered + EN_DOOR-N64. Actual state after
recent work:
- En_Hy body colors — LANDED (commit 97145451, Step 2c).
- EN_DOOR — LANDED (door.cpp inc3 covers field-keep default; the market door variant is not
  distinct, it uses `m_Fnormaldoor_omote_model.cmb`).
- En_Mu mis-render — NOT REPRODUCED in Market Day/Night at reachable positions; may need a
  different scene to observe. Left open pending user pointer to a specific instance.

Remaining Market-scene per-actor ports (each a full skinned behavior port — CSAB retarget or
n64anim hook + optional head-track / material-swap):
- En_Tg (dancing couple) — uses couple.cmb, tg_matsu.csab. See divergence #1 above.
- En_Ma1 (young Malon) — uses zelda_ma1.zar, day-market instance.
- En_Heishi4 (guards) — uses zelda_sd.zar; two instances patrol Market Day.
- En_Dog — uses zelda_dog.zar; six instances scattered in Market Day.

These are ports, not bug-fixes; each deserves its own kanban card if we intend to work them.
