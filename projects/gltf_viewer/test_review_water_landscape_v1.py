import unittest
import review_water_landscape_v1 as review


class WaterLandscapeAuditionTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
