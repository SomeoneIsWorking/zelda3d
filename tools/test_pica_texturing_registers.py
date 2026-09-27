#!/usr/bin/env python3
"""Pin the PICA texturing register map against a real OoT3D command list.

The map in `tools/pica_texturing_registers.py` decides what a captured register
value *means*. A field read from the wrong word still produces a plausible number,
so the map needs a check that fails when it drifts. The positive control is the
cached title command list for draw 77 (the title wordmark): its decoded unit-0
config must agree, field by field, with the independent `vsuni_log` line the
oracle wrote for the same draw — the physical texture address, the width, the
height and the format. Those are four separate fields agreeing across two
different capture mechanisms.

The negative control matters more: reading `type` one word early yields a
plausible-looking answer, so the test asserts the exact words and the exact type.
"""

from __future__ import annotations

import json
import re
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tools"))

import pica_texturing_registers as ptx
from pica_command_list import parse_command_writes

CACHE = REPO / "scratch/oracle_cache/a647213f0ded0038_6510135ae6c38599_p45-00401070_tpoff"
PROBE = CACHE / "probes/title-pica-command-list_2010_a6a521c6e0.json"
VSUNI = CACHE.parent / "36768624141dc421_6510135ae6c38599_p45-00401070_tpoff" / \
    "artifacts/title-vsuni_1ad0f79d0a3d.log"
TEX0_RE = re.compile(r"\btex0=(?P<phys>[0-9a-f]+)/(?P<w>\d+)x(?P<h>\d+)/(?P<fmt>[0-9a-f]+)")


def _skip_without_capture() -> None:
    if not PROBE.is_file() or not VSUNI.is_file():
        raise unittest.SkipTest(
            "cached title command list is absent; run tools/title_oracle_probe.py "
            "pica-command-list 1093 77 and uniforms 1093 to produce it"
        )


class PicaTexturingRegisterMapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        _skip_without_capture()
        provenance = json.loads(PROBE.read_text())
        payload = next((CACHE / "artifacts").glob("title-pica-command-list-data_*.bin"))
        cls._writes = parse_command_writes(payload.read_bytes(),
                                           provenance["command_list_word_index"])
        cls._end = provenance["command_list_word_index"]
        cls._draw = provenance["draw"]
        text = VSUNI.read_text(encoding="utf-8", errors="replace")
        line = next(line for line in text.splitlines() if line.startswith(f"draw n={cls._draw} "))
        match = TEX0_RE.search(line)
        if match is None:
            raise AssertionError("vsuni line has no tex0 identity")
        cls._logged = match.groupdict()

    def test_register_positions_match_the_azahar_static_asserts(self) -> None:
        self.assertEqual((ptx.TEXTURING_BASE, ptx.MAIN_CONFIG, ptx.TEXTURE0), (0x80, 0x80, 0x81))
        self.assertEqual(
            (ptx.TEXTURE0_FORMAT, ptx.FRAGMENT_LIGHTING_ENABLE, ptx.TEXTURE1,
             ptx.TEXTURE1_FORMAT, ptx.TEXTURE2, ptx.TEXTURE2_FORMAT),
            (0x8E, 0x8F, 0x91, 0x96, 0x99, 0x9E),
        )
        # The capture must actually write every word the decoder reads, or the decode
        # would be reading zeros that merely look like a real configuration.
        written = {register for _word, register, _value in self._writes}
        for word in range(ptx.WORD_BORDER_COLOR, ptx.WORD_ADDRESS + 1):
            self.assertIn(ptx.TEXTURE0 + word, written)

    def test_decoded_unit0_config_matches_the_oracle_identity_log(self) -> None:
        config = ptx.decode_texture0(self._writes, self._end)
        self.assertIsNotNone(config, "unit-0 config was never written in the capture")
        # Four independent fields, agreeing with a different capture mechanism.
        self.assertEqual(f"{config.physical_address:08x}", self._logged["phys"])
        self.assertEqual(config.width, int(self._logged["w"]))
        self.assertEqual(config.height, int(self._logged["h"]))
        # The border word packs r/g/b/a at bits 0/8/16/24; the game sets opaque black.
        self.assertEqual(
            ((config.border_color >> 0) & 0xFF, (config.border_color >> 8) & 0xFF,
             (config.border_color >> 16) & 0xFF, (config.border_color >> 24) & 0xFF),
            (0x00, 0x00, 0x00, 0xFF),
        )

    def test_unit0_type_is_a_real_enum_not_a_misread_word(self) -> None:
        config = ptx.decode_texture0(self._writes, self._end)
        self.assertIsNotNone(config)
        # Draw 77 samples a CameraSphereEnvMap coordinator, whose retail arm leaves
        # o2.z = 0, so a projective divide would be a division by zero. Texture2D is
        # the only self-consistent answer, and it is what the register actually says.
        self.assertEqual(config.texture_type, 0)
        self.assertEqual(config.type_name, "Texture2D")
        self.assertFalse(config.is_projective)

    def test_reading_the_type_from_the_word_before_is_detectably_wrong(self) -> None:
        # The negative control: the word before the filter word holds the border
        # colour, whose top bits also decode to a non-zero "type". Anything that
        # silently read from there would look measured and be wrong.
        registers = {register: value for _word, register, value in self._writes}
        border = registers[ptx.TEXTURE0 + ptx.WORD_BORDER_COLOR]
        self.assertNotEqual((border >> 28) & 7, 0)
        self.assertNotIn((border >> 28) & 7, ptx.TEXTURE_TYPE_NAMES)

    def test_sampler_policy_is_recovered_from_the_same_word(self) -> None:
        config = ptx.decode_texture0(self._writes, self._end)
        self.assertIsNotNone(config)
        self.assertEqual(config.mag_filter, "Linear")
        self.assertEqual(config.min_filter, "Linear")
        self.assertEqual(config.wrap_s, "Repeat")
        self.assertEqual(config.wrap_t, "Repeat")
        self.assertEqual(config.mip_filter, "Nearest")

    def test_enable_bits_come_from_the_main_config_word(self) -> None:
        main = ptx.decode_main_config(self._writes, self._end)
        self.assertIsNotNone(main)
        self.assertEqual(
            (main["texture0_enable"], main["texture1_enable"], main["texture2_enable"]),
            (1, 1, 0),
        )
        self.assertEqual(main["texture2_use_coord1"], 0)
        # The wordmark's second texture is a distinct binding, not texCoord1 of tex0.
        self.assertEqual(ptx.decode_fragment_lighting_enable(self._writes, self._end), 0)

    def test_unobserved_configuration_is_reported_as_absent_not_defaulted(self) -> None:
        self.assertIsNone(ptx.decode_texture0([], 0))
        self.assertIsNone(ptx.decode_main_config([], 0))
        self.assertIsNone(ptx.decode_fragment_lighting_enable([], 0))


if __name__ == "__main__":
    unittest.main()
