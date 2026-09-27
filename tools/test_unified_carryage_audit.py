"""Tests for the unified per-draw uniform carriage audit.

The audit exists because the unified route dropped two live per-draw inputs in a row (the fog mode
gate, 9.91 mean-abs at title cs=1093; the texcoord scroll, 2 of 101 draws). Both were found by
reading the packer by hand, so the check had to become a tool. These cases pin the parts of it that
can silently stop working:

* the two FIELD-NAME sets the tool reads out of the real tree are non-trivial (an empty parse that
  reports "0 per-draw fields, all clear" is the same false negative as the two bugs, one level up);
* a field written on `base` for the frame and then OVERRIDDEN per group counts as per-draw -- the
  naive "written on ubo, minus those on base" rule deletes exactly the four fields whose per-draw
  part is the interesting one, and it made the substitution table unreachable;
* every SUBSTITUTIONS entry is REACHED by the audit, because an entry nothing consults is untested
  and reads as an all-clear;
* every PER_FRAME_UNREAD entry really is written by the packer, so the list cannot assert a fact that
  is false (it did, once, with uLightVP);
* the audit FAILS on an uncarried per-draw field, checked against a fixture rather than by mutating
  the tree;
* carriage is resolved from the packer's CALL SITES, not from the helper definitions it could call.
  The first version scanned `unified_ubo.h` for `source.uX`, which counted `PackCmbFogGate`'s
  `source.uFog[3]` as carried whether or not the packer invoked it — so deleting the call, which is
  the original bug, still reported all clear. A definition nobody calls is not carriage.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import unified_carryage_audit as audit_mod  # noqa: E402

# A minimal packer/uniform pair, so the call-site resolution is testable without the real tree.
_FIXTURE_UBO = """
inline void CarryOne(CommonUbo& target, const Zelda3DSg::SgUbo& source) {
    target.uSheen[0] = source.uSheen[0];
}
inline void NeverCalled(CommonUbo& target, const Zelda3DSg::SgUbo& source) {
    target.uFogCtl[0] = source.uFog[3];
}
"""

_CALLED = """
    Zelda3DUnified::UnifiedDrawUbo uu{};
    memcpy(uu.common.uTevStages, ubo.uTevStages, sizeof(uu.common.uTevStages));
    Zelda3DUnified::CarryOne(uu.common, ubo);
"""

_UNCALLED = """
    Zelda3DUnified::UnifiedDrawUbo uu{};
    memcpy(uu.common.uTevStages, ubo.uTevStages, sizeof(uu.common.uTevStages));
