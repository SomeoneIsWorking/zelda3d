"""Focused cache-hit behavior for the title checkpoint producer."""

from __future__ import annotations

import tempfile
import unittest
import json
from pathlib import Path
from unittest.mock import patch

import title_settle


class TitleSettleTests(unittest.TestCase):
    def test_existing_checkpoint_skips_the_oracle(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "title.state"
            output.write_bytes(b"cached state")

            with patch.object(title_settle.sys, "argv", ["title_settle.py", "--out", str(output)]):
                with patch.object(title_settle, "spawn") as spawn:
                    title_settle.main()

            spawn.assert_not_called()

    def test_existing_checkpoint_records_observed_cursor_without_spawn(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            state = Path(directory) / "title.state"
            metadata = Path(directory) / "title.json"
            state.write_bytes(b"cached state")

            with patch.object(title_settle, "SAVESTATE", state), patch.object(title_settle, "METADATA", metadata):
                with patch.object(title_settle.sys, "argv", ["title_settle.py", "--record-initial-cs", "85"]):
                    with patch.object(title_settle, "spawn") as spawn:
                        title_settle.main()

            self.assertEqual(json.loads(metadata.read_text())["initial_title_cs"], 85)
            spawn.assert_not_called()


if __name__ == "__main__":
    unittest.main()
