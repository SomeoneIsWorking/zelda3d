"""Focused cache-hit behavior for the title checkpoint producer."""

from __future__ import annotations

import tempfile
import unittest
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


if __name__ == "__main__":
    unittest.main()
