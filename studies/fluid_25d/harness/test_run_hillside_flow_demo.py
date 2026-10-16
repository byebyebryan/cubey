"""Pure argument controls for the continuous hillside GUI launcher."""
import io
import shlex
import sys
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

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

    def test_wider_conserved_pulse_is_opt_in_and_continuous(self):
        args = demo.arguments("overview", "transport-inspection", 8, 512, True)
        frozen = study.arguments(512)
        frozen.remove("--headless")
        self.assertEqual(args[:len(frozen)], frozen)
        self.assertEqual(args[args.index("--fluid25d-dye-pulse-start-seconds") + 1], "3600")
        self.assertEqual(args[args.index("--fluid25d-dye-pulse-duration-seconds") + 1], "120")
        for forbidden in ("--frames", "--headless", "--fluid25d-source-active-duration-seconds",
                          "--fluid25d-hillside-inspection-advance-seconds"):
            self.assertNotIn(forbidden, args)
        with self.assertRaises(ValueError):
            demo.arguments(domain=1024)

    def test_branch_markers_and_developed_advance_are_render_and_startup_options(self):
        args = demo.arguments("branch", "composite", 8, 512, markers=True, developed=True)
        frozen = study.arguments(512)
        frozen.remove("--headless")
        self.assertEqual(args[:len(frozen)], frozen)
        self.assertEqual(args[args.index("--fluid25d-hillside-camera") + 1], "branch")
        self.assertIn("--fluid25d-motion-markers", args)
        self.assertEqual(
            args[args.index("--fluid25d-hillside-advance-and-continue-seconds") + 1], "3300")
        for forbidden in ("--headless", "--frames", "--fluid25d-hillside-inspection-advance-seconds"):
            self.assertNotIn(forbidden, args)

    def test_marker_source_selects_new_close_camera_but_water_default_stays_legacy(self):
        water_args = demo.arguments("source", "composite", 8, 256, markers=False)
        marker_args = demo.arguments("source", "flow-inspection", 8, 256, markers=True)

        self.assertIn("--fluid25d-hillside-source-context", water_args)
        self.assertNotIn("--fluid25d-hillside-camera", water_args)
        self.assertNotIn("--fluid25d-hillside-source-context", marker_args)
        self.assertEqual(marker_args[marker_args.index("--fluid25d-hillside-camera") + 1], "source")
        self.assertIn("--fluid25d-motion-markers", marker_args)

    def test_print_command_does_not_launch_or_change_state(self):
        output = io.StringIO()
        with patch.object(sys, "argv", ["run_hillside_flow_demo.py", "--print-command",
                                        "--markers", "--developed"]), \
                patch.object(demo.study, "pinned_inputs") as pinned, \
                patch.object(demo.subprocess, "run") as launched, \
                redirect_stdout(output):
            demo.main()

        pinned.assert_called_once()
        launched.assert_not_called()
        command = shlex.split(output.getvalue())
        self.assertIn("--fluid25d-motion-markers", command)
        self.assertIn("--fluid25d-hillside-advance-and-continue-seconds", command)
        self.assertNotIn("--fluid25d-hillside-source-context", command)
        self.assertEqual(command[command.index("--fluid25d-hillside-camera") + 1], "source")

    def test_v5_is_explicit_continuous_and_keeps_frozen_inputs(self):
        for camera in ("travel", "collection"):
            args = demo.arguments(camera=camera, domain=512, markers=True,
                                  local_markers=True, depth_cues=True, response=True)
            frozen = study.arguments(512)
            frozen.remove("--headless")
            self.assertEqual(args[:len(frozen)], frozen)
            self.assertEqual(args[args.index("--fluid25d-motion-marker-mode") + 1], "local")
            self.assertIn("--fluid25d-hillside-depth-cues", args)
            self.assertIn("--fluid25d-hillside-supply-response", args)
            self.assertNotIn("--frames", args)
            self.assertNotIn("--fluid25d-dye-pulse-start-seconds", args)
        with self.assertRaises(ValueError):
            demo.arguments(local_markers=True)
        for camera in ("travel", "collection"):
            with self.assertRaisesRegex(ValueError, "require domain 512"):
                demo.arguments(camera=camera, domain=256)


if __name__ == "__main__":
    unittest.main()
