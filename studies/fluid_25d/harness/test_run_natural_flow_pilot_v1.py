#!/usr/bin/env python3
"""Runner-only controls; these do not claim GPU or terrain validation."""
import csv
import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import run_natural_flow_pilot_v1 as pilot


def healthy_profile():
    frames = {}
    for f in range(pilot.FRAMES):
        t = (f + 1) * pilot.DT
        source = 30 * t
        out = 30 * max(0, t - 1000)
        values = {
            ("fluid_25d.solver", "finite_volume_status_flags"): 0.0,
            ("fluid_25d.water", "conservation_residual_m3"): 0.0,
            ("fluid_25d.water", "cumulative_source_volume_m3"): source,
            ("fluid_25d.water", "cumulative_sink_volume_m3"): 0.0,
            ("fluid_25d.water", "cumulative_boundary_outflow_volume_m3"): out,
            ("fluid_25d.water", "total_water_volume_m3"): source - out,
            ("fluid_25d.water", "maximum_depth_m"): 0.5,
            ("fluid_25d.water", "slow_pooled_wet_fraction"): 0.05,
            (pilot.BOUNDARY, pilot.EXPECTED): out,
            (pilot.BOUNDARY, pilot.OTHER): 0.0,
            (pilot.BOUNDARY, pilot.CORNER): 0.0,
            (pilot.BOUNDARY, pilot.NONEDGE): 0.0,
        }
        for name, arrival in zip(pilot.GAUGES, (2, 800, 1500)):
            values[(f"fluid_25d.natural_flow.gauge.{name}", "center_depth_m")] = 0.5 if t >= arrival else 0.0
        frames[f] = values
    return frames


