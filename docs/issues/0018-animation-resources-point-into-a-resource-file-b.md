---
id: 18
title: Animation resources point into a resource File buffer the ResourceManager frees when the load returns
status: fixed
symptom: ASAN reports a heap-use-after-free READ of 134 bytes in AnimationContext_SetLoadFrame during ordinary OoT gameplay: the animation's linkAnimationHeader.segment points into a Ship::File buffer that LoadResourceProcess destroyed when the nested load in AnimationFactory returned. Silent off the sanitizer.
tags: asan,resources,lifetime,animation,launcher
created: 2026-08-12
updated: 2026-08-12
---

## The finding

Found by running `tools/zelda3d_sequence.sh mm,oot,mm` under the ASAN build. ASAN reports a
**heap-use-after-free READ of 134 bytes** during ordinary OoT gameplay -- not at teardown, not on a
second run of anything. The read:

```
AnimationContext_SetLoadFrame   soh/src/code/z_skelanime.c:1035   (memcpy)
LinkAnimation_AnimateFrame      soh/src/code/z_skelanime.c:1321
Player_Action_8084CC98          ovl_player_actor/z_player.c:14189
Player_UpdateCommon / Player_Update / Actor_UpdateAll / Play_Update / Play_Main
```

The freed block is a 3,284-byte `std::vector<char>` allocated by
`Ship::O2rArchive::LoadFile` -- i.e. a resource FILE buffer -- and freed here:

```
Ship::File::~File()                         libultraship/include/ship/resource/File.h:57
Ship::ResourceManager::LoadResourceProcess  libultraship/src/ship/resource/ResourceManager.cpp:191
Ship::ResourceManager::LoadResourceProcess  ...:106
SOH::ResourceFactoryBinaryAnimationV0::ReadResource
                                            soh/soh/resource/importer/AnimationFactory.cpp:90
```

## What it means

`LoadResourceProcess` holds the `Ship::File` in a LOCAL `shared_ptr` and drops it when it returns
(`ResourceManager.cpp:153` to the closing brace at `:191`). So a resource may not retain pointers
into its own file buffer -- it has to copy what it needs.

`AnimationFactory`'s `AnimationType::Link` branch does exactly that, one level removed:

```cpp
auto animData = ...->LoadResourceProcess(path.c_str());       // line 90 -- nested load
...
animation->animationData.linkAnimationHeader.segment = animData->GetPointer();
```

The nested load's File is destroyed when that call returns, and `segment` is what
`AnimationContext_SetLoadFrame` later `memcpy`s frame data out of.

## Why it has not been noticed

Nothing about this is launcher-specific: it is a plain OoT gameplay path. Off the sanitizer the read
lands in freed-but-still-mapped memory that usually still contains the animation, because nothing
else has reused it yet. Two games churning one heap is simply the first workload that makes reuse
likely enough to matter -- which is why it surfaced now rather than being introduced now.

## NOT yet established

