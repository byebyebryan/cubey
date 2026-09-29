"""Pure argument controls for the continuous hillside GUI launcher."""
import unittest

import run_hillside_flow_demo as demo
import run_hillside_sustained_flow_v2 as study


class ContinuousDemoTests(unittest.TestCase):
    def test_continuous_frozen_water_recipe(self):
        args = demo.arguments()
        frozen = study.arguments(256)
        frozen.remove("--headless")
        self.assertEqual(args[:len(frozen)], frozen)
        for forbidden in ("--headless", "--frames", "--fluid25d-dye-pulse-start-seconds",
                          "--fluid25d-hillside-inspection-advance-seconds"):
            self.assertNotIn(forbidden, args)
        self.assertIn("--fluid25d-hillside-source-context", args)
        self.assertEqual(args[args.index("--fluid25d-presentation-time-scale") + 1], "8")

    def test_overview_and_reading_aid_only_change_presentation(self):
        args = demo.arguments("overview", "flow-inspection", 4)
        self.assertNotIn("--fluid25d-hillside-source-context", args)
        self.assertEqual(args[args.index("--fluid25d-catchment-view") + 1], "flow-inspection")
        self.assertEqual(args[args.index("--fluid25d-presentation-time-scale") + 1], "4")

    def test_reject_unvalidated_modes_and_pacing(self):
        for camera, view, playback in (("other", "composite", 8),
                                      ("source", "transport-inspection", 8),
                                      ("source", "composite", 0),
                                      ("source", "composite", 9),
                                      ("source", "composite", float("nan"))):
            with self.assertRaises(ValueError):
                demo.arguments(camera, view, playback)


if __name__ == "__main__":
    unittest.main()
