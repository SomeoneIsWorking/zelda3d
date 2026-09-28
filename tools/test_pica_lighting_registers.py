#!/usr/bin/env python3
"""Tests for the PICA lighting register map and the shading-term reducer.

The load-bearing claims here are the ones that would otherwise be captions:

* The field map is PARSED from `Azahar/src/video_core/pica/regs_lighting.h`, so a corrected
  upstream name cannot silently rot. `test_map_is_parsed_not_transcribed` fails if the parse
  silently returns an empty dict, which is the failure mode that would make every other test
  vacuous.
* `0xff7fffff` and `0xff7effff` differ at bit **16** (`disable_lut_d0`), not at bit `0x11`.
  `docs/project-state.md` records the difference as "bit `0x11`, which the recovered builder
  names `MODE_SPOT_INDEX`". The XOR is 0x00010000; there is no arithmetic under which that is
  bit 17. This test pins the correction so the caption cannot drift back.
* The reduction is checked in BOTH directions on real captured words: 0 active terms for the
  oracle's `0x80000400`/`0xff7fffff`, and 14 for a maximally-enabled legal word. An instrument
  that can only ever report "nothing needed" is indistinguishable from a broken one, so the
  positive direction is not optional.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from pica_lighting_registers import (  # noqa: E402
    CONFIG_ENUM,
    LIGHTING_CONFIG_NAMES,
    REGS_LIGHTING_H,
    SAMPLER_NAMES,
    LightingRegisterMapError,
    diff_config_words,
    load_register_map,
    reduce_lighting,
)

# The words the oracle actually reported, quoted from the project's recorded captures.
CAPTURED_CONFIG0 = 0x80000400
CAPTURED_CONFIG1_COMMON = 0xFF7FFFFF  # 11 of 12 MM3D draws
CAPTURED_CONFIG1_VARIANT = 0xFF7EFFFF  # 1 of 12


class RegisterMapTests(unittest.TestCase):
    def setUp(self) -> None:
        self.regmap = load_register_map()

    def test_map_is_parsed_not_transcribed(self) -> None:
        """The parse must be non-vacuous, and must come from the cited source."""
        self.assertTrue(self.regmap.config0, "config0 parsed empty")
        self.assertTrue(self.regmap.config1, "config1 parsed empty")
        self.assertEqual(self.regmap.source, REGS_LIGHTING_H)
        self.assertEqual(REGS_LIGHTING_H.name, "regs_lighting.h")

    def test_config0_field_positions(self) -> None:
        expected = {
            0: "enable_shadow",
            2: "enable_primary_alpha",
            3: "enable_secondary_alpha",
            4: "config",
            16: "shadow_primary",
            17: "shadow_secondary",
            27: "clamp_highlights",
            28: "bump_mode",
        }
        for bit, name in expected.items():
            self.assertEqual(self.regmap.config0[bit][0], name, f"config0 bit {bit}")

    def test_config1_field_positions(self) -> None:
        expected = {
            0: "disable_shadow",
            8: "disable_spot_atten",
            16: "disable_lut_d0",
            17: "disable_lut_d1",
            19: "disable_lut_fr",
            20: "disable_lut_rr",
            21: "disable_lut_rg",
            22: "disable_lut_rb",
            24: "disable_dist_atten",
        }
        for bit, name in expected.items():
            self.assertEqual(self.regmap.config1[bit][0], name, f"config1 bit {bit}")

    def test_bit18_is_not_a_named_field(self) -> None:
        """Bit 18 is the hardwired dummy: it must NOT decode to a real field.

        If a future upstream edit gave it a name, the hardwired-word refusal below would still
        pass but the field list would change; this pins that the hole is understood rather than
        merely unparsed.
        """
        self.assertNotIn(18, self.regmap.config1)

    def test_config_width_is_four_bits(self) -> None:
        name, width = self.regmap.config0[4]
        self.assertEqual(name, "config")
        self.assertEqual(width, 4)


class CapturedWordDiffTests(unittest.TestCase):
    """The correction to a recorded project claim."""

    def setUp(self) -> None:
        self.regmap = load_register_map()

    def test_the_two_captured_config1_words_differ_at_bit_16(self) -> None:
        diffs = diff_config_words(
            CAPTURED_CONFIG1_COMMON, CAPTURED_CONFIG1_VARIANT, "config1", self.regmap)
        self.assertEqual(diffs, [(16, "disable_lut_d0")])

    def test_the_difference_is_not_bit_0x11(self) -> None:
        """Explicit refutation of the recorded "bit 0x11 / MODE_SPOT_INDEX" claim.

        The XOR is 0x00010000. Bit 0x11 is 17 and is clear in BOTH words, so it cannot be the
        difference under any reading of the field map.
        """
        xor = CAPTURED_CONFIG1_COMMON ^ CAPTURED_CONFIG1_VARIANT
        self.assertEqual(xor, 0x00010000)
        self.assertEqual((CAPTURED_CONFIG1_COMMON >> 17) & 1, 1)
        self.assertEqual((CAPTURED_CONFIG1_VARIANT >> 17) & 1, 1)
        self.assertNotIn(17, [bit for bit, _ in diff_config_words(
            CAPTURED_CONFIG1_COMMON, CAPTURED_CONFIG1_VARIANT, "config1", self.regmap)])


class ReductionTests(unittest.TestCase):
    def setUp(self) -> None:
        self.regmap = load_register_map()

    def test_captured_common_word_needs_no_optional_terms(self) -> None:
        r = reduce_lighting(CAPTURED_CONFIG0, CAPTURED_CONFIG1_COMMON, self.regmap)
        self.assertEqual(r.active_terms(), [], "the captured config must need no optional term")
        self.assertEqual(r.config, 0)
        self.assertEqual(r.config_name, "Config0")

    def test_captured_common_word_disables_every_lut_and_attenuation(self) -> None:
        r = reduce_lighting(CAPTURED_CONFIG0, CAPTURED_CONFIG1_COMMON, self.regmap)
        self.assertTrue(r.all_slots_shadow_disabled)
        self.assertTrue(r.all_slots_spot_atten_disabled)
        self.assertTrue(r.all_slots_dist_atten_disabled)
        for name, enabled in r.lut_enabled.items():
            self.assertFalse(enabled, f"{name} LUT should be disabled by 0x{CAPTURED_CONFIG1_COMMON:08x}")

    def test_captured_variant_enables_exactly_the_d0_lut(self) -> None:
        """The 1-of-12 material's ONLY optional term is a real half-vector specular."""
        r = reduce_lighting(CAPTURED_CONFIG0, CAPTURED_CONFIG1_VARIANT, self.regmap)
        self.assertEqual(r.active_terms(), ["lut:Distribution0"])
        self.assertTrue(r.lut_enabled["Distribution0"])
        self.assertFalse(r.lut_enabled["Distribution1"])

    def test_instrument_can_report_the_other_answer(self) -> None:
        """CONTROL. A maximally-enabled but still legal hardware word needs every term.

        Bit 18 is kept set so this word is one the hardware could actually produce; if the
        reducer can only ever say "nothing needed" it is broken, not clever.
        """
        config0 = (8 << 4) | 1 | 4 | 8 | (1 << 27) | (1 << 28)  # Config7 + the term switches
        config1 = 1 << 18  # only the hardwired dummy set
        r = reduce_lighting(config0, config1, self.regmap)
        terms = r.active_terms()
        self.assertEqual(r.config_name, "Config7")
        for expected in ("bump", "shadow-source", "clamp-highlights", "shadow",
                         "spot-attenuation", "distance-attenuation",
                         "lut:Distribution0", "lut:Distribution1", "lut:Fresnel",
                         "lut:ReflectRed", "lut:ReflectGreen", "lut:ReflectBlue"):
            self.assertIn(expected, terms)
        self.assertGreaterEqual(len(terms), 14)


