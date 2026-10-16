import copy
import json
import tempfile
import unittest
from unittest import mock
from pathlib import Path

import run_hillside_flow_pilot_v1 as pilot


class PilotTests(unittest.TestCase):
    def test_changed_input_preserves_report_and_fails_phase(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            recipe = root / "recipe.json"
            recipe.write_text(json.dumps({"schema": "cubey.fluid25d.hillside_flow_study.v1",
                                          "crop_xzwh": [10, 20, 256, 256]}))
            args = ["pilot", "--app", str(root / "app"), "--recipe", str(recipe),
                    "--manifest", "terrain", "--palette-low", "100", "--palette-high", "500",
                    "--out", str(root / "out")]
            with mock.patch("sys.argv", args), \
                 mock.patch.object(pilot, "sha", side_effect=["app", "recipe", "runner", "changed-app"]), \
                 mock.patch.object(pilot, "run", return_value={"exit_code": 0}), \
                 mock.patch.object(pilot, "read_metrics", return_value={}), \
                 mock.patch.object(pilot, "summarize", return_value={
                     "numeric_profile_checks_passed": True, "downhill_progress_observed": True}), \
                 mock.patch.object(pilot, "gpu_timings", return_value={}), \
                 mock.patch("builtins.print"):
                with self.assertRaises(SystemExit) as failure:
                    pilot.main()
            self.assertEqual(failure.exception.code, 1)
            report = json.loads((root / "out/hydraulics.json").read_text())
            self.assertIn("changed during", report["error"])
            self.assertFalse(report["phase_checks_passed"])

    def test_failed_child_preserves_report_and_exits_nonzero(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            recipe = root / "recipe.json"
            recipe.write_text(json.dumps({"schema": "cubey.fluid25d.hillside_flow_study.v1",
                                          "crop_xzwh": [10, 20, 256, 256]}))
            app = root / "fake-app"
            app.write_text("fixture identity only; child mocked")
            args = ["pilot", "--app", str(app), "--recipe", str(recipe), "--manifest", "terrain",
                    "--palette-low", "100", "--palette-high", "500", "--out", str(root / "out")]
            with mock.patch("sys.argv", args), mock.patch.object(pilot, "run", return_value={"exit_code": 7}), \
                 mock.patch("builtins.print"):
                with self.assertRaises(SystemExit) as failure:
                    pilot.main()
            self.assertEqual(failure.exception.code, 1)
            report = json.loads((root / "out/hydraulics.json").read_text())
            self.assertFalse(report["phase_checks_passed"])
            self.assertEqual(report["cases"][0]["exit_code"], 7)

    def test_report_numeric_and_oracle_failures_gate_success(self):
        report = {"phase": "hydraulics", "cases": [{"exit_code": 0}],
                  "summary": {"numeric_profile_checks_passed": False, "downhill_progress_observed": True}}
        self.assertFalse(pilot.report_checks_passed(report))
        report = {"phase": "evidence", "cases": [{"exit_code": 0}] * 3,
                  "strict_startup_oracle": {"exit_code": 1}}
        self.assertFalse(pilot.report_checks_passed(report))
        report["strict_startup_oracle"]["exit_code"] = 0
        self.assertTrue(pilot.report_checks_passed(report))
        report["cases"][0]["error"] = "label failure"
        self.assertFalse(pilot.report_checks_passed(report))

    def test_profile_parse_error_is_reported_before_failure_exit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            recipe = root / "recipe.json"
            recipe.write_text(json.dumps({"schema": "cubey.fluid25d.hillside_flow_study.v1",
                                          "crop_xzwh": [10, 20, 256, 256]}))
            app = root / "fake-app"
            app.write_text("fixture identity only; child mocked")
            args = ["pilot", "--app", str(app), "--recipe", str(recipe), "--manifest", "terrain",
                    "--palette-low", "100", "--palette-high", "500", "--out", str(root / "out")]
            with mock.patch("sys.argv", args), mock.patch.object(pilot, "run", return_value={"exit_code": 0}), \
                 mock.patch.object(pilot, "read_metrics", side_effect=ValueError("bad metrics")), \
                 mock.patch("builtins.print"):
                with self.assertRaises(SystemExit) as failure:
                    pilot.main()
            self.assertEqual(failure.exception.code, 1)
            report = json.loads((root / "out/hydraulics.json").read_text())
            self.assertIn("bad metrics", report["error"])
            self.assertFalse(report["phase_checks_passed"])
    def frames(self):
        data = {}
        for frame in range(2):
            values = {
                ("fluid_25d.solver", "finite_volume_status_flags"): 0,
                ("fluid_25d.water", "conservation_residual_m3"): 0,
                ("fluid_25d.water", "cumulative_source_volume_m3"): 200 * (frame + 1),
                ("fluid_25d.water", "total_water_volume_m3"): 200 * (frame + 1),
                ("fluid_25d.water", "maximum_depth_m"): 1,
                ("fluid_25d.water", "cumulative_sink_volume_m3"): 0,
                ("fluid_25d.water", "cumulative_boundary_outflow_volume_m3"): 0,
            }
            for name in ("maximum_wetted_bed_drop_m", "farthest_materially_wet_distance_m",
                         "source_region_water_volume_m3", "materially_wet_cell_count"):
                values[(pilot.PROGRESS, name)] = 100
            for drop in (20, 50, 100, 200):
                values[(pilot.PROGRESS, f"below_source_wet_cells_{drop}m")] = int(drop <= 100 and frame == 1)
                values[(pilot.PROGRESS, f"below_source_water_volume_{drop}m")] = 90 if drop <= 100 else 0
            data[frame] = values
        return data

    def test_descent_without_outflow_passes(self):
        report = pilot.summarize(self.frames(), 4)
        self.assertTrue(report["downhill_progress_observed"])
        self.assertEqual(report["band_first_material_water_seconds"]["100"], 4)
        self.assertIsNone(report["band_first_material_water_seconds"]["200"])
        self.assertEqual(report["boundary_export_m3"], 0)

    def test_unhealthy_numeric_state_cannot_pass_descent(self):
        for key, value in ((("fluid_25d.solver", "finite_volume_status_flags"), 1),
                           (("fluid_25d.water", "maximum_depth_m"), -1),
                           (("fluid_25d.water", "cumulative_sink_volume_m3"), 1),
                           (("fluid_25d.water", "conservation_residual_m3"), 1)):
            frames = copy.deepcopy(self.frames())
            frames[1][key] = value
            self.assertFalse(pilot.summarize(frames, 4)["downhill_progress_observed"])

    def test_missing_frame_rejected(self):
        with self.assertRaises(ValueError):
            pilot.summarize({1: self.frames()[1]}, 4)

    def test_source_only_arguments_and_fixed_time(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "recipe.json"
            recipe = {"schema": "cubey.fluid25d.hillside_flow_study.v1", "crop_xzwh": [10, 20, 256, 256]}
            path.write_text(json.dumps(recipe))
            args = pilot.arguments(path, Path("terrain"), (100, 500))
            self.assertEqual(args[args.index("--fluid25d-substeps") + 1], "16")
            self.assertEqual(args[args.index("--fluid25d-fixed-delta-seconds") + 1], "2")
            self.assertNotIn("--fluid25d-hillside-inspection-advance-seconds", args)
            recipe["expected_outlet"] = None
            path.write_text(json.dumps(recipe))
            with self.assertRaises(ValueError):
                pilot.arguments(path, Path("terrain"), (100, 500))


if __name__ == "__main__":
    unittest.main()
