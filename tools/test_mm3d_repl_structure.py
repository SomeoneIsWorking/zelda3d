#!/usr/bin/env python3
"""Mechanical ownership gate for the focused MM REPL subsystem."""

from __future__ import annotations

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
REPL = REPO / "2ship" / "2s2h" / "zelda3d" / "repl"


class MmReplStructureTests(unittest.TestCase):
    def test_superseded_root_repl_is_gone(self) -> None:
        root = REPO / "2ship" / "2s2h"
        self.assertFalse((root / "Z3DRepl.c").exists())
        self.assertFalse((root / "Z3DRepl.h").exists())

    def test_every_command_has_one_exact_production_match(self) -> None:
        expected = {
            "actors",
            "cam",
            "linkequip",
            "linkform",
            "linkinfo",
            "linkitem",
            "linkstate",
            "mlist",
            "mptrace",
            "mscale",
            "ping",
            "posinfo",
            "quit",
            "quitteardown",
            "roomwarp",
            "switchgame",
            "tp",
            "turn",
            "warp",
        }
        matches: list[str] = []
        for source in sorted(path for path in REPL.iterdir() if path.suffix in {".c", ".cpp"}):
            matches.extend(
                re.findall(
                    r'Zelda3D_MmReplMatch\(command, "([a-z]+)"', source.read_text()
                )
            )
        self.assertEqual(set(matches), expected)
        self.assertEqual(
            len(matches), len(expected), "a command has duplicate routing ownership"
        )

    def test_router_and_entry_are_composition_only(self) -> None:
        router = (REPL / "mm3d_repl_router.c").read_text()
        entry = (REPL / "mm3d_repl.c").read_text()
        forbidden = (
            "GET_PLAYER",
            "GET_ACTIVE_CAM",
            "Room_RequestNewRoom",
            "WindowRequest",
            "Entrance_IsValid",
            "Zelda3D_SetObjectScale",
            "actor.world",
            "transitionTrigger",
            "mkfifo",
            "open(",
            "read(",
        )
        for token in forbidden:
            self.assertNotIn(
                token, router, f"router absorbed domain implementation: {token}"
            )
            self.assertNotIn(
                token, entry, f"entry absorbed domain implementation: {token}"
            )
        mutable_state = re.compile(r"^static\s+[^\n;=]+\s+s[A-Z]\w*\s*=", re.MULTILINE)
        self.assertNotRegex(router, mutable_state, "router owns mutable state")
        self.assertNotRegex(entry, mutable_state, "entry owns mutable state")

    def test_run_reset_composes_transport_and_persistent_framing(self) -> None:
        entry = (REPL / "mm3d_repl.c").read_text()
        reset = re.search(
            r"void Zelda3D_MmReplResetRunState\(void\) \{(?P<body>.*?)\n\}",
            entry,
            re.DOTALL,
        )
        self.assertIsNotNone(reset)
        body = reset.group("body")
        self.assertIn("Zelda3D_MmReplTransportReset();", body)
        self.assertIn("Zelda3D_MmFramingReplReset();", body)

        framing = (REPL / "mm3d_framing_repl.c").read_text()
        framing_reset = re.search(
            r"void Zelda3D_MmFramingReplReset\(void\) \{(?P<body>.*?)\n\}",
            framing,
            re.DOTALL,
        )
        self.assertIsNotNone(framing_reset)
        self.assertIn("sCameraActive = 0;", framing_reset.group("body"))

    def test_all_repl_sources_are_below_structure_limit(self) -> None:
        for source in sorted(path for path in REPL.iterdir() if path.suffix in {".c", ".cpp", ".h", ".hpp"}):
            self.assertLessEqual(len(source.read_text().splitlines()), 1200, source)

    def test_production_command_parser_contract(self) -> None:
        scratch = REPO / "scratch" / "tests"
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as directory:
            executable = Path(directory) / "mm3d_repl_command_test"
            subprocess.run(
                [
                    "clang",
                    "-std=c23",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(REPO / "2ship"),
                    str(REPO / "tools" / "mm3d_repl_command_test.c"),
                    str(REPL / "mm3d_repl_command.c"),
                    "-lm",
                    "-o",
                    str(executable),
                ],
                cwd=REPO,
                check=True,
            )
            subprocess.run([str(executable)], cwd=REPO, check=True)


if __name__ == "__main__":
    unittest.main()