class RefusalTests(unittest.TestCase):
    """Skipped/unusable input is a failure, not a default."""

    def setUp(self) -> None:
        self.regmap = load_register_map()

    def test_word_without_the_hardwired_bit_is_refused(self) -> None:
        with self.assertRaises(LightingRegisterMapError) as ctx:
            reduce_lighting(CAPTURED_CONFIG0, 0x00000000, self.regmap)
        self.assertIn("bit 18", str(ctx.exception))

    def test_invalid_lighting_config_is_refused(self) -> None:
        # config field = 7 is the documented hole (regs_lighting.h:58-59); it must not decode.
        with self.assertRaises(LightingRegisterMapError) as ctx:
            reduce_lighting((7 << 4), CAPTURED_CONFIG1_COMMON, self.regmap)
        self.assertIn("not a valid configuration", str(ctx.exception))

    def test_config7_hole_is_preserved(self) -> None:
        """`Config7` is 8, not 7. A dense enum would mislabel the hole."""
        self.assertEqual(LIGHTING_CONFIG_NAMES[8], "Config7")
        self.assertNotIn(7, LIGHTING_CONFIG_NAMES)
        self.assertIn(CONFIG_ENUM, REGS_LIGHTING_H.read_text(encoding="utf-8"))

    def test_shadow_and_attenuation_are_per_slot_not_global(self) -> None:
        """One cleared bit in an 8-wide field must re-enable the term, not stay 'all disabled'."""
        config1 = CAPTURED_CONFIG1_COMMON & ~(1 << 3)  # clear disable_shadow bit 3 only
        r = reduce_lighting(CAPTURED_CONFIG0, config1, self.regmap)
        self.assertFalse(r.all_slots_shadow_disabled)
        self.assertIn("shadow", r.active_terms())
        self.assertTrue(r.all_slots_dist_atten_disabled)


