# 0024 — AppImage inherited SoH packaging and working-directory storage

status: IMPLEMENTED LOCALLY — final AppImage artifact and packaged first-run gate remain open

## Root cause

The unified launcher had no release-owned provisioning boundary. Linux CPack still staged the old
SoH desktop entry and icon, declared a terminal, knew only the OoT core, and could install the
user-derived `oot.o2r`. Independently, `Context::GetAppDirectoryPath` fell through to `.` on normal
desktop builds, so settings, saves, logs, and generated archives landed in the checkout or the
read-only AppImage mount. OoT then scanned surrounding directories for a ROM while MM consumed
arguments, creating three unrelated setup paths.

## Fix

- `zelda3d_shared/platform` now owns content identity for all four N64/3DS inputs, composes Lucent's
  bounded nested-ZIP inspection/extraction, transactional selection persistence, and SDL Browse/Quit setup UI. ZIPs
  are detected by content rather than filename, and persisted selections record explicit cache
  ownership so replacing an imported ROM can never delete a directly selected user file.
- Acceptance shares the N64 extractor's exact-revision whole-image CRC validation. The existing
  `CtrRom` reader now validates the complete consumed RomFS graph and NCCH/IVFC content hash chain;
  header-family recognition alone never accepts a file. This is asset integrity, not retail RSA
  authentication or validation of unused executable/other-partition contents.
- Filesystem paths stay native through 3DS reading and process handoff, with wide Windows environment
  APIs and one UTF-8 JSON boundary. Serialization and buffered close finish before activation;
  failed publication restores the previous active selection. N64 exporter Windows path limits
  remain explicitly tracked in S010.
- The launcher activates all four validated selections before loading a core. OoT and MM consume
  only those selected N64 paths when generating their runtime archives.
- Writable state resolves through the OS configuration directory; Linux follows
  `$XDG_CONFIG_HOME/<app>` or `~/.config/<app>`.
- `tools/package_appimage.py` stages the launcher, both cores, both redistributable port archives,
  both extractor metadata sets, and the Zelda3D SVG. Its content gate refuses ROMs and the
  ROM-derived `oot.o2r`, `oot-mq.o2r`, and `mm.o2r`.
- `tools/build_appimage_release.py` compiles with Clang in an Ubuntu 22.04 baseline container. Artifact
  verification extracts the actual AppImage and rejects payloads requiring newer than GLIBC 2.35 or
  GLIBCXX 3.4.30, preventing a warm Fedora checkout from silently defining the release baseline. It
  also regenerates `2ship.o2r` with the authoritative `GAME_MM` exporter rather than accepting the
  normal host build tree's potentially stale copy.
- The old-baseline build exposed an existing lambda capture that Clang 14 cannot form from a
  structured binding. The network loop now names the map entry and captures its concrete client ID,
  preserving the same removal policy across the release compiler range.
- The inherited SoH AppImage desktop file and CPack downloader/generator were removed, leaving one
  Linux release path.

## Evidence

The current combined host build, CTest, core-isolation, incremental-build, and Clang gate results
are recorded in [project state S008](../project-state.md#s008--linux-ci-coverage). The setup suite covers all four identities, direct
persistence, nested ZIP import, ambiguous/unsafe/corrupt ZIP refusal, complete-content failure,
Unicode host paths, JSON encoding failure, buffered-close failure, rename rollback, and XDG
configuration-root resolution. The source/release Python suite passes 454 tests with three declared
skips. Both local 3DS dumps pass complete RomFS integrity: OoT3D has 1,944 files and 115,606 verified
blocks; MM3D has 1,851 files and 160,144 verified blocks. The MM provisioning suite proves that release custom-archive
generation removes a stale output, requires no ROM, uses the `GAME_MM` build, and validates the new
archive; an isolated Clang/Ninja run also generated and validated the real 1,023,393-byte ROM-free
`2ship.o2r`. Final AppImage construction, packaged first-run smoke testing, release upload, and
digest recording remain before this issue may be marked released.
