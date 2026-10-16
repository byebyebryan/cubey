from __future__ import annotations

import copy
import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_terrain_reach_neutral_v1 as runner


class TerrainReachNeutralRunnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.recipe = runner.load_frozen_recipe()

    def test_checked_in_recipe_source_scan_and_runtime_crop_bytes_preflight(self) -> None:
        checked = runner.preflight(runner.DEFAULT_APP)
        self.assertEqual(checked["scan"]["status"], "PASS_CANDIDATE_FOUND")
        self.assertEqual(
            [case["crop"]["x"] for case in checked["recipe"]["cases"]],
            [384, 843],
        )
        self.assertEqual(
            [
                checked["crop_statistics"][case["id"]]["transformed_crop_sha256"]
                for case in checked["recipe"]["cases"]
            ],
            [
                "1cabf75012716793b510dc6958c5cb05f0916356d86736fd02e59461f0016052",
                "6a9818526757333925536ba3abd0d3394981797dea4e83d7b2f182ea432d3485",
            ],
        )
        self.assertEqual(
            runner.sha256_file(runner.SCAN_PATH),
            "60bdd07c9a7fb6db6bb4c0de890fa2a49715284c5a5bdaf5e14061a449d09428",
        )

    def test_recipe_fails_closed_on_solver_or_crop_drift(self) -> None:
        changed_rain = copy.deepcopy(self.recipe)
        changed_rain["protocol"]["rainfall_mm_per_hour"] = 13
        with self.assertRaisesRegex(runner.NeutralReachError, "protocol or capture/profile schedule"):
            runner.validate_recipe_structure(changed_rain)

        changed_crop = copy.deepcopy(self.recipe)
        changed_crop["cases"][1]["crop"]["z"] += 1
        with self.assertRaisesRegex(runner.NeutralReachError, "crops/roles/bytes"):
            runner.validate_recipe_structure(changed_crop)

        changed_presentation = copy.deepcopy(self.recipe)
        changed_presentation["render_policy"]["height_scale"] = 0.08
        with self.assertRaisesRegex(runner.NeutralReachError, "render-only policy"):
            runner.validate_recipe_structure(changed_presentation)

    def test_app_commands_share_solver_and_presentation_contract_without_inlet_or_sink(self) -> None:
        commands = [
            runner.common_app_args(
                runner.DEFAULT_APP,
                case,
                runner.ROOT / "cache/terrain/sources/v1/landscape-variations/rolling-wet-lowland",
                self.recipe,
                600,
            )
            for case in self.recipe["cases"]
        ]
        shared_keys = (
            "--grid-width",
            "--grid-height",
            "--fluid25d-solver",
            "--fluid25d-fixed-delta-seconds",
            "--fluid25d-substeps",
            "--fluid25d-cell-size-m",
            "--fluid25d-scenario",
            "--fluid25d-terrain-water-protocol",
            "--fluid25d-rainfall-rate-mm-per-hour",
            "--fluid25d-source-active-duration-seconds",
        )
        selected = [
            {key: command[command.index(key) + 1] for key in shared_keys}
            for command in commands
        ]
        self.assertEqual(selected[0], selected[1])
        self.assertEqual(selected[0]["--grid-width"], "256")
        self.assertEqual(selected[0]["--grid-height"], "128")
        self.assertEqual(selected[0]["--fluid25d-fixed-delta-seconds"], "2")
        self.assertEqual(selected[0]["--fluid25d-substeps"], "8")
        self.assertEqual(selected[0]["--fluid25d-rainfall-rate-mm-per-hour"], "12")
        self.assertEqual(selected[0]["--fluid25d-source-active-duration-seconds"], "600")
        self.assertNotIn("--inlet", commands[0])
        self.assertNotIn("--sink", commands[0])
        self.assertNotIn("--fluid25d-source-x", commands[0])
        self.assertNotIn("--fluid25d-sink-x", commands[0])
        render = runner.render_args(self.recipe)
        self.assertEqual(render[render.index("--fluid25d-render-height-scale") + 1], "0.600000")
        self.assertEqual(render[render.index("--fluid25d-home-camera-distance-m") + 1], "8043.75")
        self.assertNotIn("--fluid25d-render-height-scale", commands[0])
        self.assertNotIn("--fluid25d-terrain-palette-low-m", commands[0])
        self.assertEqual(
            commands[0][commands[0].index("--fluid25d-terrain-crop-x") + 1],
            "384",
        )
        self.assertEqual(
            commands[1][commands[1].index("--fluid25d-terrain-crop-x") + 1],
            "843",
        )

    def test_render_overrides_are_attached_only_to_composite_view(self) -> None:
        case = self.recipe["cases"][0]
        manifest = runner.ROOT / "cache/terrain/sources/v1/landscape-variations/rolling-wet-lowland"
        composite = runner.build_view_command(
            runner.DEFAULT_APP, case, manifest, self.recipe, 600, "composite", Path("composite.png")
        )
        depth = runner.build_view_command(
            runner.DEFAULT_APP, case, manifest, self.recipe, 600, "water-depth", Path("depth.png")
        )
        flow = runner.build_view_command(
            runner.DEFAULT_APP, case, manifest, self.recipe, 600, "flow", Path("flow.png")
        )
        self.assertIn("--fluid25d-render-height-scale", composite)
        self.assertIn("--fluid25d-terrain-palette-low-m", composite)
        self.assertIn("--fluid25d-home-camera-distance-m", composite)
        for command in (depth, flow):
            self.assertNotIn("--fluid25d-render-height-scale", command)
            self.assertNotIn("--fluid25d-terrain-palette-low-m", command)
            self.assertNotIn("--fluid25d-home-camera-distance-m", command)

    def test_profile_rows_require_periodic_coverage_and_exact_f600(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "metrics.csv"
            fields = ["frame_index", "category", "name", "value"]
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields)
                writer.writeheader()
                for frame in (0, 15, 599):
                    writer.writerow({"frame_index": frame, "category": "fluid_25d.solver", "name": "finite_volume_status_flags", "value": "0.000000"})
                    values = {
                        "wet_cell_count": 32,
                        "wet_cell_ratio": 0.1,
                        "total_water_volume_m3": 1.0,
                        "maximum_depth_m": 0.02,
                        "wet_mean_depth_m": 0.01,
                        "active_flow_cell_count": 1,
                        "active_flow_cell_ratio": 0.01,
                        "maximum_speed_m_per_s": 0.04,
                        "active_mean_speed_m_per_s": 0.03,
                        "slow_pooled_wet_fraction": 0.0,
                        "cumulative_source_volume_m3": 58982.4,
                        "cumulative_sink_volume_m3": 0.0,
                        "cumulative_boundary_outflow_volume_m3": 0.0,
                        "conservation_residual_m3": 0.0,
                    }
                    for name, value in values.items():
                        writer.writerow({"frame_index": frame, "category": "fluid_25d.water", "name": name, "value": value})
            parsed = runner._parse_profile_rows(path, {0, 15, 599})
            self.assertEqual(set(parsed), {0, 15, 599})
            accepted = runner._profile_acceptance(parsed, self.recipe)
            self.assertTrue(accepted["status_gate_pass"])
            self.assertTrue(accepted["conservation_gate_pass"])
            self.assertTrue(accepted["uniform_rain_volume_gate_pass"])
            self.assertTrue(accepted["no_sink_gate_pass"])
            self.assertEqual(accepted["exact_final_capture_frame"], 600)

    def test_nonzero_status_or_sink_fails_the_acceptance_gate(self) -> None:
        frame = {"finite_volume_status_flags": "0", **{key: "0" for key in runner.PROFILE_FIELDS}}
        frame["cumulative_source_volume_m3"] = "58982.4"
        frame["total_water_volume_m3"] = "1"
        frame["active_mean_speed_m_per_s"] = "0"
        frames = {599: dict(frame)}
        self.assertTrue(runner._profile_acceptance(frames, self.recipe)["status_gate_pass"])
        frames[599]["finite_volume_status_flags"] = "1"
        self.assertFalse(runner._profile_acceptance(frames, self.recipe)["status_gate_pass"])
        frames[599]["finite_volume_status_flags"] = "0"
        frames[599]["cumulative_sink_volume_m3"] = "0.1"
        self.assertFalse(runner._profile_acceptance(frames, self.recipe)["no_sink_gate_pass"])

    def test_png_dimensions_and_output_overwrite_guard(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            temp_root = Path(temporary)
            output_root = temp_root / "fresh-output"
            runner.assert_output_available(output_root)
            output_root.mkdir()
            with self.assertRaisesRegex(runner.NeutralReachError, "refusing to overwrite"):
                runner.assert_output_available(output_root)

            png = temp_root / "capture.png"
            header = b"\x89PNG\r\n\x1a\n" + b"\x00\x00\x00\x0dIHDR" + (1280).to_bytes(4, "big") + (720).to_bytes(4, "big")
            png.write_bytes(header + b"payload")
            runner._check_png(png, 1280, 720)
            with self.assertRaisesRegex(runner.NeutralReachError, "dimensions differ"):
                runner._check_png(png, 640, 360)


if __name__ == "__main__":
    unittest.main()
