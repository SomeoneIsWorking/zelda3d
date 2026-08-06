---
id: 11
title: Ship::Audio bound to the first game's Config for the process lifetime, so a second game read and saved its audio settings into the departed game's config file
status: resolved
symptom: With two games run back to back in one launcher process, the sequence gate reported Audio as inherited/UNFINISHED; underneath, the running game's audio backend and channel settings resolved against the OTHER game's config file.
tags: n3,audio,config,launcher,shared-runtime
created: 2026-08-06
updated: 2026-08-06
---

## Root cause


## What was tried / dead ends


## Resolution

### Resolution (2026-08-06)
ROOT CAUSE: Audio::Init did 'mConfig = Context::GetRawInstance()->GetConfig()' once and held that shared_ptr forever, but Context::GetConfig returns the per-game GameSession's Config and a second game installs a fresh one at its own file (shipofharkinian.json vs 2ship2harkinian.json). Init never re-runs for the second game, so Audio kept the departed game's Config -- and GetSavedAudioBackend/SetCurrentAudioBackend do SetString + Save(), so changing one game's audio backend wrote into the other game's config file. MEASURED with temporary probes across 'sequence mm,oot': MM created Config 0x73fcf80 at 2ship2harkinian.json, Audio::Init bound to 0x73fcf80, OoT then created 0x7fb16414d0e0 at shipofharkinian.json, and Audio::Init never logged again. FIX (commit 88b038c0): CurrentConfig() resolves Context's config per use (10 sites) instead of caching it; Audio reclassified SplitPending -> Engine since nothing in it is per-game once the capture is gone; ApplySavedSettings() factored out of Init and re-run when a different game attaches so the new game's saved backend/channels reach the already-running device. VERIFIED: the gate's Audio UNFINISHED warning is replaced by 'InitAudio skipped -- SHARED with the previous game', Console is now the only undivided subsystem, exit 0 with 0 crash markers.
