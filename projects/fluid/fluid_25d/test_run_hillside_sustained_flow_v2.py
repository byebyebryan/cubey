from __future__ import annotations

import csv
import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import run_hillside_sustained_flow_v2 as runner


def synthetic_frames(frame_count: int = 2) -> dict[int, dict[tuple[str, str], float]]:
    frames: dict[int, dict[tuple[str, str], float]] = {}
    for frame in range(frame_count):
        source = runner.Q_M3_PER_S * runner.DT * (frame + 1)
        exported = 0.0
        values: dict[tuple[str, str], float] = {
            (runner.SOLVER, "finite_volume_status_flags"): 0.0,
            (runner.WATER, "cumulative_source_volume_m3"): source,
            (runner.WATER, "cumulative_sink_volume_m3"): 0.0,
            (runner.WATER, "cumulative_boundary_outflow_volume_m3"): exported,
            (runner.WATER, "total_water_volume_m3"): source - exported,
            (runner.WATER, "maximum_depth_m"): 0.5,
            (runner.WATER, "conservation_residual_m3"): 0.0,
            (runner.WATER, "active_flow_cell_count"): 4.0,
            (runner.WATER, "active_mean_speed_m_per_s"): 0.08,
            (runner.WATER, "slow_pooled_wet_fraction"): 0.25,
            (runner.PROGRESS, "source_minimum_bed_m"): 2066.0,
            (runner.PROGRESS, "maximum_wetted_bed_drop_m"): 50.0,
            (runner.PROGRESS, "farthest_materially_wet_distance_m"): 500.0,
            (runner.PROGRESS, "source_region_water_volume_m3"): 100.0,
            (runner.PROGRESS, "materially_wet_cell_count"): 8.0,
            (runner.SPATIAL, "material_water_volume_m3"): source,
            (runner.SPATIAL, "source_connected_material_wet_cells"): 5.0,
            (runner.SPATIAL, "source_connected_material_water_volume_m3"): 100.0,
            (runner.SPATIAL, "material_active_flow_cells"): 3.0,
            (runner.SPATIAL, "material_active_water_volume_m3"): 50.0,
            (runner.SPATIAL, "material_slow_water_volume_m3"): source - 50.0,
            (runner.SPATIAL, "minimum_material_edge_distance_m"): 240.0,
            (runner.SPATIAL, "material_edge_band_wet_cells"): 0.0,
        }
        for drop in (20, 50, 100, 200):
            values[(runner.PROGRESS, f"below_source_wet_cells_{drop}m")] = float(drop <= 50)
            values[(runner.PROGRESS, f"below_source_water_volume_{drop}m")] = float(15 if drop <= 50 else 0)
        frames[frame] = values
    return frames


