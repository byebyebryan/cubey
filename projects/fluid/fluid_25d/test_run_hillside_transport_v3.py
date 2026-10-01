from __future__ import annotations

import json
import tempfile
import unittest
from argparse import Namespace
from decimal import Decimal
from pathlib import Path
from unittest.mock import patch

import run_hillside_transport_v3 as runner
import run_hillside_sustained_flow_v2 as v2


def hydraulic_frames(count: int) -> dict[int, dict[tuple[str, str], Decimal]]:
    frames = {}
    for frame in range(count):
        frames[frame] = {
            (runner.HYDRAULIC_IDENTITY, "hash_hi_u32"): Decimal("1234567890"),
            (runner.HYDRAULIC_IDENTITY, "hash_lo_u32"): Decimal("987654321"),
            (runner.SOLVER, "finite_volume_status_flags"): Decimal("0"),
            (runner.WATER, "total_water_volume_m3"): Decimal("12.500000"),
            ("fluid_25d.natural_flow.gauge.upstream", "sampled_tracer_amount_m3"): Decimal("0"),
        }
    return frames


def dye_frames(count: int = 5) -> tuple[
    dict[int, dict[tuple[str, str], Decimal]], dict[int, dict[tuple[str, str], Decimal]]
]:
    reference = hydraulic_frames(count)
    dyed = hydraulic_frames(count)
    for frame in range(count):
        physical_seconds = (frame + 1) * 2
        injected_seconds = max(0, min(2, physical_seconds - 4))
        source = Decimal(str(injected_seconds))
        has_dye = source > 0
        dyed[frame].update({
            (runner.TRACER, "amount_weighted_centroid_cell_x"): Decimal("5" if frame < 3 else "6"),
            (runner.TRACER, "amount_weighted_centroid_cell_y"): Decimal("5"),
            (runner.TRACER, "conservation_residual_m3"): Decimal("0"),
            (runner.TRACER, "cumulative_boundary_outflow_amount_m3"): Decimal("0"),
            (runner.TRACER, "cumulative_sink_amount_m3"): Decimal("0"),
            (runner.TRACER, "cumulative_source_amount_m3"): source,
            (runner.TRACER, "dyed_wet_cell_count"): Decimal("0" if not has_dye else "1" if frame == 2 else "2"),
            (runner.TRACER, "total_tracer_amount_m3"): Decimal("0" if not has_dye else "2"),
        })
    return reference, dyed


def healthy_parent_summary() -> dict:
    return {
        "numeric_profile_checks_passed": True,
        "unhealthy_frames": [],
        "physical_horizon_seconds": runner.HORIZON_SECONDS,
        "nonzero_solver_flag_frames": [],
        "sink_volume_m3": 0,
        "first_edge_trigger": {"physical_seconds": 2328, "reasons": ["edge band"]},
    }


