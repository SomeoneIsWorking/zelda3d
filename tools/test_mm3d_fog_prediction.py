"""The fog-prediction instrument must be able to say BOTH answers, without a ROM.

Three instruments in a row reported a positive result they had no way to avoid, and this one is the
fourth attempt at the same question. The failure mode is always the same shape: when no candidate
produced a usable prediction the code returned the *worst* error in the "best" slot, so the verdict
compared a real worst against a control's best -- which guarantees separation, and therefore guarantees
success. One version was worse and read "no candidate was valid" **on the control** as support for the
positive conclusion.

Then the fourth attempt got the OPPOSITE wrong, in a way no MM3D data could reveal: the validity check
on a solved `cameraNear` was written `cn < fogNear`, which is backwards, because the form returns
exactly 1.0 whenever `d(0) = cameraNear < fogNear` -- so "below fogNear" IS the clamped case. That
rejected all 8 windows and turned a CONFIRMED result into a REFUTED one, recorded and committed as a
finding. Only a synthetic target generated from a known window exposed it.

So these tests pin the instrument's behaviour on targets it generated itself, which needs no ROM, no
emulator and no measured LUT. They are the standing guarantee that the tool can say NO as well as YES;
if a future edit makes it one-sided, these fail rather than the authorities quietly becoming wrong.
"""

from __future__ import annotations

import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

import mm3d_fog_prediction as tool  # noqa: E402


class TheFormIsTranscribedCorrectly(unittest.TestCase):
    """The form must reproduce the OoT3D case it was validated on, or nothing it says is conclusive."""

    def test_the_positive_control_still_holds(self) -> None:
        ok, text = tool.check_oot3d()
        self.assertTrue(ok, f"the form no longer reproduces OoT3D's recorded values:\n{text}")

    def test_the_recorded_values_are_the_oracles_own(self) -> None:
        """Guard the literals: if these are edited, the control becomes self-certifying."""
        w = tool.OOT3D_ZORA
        self.assertEqual(w["recorded_d127"], 834.0)
        self.assertEqual(w["recorded_lut127"], 0.979)
        self.assertEqual(w["recorded_lut125"], 1.0)
        self.assertEqual(w["camera_near"], 7.0)


class TheSolvedBranchRecoversWhatItWasGiven(unittest.TestCase):
    def test_it_recovers_a_window_from_a_curve_it_generated(self) -> None:
        """The ramp must be ACTIVE at t=0, which is the only case where LUT(0) carries any
        information about cameraNear -- and the case MM3D exercises."""
        zfar, camera_near, fog_near, fog_far = 16000.0, 197.0, 160.0, 13200.0
        self.assertGreaterEqual(camera_near, fog_near, "this case needs the ramp active at t=0")
        target = [tool.fog3d_node(i / 128.0, zfar, camera_near, fog_near, fog_far) for i in range(128)]
        result = tool.predict(target, [(0, int(fog_near), fog_far, zfar)], None)
        self.assertIsNotNone(result.best, "the solved branch produced no prediction on its own output")
        _index, recovered, _n, _f, _z = result.best[0]
        self.assertAlmostEqual(recovered, camera_near, delta=1.0)
        self.assertLess(result.error, 0.002)

    def test_it_refuses_to_invert_a_clamped_first_entry(self) -> None:
        """cameraNear < fogNear makes LUT(0) exactly 1.0, which says nothing about cameraNear.

        Inverting it anyway returns fogNear and looks like a prediction. Accepting that is the bug
        that flipped the MM3D verdict, so it gets a test of its own.
        """
        zfar, camera_near, fog_near, fog_far = 12000.0, 7.0, 800.0, 2400.0
        target = [tool.fog3d_node(i / 128.0, zfar, camera_near, fog_near, fog_far) for i in range(128)]
        self.assertEqual(target[0], 1.0, "this case needs LUT(0) clamped")
        result = tool.predict(target, [(0, int(fog_near), fog_far, zfar)], None)
        self.assertIsNone(result.best, "a clamped LUT(0) must not yield a prediction")

    def test_a_prediction_can_report_that_none_exists(self) -> None:
        """The property whose absence made three instruments report success unconditionally."""
        result = tool.predict([1.0] * 128, [(0, 800, 2400.0, 12000.0)], None)
        self.assertIsNone(result.best)
        self.assertEqual(result.error, float("inf"))
        self.assertIn("NO PREDICTION EXISTS", result.summary())


class TheInstrumentCanSayNo(unittest.TestCase):
    def test_a_target_no_window_can_produce_is_refuted(self) -> None:
        target = [1.0 - (i / 127.0) ** 3 * 0.9 for i in range(128)]
        result = tool.predict(target, tool.scene_slots(tool.SCENE), None)
        control = tool.predict(target, tool.scene_slots(tool.SCENE), tool.OOT3D_CAMERA_NEAR)
        self.assertGreater(min(result.error, control.error), 0.05,
                           "a cubic curve the recovered windows cannot produce came out close to a "
                           "match, so the instrument is not discriminating")

    def test_the_built_in_selftest_agrees(self) -> None:
        ok_pos, _pos = tool.synthetic_positive()
        ok_neg, _neg = tool.synthetic_negative()
        self.assertTrue(ok_pos, "the instrument cannot say YES on a target it generated")
        self.assertTrue(ok_neg, "the instrument cannot say NO on an unproducible target")


class TheVerdictNeverTurnsAMissingControlIntoSuccess(unittest.TestCase):
    """The exact defect: a control that produced nothing was read as support for the positive."""

    def test_no_real_prediction_with_a_live_control_is_refuted(self) -> None:
        dead = tool.Prediction(best=None, worst=float("inf"), candidates=8, usable=0)
        alive = tool.Prediction(best=((0, 1.0, 1.0, 2.0, 3.0), 0.01), worst=0.05, candidates=8, usable=8)
        text, predicted = tool.verdict(dead, alive)
        self.assertFalse(predicted, f"a missing prediction was treated as a hit: {text}")

    def test_no_prediction_on_either_side_is_inconclusive_not_positive(self) -> None:
        dead = tool.Prediction(best=None, worst=float("inf"), candidates=8, usable=0)
        text, predicted = tool.verdict(dead, dead)
        self.assertFalse(predicted)
        self.assertIn("could not discriminate", text)

    def test_a_real_separation_is_reported_as_one(self) -> None:
        real = tool.Prediction(best=((0, 1.0, 1.0, 2.0, 3.0), 0.005), worst=0.02, candidates=8, usable=8)
        fake = tool.Prediction(best=((0, 1.0, 1.0, 2.0, 3.0), 0.02), worst=0.05, candidates=8, usable=8)
        _text, predicted = tool.verdict(real, fake)
        self.assertTrue(predicted)


if __name__ == "__main__":
    unittest.main()
