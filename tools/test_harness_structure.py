"""Structural contracts for the embedded OoT3D harness composition."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
HARNESS = REPO / "tools" / "soh3d_harness"
MANIFEST = HARNESS / "harness.cmake"
ARTIFACT_DISCOVERY = HARNESS / "shipping_artifacts.cmake"
MAIN = HARNESS / "main.cpp"
SOURCE_LINE_LIMIT = 1200


class HarnessStructureTests(unittest.TestCase):
    def test_cmake_manifest_owns_every_translation_unit_exactly_once(self) -> None:
        source = MANIFEST.read_text(encoding="utf-8")
        listed = re.findall(r"\$\{_harness_root\}/([A-Za-z0-9_]+\.cpp)", source)
        actual = sorted(path.name for path in HARNESS.glob("*.cpp"))

        self.assertEqual(
            len(listed), len(set(listed)), "duplicate harness source in CMake"
        )
        self.assertEqual(sorted(listed), actual)

    def test_harness_sources_stay_below_the_normal_ceiling(self) -> None:
        for path in (
            *HARNESS.rglob("*.cpp"),
            *HARNESS.rglob("*.h"),
            *HARNESS.rglob("*.cmake"),
        ):
            with self.subTest(path=path.name):
                lines = len(path.read_text(encoding="utf-8").splitlines())
                self.assertLessEqual(lines, SOURCE_LINE_LIMIT, f"{path}: {lines} lines")

    def test_entry_point_only_includes_the_owners_it_calls(self) -> None:
        source = MAIN.read_text(encoding="utf-8")
        retired_monolith_dependencies = (
            "actor_compare.h",
            "boss_fd_compare.h",
            "comparison_commands.h",
            "first_div_compare.h",
            "oracle_control_commands.h",
            "repl_protocol.h",
            "title_sync.h",
        )
        for header in retired_monolith_dependencies:
            with self.subTest(header=header):
                self.assertNotIn(f'#include "{header}"', source)

        for owner in (
            "HarnessBinaryFile::Read",
            "HarnessFrontend::ConfigureDirectories",
            "HarnessFrontend::InitializeOracleVideo",
            "HarnessFrontend::PresentSideBySide",
            "HarnessProcess::LoadRepoEnvironment",
            "HarnessRepl::Run",
            "HarnessWatchdog::Install",
        ):
            with self.subTest(owner=owner):
                self.assertIn(owner, source)

    def test_deleted_python_facade_is_not_reintroduced(self) -> None:
        self.assertFalse((REPO / "tools" / "harness_ctl.py").exists())
        self.assertTrue((REPO / "tools" / "harness_cli.py").is_file())

    def test_bridge_headers_are_the_only_harness_abi_declaration_owners(self) -> None:
        contracts = {
            "oracle_render_debug_bridge.h": {
                "soh3d_draw_log_path",
                "soh3d_draw_log_active",
                "soh3d_vsuni_log_path",
                "soh3d_vsuni_log_active",
                "soh3d_draw_skip",
                "soh3d_fog_dump",
            },
            "soh_capture_bridge.h": {
                "gSoh3dCaptureBuf",
                "gSoh3dCaptureCap",
                "gSoh3dCaptureW",
                "gSoh3dCaptureH",
                "gSoh3dCapturePending",
                "gSoh3dDepthDumpPath",
                "gSoh3dDepthDumpPending",
                "gSoh3dFb0LastCaptureAttempt",
                "gSoh3dFb0LastW",
                "gSoh3dFb0LastH",
                "gSoh3dFb0LastHasColor",
                "gSoh3dFb0LastInRange",
            },
            "soh_render_debug_bridge.h": {
                "gZelda3dFog3dForceOff",
                "gZelda3dFog3dOn",
                "gZelda3dFog3d",
            },
            "soh_title_bridge.h": {
                "Zelda3D_TitleCsFrame",
                "Zelda3D_TitleCsSetFrame",
                "Zelda3D_TitleCsEndFrame",
                "Zelda3D_TitleCsCamera",
                "Zelda3D_Title_RiderState",
                "Zelda3D_Title_CameraState",
            },
        }
        sources = list(HARNESS.glob("*.cpp"))
        for header, symbols in contracts.items():
            owner = (HARNESS / header).read_text(encoding="utf-8")
            for symbol in symbols:
                self.assertIn(symbol, owner, f"{header} no longer owns {symbol}")
                declaration = re.compile(
                    rf"^\s*(?:extern\s+.*\b{re.escape(symbol)}\b|"
                    rf"(?:int|void|bool|std::size_t)\s+{re.escape(symbol)}\s*\([^;{{]*\)\s*;)",
                    re.MULTILINE,
                )
                for source_path in sources:
                    with self.subTest(
                        header=header, symbol=symbol, source=source_path.name
                    ):
                        self.assertIsNone(
                            declaration.search(source_path.read_text(encoding="utf-8"))
                        )

    def test_watchpoint_layout_and_c_abi_have_one_header_owner(self) -> None:
        owner = (HARNESS / "oracle_watch_bridge.h").read_text(encoding="utf-8")
        self.assertEqual(owner.count("struct WatchRecord"), 1)
        self.assertEqual(owner.count("struct WatchRange"), 1)
        for consumer in ("watch_commands.cpp", "watchhook.cpp"):
            source = (HARNESS / consumer).read_text(encoding="utf-8")
            self.assertIn('#include "oracle_watch_bridge.h"', source)
            self.assertNotIn("struct WatchRecord", source)
            self.assertNotIn("struct WatchRange", source)

    def test_lockstep_owner_does_not_forward_other_subsystems(self) -> None:
        header = (HARNESS / "lockstep_runner.h").read_text(encoding="utf-8")
        self.assertIn("HandleStep", header)
        for forwarded in (
            "SohBooted",
            "BootSoh",
            "HandleSohBoot",
            "HandleSohStep",
            "MarkManualStateTouch",
            "PrintTitleSyncStatus",
            "ReadOracleVblankCounter",
        ):
            self.assertNotIn(forwarded, header)

    def test_actor_command_has_one_dispatch_owner(self) -> None:
        repl = (HARNESS / "harness_repl.cpp").read_text(encoding="utf-8")
        player_probe = (HARNESS / "player_probe_commands.cpp").read_text(
            encoding="utf-8"
        )

        self.assertEqual(repl.count('cmd == "actors"'), 1)
        self.assertEqual(repl.count("HarnessOracle::HandleActors(toks)"), 1)
        self.assertNotIn('command == "actors"', player_probe)
        self.assertNotIn('#include "oracle_actor_commands.h"', player_probe)

    def test_boss_fd_interfaces_have_one_responsibility_each(self) -> None:
        comparison = (HARNESS / "boss_fd_compare.h").read_text(encoding="utf-8")
        state = (HARNESS / "soh_boss_fd_state.h").read_text(encoding="utf-8")
        profile = (HARNESS / "boss_fd_profile_validation.h").read_text(encoding="utf-8")
        policy = (HARNESS / "boss_fd_comparison_policy.h").read_text(encoding="utf-8")

        self.assertIn("BossFdCompareStatus", comparison)
        self.assertNotIn("BossFdAuthoredState", comparison)
        self.assertNotIn('extern "C"', comparison)
        self.assertIn("BossFdAuthoredState", state)
        self.assertIn("SohState_BossFdAuthoredState", state)
        self.assertIn("MatchesComparisonScope", profile)
        self.assertIn("MatchesForcedInitialization", profile)
        self.assertIn("Result Evaluate", policy)

    def test_harness_imports_shipping_artifacts_portably(self) -> None:
        manifest = MANIFEST.read_text(encoding="utf-8")
        source = ARTIFACT_DISCOVERY.read_text(encoding="utf-8")
        self.assertNotIn("libsoh_core.so", source)
        self.assertNotIn("libultraship.so", source)
        self.assertIn("GLOB_RECURSE", source)
        self.assertIn("multiple", source)
        self.assertIn("shipping_artifacts.cmake", manifest)
        self.assertIn("CMAKE_SHARED_LIBRARY_PREFIX", manifest)
        self.assertIn("CMAKE_SHARED_LIBRARY_SUFFIX", manifest)
        self.assertIn("ZELDA3D_SHIPPING_BUILD_DIR", manifest)


if __name__ == "__main__":
    unittest.main()
