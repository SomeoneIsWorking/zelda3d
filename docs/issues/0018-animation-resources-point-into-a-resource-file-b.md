---
id: 18
title: Animation resources point into a resource File buffer the ResourceManager frees when the load returns
status: open
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
