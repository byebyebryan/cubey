import copy
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import review_terrain_presentation_v1 as review
import run_terrain_presentation_perf_v1 as perf
import report_terrain_presentation_v1 as report


class TerrainPresentationReviewTests(unittest.TestCase):
    def test_paired_plan_is_balanced_headless_and_bounded(self):
        rows = perf.plan()
        self.assertEqual(len(rows), 16)
        self.assertEqual({r["mode"] for r in rows}, {"renderer-only", "live"})
        self.assertEqual(len({r["name"] for r in rows}), 16)
        for site in ("mountain", "steep-ridges"):
            for repeat, first in ((0, "legacy"), (1, "candidate")):
                group = [r for r in rows if r["site"] == site and r["repeat"] == repeat]
                self.assertEqual(group[0]["variant"], first)
                self.assertEqual({r["variant"] for r in group}, {"legacy", "candidate"})

    def test_report_rejects_incomplete_or_duplicate_pairs(self):
        with self.assertRaises(ValueError):
            report.performance({"trials": []})
        rows = perf.plan()
        rows[-1] = rows[0]
        with self.assertRaises(ValueError):
            report.performance({"trials": rows})

    def test_report_parity_requires_matched_unique_views(self):
        row = dict(site="mountain", camera="slope", view="shaded", sha256="same")
        self.assertEqual(report.paired_hashes({"assets": [row]}, {"assets": [row]}),
                         dict(total=1, identical=1))
        with self.assertRaises(ValueError):
            report.paired_hashes({"assets": [row, row]}, {"assets": [row]})
        with self.assertRaises(ValueError):
            report.paired_hashes({"assets": [row]}, {"assets": [dict(row, view="albedo")]})

    def test_report_nonfinite_or_empty_metrics_are_not_a_pass(self):
        for value in (dict(samples=0, p50=1, p95=1, max=1),
                      dict(samples=1, p50=1, p95=float("nan"), max=1)):
            with self.assertRaises(ValueError):
                report.stat({"passes_ms": {report.GPU: value}}, "passes_ms", report.GPU)

    def test_report_incomplete_managed_actions_do_not_pass(self):
        event = dict(event="action-resume", hydraulic_dispatches=0,
                     solver_bed_sha256="bed", case_id="mountain")
        script = dict(actions=[dict(kind="resume")])
        with self.assertRaises(ValueError):
            report.lifecycle_checked(dict(events=[event]), script)
        self.assertEqual(report.lifecycle_checked(dict(events=[event, dict(event, event="action-applied")]),
                                                 script)["actions_applied"], 1)

    def test_report_keeps_per_trial_gate_failures_and_rejects_changed_inputs(self):
        timing = dict(samples=2, p50=2, p95=4, max=5)
        zero = dict(samples=2, p50=0, p95=0, max=0)
        age = dict(samples=2, p50=.1, p95=.15, max=.2)
        result = dict(status="completed", errors=[], host="offscreen", app_sha256="frozen",
            cleanup=[dict(active_group_remaining=[])], input_identity={"bed": "frozen"},
            input_identity_after={"bed": "frozen"}, physical_wall_ratio=60,
            completion=dict(inputs_unchanged=True, reference_inputs_unchanged=True, failure_message=""),
            profile=dict(viewport=[[1920, 1080]], frame_ms=timing,
                passes_ms={report.GPU: timing, report.CPU: timing}, metrics={report.AGE: age,
                    "fluid_25d.external.hydraulic_dispatch_count": zero,
                    "fluid_25d.recording.hydraulic_dispatch_count": zero}))
        value = dict(runtime=dict(app_sha256="frozen"), trials=[
            dict(task, result=copy.deepcopy(result)) for task in perf.plan()])
        self.assertTrue(all(r["gate_pass"] for r in report.performance(value)))
        candidate = next(r for r in value["trials"] if r["variant"] == "candidate" and r["mode"] == "live")
        candidate["result"]["profile"]["metrics"][report.AGE]["p95"] = .6
        self.assertEqual(sum(not r["gate_pass"] for r in report.performance(value)), 1)
        candidate["result"]["input_identity_after"]["bed"] = "changed"
        with self.assertRaises(ValueError):
            report.performance(value)

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
        self.assertAlmostEqual(float(argv[argv.index("--fluid25d-recording-frame-interval-seconds") + 1]), 1/24)
        self.assertEqual(float(argv[argv.index("--fluid25d-recording-time-seconds") + 1]), 6000)
        self.assertEqual(argv[argv.index("--fluid25d-native-camera-sweep") + 1], "0.2")

    def test_raw_diagnostics_bypass_material_shading(self):
        with patch.object(review, "scene", return_value=(Path("/case"), Path("/recording"),
                {"frames": [{"time_s": 0}, {"time_s": 6000}]})):
            argv = review.command("mountain", "overview", "raw-depth", Path("/image.png"),
                                   surface="correlated", shadow="scene")
        self.assertNotIn("--fluid25d-scenic-terrain-view", argv)
        self.assertEqual(argv[argv.index("--fluid25d-view") + 1], "diagnostics")
        self.assertEqual(argv[argv.index("--debug-view") + 1], "depth")
        self.assertEqual(argv.count("--fluid25d-native-shadow-projection"), 1)
        self.assertNotIn("--fluid25d-debug-view", argv)
        with patch.object(review, "scene", return_value=(Path("/case"), Path("/recording"),
                {"frames": [{"time_s": 0}, {"time_s": 6000}]})):
            speed = review.command("mountain", "overview", "raw-speed", Path("/image.png"))
        self.assertEqual(speed[speed.index("--debug-view") + 1], "flow")

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