class NaturalFlowRunnerControls(unittest.TestCase):
    def test_failed_strict_oracle_raises_after_saving_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            report = {"app_sha256": "pinned", "cases": [{
                "site": "a", "q_m3_per_s": 30, "recipe_sha256": "pinned",
                "metrics": {"sustained_expected_route_checks_passed": True,
                            "first_measured_600s_sustained_window_end_seconds": 2100},
            }]}
            (out / "hydraulics.json").write_text(json.dumps(report))
            executions = [{"exit_code": code} for code in (0, 0, 0, 0, 0, 1)]
            with patch.object(pilot, "sha", return_value="pinned"), \
                 patch.object(pilot, "run", side_effect=executions), \
                 patch.object(pilot, "label_video", return_value={}), \
                 patch.object(pilot, "arguments", return_value=[]):
                with self.assertRaisesRegex(RuntimeError, "strict oracle failed"):
                    pilot.evidence(Path("mock-app"), out, "a:30")
            retained = json.loads((out / "evidence.json").read_text())
            self.assertEqual(retained["strict_oracle_checks"][-1]["label"], "dye-long-oracle")
            self.assertEqual(retained["strict_oracle_checks"][-1]["execution"]["exit_code"], 1)

    def test_changed_recipe_cannot_reuse_hydraulic_evidence(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary)
            (out / "hydraulics.json").write_text(json.dumps({
                "app_sha256": "pinned", "cases": [{
                    "site": "a", "q_m3_per_s": 30, "recipe_sha256": "old",
                }],
            }))
            with patch.object(pilot, "sha", return_value="pinned"):
                with self.assertRaisesRegex(RuntimeError, "recipe differs"):
                    pilot.evidence(Path("mock-app"), out, "a:30")

    def test_malformed_profile_is_saved_before_hydraulic_failure(self):
        with tempfile.TemporaryDirectory() as temporary:
            out = Path(temporary) / "new-evidence"
            with patch.object(pilot, "sha", return_value="pinned"), \
                 patch.object(pilot, "run", return_value={"exit_code": 0}), \
                 patch.object(pilot, "read_metrics", side_effect=ValueError("bad profile")), \
                 patch.object(pilot, "arguments", return_value=[]), \
                 patch("builtins.print"):
                with self.assertRaisesRegex(RuntimeError, "hydraulic validation failed"):
                    pilot.hydraulics(Path("mock-app"), out)
            retained = json.loads((out / "hydraulics.json").read_text())
            self.assertEqual(retained["cases"][0]["error"], "bad profile")

    def test_zero_based_profile_times_and_sustained_window(self):
        result = pilot.summarize(healthy_profile(), 30)
        self.assertTrue(result["numeric_health_checks_passed"])
        self.assertTrue(result["sustained_expected_route_checks_passed"])
        self.assertEqual(result["physical_horizon_seconds"], 7200)
        self.assertEqual(result["gauge_front_arrival_seconds"], {"upstream": 2, "midstream": 800, "downstream": 1500})
        self.assertEqual(result["first_measured_600s_sustained_window_end_seconds"], 2100)
        self.assertEqual(result["late_expected_outflow_m3_per_s"], 30)

    def test_flags_and_false_boundary_attribution_reject_health(self):
        frames = healthy_profile()
        frames[5][("fluid_25d.solver", "finite_volume_status_flags")] = 1
        self.assertFalse(pilot.summarize(frames, 30)["numeric_health_checks_passed"])
        frames = healthy_profile()
        frames[1000][(pilot.BOUNDARY, pilot.OTHER)] = 1
        self.assertFalse(pilot.summarize(frames, 30)["numeric_health_checks_passed"])

    def test_pooling_or_wrong_exit_is_not_sustained_expected_flow(self):
        frames = healthy_profile()
        for values in frames.values():
            values[(pilot.BOUNDARY, pilot.OTHER)] = values[(pilot.BOUNDARY, pilot.EXPECTED)]
            values[(pilot.BOUNDARY, pilot.EXPECTED)] = 0
        result = pilot.summarize(frames, 30)
        self.assertTrue(result["numeric_health_checks_passed"])
        self.assertFalse(result["sustained_expected_route_checks_passed"])
        self.assertIsNone(result["first_measured_600s_sustained_window_end_seconds"])

    def test_a_past_arrival_does_not_prove_continuous_flow(self):
        frames = healthy_profile()
        frames[pilot.FRAMES - 10][("fluid_25d.natural_flow.gauge.downstream", "center_depth_m")] = 0
        result = pilot.summarize(frames, 30)
        self.assertIsNotNone(result["gauge_front_arrival_seconds"]["downstream"])
        self.assertFalse(result["sustained_expected_route_checks_passed"])

    def test_read_metrics_rejects_nonfinite_and_duplicate_values(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "test.csv"
            for rows in (([0, "category", "name", "nan"],), ([0, "category", "name", "0"], [0, "category", "name", "1"])):
                with path.open("w") as stream:
                    writer = csv.writer(stream)
                    writer.writerow(("frame_index", "category", "name", "value"))
                    writer.writerows(rows)
                with self.assertRaises(ValueError):
                    pilot.read_metrics(path)

    def test_missing_internal_profile_frame_is_rejected(self):
        frames = healthy_profile()
        del frames[1000]
        with self.assertRaises(ValueError):
            pilot.summarize(frames, 30)

    def test_commands_use_identical_physics_and_distinct_pinned_crops(self):
        for site in pilot.SITES:
            args = pilot.arguments(site, 30)
            options = dict(zip(args[1::2], args[2::2]))
            self.assertEqual(options["--fluid25d-fixed-delta-seconds"], "2")
            self.assertEqual(options["--fluid25d-substeps"], "8")
            self.assertEqual(options["--fluid25d-natural-flow-source-m3-per-s"], "30")
            self.assertEqual(options["--grid-width"], "48")
            self.assertIn("--fluid25d-terrain-crop-x", options)
            self.assertIn("--fluid25d-terrain-crop-z", options)
            self.assertNotIn("--terrain-crop-x", options)
            self.assertNotIn("--fluid25d-source-duration-seconds", args)


if __name__ == "__main__":
    unittest.main()
