import argparse
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run_offscreen_perf_v2 as study
import run_offscreen_perf_campaign_v2 as campaign
import review_offscreen_perf_v2 as review


class ContractTests(unittest.TestCase):
    def test_offscreen_has_no_display_and_native_has_no_x11_fallback(self):
        with patch.dict(os.environ, {"DISPLAY": ":88", "WAYLAND_DISPLAY": "wayland-1",
                                    "CUBEY_PRIVATE_XVFB": "1"}, clear=True):
            env = study.environment("offscreen")
            self.assertNotIn("DISPLAY", env)
            self.assertNotIn("WAYLAND_DISPLAY", env)
            self.assertNotIn("CUBEY_PRIVATE_XVFB", env)
            native = study.environment("wayland")
            self.assertNotIn("DISPLAY", native)
            self.assertEqual(native["XDG_SESSION_TYPE"], "wayland")
            self.assertEqual(native["WAYLAND_DISPLAY"], "wayland-1")
        with patch.dict(os.environ, {}, clear=True):
            with self.assertRaisesRegex(ValueError, "unavailable"):
                study.environment("wayland")

    def test_render_only_reuses_existing_recipe_and_has_no_encoder(self):
        args = argparse.Namespace(width=1920, height=1080, wall=30, warmup=5,
            render_fps=0, app=Path("release-app"), prefix=Path("profile"),
            camera="overview", host="offscreen")
        argv = study.viewer_command(Path("case"), Path("recording.json"), "owned", args, False, 123)
        self.assertIn("--headless-render-only", argv)
        self.assertIn("--headless", argv)
        self.assertNotIn("--capture", argv)
        self.assertIn("bspline-2x", argv)
        self.assertIn("--run-seconds", argv)
        self.assertIn("--no-validation", argv)
        self.assertEqual(argv[argv.index("--render-fps")+1], "0")
        args.host = "wayland"
        native = study.viewer_command(Path("case"), Path("session"), "owned", args, True, 0)
        self.assertNotIn("--headless", native)
        self.assertIn("--fluid25d-external-session", native)
        self.assertNotIn("--fluid25d-recording", native)

    def test_campaign_is_sequential_and_has_42_primary_and_56_total_trials(self):
        rows = campaign.plan(Path("frozen-study"))
        primary = total = 0
        for name, case, host, options in rows:
            repeats = int(options[options.index("--repeats") + 1])
            if "--modes" in options:
                index = options.index("--modes") + 1
                modes = []
                while index < len(options) and not options[index].startswith("--"):
                    modes.append(options[index])
                    index += 1
            else:
                modes = ["live"]
            count = repeats * len(modes)
            total += count
            if name.startswith("primary-"):
                primary += count
            self.assertIn(host, ("offscreen", "wayland"))
            if host == "wayland":
                self.assertNotIn("worker-only", modes)
            if name.startswith("held-"):
                self.assertEqual(host, "offscreen")
        self.assertEqual((primary, total), (42, 56))

    def test_summary_separates_pacing_service_and_gpu(self):
        row = {"host": "offscreen", "profile": {"frame_ms": {"p95": 16.8},
               "passes_ms": {"cpu:headless.frame_service": {"p95": 3.2},
                             review.GPU: {"p95": 2.9},
                             "cpu:headless.intentional_pacing": {"p95": 13.5}}}}
        values = review.measure(row)
        self.assertEqual(values["loop_p95_ms"], 16.8)
        self.assertEqual(values["service_p95_ms"], 3.2)
        self.assertEqual(values["gpu_p95_ms"], 2.9)
        self.assertEqual(values["intentional_pacing_p95_ms"], 13.5)
        self.assertIsNone(values["pace_x"])
        self.assertEqual(review.spread([3, 4, 5]), {"n": 3, "median": 4, "min": 3, "max": 5})

    def test_report_checks_do_not_turn_x11_into_native_evidence(self):
        row = {"host": "wayland", "mode": "renderer-only", "status": "completed",
               "input_identity": {"sha": "unchanged"}, "input_identity_after": {"sha": "unchanged"},
               "cleanup": [{"exit_code": 0, "active_group_remaining": [], "escalated": False}]}
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "viewer.log").write_text("cubey_window_backend: x11\nNVIDIA GeForce RTX 5070 Ti\n")
            checks = review.checks(row, root)
            self.assertFalse(checks["actual_expected_backend"])
            self.assertTrue(checks["actual_expected_device"])
            self.assertTrue(checks["owned_cleanup"])
            self.assertIsNone(checks["native_inputs_unchanged"])


if __name__ == "__main__":
    unittest.main()
