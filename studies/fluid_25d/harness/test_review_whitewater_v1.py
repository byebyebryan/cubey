import unittest
from pathlib import Path

import review_whitewater_v1 as review


class WhitewaterReviewTests(unittest.TestCase):
    def test_retained_controls_do_not_follow_macro_defaults(self):
        import review_stream_foam_v1 as foam

        retained = review.RETAINED_FLOW_CUES
        for key in ("water_rapid_strength", "water_cascade_strength", "water_landing_strength",
                    "water_whitewater_strength", "water_stream_foam_strength"):
            self.assertEqual(retained[key], 0)
        self.assertEqual(retained["water_stream_foam_patchiness"], 0)
        self.assertEqual(retained["water_stream_foam_brightness"], 1)
        subtle = {**retained, **foam.VARIANTS["subtle"]}
        self.assertEqual(subtle["water_stream_foam_strength"], .3)
        self.assertEqual(subtle["water_stream_foam_patchiness"], 1)
        self.assertEqual(subtle["water_stream_foam_brightness"], .55)
        self.assertEqual(subtle["water_whitewater_speed"], 2)
        self.assertFalse(any("solver" in key or "rain" in key for key in retained))

    def test_ablations_and_clock_rate_are_independent(self):
        self.assertNotIn("water_whitewater_strength", review.VARIANTS["off"])
        self.assertEqual(review.VARIANTS["both"]["water_whitewater_strength"], 1)
        self.assertEqual(review.VARIANTS["fast"]["water_whitewater_speed"], 2)
        self.assertNotIn("water_stream_foam_strength", review.VARIANTS["fast"])
        self.assertEqual(review.VARIANTS["network"]["water_stream_foam_strength"], 1)

    def test_replay_clock_does_not_accelerate_fields(self):
        for fps in (30, 60):
            cmd = review.command(review.review.SCENES[0], Path("clip.mp4"), Path("tuning.json"),
                                 frames=fps * 12, fps=fps)
            self.assertEqual(cmd[cmd.index("--frames") + 1], str(fps * 12))
            self.assertEqual(cmd[cmd.index("--fps") + 1], str(fps))
            self.assertAlmostEqual(float(cmd[cmd.index("--fluid25d-recording-frame-interval-seconds") + 1]),
                                   1 / fps)
            self.assertIn("--fluid25d-recording-gpu-validation", cmd)
            self.assertNotIn("--fluid25d-rainfall-rate-mm-per-hour", cmd)
            self.assertNotIn("--fluid25d-solver", cmd)

    def test_advancing_is_explicit(self):
        cmd = review.command(review.review.SCENES[0], Path("clip.mp4"), Path("tuning.json"),
                             frames=360, advancing=True)
        self.assertEqual(cmd[cmd.index("--fluid25d-recording-frame-interval-seconds") + 1], "10")


if __name__ == "__main__":
    unittest.main()