def write_profile(path: Path, frames: dict[int, dict[tuple[str, str], float | str]]) -> None:
    with path.open("w", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(("frame_index", "category", "name", "value"))
        for frame, values in sorted(frames.items()):
            for (category, name), value in sorted(values.items()):
                writer.writerow((frame, category, name, value))


def pinned_report_identity(domain: int = 256) -> tuple[dict, dict[str, str]]:
    recipe = json.loads(runner.RECIPES[domain].read_text())
    return (
        {
            "domain_cells": [domain, domain],
            "crop_xzwh": recipe["crop_xzwh"],
            "transformed_crop_sha256": recipe["transformed_crop_sha256"],
            "recipe_sha256": runner.RECIPE_SHA256[domain],
            "shader_map_sha256": "current-shaders",
            "forcing_identity": runner.forcing_identity(recipe, runner.MANIFEST_SHA256),
        },
        {
            "app_sha256": "current-app",
            "recipe_sha256": runner.RECIPE_SHA256[domain],
            "manifest_sha256": runner.MANIFEST_SHA256,
            "elevation_sha256": runner.ELEVATION_SHA256,
            "shader_map_sha256": "current-shaders",
        },
    )


def synthetic_hydraulic_report(*, diagnostic_failure: bool = True, domain: int = 256) -> dict:
    identity, hashes = pinned_report_identity(domain)
    first_trigger = {
        "frame_index": 1163,
        "physical_seconds": 2328,
        "reasons": ["material_wet_within_120m_of_edge"],
    }
    summary = {
        "numeric_profile_checks_passed": not diagnostic_failure,
        "physical_horizon_seconds": runner.HORIZON_SECONDS,
        "unhealthy_frames": (
            [{"frame_index": 2054, "physical_seconds": 4110,
              "failures": [runner.WATER_LEDGER_FAILURE]},
             {"frame_index": 3599, "physical_seconds": 7200,
              "failures": [runner.WATER_LEDGER_FAILURE]}]
            if diagnostic_failure else []
        ),
        "nonzero_solver_flag_frames": [],
        "maximum_absolute_source_volume_error_m3": 0,
        "sink_volume_m3": 0,
        "edge_triggered": True,
        "first_edge_trigger": first_trigger,
    }
    return {
        "phase": "hydraulics",
        "domain": domain,
        "app_sha256": "current-app",
        "shader_map_sha256": "current-shaders",
        "recipe_sha256": runner.RECIPE_SHA256[domain],
        "manifest_sha256": runner.MANIFEST_SHA256,
        "elevation_sha256": runner.ELEVATION_SHA256,
        "input_identity": identity,
        "start_input_hashes": hashes,
        "end_input_hashes": dict(hashes),
        "cases": [{"exit_code": 0, "input_hashes": dict(hashes)}],
        "phase_checks_passed": not diagnostic_failure,
        "summary": summary,
        "baseline_prefix_comparison": {"passed": True},
    }


class HillsideSustainedFlowRunnerTests(unittest.TestCase):
    def test_arguments_pin_source_only_256_and_conditional_512_crops(self) -> None:
        args256 = runner.arguments(256)
        args512 = runner.arguments(512)
        for args, domain, origin, local_center in (
            (args256, 256, (1280, 1536), (85, 85)),
            (args512, 512, (1152, 1408), (213, 213)),
        ):
            self.assertEqual(args[args.index("--grid-width") + 1], str(domain))
            self.assertEqual(args[args.index("--grid-height") + 1], str(domain))
            self.assertEqual(args[args.index("--fluid25d-terrain-crop-x") + 1], str(origin[0]))
            self.assertEqual(args[args.index("--fluid25d-terrain-crop-z") + 1], str(origin[1]))
            self.assertEqual(args[args.index("--fluid25d-natural-flow-source-m3-per-s") + 1], "100")
            self.assertEqual(args[args.index("--fluid25d-fixed-delta-seconds") + 1], "2")
            self.assertEqual(args[args.index("--fluid25d-substeps") + 1], "16")
            self.assertEqual(args[args.index("--fluid25d-gravity-m-per-s2") + 1], "9.81")
            self.assertEqual(args[args.index("--fluid25d-flow-damping-per-second") + 1], "0.15")
            self.assertNotIn("--fluid25d-dye-pulse-start-seconds", args)
            self.assertNotIn("--fluid25d-dye-pulse-duration-seconds", args)
            self.assertNotIn("--fluid25d-rain-rate-mm-per-hour", args)
            self.assertNotIn("--fluid25d-natural-flow-sink-m3-per-s", args)
            recipe_path = Path(args[args.index("--fluid25d-natural-flow-recipe") + 1])
            recipe = json.loads(recipe_path.read_text())
            crop_x, crop_z, _, _ = recipe["crop_xzwh"]
            actual = [crop_x + recipe["source_center_cell_xz"][0],
                      crop_z + recipe["source_center_cell_xz"][1]]
            self.assertEqual(actual, list(runner.SOURCE_FULL_MAP_XZ))
            self.assertEqual(recipe["source_center_cell_xz"], list(local_center))

    def test_recipe_validation_refuses_outlet_or_moved_source(self) -> None:
        recipe_path = runner.RECIPES[256]
        source = json.loads(recipe_path.read_text())
        invalid = copy.deepcopy(source)
        invalid["expected_outlet"] = None
        with self.assertRaises(ValueError):
            runner.validate_recipe_data(invalid, 256)
        invalid = copy.deepcopy(source)
        invalid["source_cells_xz"][0][0] += 1
        with self.assertRaises(ValueError):
            runner.validate_recipe_data(invalid, 256)

    def test_summary_reports_first_spatial_edge_trigger_and_snapshots(self) -> None:
        frames = synthetic_frames()
        frames[1][(runner.SPATIAL, "minimum_material_edge_distance_m")] = 120.0
        frames[1][(runner.SPATIAL, "material_edge_band_wet_cells")] = 2.0
        report = runner.summarize(frames, 4, snapshot_seconds=(2, 4))
        self.assertTrue(report["numeric_profile_checks_passed"])
        self.assertTrue(report["edge_triggered"])
        self.assertEqual(report["first_edge_trigger"]["physical_seconds"], 4)
        self.assertEqual(report["first_edge_trigger"]["reasons"], ["material_wet_within_120m_of_edge"])
        self.assertEqual(set(report["snapshots"]), {"2", "4"})
        self.assertEqual(report["snapshots"]["4"]["spatial"]["material_edge_band_wet_cells"], 2.0)
        self.assertIn("not checked", report["strict_cpu_gpu_parity"])

    def test_positive_boundary_export_triggers_even_without_edge_distance(self) -> None:
        frames = synthetic_frames()
        values = frames[1]
        values[(runner.WATER, "cumulative_boundary_outflow_volume_m3")] = 1.0
        values[(runner.WATER, "total_water_volume_m3")] -= 1.0
        values[(runner.SPATIAL, "material_water_volume_m3")] -= 1.0
        values[(runner.SPATIAL, "material_slow_water_volume_m3")] -= 1.0
        values[(runner.WATER, "conservation_residual_m3")] = 0.0
        report = runner.summarize(frames, 4, snapshot_seconds=(2, 4))
        self.assertTrue(report["edge_triggered"])
        self.assertEqual(report["first_edge_trigger"]["physical_seconds"], 4)
        self.assertEqual(report["first_edge_trigger"]["reasons"], ["positive_boundary_export"])

    def test_no_material_water_requires_distance_sentinel(self) -> None:
        frames = synthetic_frames(1)
        values = frames[0]
        values[(runner.SPATIAL, "material_water_volume_m3")] = 0.0
        values[(runner.SPATIAL, "source_connected_material_wet_cells")] = 0.0
        values[(runner.SPATIAL, "source_connected_material_water_volume_m3")] = 0.0
        values[(runner.SPATIAL, "material_active_flow_cells")] = 0.0
        values[(runner.SPATIAL, "material_active_water_volume_m3")] = 0.0
        values[(runner.SPATIAL, "material_slow_water_volume_m3")] = 0.0
        values[(runner.SPATIAL, "minimum_material_edge_distance_m")] = 30.0
        values[(runner.SPATIAL, "material_edge_band_wet_cells")] = 0.0
        values[(runner.PROGRESS, "materially_wet_cell_count")] = 0.0
        report = runner.summarize(frames, 2, snapshot_seconds=(2,))
        self.assertFalse(report["numeric_profile_checks_passed"])
        self.assertTrue(any("-1 edge-distance sentinel" in failure
                            for failure in report["unhealthy_frames"][0]["failures"]))

    def test_profile_requires_every_frame_and_spatial_metric(self) -> None:
        frames = synthetic_frames()
        del frames[1][(runner.SPATIAL, "material_slow_water_volume_m3")]
        with self.assertRaisesRegex(ValueError, "missing required metrics"):
            runner.summarize(frames, 4, snapshot_seconds=(2, 4))
        frames = synthetic_frames()
        del frames[1]
        with self.assertRaisesRegex(ValueError, "every required fixed step"):
            runner.summarize(frames, 4, snapshot_seconds=(2, 4))

    def test_profile_health_checks_flags_sink_and_ledger_per_frame(self) -> None:
        for metric, bad_value in (
            ((runner.SOLVER, "finite_volume_status_flags"), 1.0),
            ((runner.WATER, "cumulative_sink_volume_m3"), 1.0),
            ((runner.WATER, "conservation_residual_m3"), 1.0),
        ):
            with self.subTest(metric=metric):
                frames = synthetic_frames()
                frames[1][metric] = bad_value
                report = runner.summarize(frames, 4, snapshot_seconds=(2, 4))
                self.assertFalse(report["numeric_profile_checks_passed"])
                self.assertEqual(report["unhealthy_frames"][0]["frame_index"], 1)

    def test_failed_hydraulics_child_report_is_retained_and_existing_profile_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            report_path = out / "hydraulics.json"
            report = {
                "report_path": str(report_path),
                "start_input_hashes": {"app_sha256": "app", "recipe_sha256": "recipe"},
                "cases": [],
            }
            with mock.patch.object(runner, "run_command", return_value={"exit_code": 9}):
                runner.hydraulics_phase(Path("fake-app"), out, 256, report)
            retained = json.loads(report_path.read_text())
            self.assertEqual(retained["cases"][0]["exit_code"], 9)
            self.assertIn("exit code 9", retained["error"])

        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / "hydraulics-profile.metrics.csv").write_text("prior evidence")
            report = {
                "report_path": str(out / "hydraulics.json"),
                "start_input_hashes": {},
                "cases": [],
            }
            with mock.patch.object(runner, "run_command") as child:
                with self.assertRaisesRegex(FileExistsError, "refusing to overwrite"):
                    runner.hydraulics_phase(Path("fake-app"), out, 256, report)
            child.assert_not_called()

    def test_oracles_stop_at_first_failure_and_retain_exact_coverage(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            report_path = out / "oracles.json"
            report = {
                "report_path": str(report_path),
                "start_input_hashes": {"app_sha256": "app", "recipe_sha256": "recipe"},
            }
            with mock.patch.object(runner, "run_command", return_value={"exit_code": 1}) as child:
                runner.oracles_phase(Path("fake-app"), out, 256, report)
            child.assert_called_once()
            retained = json.loads(report_path.read_text())
            coverage = retained["strict_oracle_coverage"]
            self.assertEqual(coverage["planned_physical_seconds"], [60, 600, 1800])
            self.assertEqual([item["physical_seconds"] for item in coverage["checks"]], [60])
            self.assertEqual(coverage["stopped_at_first_failure_seconds"], 60)
            self.assertFalse(coverage["complete"])

    def test_csv_parser_rejects_duplicate_and_nonfinite_metrics(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "profile.csv"
            path.write_text(
                "frame_index,category,name,value\n"
                "0,fluid_25d.water,total_water_volume_m3,1.0\n"
                "0,fluid_25d.water,total_water_volume_m3,1.0\n"
            )
            with self.assertRaisesRegex(ValueError, "duplicate metric"):
                runner.read_metrics(path)
            path.write_text("frame_index,category,name,value\n0,fluid_25d.water,total_water_volume_m3,nan\n")
            with self.assertRaisesRegex(ValueError, "nonfinite"):
                runner.read_metrics(path)

    def test_baseline_helper_compares_shared_keys_at_six_decimal_places(self) -> None:
        baseline_values = {metric: 1.0000004 for metric in runner.BASELINE_CORE_KEYS}
        current_values = dict(baseline_values)
        current_values[(runner.SPATIAL, "material_water_volume_m3")] = 42.0
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            baseline = root / "baseline.csv"
            current = root / "current.csv"
            write_profile(baseline, {0: baseline_values, 1: baseline_values})
            write_profile(current, {0: current_values, 1: current_values})
            result = runner.compare_prefix_to_baseline(current, baseline, frame_count=2)
            self.assertTrue(result["passed"])
            self.assertEqual(result["metric_values_compared"], len(runner.BASELINE_CORE_KEYS) * 2)
            self.assertGreater(result["current_only_metric_rows"], 0)

            changed = dict(current_values)
            changed[(runner.WATER, "total_water_volume_m3")] = 1.000002
            write_profile(current, {0: current_values, 1: changed})
            result = runner.compare_prefix_to_baseline(current, baseline, frame_count=2)
            self.assertFalse(result["passed"])
            self.assertEqual(result["mismatches"][0]["frame_index"], 1)

    def test_512_parent_must_be_successful_current_app_and_triggered(self) -> None:
        recipe = json.loads(runner.RECIPES[256].read_text())
        expected_forcing = runner.forcing_identity(recipe, runner.MANIFEST_SHA256)
        parent = synthetic_hydraulic_report(diagnostic_failure=False)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "hydraulics.json"
            path.write_text(json.dumps(parent))
            accepted = runner.verify_parent_evidence(
                path, "current-app", "current-shaders", expected_forcing
            )
            self.assertEqual(accepted["sha256"], runner.sha256_file(path))
            parent["summary"]["edge_triggered"] = False
            path.write_text(json.dumps(parent))
            with self.assertRaisesRegex(ValueError, "real edge trigger"):
                runner.verify_parent_evidence(
                    path, "current-app", "current-shaders", expected_forcing
                )

    def test_diagnostic_switch_is_limited_to_captures_and_domain_512_hydraulics(self) -> None:
        runner.validate_diagnostic_scope("captures", 256, True)
        runner.validate_diagnostic_scope("captures", 512, True)
        runner.validate_diagnostic_scope("hydraulics", 512, True)
        for phase, domain in (("hydraulics", 256), ("oracles", 256), ("oracles", 512)):
            with self.subTest(phase=phase, domain=domain):
                with self.assertRaisesRegex(ValueError, "limited to captures"):
                    runner.validate_diagnostic_scope(phase, domain, True)
        runner.validate_diagnostic_scope("oracles", 256, False)

    def test_diagnostic_parent_requires_flag_and_only_ledger_failure(self) -> None:
        recipe = json.loads(runner.RECIPES[256].read_text())
        expected_forcing = runner.forcing_identity(recipe, runner.MANIFEST_SHA256)
        parent = synthetic_hydraulic_report()
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "hydraulics.json"
            path.write_text(json.dumps(parent))
            with self.assertRaisesRegex(ValueError, "ledger-only diagnostic"):
                runner.verify_parent_evidence(path, "current-app", "current-shaders", expected_forcing)
            accepted = runner.verify_parent_evidence(
                path, "current-app", "current-shaders", expected_forcing,
                allow_diagnostic_unhealthy=True,
            )
            self.assertTrue(accepted["diagnostic_only"])
            self.assertFalse(accepted["phase_checks_passed"])
            self.assertTrue(accepted["baseline_prefix_passed"])

            mutations = (
                ("flags", lambda p: p["summary"].update(nonzero_solver_flag_frames=[7])),
                ("source error", lambda p: p["summary"].update(
                    maximum_absolute_source_volume_error_m3=0.1)),
                ("sink", lambda p: p["summary"].update(sink_volume_m3=0.1)),
                ("other failure", lambda p: p["summary"]["unhealthy_frames"][0].update(
                    failures=["source volume differs from frozen Q*time"])),
                ("baseline", lambda p: p["baseline_prefix_comparison"].update(passed=False)),
                ("app pin", lambda p: p.update(app_sha256="stale-app")),
                ("shader pin", lambda p: p.update(shader_map_sha256="stale-shaders")),
                ("recipe pin", lambda p: p.update(recipe_sha256="stale-recipe")),
                ("manifest pin", lambda p: p.update(manifest_sha256="stale-manifest")),
                ("unstable inputs", lambda p: p["end_input_hashes"].update(app_sha256="changed")),
                ("forcing", lambda p: p["input_identity"]["forcing_identity"].update(
                    source_rate_m3_per_s=99)),
            )
            for name, mutate in mutations:
                with self.subTest(mutation=name):
                    changed = copy.deepcopy(parent)
                    mutate(changed)
                    path.write_text(json.dumps(changed))
                    with self.assertRaises(ValueError):
                        runner.verify_parent_evidence(
                            path, "current-app", "current-shaders", expected_forcing,
                            allow_diagnostic_unhealthy=True,
                        )

    def test_diagnostic_matching_hydraulics_is_opt_in_and_health_gate_stays_failed(self) -> None:
        parent = synthetic_hydraulic_report()
        identity, hashes = pinned_report_identity(256)
        capture_report = {
            "domain": 256,
            "app_sha256": "current-app",
            "shader_map_sha256": "current-shaders",
            "recipe_sha256": runner.RECIPE_SHA256[256],
            "manifest_sha256": runner.MANIFEST_SHA256,
            "input_identity": identity,
            "start_input_hashes": hashes,
        }
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / "hydraulics.json").write_text(json.dumps(parent))
            with self.assertRaisesRegex(ValueError, "healthy matching hydraulics"):
                runner.load_matching_hydraulics(out, capture_report)
            accepted, _ = runner.load_matching_hydraulics(
                out, capture_report, allow_diagnostic_unhealthy=True
            )
            self.assertTrue(runner.diagnostic_hydraulics_eligible(accepted))

            mutated = copy.deepcopy(parent)
            mutated["summary"]["nonzero_solver_flag_frames"] = [7]
            (out / "hydraulics.json").write_text(json.dumps(mutated))
            with self.assertRaises(ValueError):
                runner.load_matching_hydraulics(
                    out, capture_report, allow_diagnostic_unhealthy=True
                )
            mutated = copy.deepcopy(parent)
            mutated["end_input_hashes"]["shader_map_sha256"] = "changed-shaders"
            (out / "hydraulics.json").write_text(json.dumps(mutated))
            with self.assertRaises(ValueError):
                runner.load_matching_hydraulics(
                    out, capture_report, allow_diagnostic_unhealthy=True
                )

    def test_512_diagnostic_hydraulics_inherits_parent_and_captures_need_edge_trigger(self) -> None:
        hydraulic = synthetic_hydraulic_report(diagnostic_failure=False, domain=512)
        parent = synthetic_hydraulic_report()
        recipe = json.loads(runner.RECIPES[256].read_text())
        forcing = runner.forcing_identity(recipe, runner.MANIFEST_SHA256)
        with tempfile.TemporaryDirectory() as parent_directory:
            parent_path = Path(parent_directory) / "hydraulics.json"
            parent_path.write_text(json.dumps(parent))
            verified_parent = runner.verify_parent_evidence(
                parent_path,
                "current-app",
                "current-shaders",
                forcing,
                allow_diagnostic_unhealthy=True,
            )
        hydraulic["parent_evidence"] = verified_parent
        hydraulic["parent_evidence_sha256"] = verified_parent["sha256"]
        hydraulic["diagnostic_only"] = True
        hydraulic["inherited_parent_failure"] = True
        hydraulic["phase_checks_passed"] = False
        identity, hashes = pinned_report_identity(512)
        capture_report = {
            "domain": 512,
            "app_sha256": "current-app",
            "shader_map_sha256": "current-shaders",
            "recipe_sha256": runner.RECIPE_SHA256[512],
            "manifest_sha256": runner.MANIFEST_SHA256,
            "input_identity": identity,
            "start_input_hashes": hashes,
            "parent_evidence_sha256": verified_parent["sha256"],
        }
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            (out / "hydraulics.json").write_text(json.dumps(hydraulic))
            with self.assertRaisesRegex(ValueError, "healthy matching hydraulics"):
                runner.load_matching_hydraulics(out, capture_report, require_edge_trigger=True)
            accepted, _ = runner.load_matching_hydraulics(
                out, capture_report, allow_diagnostic_unhealthy=True, require_edge_trigger=True
            )
            self.assertTrue(accepted["diagnostic_only"])
            self.assertTrue(accepted["inherited_parent_failure"])
            hydraulic["summary"]["edge_triggered"] = False
            (out / "hydraulics.json").write_text(json.dumps(hydraulic))
            with self.assertRaisesRegex(ValueError, "actual current-domain edge trigger"):
                runner.load_matching_hydraulics(
                    out, capture_report, allow_diagnostic_unhealthy=True, require_edge_trigger=True
                )

    def test_diagnostic_captures_complete_but_never_pass_and_keep_raw_unlabelled(self) -> None:
        hydraulic = synthetic_hydraulic_report()
        identity, hashes = pinned_report_identity(256)
        with tempfile.TemporaryDirectory() as directory:
            out = Path(directory)
            report_path = out / "captures.json"
            (out / "hydraulics.json").write_text(json.dumps(hydraulic))
            report = {
                "report_path": str(report_path),
                "phase": "captures",
                "domain": 256,
                "app_sha256": "current-app",
                "shader_map_sha256": "current-shaders",
                "recipe_sha256": runner.RECIPE_SHA256[256],
                "manifest_sha256": runner.MANIFEST_SHA256,
                "input_identity": identity,
                "start_input_hashes": hashes,
                "diagnostic_unhealthy_requested": True,
                "cases": [],
            }

            def fake_run(_app, args, log):
                raw = Path(args[args.index("--output") + 1])
                raw.write_bytes(b"synthetic raw capture")
                return {"exit_code": 0, "wall_seconds": 0.01,
                        "command": ["fake-fluid", *args], "log": str(log)}

            def fake_label(source, _out, *, diagnostic_only=False, capture_label=None):
                return {
                    "exit_code": 0,
                    "sha256": f"label-hash-{capture_label}",
                    "source_sha256": runner.sha256_file(source),
                    "diagnostic_only": diagnostic_only,
                }

            with mock.patch.object(runner, "run_command", side_effect=fake_run) as child, \
                    mock.patch.object(runner, "label_video", side_effect=fake_label):
                runner.captures_phase(Path("fake-app"), out, 256, report)
            self.assertEqual(child.call_count, 3)
            self.assertTrue(report["diagnostic_only"])
            self.assertTrue(report["inherited_parent_failure"])
            self.assertTrue(report["artifact_completion_passed"])
            self.assertFalse(runner.report_checks_passed(report))
            self.assertEqual(runner.phase_exit_code({
                "phase_checks_passed": runner.report_checks_passed(report)
            }), 1)
            self.assertEqual(
                [case["label"] for case in report["cases"]],
                ["overview-composite", "close-oblique-composite", "close-topdown-water-isolation"],
            )
            for case in report["cases"]:
                self.assertIn("diagnostic-unlabelled-raw.mp4", case["raw_video_path"])
                self.assertIn("not reviewed; not validated", case["raw_video_status"])
            self.assertEqual(
                report["cases"][1]["moving_highlight_interpretation"],
                "render-only, not conserved dye",
            )
            diagnostic_filter = runner.video_label_filter(diagnostic_only=True)
            self.assertIn("DIAGNOSTIC ONLY | water ledger tolerance failed", diagnostic_filter)
            self.assertIn("Moving highlight is render-only, not conserved dye", diagnostic_filter)
            self.assertIn(r"%{eif\:n*2+2\:d}", diagnostic_filter)
            self.assertNotIn("floor(t*60", diagnostic_filter)
            normal_filter = runner.video_label_filter(diagnostic_only=False)
            self.assertNotIn("DIAGNOSTIC ONLY", normal_filter)

    def test_diagnostic_flag_does_not_change_healthy_phase_controls(self) -> None:
        healthy = synthetic_hydraulic_report(diagnostic_failure=False)
        self.assertTrue(runner.report_checks_passed(healthy))
        captures = {
            "phase": "captures",
            "artifact_completion_passed": True,
            "cases": [
                {"exit_code": 0, "raw_video_sha256": "raw",
                 "labelled_video": {"exit_code": 0, "sha256": "labelled"}}
                for _ in range(3)
            ],
        }
        self.assertTrue(runner.report_checks_passed(captures))
        self.assertEqual(runner.phase_exit_code({"phase_checks_passed": True}), 0)

    def test_concise_headline_omits_full_unhealthy_frame_list(self) -> None:
        status = runner.concise_status({
            "phase": "hydraulics",
            "domain": 256,
            "phase_checks_passed": False,
            "summary": {"unhealthy_frames": [{"frame_index": n} for n in range(1546)]},
        }, Path("hydraulics.json"))
        self.assertEqual(status["report_path"], "hydraulics.json")
        self.assertEqual(status["exit_code"], 1)
        self.assertNotIn("unhealthy_frames", status)

    def test_metric_float_overflow_and_decimal_quantize_are_reportable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            profile = root / "overflow.csv"
            profile.write_text("frame_index,category,name,value\n0,a,b,1e999\n")
            with self.assertRaisesRegex(ValueError, "finite float range"):
                runner.read_metrics(profile)
            baseline = root / "baseline.csv"
            current = root / "current.csv"
            values = {metric: "1e999" for metric in runner.BASELINE_CORE_KEYS}
            write_profile(baseline, {0: values})
            write_profile(current, {0: values})
            with self.assertRaisesRegex(ValueError, "supported CSV precision"):
                runner.compare_prefix_to_baseline(current, baseline, frame_count=1)

    def test_shader_map_identity_detects_a_synthetic_module_mutation(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            shader_dir = Path(directory) / "shaders"
            shader_dir.mkdir()
            module = shader_dir / "fluid_25d_fv_update.comp.spv"
            module.write_bytes(b"compiled shader before")
            start = runner.shader_map_identity(shader_dir)
            module.write_bytes(b"compiled shader after")
            end = runner.shader_map_identity(shader_dir)
            self.assertNotEqual(start["shader_map_sha256"], end["shader_map_sha256"])
            with self.assertRaisesRegex(ValueError, "shader_map_sha256"):
                runner.require_stable_inputs(
                    {"app_sha256": "same", "shader_map_sha256": start["shader_map_sha256"]},
                    {"app_sha256": "same", "shader_map_sha256": end["shader_map_sha256"]},
                )

    def test_phase_reports_keep_strict_oracle_distinct_from_hydraulic_profile(self) -> None:
        hydraulic = {
            "phase": "hydraulics",
            "domain": 256,
            "cases": [{"exit_code": 0}],
            "summary": {"numeric_profile_checks_passed": True},
            "baseline_prefix_comparison": {"passed": True},
        }
        self.assertTrue(runner.report_checks_passed(hydraulic))
        oracles = {
            "phase": "oracles",
            "strict_oracle_coverage": {
                "complete": True,
                "checks": [
                    {"physical_seconds": seconds,
                     "execution": {"exit_code": 0, "output_sha256": "artifact-hash"}}
                    for seconds in runner.STRICT_ORACLE_SECONDS
                ],
            },
        }
        self.assertTrue(runner.report_checks_passed(oracles))
        oracles["strict_oracle_coverage"]["checks"].pop()
        self.assertFalse(runner.report_checks_passed(oracles))


if __name__ == "__main__":
    unittest.main()
