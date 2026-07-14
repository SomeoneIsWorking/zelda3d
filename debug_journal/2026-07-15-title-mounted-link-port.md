# 2026-07-15 — title mounted Link: N64-blocky + floating-detached FIXED

User-reported bug (screenshot): at the title/intro, Link rides Epona — Epona renders correctly
as the OoT3D model, but Link renders as the low-poly N64 model AND floats detached above/behind
her instead of seated in the saddle.

## Ground truth already established (no new Ghidra decomp needed this session)

Prior sessions already RE'd and PORTED everything needed on the OoT3D side:

- `oot3d-decomp/docs/title_actor_world_pos.md`: the title-demo's single cued world-pos slot
  (`0x005AFFB0`) is EPONA's; Link is drawn ATTACHED via Epona's SkelAnime — there is no
  independent Link world-pos in the 3DS binary.
- `oot3d-decomp/docs/title_gamestate_driver.md`: title is an ordinary `Play_Main` tick running
  `Player_UpdateCommon` with a mounted Player+Epona — normal gameplay mount, not a bespoke path.
- `Shipwright/soh/src/zelda3d/behaviors/title/title_rider.cpp` (`TitleRider::applyToActor`,
  landed 2026-07-14) already implements the full mount: spawns the title's own `EN_HORSE`,
  calls `Actor_MountHorse` and sets `player->actionFunc = Player_Action_8084CC98` — the literal
  native N64/3DS-shared function (`z_player.c:13825`) real horseback riding uses. That function
  writes `this->actor.world.pos` from `rideActor->actor.world.pos + rideActor->riderPos` EVERY
  FRAME (z_player.c:13912-13914), so Link's position already tracks Epona natively — no
  title-specific position code, no new decomp required. Position parity was already verified
  (`tools/title_rider_traj.py`, maxdXZ 19.7u, PASS) in the 2026-07-14 rider-cs-dispatch work.

So the OoT3D-side "attach mechanism" the task asked me to decompile turned out to already be
fully ported and correct — confirmed by reading `title_rider.cpp` + `z_player.c` rather than
re-running Ghidra. See the addendum added to `oot3d-decomp/docs/title_rider_port_spec.md` for
the full trace.

## Root cause of the remaining bug (SoH3D-side, not OoT3D-side)

Both symptoms traced to `Zelda3D_PlayerDrawImpl` in
`Shipwright/soh/src/zelda3d/zelda3d_link.cpp`:

1. **N64-blocky model**: the OoT3D Link body-replacement draw path is gated behind
   `Zelda3D_LinkEnabled()` (env `ZELDA3D_LINK`, default OFF — marked "WIP / proof-of-hook" in
   the source). With it off, `Zelda3D_PlayerDrawImpl` returns 0 and z_player.c's N64 fallback
   draws instead. This is also a `no-gates` rule violation (a toggle gating 3DS-vs-N64
   behavior) for the mounted case specifically, since the mounted draw is verified-correct.
