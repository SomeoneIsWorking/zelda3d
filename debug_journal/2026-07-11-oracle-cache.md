# 2026-07-11 — persistent oracle data cache

Tooling task (workflow-first, user-requested): repeated A/B and probe runs against the
embedded-Azahar oracle (`tools/soh3d_harness`) were re-running Az from scratch for every
request, even for az (Azahar) title-cs frames already captured in a prior session. That's
pure waste — the oracle's output at a given az frame is fully determined by (savestate,
ROM, Azahar rendering patches), so it's cacheable.

## Design

- **Key**: `sha256(scratch/title_settled.state)[:16]` + `sha256($ZELDA3D_OOT3D_ROM)[:16]` +
  a marker derived from `tools/soh3d_harness/AZAHAR_PATCH.md`'s heading list (patch count +
  hash of headings). Any savestate change, ROM swap, or Azahar-patch edit mints a fresh,
  independent cache context — a stale cache just sits unused rather than silently serving
  wrong frames. Implemented as `harness_ctl.cache_key()` / `harness_ctl.OracleCache`.
- **Layout**: `scratch/oracle_cache/<key>/{index.json, frames/az<N>.png, probes/<probe>_<N>_<argship>.json}`.
  Frames stored as PNG (not the raw ~280KB PPM) to shrink cache size. `index.json` records
  the full key metadata (savestate/ROM paths+hashes, patch marker) for auditability plus a
  manifest of every cached frame/probe.
- **API** (`tools/harness_ctl.py`): `OracleCache.get_frame`/`put_frame` (by az frame
  number), `get_probe`/`put_probe` (by probe name + az frame + args-hash — covers az camera
  eye/at, `az_daytime`, `titleactors`, `vsuni_log`, `az_fog` LUTs, or any future deterministic
  probe), `stats()`, `invalidate()`.
- **CLI** (`tools/oracle_cache.py`): `stats` (entries/size/active key + all on-disk key
  contexts), `warm [az1 az2 ...]` (batch-captures frames in ONE harness session; no-arg form
  warms the standard sweep points `{100,200,360,500,700,764,1000,1300,1522,1700,1900}`),
  `invalidate` (clears the current key's entries). Warns above 2 GB.
  - NOTE: `tools/oracle_cache.py` already existed as a DIFFERENT cache (memoizing
    `link_ctl.py warp` scene-probe results for `tools/market_scene_probe.py`, at
    `scratch/oracle_cache/warp/`). Merged rather than clobbered: the old `warp()` /
    `invalidate_warp_cache()` (alias `invalidate`) API is untouched and still used by
    `market_scene_probe.py`; the new frame/probe-cache CLI lives alongside it as
    argparse subcommands. Both namespaces share the `scratch/oracle_cache/` root but never
    collide on disk (`warp/` vs sha-keyed dirs).
- **title_ab.py `ab` integration**: cache-aware on the oracle side. On a cache hit for the
  target az frame, the `run <az>` stepping loop (the expensive part) is skipped entirely and
  the stored PNG is reused for the oracle pane; the harness process still boots both engines
  together (title_ab's harness has no clean SoH-only boot path — restructuring that further
  was out of scope for this pass), but `soh_step` for the SoH side always runs live (SoH
  changes every build, so it's never cached). Reports "oracle: cache hit" or "oracle: live
  run (cached now)" so the caller can see which path ran. On a miss, the captured az PNG is
  written into the cache before returning.

## Constraint hit: soh3d_harness is single-instance (PID-locked)

`tools/soh3d_harness/main.cpp` takes an exclusive lock at
`$XDG_RUNTIME_DIR/soh3d_harness.lock` — only one harness process can run at a time, regardless
of `ZELDA3D_INSTANCE` (that env var is for the multi-instance GAME harness,
`tools/zelda3d_game.sh`, not this RE harness). Another agent had a harness instance running
when this session started `warm`; had to wait for the lock to free rather than kill it
(never hand-kill another agent's process — tooling owns lifecycle). The cache doesn't change
this constraint — `warm`/cache-miss paths still need exclusive harness access, same as any
other harness-driving script.

## Verification

`tools/oracle_cache.py warm` (standard sweep points) pre-warmed the cache under key
`def9e41b126c7991_6510135ae6c38599_p24-350d6c1f` (ROM = the provisioned OoT3D USA .3ds,
savestate = the existing `scratch/title_settled.state`, 24 headings in AZAHAR_PATCH.md).

`title_ab.py ab 1000 --soh 1408 --name cachetest` timing (same machine, same session,
harness lock free both times):
- cold (az=1000 entry evicted, oracle stepped live): **165.3 s** — reported
  "oracle: live run (cached now)".
- cache-hit (az=1000 cached): **66.8 s** — reported "oracle: cache hit". The residual
  66.8 s is SoH boot + 1408 live `soh_step`s, which correctly always run (SoH is the
  thing under test). Delta: **-98.5 s, 2.47x faster** — the entire Az stepping cost
  eliminated.
- Determinism + byte-identity: sha256 `65e3f94d3fe0…` identical across (a) the original
  `warm` capture, (b) an independent cold-run recapture after evicting the entry, and
  (c) the oracle pane (`cachetest.az.png`) the cache-hit run emitted. Three independent
  Az runs/reads, one byte-exact frame.

Cache stats after warm+verify: 11 frames, 0 probes, 1.3 MB under key
`def9e41b126c7991_6510135ae6c38599_p24-350d6c1f` (PNG shrank the ~280 KB/frame PPMs to
~120 KB/frame; a 2 GB warn threshold guards runaway growth).

## Files

- `tools/harness_ctl.py` — `OracleCache` class + `cache_key()`.
- `tools/oracle_cache.py` — CLI (`stats`/`warm`/`invalidate`), merged with the pre-existing
  warp-probe cache module.
- `tools/title_ab.py` — cache-aware `cmd_ab`.
- `docs/parity-workflow.md` — "Oracle data cache" section.
