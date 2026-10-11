import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import review_terrain_presentation_v1 as review


class TerrainPresentationReviewTests(unittest.TestCase):
    def test_headless_environment_has_no_display_fallback(self):
        with patch.dict("os.environ", {"DISPLAY": ":99", "WAYLAND_DISPLAY": "wayland-0",
                                      "CUBEY_PRIVATE_XVFB": "1"}):
            self.assertTrue(all(k not in review.environment() for k in
                ("DISPLAY", "WAYLAND_DISPLAY", "CUBEY_PRIVATE_XVFB")))

    def build(self, **kwargs):
        with patch.object(review, "scene", return_value=(Path("/case"), Path("/recording"),
                {"frames": [{"time_s": 0}, {"time_s": 6000}]})):
            return review.command("joining-valleys", "slope", "base-normal", Path("/image.png"), **kwargs)

    def test_explicit_site_target_not_mountain_preset(self):
        argv = self.build()
        self.assertEqual(argv[argv.index("--fluid25d-native-camera-target-x") + 1], "0.181")
        self.assertEqual(argv[argv.index("--fluid25d-native-camera-span-m") + 1], "900")
        self.assertIn("--headless", argv)
        self.assertIn("--fluid25d-recording-gpu-validation", argv)
        self.assertNotIn("--fluid25d-motion-markers", argv)
        self.assertNotIn("--fluid25d-rain-visuals", argv)
        self.assertNotIn("--fluid25d-native-bank-view", argv)
        self.assertEqual(argv[argv.index("--fluid25d-native-water-sampling") + 1], "bspline")

    def test_density_and_profile_are_display_only(self):
        argv = self.build(subdivision=4, profile=Path("/profile"), seconds=5)
        self.assertIn("--headless-render-only", argv)
        self.assertNotIn("--output", argv)
        self.assertNotIn("--capture", argv)
        self.assertNotIn("--fluid25d-grid", argv)
        self.assertEqual(argv[argv.index("--fluid25d-native-surface-subdivision") + 1], "4")
        self.assertEqual(argv[argv.index("--render-fps") + 1], "60")

    def test_motion_holds_native_state(self):
        argv = self.build(video=True, sweep=.2)
        self.assertEqual(argv[argv.index("--fluid25d-recording-frame-interval-seconds") + 1], "0")
        self.assertEqual(argv[argv.index("--fluid25d-native-camera-sweep") + 1], "0.2")

    def test_raw_diagnostics_bypass_material_shading(self):
        with patch.object(review, "scene", return_value=(Path("/case"), Path("/recording"),
                {"frames": [{"time_s": 0}, {"time_s": 6000}]})):
            argv = review.command("mountain", "overview", "raw-depth", Path("/image.png"),
                                   surface="correlated", shadow="scene")
        self.assertNotIn("--fluid25d-scenic-terrain-view", argv)
        self.assertEqual(argv[argv.index("--fluid25d-view") + 1], "diagnostics")
        self.assertEqual(argv[argv.index("--fluid25d-debug-view") + 1], "depth")
        self.assertEqual(argv.count("--fluid25d-native-shadow-projection"), 1)

    def test_failed_capture_always_cleans_owned_group(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(review.subprocess, "Popen") as launch, \
                patch.object(review.demo, "stop_owned") as cleanup:
            launch.return_value.wait.side_effect = TimeoutError("fixture")
            with self.assertRaises(TimeoutError):
                review.run_capture(["rtk", "proxy", "fixture"], Path(tmp)/"log")
            cleanup.assert_called_once_with(launch.return_value, grace_s=5)
            self.assertTrue(launch.call_args.kwargs["start_new_session"])


if __name__ == "__main__":
    unittest.main()