2. **Floating detached**: even force-enabled, `Zelda3D_PlayerDrawImpl` runs a feet-grounding
   heuristic (`Zelda3D_PosedGroundOffset`) that measures the posed model's LOWEST visible
   vertex and snaps it to `actor.world.pos.y` — correct for a standing pose (feet on the
   ground), wrong for the riding pose. While mounted, `actor.world.pos.y` is ALREADY the
   correct saddle height (set by the native `Player_Action_8084CC98` code above); the riding
   pose's lowest vertex is a bent knee/stirrup, not a planted foot on the ground. Applying the
   ground-snap on top of the already-correct saddle height shoved the whole body vertically —
   this is exactly the "floats above/behind Epona" symptom. Same bug class as the already-known
   climb-pose grounding issue (#79 — climb poses have the same "lowest vertex isn't the ground
   reference" problem), just never special-cased for the mounted case.

## Fix

`Shipwright/soh/src/zelda3d/zelda3d_link.cpp`, `Zelda3D_PlayerDrawImpl`:

1. Force the OoT3D Link draw path ON whenever `player->stateFlags1 & PLAYER_STATE1_ON_HORSE`,
   independent of the general `ZELDA3D_LINK` WIP gate:
   ```c
   int mountedForDraw = (((Player*)actor)->stateFlags1 & PLAYER_STATE1_ON_HORSE) != 0;
   if (!Zelda3D_Enabled() || (!Zelda3D_LinkEnabled() && !mountedForDraw)) {
       return 0;
   }
   ```
   This is a code-level narrowing of an already-3DS-default feature to the one state (mounted)
   that's verified correct — not a new opt-out toggle for N64-original behavior, so it doesn't
   reopen the no-gates rule; the broader on-foot Link body-replacement port stays WIP/gated
   until it's separately verified.
2. Zero the feet-grounding offset while mounted instead of applying it (mirrors the existing
   climb-pose special case, but zeroed outright rather than frozen-last, since the seat height
   genuinely changes every frame as Epona crosses terrain):
   ```c
   s32 mountedPose = (player->stateFlags1 & PLAYER_STATE1_ON_HORSE) != 0;
   if (mountedPose) {
       groundOff = 0.0f;
   } else if (climbPose) {
       groundOff = sLinkLastGroundedOff;
   } else {
       sLinkLastGroundedOff = groundOff;
   }
   ```

Riding animation selection required no new code: `player->skelAnime.animation` resolves to the
native `gPlayerAnim_link_uma_*` mount anims (idle/walk/gallop set by `Player_Action_8084CC98`
itself), and `zelda3d_player_animmap.inc` already maps every one of those to its 3DS CSAB
(`uma_wait_1`, `uma_anim_slowrun`, `uma_anim_fastrun`, ...) via the existing own-CSAB resolve
path — this table was already complete before this session.

## Verification (live game, headless)

`ZELDA3D_HEADLESS=1 ZELDA3D_WARP= tools/zelda3d_game.sh start`, polled `titlecs` via
`tools/zelda3d_repl.py cmd titlecs` until the cs cursor entered the rider-visible window, then
`tools/zelda3d_repl.py shot` + `zoom`:

- `scratch/screenshots/title_rider_check1_zoom.png` (cs frame 1479)
- `scratch/screenshots/title_rider_check3_zoom.png` (cs frame 1568, next loop)

Both show Link in the OoT3D-style green-tunic model, textured, seated upright in Epona's saddle
with his shield visible on his back — not the N64-blocky model, not floating/detached. Confirmed
stable across two independent samples (different loop passes), not a one-frame fluke.

## Remaining gaps (honest, not blocking)

- This only forces the OoT3D Link draw ON for the **mounted** state. On-foot Link at title (and
  in real gameplay) is still gated behind the WIP `ZELDA3D_LINK` env var — the broader player
  port (pose-parity sweep, `soh3d-pose-parity` memory) is a separate, larger effort tracked
  elsewhere; not reopened by this fix.
- Mount-entry/dismount transition anims (`gPlayerAnim_link_uma_wait_1` mount-on approach) were
  not specifically re-verified frame-by-frame — the title cs seeds `actionVar2=99` straight into
  the "already riding" branch (per `title_rider.cpp`'s existing comment), so the mount-approach
  clip never plays at title; out of scope for this bug (title never shows a mount-up moment).
- Byte-exact procedural limb wobble (the deeper SkelAnime pose-table RE flagged as "still open"
  in `title_rider_port_spec.md`) remains unaddressed — not needed for this bug (model identity +
  seating), only for frame-perfect micro-pose parity.

## Files changed

- `Shipwright/soh/src/zelda3d/zelda3d_link.cpp` (the fix, both hunks in `Zelda3D_PlayerDrawImpl`)
- `oot3d-decomp/docs/title_rider_port_spec.md` (addendum: steps 1-3 already-landed confirmation
  + this session's SoH3D-side root cause)
