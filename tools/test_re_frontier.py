import importlib.util
import tempfile
import unittest
from pathlib import Path


MODULE_PATH = Path(__file__).with_name("re_frontier.py")


def load_module():
    spec = importlib.util.spec_from_file_location("re_frontier_under_test", MODULE_PATH)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


class ReFrontierSerializationTests(unittest.TestCase):
    def test_round_trip_has_no_trailing_whitespace_or_extra_eof_line(self):
        frontier = load_module()
        with tempfile.TemporaryDirectory() as temp_dir:
            roadmap = Path(temp_dir) / "re-frontier.md"
            roadmap.write_text(
                "# Test frontier\n\n"
                "## Render\n\n"
                "### render.test — Test entry\n"
                "- status: todo\n"
                "- deps:\n"
                "- evidence: binary\n"
                "- where: source.cpp\n"
                "- gap:\n"
                "- notes:\n",
                encoding="utf-8",
            )
            frontier.ROADMAP = str(roadmap)

            entries, order = frontier.load()
            frontier.save(entries, order)

            serialized = roadmap.read_text(encoding="utf-8")
            self.assertTrue(serialized.endswith("\n"))
            self.assertFalse(serialized.endswith("\n\n"))
            self.assertFalse(
                any(line.endswith((" ", "\t")) for line in serialized.splitlines())
            )


if __name__ == "__main__":
    unittest.main()
