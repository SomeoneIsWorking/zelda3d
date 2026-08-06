---
id: I028
kind: instrument
status: trusted
created: 2026-08-06
---

## Instrument

g++ -std=gnu++20 driver linking the REAL Extractor objects out of the build tree, used to A/B Extractor::GetRoms between the HEAD and merged copies

## Validated by

Fed a case that MUST differ: cwd != searchPath with one .z64 in searchPath. HEAD copy -> found=0, merged -> found=1 with an openable absolute path. CAUGHT LYING FIRST: at -std=c++20 it compiled the std::filesystem fallback branch that NEITHER game builds (c++20 does not define 'unix'), so both copies reported found=1 and the instrument could not have shown the other answer. Rule adopted: confirm nm reports 2 dirent refs (opendir/readdir) in the object under test before believing a GetRoms result.

## Known failure modes

(none recorded yet)
