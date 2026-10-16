"""CPU-only recipes for the shared environment evidence matrix."""
import unittest
from unittest.mock import patch

import review_environment_v1 as review


class Recipes(unittest.TestCase):
    def args(self, **kwargs):
        with patch.object(review.patch, "command", return_value=["renderer"]):
            return review.command("collection", **kwargs)

    def test_fixed_reference_never_enables_weather(self):
        self.assertEqual(self.args(treatment="reference"), ["renderer"])

    def test_shared_daylight_preserves_manual_reference_direction(self):
        args=self.args()
        self.assertIn("shared",args)
        self.assertNotIn("--time-hours",args)
        self.assertNotIn("--clouds",args)

    def test_solar_presets_are_held_and_reproducible(self):
        for name, hour in (("noon",12),("sunset",18),("night",0)):
            args=self.args(time=name)
            self.assertEqual(args[args.index("--time-hours")+1],str(hour))
            self.assertIn("--pause-time",args)
            self.assertIn("--latitude-degrees",args)

    def test_independent_cloud_ablations(self):
        background=self.args(treatment="background")
        reflection=self.args(treatment="reflection")
        shadow=self.args(treatment="shadow")
        self.assertIn("--no-fluid25d-cloud-reflections",background)
        self.assertNotIn("--no-fluid25d-cloud-background",background)
        self.assertIn("--no-fluid25d-cloud-background",reflection)
        self.assertNotIn("--no-fluid25d-cloud-reflections",reflection)
        self.assertIn("--no-fluid25d-cloud-background",shadow)
        self.assertIn("--no-fluid25d-cloud-reflections",shadow)
        for args in (background,reflection,shadow):
            self.assertIn("--clouds",args)
            self.assertNotIn("--fluid25d-rain-rate",args)

    def test_quality_changes_only_existing_cloud_controls(self):
        args=self.args(treatment="weather",quality="half")
        self.assertEqual(args[args.index("--cloud-quality")+1],"half")
        self.assertEqual(args[args.index("--cloud-weather-preset")+1],"broken-cumulus")


if __name__ == "__main__":
    unittest.main()