class SamplerSupportTableTests(unittest.TestCase):
    """`config_supports` is a property of the CONFIG alone, so it is testable directly.

    It is not observable through `active_terms` on the captured words, because every sampler the
    captured config could enable is also switched off by config1 there. A mutation that emptied
    `Distribution1`'s exclusion set therefore passed every reduction test while making the table
    wrong -- the mutation check found it, and this is the test that closes it.
    """

    def setUp(self) -> None:
        self.regmap = load_register_map()

    def _supports(self, config: int, name: str) -> bool:
        # config1 = only the hardwired dummy: every LUT bit clear, so config alone decides.
        r = reduce_lighting((config << 4), 1 << 18, self.regmap)
        return r.config_supports[name]

    def test_distribution1_is_unsupported_by_config0(self) -> None:
        # regs_lighting.h:108-110: Distribution1 is out for Config0, Config1 and Config5.
        self.assertFalse(self._supports(0, "Distribution1"))
        self.assertFalse(self._supports(1, "Distribution1"))
        self.assertFalse(self._supports(5, "Distribution1"))
        self.assertTrue(self._supports(4, "Distribution1"))

    def test_distribution0_is_only_unsupported_by_config1(self) -> None:
        for config in sorted(LIGHTING_CONFIG_NAMES):
            self.assertEqual(
                self._supports(config, "Distribution0"), config != 1,
                f"Distribution0 under {LIGHTING_CONFIG_NAMES[config]}")

    def test_spotlight_attenuation_is_unsupported_by_config2_and_3(self) -> None:
        for config in sorted(LIGHTING_CONFIG_NAMES):
            self.assertEqual(
                self._supports(config, "SpotlightAttenuation"), config not in (2, 3),
                f"SpotlightAttenuation under {LIGHTING_CONFIG_NAMES[config]}")

    def test_fresnel_is_unsupported_by_config0_2_4(self) -> None:
        for config in sorted(LIGHTING_CONFIG_NAMES):
            self.assertEqual(
                self._supports(config, "Fresnel"), config not in (0, 2, 4),
                f"Fresnel under {LIGHTING_CONFIG_NAMES[config]}")

    def test_reflect_red_is_unsupported_only_by_config3(self) -> None:
        for config in sorted(LIGHTING_CONFIG_NAMES):
            self.assertEqual(
                self._supports(config, "ReflectRed"), config != 3,
                f"ReflectRed under {LIGHTING_CONFIG_NAMES[config]}")

    def test_reflect_green_and_blue_require_config4_5_7(self) -> None:
        for name in ("ReflectGreen", "ReflectBlue"):
            for config in sorted(LIGHTING_CONFIG_NAMES):
                self.assertEqual(
                    self._supports(config, name), config in (4, 5, 8),
                    f"{name} under {LIGHTING_CONFIG_NAMES[config]}")


class SamplerTableTests(unittest.TestCase):
    def test_every_sampler_name_is_a_real_source_name(self) -> None:
        source = REGS_LIGHTING_H.read_text(encoding="utf-8")
        for name in SAMPLER_NAMES:
            self.assertIn(name, source, f"{name} is not a real LightingSampler")

    def test_spotlight_attenuation_has_no_disable_lut_bit(self) -> None:
        """Guards the bug this module shipped with: treating a per-slot gate as a LUT gate."""
        source = REGS_LIGHTING_H.read_text(encoding="utf-8")
        self.assertIn("disable_spot_atten", source)
        self.assertNotIn("disable_lut_sp;", source)


if __name__ == "__main__":
    unittest.main()