- Whether this is the cause of the `mm,oot,mm` release-build SIGSEGV (`SkelAnime_DrawFlexLod` under
  MM's stock player draw, core 3). Same family, different game, and the ASAN run died earlier -- in
  core 2 -- so it never reached the release build's failure point. Do not assume one fix closes both.
- **How file memory gets into `segment` at all -- the obvious reading is already ruled out.**
  `Animation::GetPointer()` returns `&animationData`, a member of the `Animation` resource object,
  NOT the File buffer (`soh/resource/type/Animation.h:73,77`). Yet ASAN says the read lands 168 bytes
  into a 3,284-byte block allocated by `O2rArchive::LoadFile`, and ASAN only reports use-after-free
  while a region is still in quarantine -- so it is genuinely freed file memory, not an `Animation`
  reallocated at the same address. Something between the nested `LoadResourceProcess` and
  `linkAnimationHeader.segment` is therefore handing back file-backed memory, and finding what is the
  next step. Candidates: the nested load resolving to a factory OTHER than the animation one (the
  `static_pointer_cast<Animation>` at `AnimationFactory.cpp:89` is unchecked, so a different resource
  type would be reinterpreted silently), or a factory in that chain that stores `file->Buffer`.
  The consumer is `memcpy(ram, animData + (sizeof(Vec3s)*limbCount + 2) * frame, ...)`
  (`z_skelanime.c:1035`) -- so `animData` is treated as a flat frame table, which is what a raw file
  blob looks like.
- Whether other factories have the same shape. `LoadResourceProcess` returning while its File dies is
  a general hazard, so a sweep of the factories for retained pointers is worth more than a point fix.

## Repro

```sh
cmake -S . -B scratch/build-asan -G Ninja -DZELDA3D_SANITIZE=address   # see issue 0009
cmake --build scratch/build-asan --target zelda3d_app -j3
cp Shipwright/build-cmake/soh/{oot,soh}.o2r scratch/build-asan/soh/    # ASAN dir needs its own
cp Shipwright/build-cmake/mm/{mm,2ship}.o2r scratch/build-asan/mm/
ASAN_OPTIONS="detect_odr_violation=0:log_path=$PWD/scratch/logs/asan/asan:detect_leaks=0" \
ZELDA3D_LAUNCHER_BIN=$PWD/scratch/build-asan/zelda3d/zelda3d ZELDA3D_SEQ_BOOT_WAIT=900 \
    tools/zelda3d_sequence.sh mm,oot,mm
```


## FIXED 2026-08-12 -- an unchecked cast, not a file-lifetime bug

The title of this issue is wrong and is kept for searchability. Nothing was pointing into a `File`
buffer. The line ASAN named was reinterpreting one resource class as another:

```cpp
auto animData = std::static_pointer_cast<Animation>(...LoadResourceProcess(path));  // ASSERTS a type
...
animation->animationData.linkAnimationHeader.segment = animData->GetPointer();
```

A Link animation's frame data is a **`PlayerAnimation`** resource -- `misc/link_animetion/
gPlayerAnimData_*`, the same resources `ResourceMgr_LoadPlayerAnimByName` loads -- and
`static_pointer_cast` does not test anything. The `PlayerAnimation` object was reinterpreted with
`Animation`'s layout, so `Animation::GetPointer()` returned `&animationData` computed from the wrong
offsets: an address inside or past an object that does not own it. `AnimationContext_SetLoadFrame`
then `memcpy`'d 134 bytes out of it every time one of those animations played, which is why the read
landed in an unrelated freed heap block and looked like a file-lifetime bug.

**Measured, not assumed:** replacing the cast with a checked `dynamic_pointer_cast` and logging the
failure reported **3 mismatches on one ordinary OoT run**, naming the three paths. Loading them as
`PlayerAnimation` and taking `limbRotData.data()` gives 0 mismatches and 0 "segment not found" --
i.e. all three now get real frame data, where before they got a bogus pointer. That is a rendering
fix as much as a memory-safety one: those three animations were reading garbage every run, silently.

Handling the type rather than merely refusing it matters -- refusing would have left them
unanimated, trading a silent corruption for a silent omission.

**Evidence:** the ASAN run that produced this issue's report now reaches gameplay with the
`AnimationContext_SetLoadFrame` use-after-free GONE. `oot`, `oot,oot`, `mm,oot` and the switch test
all exit 0.

### The two things this did NOT settle

1. **The `mm,oot,mm` crash is still there** (issue 0016) -- MM core 3, `SkelAnime_DrawFlexLod`. As
   warned above, one fix did not close both.
2. **ASAN now reports a DIFFERENT use-after-free**, previously masked because the run died here
   first: `InputViewerSettingsWindow::~InputViewerSettingsWindow` (`InputViewer.cpp:462`) logging
   through a freed spdlog logger, from `__run_exit_handlers`. That is
   [issue 0017](0017-context-destructor-logs-through-a-freed-spdlog-r.md)'s class exactly -- 0017's
   fix gave the EARLY logger an owner that never releases it, and this is a different logger, still
   registry-owned and freed before static destructors run. Recorded there.

### Still worth doing

`static_pointer_cast` on a `LoadResourceProcess` result is the general hazard, not this one line.
The same shape appears elsewhere in the resource helpers (e.g.
`ResourceMgr_LoadPlayerAnimByName` itself), and each is a silent reinterpret if the asset type ever
differs from the assumption. A sweep is worth more than this point fix.


## REOPENED the same day -- the fix closed one path, not the bug

Marking this fixed was premature. `oot` alone is genuinely clean (0 mismatches, 0 missing segments,
and an ASAN run with no report file). But `mm,oot,mm` reproduces the SAME read -- byte-identical
symptom, 134 bytes at +168 of a 3,284-byte block -- with the free now attributed to
**`AnimationFactory.cpp:102`, the ALT-ASSETS branch**, rather than line 90:

```
Ship::File::~File()  <- ResourceManager::LoadResourceProcess:191  <- :106  <- :195
                     <- ResourceFactoryBinaryAnimationV0::ReadResource  AnimationFactory.cpp:102
```

Facts, separated from guesses:

- **Fact.** The wrong-type cast at line 90 was real and is fixed: measured 3 mismatches per OoT run
  before, 0 after, and those three animations now get real frame data instead of a bogus pointer.
  That stands on its own regardless of what follows.
- **Fact.** OoT's `InitOTR` enables alt assets unconditionally
  (`CVarGetInteger(CVAR_SETTING("AltAssets"), 1)`), so the alt branch is live on every OoT run, not
  just when a user turns something on.
- **Fact.** The read target is a `Ship::File` buffer, freed when the nested load returns -- so
  something still puts file-backed memory into `linkAnimationHeader.segment` on this path. The
  `PlayerAnimation` route cannot be it: `limbRotData` is a copied `std::vector<int16_t>`.
- **Not established.** Why `oot` alone does not hit it while `oot` as the second core does. MM
  running first is the only variable, and MM's own `InitOTR`/ResourceManager is torn down before
  OoT's starts, so the mechanism is not obvious. Do not assume it is heap luck without checking --
  that assumption is what made the first version of this issue blame file lifetime.

The next step is an instrument, not more reading: log the resolved resource TYPE and the branch taken
at both `AnimationFactory.cpp:90` and `:102`, then run `mm,oot,mm`. The `oot`-alone run already
provides the negative control, which is what makes that comparison worth anything.


## The instrument ran, and it RULES OUT the AnimationFactory Link branch

`ZELDA3D_ANIMTYPE_LOG=1` (committed, gated, in `AnimationFactory.cpp`) prints branch and resolved
resource Type for EVERY Link animation, so the good and bad runs can be compared rather than only
the bad one inspected. Result:

| run | resolutions | line |
|---|---|---|
| `oot` (clean under ASAN) | 3 | `firstLoad=hit (type=1330659661) altTried=yes altResolved=no -> using PlayerAnimation` |
| `mm,oot,mm` (reproduces) | 4 | **identical** |

Every Link animation in both runs resolves the same way, to a `PlayerAnimation` whose
`limbRotData` is an owned `std::vector<int16_t>`. The extra fourth line in the longer run is one more
animation being reached, not a different outcome.

**So the freed pointer does not come from this branch.** ASAN's allocation stack in the failing run
is unambiguous -- `O2rArchive::LoadFile` -> `make_shared<std::vector<char>>`, a resource FILE buffer,
not a `limbRotData` vector -- and the only three writers of `linkAnimationHeader.segment` in the tree
are the three lines in this factory (`:83` nullptr, `:161` `Animation::GetPointer()`, `:166`
`limbRotData.data()`). None of them can produce file memory on the observed path.

That leaves the pointer arriving through `AnimationContext_SetLoadFrame`'s OWN resolution:

```c
if (ResourceMgr_OTRSigCheck(animation) != 0)
    animation = ResourceMgr_LoadAnimByName(animation);   // path string -> resource data
s16* animData = animation->segment;
```

`ResourceMgr_LoadAnimByName` returns `ResourceGetDataByName(...)`, i.e. some resource's data pointer,
and with alt assets ON (OoT enables them unconditionally) it tries an `alt/`-prefixed path first.
**That is where to look next**: which resource type `ResourceGetDataByName` returns for these paths,
and whether that type's data pointer is file-backed. It is a different mechanism from the factory
and was not on the list until the instrument eliminated the factory.

The comparative form is what made this worth running -- a log that only fired on the failing case
would have shown the same four lines and proved nothing.


## Where the trail stands (end of 2026-08-12 session)

Traced the second resolution path end to end:

```
AnimationContext_SetLoadFrame                   z_skelanime.c:1021
  ResourceMgr_LoadAnimByName(path)              ResourceManagerHelpers.cpp:533
    ResourceGetDataByName                       resourcebridge.cpp:44
      ResourceManager::GetResourceRawPointer    ResourceManager.cpp:508
        IResource::GetRawPointer()              -> for Animation, &animationData (OWNED)
```

`ResourceMgr_LoadAnimByName` casts that `void*` to `AnimationHeaderCommon*` **with no type check at
all** -- `return (AnimationHeaderCommon*)ResourceGetDataByName(path);` -- and then reads
`animHeader->frameCount` and `linkAnim->segment` through it. That is the same unchecked-reinterpret
pattern as the `static_pointer_cast<Animation>` bug confirmed and fixed above, one layer down, and it
is the strongest remaining candidate. If those paths resolve to a `PlayerAnimation`, `GetRawPointer()`
returns `limbRotData.data()` and `->segment` is two `int16_t`s of animation data read as a pointer.

**What that does not yet explain**, and the reason this is recorded as a candidate rather than a
conclusion: ASAN says the memcpy SOURCE is a real heap address inside a known 3,284-byte
`O2rArchive::LoadFile` block, at a consistent +168, which is not what reinterpreted `int16_t` payload
would usually produce. Either the reinterpretation happens to land on a plausible pointer, or there
is a third mechanism. Do not write the fix until a run shows which -- this issue has already had two
confident wrong readings (file lifetime, then the factory branch), both killed by instruments.

**Next step, concretely:** log inside `AnimationContext_SetLoadFrame` whether `ResourceMgr_OTRSigCheck`
fired, the path, and the resolved resource's `GetInitData()->Type`, then diff `oot` against
`mm,oot,mm` -- the same comparative shape that eliminated the factory. The negative control already
exists: `oot` alone is ASAN-clean.


## CORRECTION 2026-08-12: it is not a launcher bug at all

Everything above that treats "MM having run first" as the variable is **wrong**, and the error is
instructive: the gate was quitting before the demo reached the animation.

`tools/zelda3d_sequence.sh` quit each core as soon as it answered `posinfo` with a scene. `oot` alone
therefore ran a few seconds of the title demo and loaded **3** player animations; the longer
`mm,oot,mm` run happened to reach a fourth. Adding a dwell (`ZELDA3D_SEQ_DWELL=60`, committed)
reproduces the identical over-read in **`oot` ALONE**:

```
ZELDA3D ANIM: frame 28 requested from an animation whose header says 24 frame(s)
  -- path "__OTR__objects/gameplay_keep/gPlayerAnim_link_uma_anim_fastrun", limbCount 22.
     Reading 670 bytes past what this table can hold.
```

So: a plain OoT gameplay bug, in the title demo's horse-riding segment, reachable in a single run
with no launcher involved. Every "requires MM first" discriminator recorded above measured **how long
the gate let the run last**, not what the run did. `oot,oot` showing 0 occurrences said the same
thing.

The remaining question is unchanged and is now much better posed: `gPlayerAnim_link_uma_anim_fastrun`
has a 24-frame header and exactly 24 frames of data (3,216 bytes at 134 bytes/frame -- internally
consistent), and the player asks for frame 28. That is a PLAYBACK overrun -- `curFrame` past the end
-- not an asset or resource-resolution problem, so both of this issue's earlier titles are wrong.
`Player_Action_8084CC98` (`z_player.c:14189`) is the horse-riding action that requests it.

**The gate lesson is the durable part**: an acceptance gate that stops at the first playable frame
reports "clean" for everything that happens afterwards, and three separate conclusions in this issue
were built on exactly that silence.


## FIXED 2026-08-12 -- a frame index taken from another actor, with no bound

`z_player.c:14189`, the horse-riding action:

```c
this->skelAnime.curFrame = rideActor->curFrame;   // the HORSE's playhead
LinkAnimation_AnimateFrame(play, &this->skelAnime);
```

Link's riding pose is frame-locked to the horse's animation, and the horse's animation can be longer
than Link's. Two lines above, `this->skelAnime.animation` is assigned directly out of `D_80854944`
rather than through `LinkAnimation_Change`, so nothing ever tells that skelAnime how long its new
animation is -- and the frame index then comes from a different actor entirely. Nothing in that path
compares the two.

On N64 the animation data was one contiguous segment, so overrunning it quietly read the neighbouring
animation and nobody noticed. Here each animation is its own heap allocation, so it is a
heap-buffer-overflow: 134 bytes read from 670 past the end of a 3,216-byte buffer.

Fixed by clamping to Link's own last frame -- the bound that was missing, not a workaround for a
symptom: frame N of an animation with fewer than N frames has no correct value. The visible effect is
that Link's pose holds on its final frame for the few frames the horse's animation runs longer, which
is what the data can actually express.

**Evidence:** `ZELDA3D_SEQ_DWELL=60 tools/zelda3d_sequence.sh oot` reported the over-read 1/1 before
and **0** after. `oot`, `oot,oot`, `mm,oot,mm` and the switch test all exit 0. The permanent check in
`AnimationContext_SetLoadFrame` stays -- it is what will catch the next one, and it reports rather
than clamps precisely so a mismatch elsewhere cannot hide behind a plausible pose.

### What this issue got wrong three times, kept as the record

1. **"Animation resources point into a resource File buffer"** (the title) -- no. Nothing pointed into
   a File.
2. **"An unchecked `static_pointer_cast<Animation>` in AnimationFactory"** -- a real bug, found and
   fixed (3 animations per run were reading garbage), but not this one.
3. **"Requires MM to have run first"** -- an artifact of the gate quitting at the first playable
   frame. `oot` alone reproduces it once allowed to run 60s longer.

Each was killed by an instrument rather than by argument, and each instrument was built only after
the previous conclusion had already been written down as fact.