"""


class TheParseIsNotSilentlyEmpty(unittest.TestCase):
    def test_the_ubo_field_list_is_parsed(self) -> None:
        fields = audit_mod.ubs_fields(audit_mod.UBO_HEADER.read_text())
        self.assertGreater(len(fields), 20, "SgUbo field parse collapsed; the audit is now vacuous")
        for expected in ("uFog", "uParams", "uExtra", "uTintSkin", "uSheen", "uTevStages"):
            self.assertIn(expected, fields)

    def test_the_per_frame_and_per_draw_sets_are_both_populated(self) -> None:
        result = audit_mod.audit()
        self.assertGreater(len(result.per_draw), 10, "per-draw parse collapsed")
        self.assertGreater(len(result.per_frame), 0, "per-frame parse collapsed")

    def test_read_parsing_excludes_assignments(self) -> None:
        text = "ubo.uFog[3] = 2.0f; float d = ubo.uFog3d0.y; ubo.uParams.x;"
        self.assertEqual(audit_mod.read_fields(text), {"uFog3d0", "uParams"})


class PerDrawBeatsPerFrame(unittest.TestCase):
    def test_a_base_field_the_group_loop_overrides_is_per_draw(self) -> None:
        """uParams/uExtra/uTintSkin/uFog are set per frame AND overridden per group.

        The first version subtracted the `base` write set from the `ubo` write set and so deleted all
        four. That reported 10 per-draw fields and consulted no substitution at all -- the audit said
        all clear while the very fields it was written to check were missing from its own view.
        """
        result = audit_mod.audit()
        for name in ("uParams", "uExtra", "uTintSkin", "uFog"):
            self.assertIn(name, result.per_draw, f"{name} is overridden per group and must count")
            self.assertNotIn(name, result.per_frame, f"{name} must not be counted as frame-only")


class TheTablesAreReachedAndTrue(unittest.TestCase):
    def test_every_substitution_is_consulted(self) -> None:
        result = audit_mod.audit()
        for name in audit_mod.SUBSTITUTIONS:
            self.assertIn(name, result.substituted, f"{name} is documented but never consulted")

    def test_every_substitution_names_a_target_and_a_reason(self) -> None:
        for name, (target, reason) in audit_mod.SUBSTITUTIONS.items():
            self.assertTrue(target.strip(), f"{name} names no target")
            self.assertGreater(len(reason), 40, f"{name} has no real reason recorded")

    def test_every_per_frame_unread_entry_really_is_written(self) -> None:
        result = audit_mod.audit()
        for name in audit_mod.PER_FRAME_UNREAD:
            self.assertIn(name, result.per_frame, f"{name} is listed as frame-unread but not written")
            self.assertNotIn(name, result.per_draw, f"{name} is listed as frame-only but is per-draw")


class CarriageFollowsCallSites(unittest.TestCase):
    def test_a_called_helper_carries_its_field(self) -> None:
        carried = audit_mod.carried_fields(_CALLED, _FIXTURE_UBO)
        self.assertIn("uTevStages", carried, "a direct memcpy in the packer block is carriage")
        self.assertIn("uSheen", carried, "a called helper's source read is carriage")

    def test_an_uncalled_helper_carries_nothing(self) -> None:
        """Deleting `PackCmbFogGate(uu.common, ubo)` is the ORIGINAL bug; this is its detector."""
        carried = audit_mod.carried_fields(_UNCALLED, _FIXTURE_UBO)
        self.assertNotIn("uFog", carried, "an uncalled helper must not count as carriage")
        self.assertNotIn("uSheen", carried)

    def test_the_real_tree_carries_the_fog_gate_through_its_call_site(self) -> None:
        result = audit_mod.audit()
        self.assertIn("uFog", result.carried)
        self.assertEqual(result.dropped, [])


class TheAuditFailsOnADrop(unittest.TestCase):
    def test_an_unclassified_per_draw_field_is_reported_as_dropped(self) -> None:
        fixture = audit_mod.Audit(
            per_draw={"uFog", "uMystery"},
            carried={"uFog"},
            read_native={"uMystery"},
            read_unified=set(),
        )
        for name in sorted(fixture.per_draw):
            if name in fixture.carried:
                continue
            if name in audit_mod.SUBSTITUTIONS:
                continue
            if not (fixture.read_native | fixture.read_unified):
                continue
            fixture.dropped.append(name)
        self.assertEqual(fixture.dropped, ["uMystery"])

    def test_the_real_tree_has_no_drop(self) -> None:
        result = audit_mod.audit()
        self.assertEqual(result.dropped, [], "a per-draw native field is neither carried nor named")

    def test_a_field_read_by_nobody_is_dead_not_dropped(self) -> None:
        """Conflating 'dead' with 'dropped' would make the tool cry wolf on uShadow/uFog2."""
        result = audit_mod.Audit(
            per_draw={"uShadow"},
            carried=set(),
            read_native=set(),
            read_unified=set(),
        )
        for name in sorted(result.per_draw):
            if name in result.carried or name in audit_mod.SUBSTITUTIONS:
                continue
            if not (result.read_native | result.read_unified):
                result.dead.add(name)
                continue
            result.dropped.append(name)
        self.assertEqual(result.dead, {"uShadow"})
        self.assertEqual(result.dropped, [])


if __name__ == "__main__":
    unittest.main()
