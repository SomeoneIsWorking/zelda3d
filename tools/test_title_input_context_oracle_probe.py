"""Focused cache behavior and response parsing for the title input probe."""

from __future__ import annotations

import unittest

from title_input_context_oracle_probe import (
    RELEASE_FRAMES,
    advance_title,
    capture_probe,
    parse_buttons,
    watch_record_count,
)


class FakeCache:
    key = "test"

    def __init__(self) -> None:
        self.result = None
        self.calls = 0

    def get_probe(self, _name, _frame, _args):
        return self.result

    def put_probe(self, _name, _frame, _args, result):
        self.result = result


class TitleInputContextProbeTests(unittest.TestCase):
    def test_advance_uses_bounded_commands(self) -> None:
        commands = []

        class Harness:
            def send(self, command):
                commands.append(command)
                return f"ok {command}"

        advance_title(Harness(), 65)

        self.assertEqual(commands, ["run 30", "run 30", "run 5"])

    def test_cache_hit_skips_live_capture(self) -> None:
        cache = FakeCache()
        expected = {"input_context": "0x08000000"}
        cache.result = expected

        result, hit = capture_probe(cache, 85, capture_live=lambda *_args: self.fail("must not capture"))

        self.assertTrue(hit)
        self.assertEqual(result, expected)

    def test_watch_response_rejects_a_truncated_capture(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "truncated"):
            watch_record_count(["ok hits 128", "ok end"])

    def test_watch_response_accepts_a_valid_empty_observation(self) -> None:
        self.assertEqual(watch_record_count(["ok hits 0", "ok end"]), 0)

    def test_cache_miss_persists_a_complete_negative_observation(self) -> None:
        cache = FakeCache()
        negative = {"input_context": "0x00000000", "outcome": "no-input-context"}

        result, hit = capture_probe(cache, 85, capture_live=lambda *_args: negative)

        self.assertFalse(hit)
        self.assertEqual(result, negative)
        self.assertEqual(cache.result, negative)

    def test_probe_key_changes_with_title_advance(self) -> None:
        cache = FakeCache()
        seen = []

        def capture(_cache, args, advance, settle, buttons):
            seen.append((args["advance_frames"], advance, args["settle_frames"], settle, buttons))
            return {"advance": advance}

        result, hit = capture_probe(cache, 85, 300, capture_live=capture)

        self.assertFalse(hit)
        self.assertEqual(result, {"advance": 300})
        self.assertEqual(seen, [(300, 300, RELEASE_FRAMES, RELEASE_FRAMES, ("start",))])

    def test_button_parser_rejects_unknown_controls(self) -> None:
        self.assertEqual(parse_buttons("start,a"), ("start", "a"))
        with self.assertRaisesRegex(ValueError, "subset"):
            parse_buttons("start,b")


if __name__ == "__main__":
    unittest.main()
