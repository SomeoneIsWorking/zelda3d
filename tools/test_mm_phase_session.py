#!/usr/bin/env python3
"""Exact-scene and ownership falsifiers for the live MM phase session."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from mm_phase_catalog import Scene
from mm_phase_session import LiveMM, PhaseSessionError


class StubClient:
    def __init__(self, replies: list[str]):
        self.replies = iter(replies)

    def request(self, _command: str) -> str:
        return next(self.replies)


class StubRuntime:
    def __init__(self, status: object):
        self.paths = SimpleNamespace(repl_fifo=Path("unused.fifo"))
        self._status = status

    def status(self, _lease: object) -> object:
        return self._status


class PhaseSessionTests(unittest.TestCase):
    def test_refuses_foreign_runtime_and_names_its_pid(self) -> None:
        status = SimpleNamespace(
            game_alive=False,
            xvfb_alive=False,
            instance=None,
            foreign_games=(SimpleNamespace(pid=4312),),
        )
        session = LiveMM(
            StubRuntime(status),  # type: ignore[arg-type]
            object(),  # type: ignore[arg-type]
            1.0,
            client=StubClient([]),  # type: ignore[arg-type]
        )
        with self.assertRaisesRegex(PhaseSessionError, "4312"):
            session.require_idle()

    def test_accepts_only_the_exact_expected_scene(self) -> None:
        status = SimpleNamespace(
            game_alive=False, xvfb_alive=False, instance=None, foreign_games=()
        )
        session = LiveMM(
            StubRuntime(status),  # type: ignore[arg-type]
            object(),  # type: ignore[arg-type]
            1.0,
            client=StubClient(["scene=45 pos=(1,2,3)"]),  # type: ignore[arg-type]
        )
        scene = Scene("termina-field", 0x5400, 45)
        self.assertEqual(session.wait_for_scene(scene, 1.0), "scene=45 pos=(1,2,3)")


if __name__ == "__main__":
    unittest.main()
