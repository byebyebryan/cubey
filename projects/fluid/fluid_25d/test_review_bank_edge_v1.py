import unittest
from pathlib import Path

import review_bank_edge_v1 as review


class BankEdgeReviewTests(unittest.TestCase):
    def test_variants_change_only_appearance(self):
        self.assertEqual(review.VARIANTS["baseline"]["water_bank_irregularity_m"], 0)
        self.assertEqual(review.VARIANTS["static"]["water_bank_motion_m"], 0)
        self.assertEqual(review.VARIANTS["moving"]["water_bank_irregularity_m"], 3)
        self.assertEqual(review.VARIANTS["moving"]["water_bank_motion_m"], 1)
        self.assertEqual(review.VARIANTS["bold-static"]["water_bank_motion_m"], 0)
        self.assertEqual(review.VARIANTS["bold-moving"]["water_bank_motion_m"], 12)
        for name in ("bold-static", "bold-moving"):
            self.assertEqual(review.VARIANTS[name]["water_bank_irregularity_m"], 24)
            self.assertEqual(review.VARIANTS[name]["water_bank_scale_m"], 128)
            self.assertEqual(review.VARIANTS[name]["water_bank_band_m"], 64)
        for values in review.VARIANTS.values():
            self.assertTrue(all(key.startswith("water_bank_") for key in values))

    def test_held_clock_is_not_solver_acceleration(self):
        for fps in (30, 60):
            cmd = review.command(review.review.SCENES[0], Path("clip.mp4"), Path("tuning.json"),
                                 frames=fps * 16, fps=fps)
            self.assertEqual(cmd[cmd.index("--frames") + 1], str(fps * 16))
            self.assertAlmostEqual(float(cmd[cmd.index("--fluid25d-recording-frame-interval-seconds") + 1]), 1 / fps)
            self.assertEqual(cmd[cmd.index("--fluid25d-native-bank-view") + 1], "bspline-2x")
            self.assertNotIn("--fluid25d-solver", cmd)
            self.assertNotIn("--fluid25d-rainfall-rate-mm-per-hour", cmd)

    def test_explicit_advancing_and_ablation(self):
        cmd = review.command(review.review.SCENES[0], Path("clip.mp4"), Path("tuning.json"),
                             frames=360, advancing=True, view="no-bank-edge", bank="reference")
        self.assertEqual(cmd[cmd.index("--fluid25d-recording-frame-interval-seconds") + 1], "10")
        self.assertEqual(cmd[cmd.index("--fluid25d-scenic-water-view") + 1], "no-bank-edge")
        self.assertEqual(cmd[cmd.index("--fluid25d-native-bank-view") + 1], "reference")


if __name__ == "__main__":
    unittest.main()
