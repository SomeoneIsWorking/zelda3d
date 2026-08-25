#!/usr/bin/env python3
"""Structural tests for the SoH Zelda3D REPL composition boundary."""

from pathlib import Path
import re
import unittest


REPO = Path(__file__).resolve().parents[1]
DISPATCHER = REPO / "Shipwright/soh/src/zelda3d/repl/zelda3d_repl.cpp"
RUNTIME = REPO / "Shipwright/soh/src/zelda3d/repl/repl_runtime.cpp"
RESPONSE = REPO / "Shipwright/soh/src/zelda3d/repl/repl_response.cpp"
COMMANDS = DISPATCHER.parent / "commands"


class ReplCompositionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = DISPATCHER.read_text(encoding="utf-8")

    def test_dispatcher_has_no_command_implementation_branches(self) -> None:
        direct_command_branch = re.compile(
            r"(?:if|else\s+if)\s*\(\s*(?:std::)?strcmp\(\s*(?:cmd|command)\s*,\s*\""
        )
        self.assertIsNone(
            direct_command_branch.search(self.source),
            "zelda3d_repl.cpp must delegate command families instead of implementing literal command branches",
        )

    def test_dispatcher_composes_the_handler_families(self) -> None:
        handler_branches = re.findall(r"else\s+if\s*\(Zelda3D_[A-Za-z0-9_]+Repl(?:Command)?\(", self.source)
        self.assertGreaterEqual(len(handler_branches), 20, "dispatcher no longer composes the expected command owners")

    def test_dispatcher_does_not_mutate_gameplay_or_render_globals(self) -> None:
        self.assertNotRegex(
            self.source,
            r"\bgZelda3d[A-Za-z0-9_]*\s*=",
            "global command-family mutations belong in a focused handler module",
        )

    def test_response_file_transport_has_its_own_owner(self) -> None:
        self.assertNotIn("void Zelda3D_ReplReply", self.source)
        self.assertIn("Zelda3D_ReplReply", RESPONSE.read_text(encoding="utf-8"))

    def test_runtime_only_sequences_frame_owners(self) -> None:
        runtime = RUNTIME.read_text(encoding="utf-8")
        for operation in ("mkfifo(", "open(", "read(", "CVarSetInteger(", "Audio_QueueSeqCmd("):
            self.assertNotIn(operation, runtime, f"{operation} belongs in a focused runtime owner")
        for owner_call in (
            "TickFps",
            "ApplyTimeOverride",
            "ApplyGameCamera",
            "UpdateScreenDiagnostics",
            "ApplySessionDefaults",
            "ApplyMenuState",
            "PollTransport",
            "ApplyCameraOverrides",
        ):
            self.assertIn(owner_call, runtime)

    def test_command_composers_do_not_reimplement_command_branches(self) -> None:
        composers = (
            "actor_behavior_diagnostics.cpp",
            "actor_control.cpp",
            "instrumentation.cpp",
            "menu_launcher.cpp",
            "model_control.cpp",
            "player_control.cpp",
            "process_control.cpp",
            "render_environment.cpp",
        )
        literal_branch = re.compile(r"(?:if|else\s+if)\s*\([^\n]*strcmp\([^\n]*\"")
        for filename in composers:
            with self.subTest(filename=filename):
                source = (COMMANDS / filename).read_text(encoding="utf-8")
                self.assertIsNone(literal_branch.search(source))
                self.assertNotIn("sscanf(", source)
                self.assertGreaterEqual(source.count("ReplCommand("), 2)


if __name__ == "__main__":
    unittest.main()
