import unittest
import json
from pathlib import Path
import tempfile
from unittest.mock import patch
import review_water_landscape_v1 as review
import review_landscape_depth_v1 as depth
import review_showcase_solar_v1 as solar


class WaterLandscapeAuditionTests(unittest.TestCase):
    def test_solar_recipe_starts_front_on_and_preserves_depth_and_fields(self):
        args = solar.command(Path("candidate.mp4"), 45)
        values = {flag: args[args.index(flag) + 1] for flag in (
            "--time-hours", "--time-speed-hours-per-second", "--capture-camera-yaw",
            "--capture-video-orbit-degrees", "--terrain-landscape-depth", "--terrain-landscape",
            "--frames", "--fps", "--width", "--height")}
        self.assertEqual(values, {"--time-hours": "14.0", "--time-speed-hours-per-second": "0.875",
            "--capture-camera-yaw": "0.0", "--capture-video-orbit-degrees": "45",
            "--terrain-landscape-depth": "fitted", "--terrain-landscape": "mountain-local",
            "--frames": "480", "--fps": "60", "--width": "1920", "--height": "1080"})
        for flag in ("--headless", "--require-validation", "--terrain-water-audit", "--no-clouds"):
            self.assertIn(flag, args)

    def test_solar_caption_matches_discrete_eased_endpoints(self):
        start, end = solar.sample(0, 45), solar.sample(479, 45)
        self.assertEqual(start["yaw_degrees"], 0)
        self.assertAlmostEqual(start["solar_hours"], 14.0145833333)
        self.assertEqual(end["yaw_degrees"], 45)
        self.assertAlmostEqual(end["solar_hours"], 21)
        self.assertLess(solar.sample(120, 45)["yaw_degrees"], 45 / 4)
        with self.assertRaises(ValueError):
            solar.sample(480, 45)

    def test_refined_solar_reuses_auto_exposure_and_ends_at_night(self):
        args = solar.command(Path("candidate.mp4"), 45, refined=True)
        self.assertNotIn("--exposure", args)
        self.assertEqual(args[args.index("--star-intensity") + 1], "0")
        self.assertEqual(args[args.index("--time-speed-hours-per-second") + 1], "0.6875")
        self.assertEqual(solar.sample(479, 45, solar.REFINED_HOURS_PER_SECOND)["solar_hours"], 19.5)

    def test_solar_freeze_refuses_overwrite_and_runtime_drift(self):
        with tempfile.TemporaryDirectory() as temporary, \
             patch.object(solar.base, "runtime", return_value={"binary": "frozen"}), \
             patch.object(solar.base, "identities", return_value={}), \
             patch.object(solar.base, "digest", return_value="recipe-hash"), \
             patch.object(solar.subprocess, "check_output", side_effect=[b"", "source-head"]):
            out = Path(temporary) / "fresh"
            solar.freeze(out)
            solar.verify(out)
            with self.assertRaises(FileExistsError):
                solar.freeze(out)
            with patch.object(solar.base, "runtime", return_value={"binary": "changed"}):
                with self.assertRaisesRegex(ValueError, "runtime or inputs changed"):
                    solar.verify(out)

    def test_bounded_matrix(self):
        self.assertEqual(len(review.OUT_IDS) * len(review.HEADINGS) * len(review.PITCHES), 24)
        self.assertEqual(len(set(review.OUT_IDS)), 4)

    def test_low_view_is_not_overview(self):
        args = review.view_command("mountain-local", -1.16, -.12,
            {"focus_xz": [11520, 19200], "focus_height_m": 3503.86})
        self.assertEqual(args[args.index("--terrain-water-span") + 1], "16")
        self.assertEqual(args[args.index("--terrain-water-focus-height") + 1], "3503.86")
        self.assertNotIn("--capture", args)
        self.assertNotIn("--no-terrain-water-visible", args)
        self.assertIn("--headless", args)

    def test_hidden_control_changes_no_fields(self):
        info = {"focus_xz": [0, 0], "focus_height_m": 200}
        on = review.view_command("test", .25, -.26, info)
        off = review.view_command("test", .25, -.26, info, water=False)
        self.assertEqual(off, on + ["--no-terrain-water-visible"])

    def test_actual_camera_and_static_hydraulics(self):
        args = review.integrated_command("mountain-local")
        self.assertIn("--no-clouds", args)
        self.assertEqual(args[args.index("--time-speed-hours-per-second") + 1], "0")
        self.assertEqual(args[args.index("--capture-camera-distance-scale") + 1], ".70")
        off = review.integrated_command("mountain-local", water=False)
        self.assertEqual(off, args + ["--no-terrain-water-visible"])

    def test_depth_controls_only_change_the_named_render_setting(self):
        self.assertEqual(len(depth.CASES), 5)
        self.assertEqual(depth.CASES["fitted"], [])
        self.assertEqual(depth.CASES["split-8m"], ["--terrain-landscape-near", "8"])
        self.assertEqual(depth.CASES["legacy-no-shadow"], depth.CASES["legacy"] +
                         ["--terrain-landscape-shadow-strength", "0"])

    def test_depth_freeze_is_immutable_and_rejects_changed_inputs(self):
        with tempfile.TemporaryDirectory() as temporary, \
             patch.object(depth.base, "runtime", return_value={"binary": "frozen"}), \
             patch.object(depth.base, "identities", return_value={}), \
             patch.object(depth.subprocess, "check_output", return_value=""):
            out = Path(temporary) / "fresh"
            depth.freeze(out)
            depth.verify(out)
            with self.assertRaises(FileExistsError):
                depth.freeze(out)
            protocol = json.loads((out / "protocol.json").read_text())
            protocol["inputs"] = {"tampered": "input"}
            (out / "protocol.json").write_text(json.dumps(protocol))
            with self.assertRaisesRegex(ValueError, "inputs changed"):
                depth.verify(out)


if __name__ == "__main__":
    unittest.main()