class HillsideTransportV3Tests(unittest.TestCase):
    def test_hydraulic_profiles_require_exact_full_field_identity(self) -> None:
        reference = hydraulic_frames(3)
        dyed = hydraulic_frames(3)
        result = runner.compare_hydraulic_profiles(reference, dyed, frame_count=3)
        self.assertTrue(result["passed"])
        self.assertEqual(result["frames_compared"], 3)
        dyed[1][(runner.HYDRAULIC_IDENTITY, "hash_lo_u32")] += 1
        with self.assertRaisesRegex(ValueError, "metrics differ"):
            runner.compare_hydraulic_profiles(reference, dyed, frame_count=3)

    def test_hydraulic_profiles_reject_short_cadence_and_missing_hash(self) -> None:
        reference = hydraulic_frames(3)
        with self.assertRaisesRegex(ValueError, "every fixed-step frame"):
            runner.compare_hydraulic_profiles(reference, hydraulic_frames(2), frame_count=3)
        missing = hydraulic_frames(3)
        del missing[1][(runner.HYDRAULIC_IDENTITY, "hash_hi_u32")]
        with self.assertRaisesRegex(ValueError, "identity hashes"):
            runner.compare_hydraulic_profiles(reference, missing, frame_count=3)

    def test_hydraulic_identity_words_must_be_integer_uint32(self) -> None:
        for invalid in (Decimal("1.5"), Decimal("4294967296"), Decimal("-1")):
            with self.subTest(invalid=invalid):
                reference = hydraulic_frames(1)
                reference[0][(runner.HYDRAULIC_IDENTITY, "hash_hi_u32")] = invalid
                with self.assertRaisesRegex(ValueError, "non-uint32 state hash"):
                    runner.compare_hydraulic_profiles(reference, hydraulic_frames(1), frame_count=1)

    def test_dye_gate_accepts_exact_matched_water_and_moving_postpulse_parcel(self) -> None:
        reference, dyed = dye_frames()
        result = runner.validate_dye_profile(
            dyed, reference, frame_count=5, pulse_start=4, pulse_duration=2,
            source_rate=1.0, postpulse_seconds=8,
        )
        self.assertTrue(result["passed"])
        self.assertEqual(result["dye_source_amount_m3"], 2.0)
        self.assertGreater(result["centroid_displacement_cells"], 0.5)
        self.assertGreater(result["maximum_dyed_wet_cell_count"], 1)

    def test_dye_gate_recomputes_residual_instead_of_trusting_reported_zero(self) -> None:
        reference, dyed = dye_frames()
        total_key = (runner.TRACER, "total_tracer_amount_m3")
        residual_key = (runner.TRACER, "conservation_residual_m3")
        dyed[4][total_key] = Decimal("1.5")
        with self.assertRaisesRegex(ValueError, "reported tracer residual disagrees"):
            runner.validate_dye_profile(
                dyed, reference, frame_count=5, pulse_start=4, pulse_duration=2,
                source_rate=1.0, postpulse_seconds=8,
            )
        dyed[4][total_key] = Decimal("2.003001")
        dyed[4][residual_key] = Decimal("0.002999")
        with self.assertRaisesRegex(ValueError, "independently recomputed dye residual"):
            runner.validate_dye_profile(
                dyed, reference, frame_count=5, pulse_start=4, pulse_duration=2,
                source_rate=1.0, postpulse_seconds=8,
            )

        reference, dyed = dye_frames()
        dyed[4][total_key] = Decimal("2.002999")
        dyed[4][residual_key] = Decimal("0.003001")
        with self.assertRaisesRegex(ValueError, "reported dye residual exceeds the unchanged tolerance"):
            runner.validate_dye_profile(
                dyed, reference, frame_count=5, pulse_start=4, pulse_duration=2,
                source_rate=1.0, postpulse_seconds=8,
            )

    def test_dye_gate_rejects_changed_hydraulics_and_forcing(self) -> None:
        reference, dyed = dye_frames()
        dyed[2][(runner.WATER, "total_water_volume_m3")] += 1
        with self.assertRaisesRegex(ValueError, "hydraulic metrics differ"):
            runner.validate_dye_profile(
                dyed, reference, frame_count=5, pulse_start=4, pulse_duration=2,
                source_rate=1.0, postpulse_seconds=8,
            )
        reference, dyed = dye_frames()
        dyed[2][(runner.TRACER, "cumulative_source_amount_m3")] = Decimal("1")
        with self.assertRaisesRegex(ValueError, "frozen 60-62 minute pulse"):
            runner.validate_dye_profile(
                dyed, reference, frame_count=5, pulse_start=4, pulse_duration=2,
                source_rate=1.0, postpulse_seconds=8,
            )

    def test_dye_gate_rejects_missing_flag_sink_and_nonfinite_residual_gates(self) -> None:
        mutations = (
            ((runner.TRACER, "total_tracer_amount_m3"), None),
            ((runner.SOLVER, "finite_volume_status_flags"), Decimal("1")),
            ((runner.TRACER, "cumulative_sink_amount_m3"), Decimal("0.01")),
            ((runner.TRACER, "conservation_residual_m3"), Decimal("NaN")),
        )
        for metric, value in mutations:
            with self.subTest(metric=metric, value=value):
                reference, dyed = dye_frames()
                if value is None:
                    del dyed[2][metric]
                    expected = "missing required tracer metrics"
                elif metric == (runner.SOLVER, "finite_volume_status_flags"):
                    dyed[2][metric] = value
                    expected = "nonzero finite-volume"
                elif metric == (runner.TRACER, "cumulative_sink_amount_m3"):
                    dyed[2][metric] = value
                    expected = "nonzero tracer sink"
                else:
                    dyed[2][metric] = value
                    expected = "nonfinite tracer value"
                with self.assertRaisesRegex(ValueError, expected):
                    runner.validate_dye_profile(
                        dyed, reference, frame_count=5, pulse_start=4, pulse_duration=2,
                        source_rate=1.0, postpulse_seconds=8,
                    )

    def test_parent_gate_accepts_only_the_exact_prefix_only_historical_failure(self) -> None:
        summary = healthy_parent_summary()
        prefix = {"passed": False, "frame_count": 900,
                  "metric_values_compared": 96300,
                  "missing_required_old_metric_keys": [], "mismatches": [{"frame_index": 0}]}
        report = {"phase_checks_passed": False, "summary": summary}
        result = runner._verify_parent_prefix_gate(report, summary, prefix)
        self.assertEqual(result["status"], "historical_prefix_mismatch_only")
        self.assertFalse(result["historical_prefix_passed"])
        for changed in (
            {**prefix, "metric_values_compared": 96300 - 1},
            {**prefix, "missing_required_old_metric_keys": [["fluid_25d.water", "total_water_volume_m3"]]},
            {**prefix, "mismatches": []},
        ):
            with self.subTest(prefix=changed):
                with self.assertRaises(ValueError):
                    runner._verify_parent_prefix_gate(report, summary, changed)

    def test_parent_gate_rejects_arbitrary_failure_and_unhealthy_current_profile(self) -> None:
        prefix = {"passed": False, "frame_count": 900, "metric_values_compared": 96300,
                  "missing_required_old_metric_keys": [], "mismatches": [{"frame_index": 0}]}
        summary = healthy_parent_summary()
        with self.assertRaisesRegex(ValueError, "error or diagnostic"):
            runner._verify_parent_prefix_gate(
                {"phase_checks_passed": False, "error": "child failure", "summary": summary},
                summary, prefix,
            )
        unhealthy = dict(summary, numeric_profile_checks_passed=False, unhealthy_frames=[{"frame_index": 1}])
        with self.assertRaisesRegex(ValueError, "numeric"):
            runner._verify_parent_prefix_gate(
                {"phase_checks_passed": False, "summary": unhealthy}, unhealthy, prefix,
            )
        with self.assertRaisesRegex(ValueError, "phase failure"):
            runner._verify_parent_prefix_gate(
                {"phase_checks_passed": True, "summary": summary}, summary, prefix,
            )

    def _write_probe_fixture(self, folder: Path, app: Path, *, case: str = "512",
                             dye: bool = False, strict: bool = False,
                             exit_code: int = 0) -> tuple[dict, dict]:
        folder.mkdir()
        app.write_bytes(b"synthetic app")
        (folder / "child.log").write_text("synthetic successful run\n")
        (folder / "profile.metrics.csv").write_text("frame_index,category,name,value\n")
        (folder / "final.png").write_bytes(b"synthetic image")
        shader_map = {"fluid_25d_fv_update.comp.spv": "shader-hash"}
        pinned = {"app_sha256": "app-hash",
                  "shader_identity": {"files": dict(shader_map)}}
        receipt = {
            "case": case, "frames": runner.FRAME_COUNT, "interval": 1,
            "strict": strict, "dye": dye, "exit_code": exit_code,
            "before": {"app_sha256": "app-hash", "shader_hashes": dict(shader_map)},
            "after": {"app_sha256": "app-hash", "shader_hashes": dict(shader_map)},
            "command": runner._probe_expected_command(app, folder, case=case, dye=dye, strict=strict),
            "artifact_hashes": {
                name: runner.sha256_file(folder / name)
                for name in ("child.log", "profile.metrics.csv", "final.png")
            },
        }
        (folder / "execution.json").write_text(json.dumps(receipt))
        return pinned, receipt

    def test_probe_receipt_requires_exact_forcing_identity_and_artifacts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "app"
            folder = root / "water"
            pinned, receipt = self._write_probe_fixture(folder, app)
            checked = runner._validate_probe_receipt(folder, app, pinned,
                                                     case="512", dye=False, strict=False)
            self.assertEqual(checked["exit_code"], 0)
            changed_command = dict(receipt)
            changed_command["command"] = list(receipt["command"])
            q_index = changed_command["command"].index("--fluid25d-natural-flow-source-m3-per-s") + 1
            changed_command["command"][q_index] = "101"
            (folder / "execution.json").write_text(json.dumps(changed_command))
            with self.assertRaisesRegex(ValueError, "command differs"):
                runner._validate_probe_receipt(folder, app, pinned,
                                               case="512", dye=False, strict=False)

    def test_probe_receipt_rejects_exit_identity_and_artifact_failures(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "app"
            folder = root / "water"
            pinned, receipt = self._write_probe_fixture(folder, app, exit_code=1)
            with self.assertRaisesRegex(ValueError, "successful frozen run"):
                runner._validate_probe_receipt(folder, app, pinned,
                                               case="512", dye=False, strict=False)
            receipt["exit_code"] = 0
            receipt["before"]["shader_hashes"]["fluid_25d_fv_update.comp.spv"] = "other"
            receipt["after"] = dict(receipt["before"])
            (folder / "execution.json").write_text(json.dumps(receipt))
            with self.assertRaisesRegex(ValueError, "identity"):
                runner._validate_probe_receipt(folder, app, pinned,
                                               case="512", dye=False, strict=False)
            receipt["before"]["shader_hashes"]["fluid_25d_fv_update.comp.spv"] = "shader-hash"
            receipt["after"] = dict(receipt["before"])
            (folder / "profile.metrics.csv").write_text("tampered\n")
            (folder / "execution.json").write_text(json.dumps(receipt))
            with self.assertRaisesRegex(ValueError, "artifact hash mismatch"):
                runner._validate_probe_receipt(folder, app, pinned,
                                               case="512", dye=False, strict=False)

    def test_probe_receipt_requires_final_png_integrity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "app"
            folder = root / "water"
            pinned, receipt = self._write_probe_fixture(folder, app)
            receipt["artifact_hashes"].pop("final.png")
            (folder / "execution.json").write_text(json.dumps(receipt))
            with self.assertRaisesRegex(ValueError, "final.png integrity"):
                runner._validate_probe_receipt(folder, app, pinned,
                                               case="512", dye=False, strict=False)

    def test_512_water_evidence_does_not_require_its_own_edge_trigger(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "app"
            folder = root / "water"
            pinned, _ = self._write_probe_fixture(folder, app)
            summary = {"numeric_profile_checks_passed": True, "unhealthy_frames": []}
            with patch.object(v2, "read_metrics", return_value={0: {}}), \
                    patch.object(v2, "summarize", return_value=summary), \
                    patch.object(runner, "read_profile", return_value=hydraulic_frames(runner.FRAME_COUNT)):
                checked = runner._validate_water_evidence(folder, app, pinned)
            self.assertEqual(checked["summary"], summary)
            self.assertFalse(checked["receipt"]["edge_trigger_required"])

    def test_failed_profile_child_keeps_command_exit_log_and_partial_hashes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "app"
            out = root / "profile-out"
            expected_hashes = {
                "app_sha256": "app-hash", "recipe_sha256": "recipe-hash",
                "manifest_sha256": "manifest-hash", "elevation_sha256": "elevation-hash",
                "shader_map_sha256": "shader-map-hash",
            }
            pinned = dict(expected_hashes)
            ns = Namespace(
                app=app, out=out, phase="profile", parent_evidence=root / "parent.json",
                water_evidence=root / "water", dye_evidence=None,
                site_a_strict_evidence=None, hillside_strict_evidence=None,
                profile_evidence=None, captures_evidence=None,
            )
            child = {
                "command": runner._transport_app_command(
                    app, v2.arguments(512) + [
                        "--frames", str(runner.FRAME_COUNT), "--capture", "png",
                        "--fluid25d-catchment-view", "water-isolation",
                        "--fluid25d-natural-flow-home-pitch-radians", "-1.55",
                        "--fluid25d-dye-pulse-start-seconds", str(runner.DYE_START_SECONDS),
                        "--fluid25d-dye-pulse-duration-seconds", str(runner.DYE_DURATION_SECONDS),
                        "--profile-output", str((out / "dye-profile").resolve()),
                        "--profile-diagnostics", "--profile-diagnostic-interval", "1",
                        "--output", str((out / "dye-profile-final.png").resolve()),
                    ]),
                "exit_code": 7, "log_path": str((out / "dye-profile-child.log").resolve()),
                "log_sha256": "log-hash", "wall_seconds": 1.0,
            }
            with patch.object(runner, "parse_args", return_value=ns), \
                    patch.object(runner, "_validate_current_inputs", return_value=pinned), \
                    patch.object(runner, "validate_parent_evidence", return_value={"sha256": "parent-hash"}), \
                    patch.object(runner, "_validate_water_evidence", return_value={
                        "receipt": {"sha256": "water-hash"}, "summary": {}, "frames": {},
                    }), \
                    patch.object(runner, "_run_app", return_value=child):
                with self.assertRaises(SystemExit):
                    runner.main()
            report = json.loads((out / "profile.json").read_text())
            execution = report["dye_execution"]
            self.assertFalse(report["phase_checks_passed"])
            self.assertEqual(report["error"], "dye-profile child failed with exit code 7")
            self.assertEqual(execution["exit_code"], 7)
            self.assertEqual(execution["log_path"], child["log_path"])
            self.assertEqual(execution["log_sha256"], "log-hash")
            self.assertEqual(execution["command"], child["command"])
            self.assertIsNone(execution["metrics_sha256"])
            self.assertIsNone(execution["output_sha256"])

    def test_still_capture_label_is_pinned_to_raw_artifact_time_and_status(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "app"
            capture_dir = root / "captures"
            capture_dir.mkdir()
            label = "10min"
            seconds = 600
            raw = capture_dir / f"transport-{label}.png"
            labelled = capture_dir / f"transport-{label}-labelled.png"
            raw.write_bytes(b"raw png evidence")
            labelled.write_bytes(b"labelled png evidence")
            app_log = capture_dir / f"transport-{label}.log"
            label_log = capture_dir / f"transport-{label}-label.log"
            app_log.write_text("capture child complete")
            label_log.write_text("ffmpeg label child complete")
            raw_sha = runner.sha256_file(raw)
            label_sha = runner.sha256_file(labelled)
            case = {
                "label": label, "exit_code": 0, "physical_seconds": seconds,
                "frame_count": seconds // v2.DT,
                "command": runner._transport_app_command(
                    app, runner._transport_still_arguments(seconds, raw)),
                "path": str(raw.resolve()), "sha256": raw_sha,
                "log_path": str(app_log.resolve()), "log_sha256": runner.sha256_file(app_log),
                "labelled_still": {
                    "exit_code": 0,
                    "command": runner._label_still_command(raw, labelled, seconds),
                    "path": str(labelled.resolve()), "sha256": label_sha,
                    "source_sha256": raw_sha, "physical_seconds": seconds,
                    "label": label, "log_path": str(label_log.resolve()),
                    "log_sha256": runner.sha256_file(label_log),
                    "interpretation": "Magenta represents dye concentration; blue represents water depth, not speed",
                },
            }
            runner._validate_still_capture_case(case, app, capture_dir)
            self.assertIn("Water 100 m3/s continuous", runner._transport_still_label_filter(seconds))
            self.assertIn("no prescribed drain", runner._transport_still_label_filter(seconds))
            self.assertIn("Physical time 600 s", runner._transport_still_label_filter(seconds))
            self.assertIn("60-62 min", runner._transport_still_label_filter(seconds))
            self.assertIn("not speed", runner._transport_still_label_filter(seconds))
            changed = json.loads(json.dumps(case))
            changed["labelled_still"]["exit_code"] = 1
            with self.assertRaisesRegex(ValueError, "failed or changed transport-still"):
                runner._validate_still_capture_case(changed, app, capture_dir)

    def test_parent_evidence_rejects_mismatched_forcing_and_child_exit(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / "app"
            app.write_bytes(b"synthetic app")
            output = root / "final-256"
            output.mkdir()
            metrics = output / "hydraulics-profile.metrics.csv"
            metrics.write_text("synthetic metrics")
            report_path = output / "hydraulics.json"
            expected_hashes = {
                "app_sha256": "app-hash", "recipe_sha256": v2.RECIPE_SHA256[256],
                "manifest_sha256": v2.MANIFEST_SHA256, "elevation_sha256": v2.ELEVATION_SHA256,
                "shader_map_sha256": "shader-hash",
            }
            identity = {"domain_cells": [256, 256], "forcing_identity": {"rate": 100}}
            pinned256 = {**expected_hashes, "input_identity": identity}
            pinned512 = {"app_sha256": "app-hash", "shader_map_sha256": "shader-hash"}
            prefix = {"passed": False, "frame_count": 900,
                      "metric_values_compared": 96300,
                      "missing_required_old_metric_keys": [],
                      "mismatches": [{"frame_index": 0}],
                      "baseline_sha256": "baseline-hash",
                      "baseline_path": str(v2.BASELINE_METRICS.resolve()),
                      "current_path": str(metrics.resolve())}
            summary = healthy_parent_summary()
            command = runner._parent_expected_command(app, output)
            report = {
                "phase": "hydraulics", "domain": 256,
                "app_sha256": "app-hash", "shader_map_sha256": "shader-hash",
                "recipe_sha256": v2.RECIPE_SHA256[256], "manifest_sha256": v2.MANIFEST_SHA256,
                "elevation_sha256": v2.ELEVATION_SHA256, "input_identity": identity,
                "start_input_hashes": expected_hashes, "end_input_hashes": expected_hashes,
                "cases": [{"exit_code": 0, "input_hashes": expected_hashes, "command": command}],
                "summary": summary, "phase_checks_passed": False,
                "baseline_prefix_comparison": prefix,
            }
            report_path.write_text(json.dumps(report))
            recomputed_prefix = dict(prefix)
            with patch.object(v2, "pinned_inputs", return_value=pinned256), \
                    patch.object(v2, "read_metrics", return_value={0: {}}), \
                    patch.object(v2, "summarize", return_value=summary), \
                    patch.object(v2, "_has_edge_trigger", return_value=True), \
                    patch.object(v2, "compare_prefix_to_baseline", return_value=recomputed_prefix):
                accepted = runner.validate_parent_evidence(report_path, app, pinned512)
                self.assertEqual(accepted["prefix_gate"]["status"], "historical_prefix_mismatch_only")
                report["input_identity"] = {"domain_cells": [256, 256], "forcing_identity": {"rate": 99}}
                report_path.write_text(json.dumps(report))
                with self.assertRaisesRegex(ValueError, "forcing"):
                    runner.validate_parent_evidence(report_path, app, pinned512)
                report["input_identity"] = identity
                report["cases"][0]["exit_code"] = 1
                report_path.write_text(json.dumps(report))
                with self.assertRaisesRegex(ValueError, "child exit"):
                    runner.validate_parent_evidence(report_path, app, pinned512)


if __name__ == "__main__":
    unittest.main()
