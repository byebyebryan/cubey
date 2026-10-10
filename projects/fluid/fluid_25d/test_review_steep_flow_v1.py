import unittest
from pathlib import Path

import review_steep_flow_v1 as review


class SteepFlowReviewTests(unittest.TestCase):
    def test_default_is_off(self):
        self.assertEqual(review.VARIANTS["off"], {})
        for variant in ("rapid", "rapid-large"):
            self.assertEqual(review.VARIANTS[variant]["water_rapid_strength"], 1)
            self.assertTrue(64 <= review.VARIANTS[variant]["water_rapid_scale_m"] <= 384)

    def test_held_clip_crosses_real_time_phase_without_solver_flags(self):
        cmd = review.command(review.review.SCENES[0], Path("clip.mp4"), frames=360)
        self.assertEqual(cmd[cmd.index("--frames") + 1], "360")
        self.assertEqual(cmd[cmd.index("--fps") + 1], "30")
        self.assertAlmostEqual(float(cmd[cmd.index("--fluid25d-recording-frame-interval-seconds") + 1]), 1 / 30)
        self.assertIn("--fluid25d-recording-gpu-validation", cmd)
        self.assertNotIn("--fluid25d-rainfall-rate-mm-per-hour", cmd)
        self.assertNotIn("--fluid25d-solver", cmd)

    def test_diagnostic_selection_is_explicit(self):
        cmd = review.command(review.review.SCENES[1], Path("mask.png"), view="rapid-activity")
        self.assertEqual(cmd[cmd.index("--fluid25d-scenic-water-view") + 1], "rapid-activity")
        self.assertNotIn("--capture", cmd)

    def test_cascade_ablations_are_independent(self):
        self.assertEqual(review.VARIANTS["cascade-only"], {"water_cascade_strength": 1})
        self.assertEqual(review.VARIANTS["landing-only"], {"water_landing_strength": 1})
        self.assertEqual(review.VARIANTS["cascade"], {
            "water_cascade_strength": 1, "water_landing_strength": 1})


if __name__ == "__main__":
    unittest.main()
