#!/usr/bin/env python3
"""GPU-free focused checks for the opt-in native rain-intensity harness."""

from __future__ import annotations

import hashlib
import importlib.util
import shutil
import tempfile
import unittest
from pathlib import Path

import numpy as np

import run_native_rain_intensity_v1 as rain


class FakeHelper:
    np = np

    def __init__(self, *, change_manning: bool = False):
        self.change_manning = change_manning

    @staticmethod
    def _input_hashes(input_dir: Path) -> dict[str, str]:
        return {
            path.relative_to(input_dir).as_posix(): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(input_dir.rglob("*"))
            if path.is_file()
        }

    def _make_model_inputs(self, spec: dict, case_dir: Path) -> None:
        field = case_dir / "native/input/field"
        field.mkdir(parents=True)
        (field / "h.dat").write_text("initial dry state\n", encoding="ascii")
        (field / "z.dat").write_text("immutable bed\n", encoding="ascii")
        (field / "manning.dat").write_text(
            "0.06\n" if self.change_manning else "0.05\n", encoding="ascii"
        )
        (field / "precipitation_mask.dat").write_text("0\n", encoding="ascii")
        rate = float(spec["rain_rate_m_per_s"])
        (field / "precipitation_source_all.dat").write_text(
            f"time rate\n0 {rate:.12g}\n7200 {rate:.12g}\n", encoding="ascii"
        )

    def _native_input_audit(self, spec: dict, case_dir: Path) -> dict:
        return {
            "z_field_sha256": hashlib.sha256(
                (case_dir / "native/input/field/z.dat").read_bytes()
            ).hexdigest(),
            "serialized_runtime_values": ["0", "7200", "60", "7200"],
            "declared_boundary": "fall",
            "serialized_initial_h_unique_values": [0.0],
            "serialized_initial_hU_max_abs_m2_per_s": 0.0,
            "serialized_manning_unique_values": [0.05],
            "serialized_sink_unique_values": {
                name: [0.0] for name in (
                    "sewer_sink", "cumulative_depth", "hydraulic_conductivity",
                    "capillary_head", "water_content_diff",
                )
            },
            "serialized_rain_mask_unique_values": [0.0],
        }


class NativeRainIntensityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.protocol = rain.read_json(rain.DEFAULT_PROTOCOL)

    def test_frozen_case_specs_change_only_name_purpose_and_rainfall(self):
        specs = rain.case_specs(self.protocol, (48, 120))
        self.assertEqual([item["name"] for item in specs], [
            "mountain-30m-7200s-rain48", "mountain-30m-7200s-rain120"
        ])
        for rate, spec in zip((48, 120), specs):
            self.assertEqual(spec["rain_rate_m_per_s"], rate / 3_600_000.0)
            changed = {key for key in self.protocol["baseline_case_spec"]
                       if spec[key] != self.protocol["baseline_case_spec"][key]}
            self.assertEqual(changed, {"name", "purpose", "rain_rate_m_per_s"})
        with self.assertRaises(rain.RainIntensityError):
            rain.rate_m_per_s(12)

    def test_refuses_existing_escape_and_symlink_case_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "worker"
            root.mkdir()
            with self.assertRaises(rain.RainIntensityError):
                rain.checked_output_root(root.parent / "escape", root)
            target = rain.case_output_path(root, "mountain-case")
            target.mkdir(parents=True)
            with self.assertRaises(rain.RainIntensityError):
                rain.assert_fresh_case_directory(target)
            real = root / "real"
            real.mkdir()
            link = root / "link"
            link.symlink_to(real, target_is_directory=True)
            with self.assertRaises(rain.RainIntensityError):
                rain.checked_output_root(link, root)
            with self.assertRaises(rain.RainIntensityError):
                rain.case_output_path(root, "../escape")

    def test_frozen_protocol_hash_and_immutable_input_audit_fail_closed(self):
        with self.assertRaises(rain.RainIntensityError):
            rain.load_protocol(rain.DEFAULT_PROTOCOL, "0" * 64)
        helper = FakeHelper()
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            baseline_rain = root / "baseline-rain.dat"
            baseline_rain.write_text("time rate\n0 3.33333333333e-06\n7200 3.33333333333e-06\n",
                                     encoding="ascii")
            baseline_hashes = {
                "field/h.dat": "same-hash",
                "field/z.dat": "same-bed-hash",
                "field/manning.dat": "baseline-manning",
                "field/precipitation_mask.dat": "same-mask",
                "field/precipitation_source_all.dat": "old-rain",
            }
            actual = dict(baseline_hashes)
            actual["field/manning.dat"] = "changed-manning"
            actual["field/precipitation_source_all.dat"] = "new-rain"
            case_rain = root / "case-rain.dat"
            case_rain.write_text("time rate\n0 1.33333333333e-05\n7200 1.33333333333e-05\n",
                                 encoding="ascii")
            with self.assertRaises(rain.RainIntensityError):
                rain.compare_native_inputs(
                    baseline_hashes, actual, helper=helper, baseline_rain_path=baseline_rain,
                    actual_rain_path=case_rain, expected_rate=48 / 3_600_000.0,
                )

    def test_changed_immutable_serialized_field_blocks_native_launch(self):
        helper = FakeHelper(change_manning=True)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            baseline_input = root / "baseline/native/input/field"
            baseline_input.mkdir(parents=True)
            (baseline_input / "h.dat").write_text("initial dry state\n", encoding="ascii")
            (baseline_input / "z.dat").write_text("immutable bed\n", encoding="ascii")
            (baseline_input / "manning.dat").write_text("0.05\n", encoding="ascii")
            (baseline_input / "precipitation_mask.dat").write_text("0\n", encoding="ascii")
            baseline_rain = baseline_input / "precipitation_source_all.dat"
            baseline_rain.write_text("time rate\n0 3.33333333333e-06\n7200 3.33333333333e-06\n",
                                     encoding="ascii")
            baseline_hashes = helper._input_hashes(root / "baseline/native/input")
            spec = rain.case_specs(self.protocol, (48,))[0]
            case_dir = root / "case"
            case_dir.mkdir()
            launched = []
            with self.assertRaises(rain.RainIntensityError):
                rain.run_child_with_preflight(
                    case_dir, spec, helper, baseline_hashes,
                    hashlib.sha256((baseline_input / "z.dat").read_bytes()).hexdigest(),
                    lambda native_dir: launched.append(native_dir),
                    synxflow_version="1.0.1", python_executable="synthetic-python",
                    baseline_rain_path=baseline_rain,
                )
            self.assertEqual(launched, [])
            self.assertFalse((case_dir / "native-preflight.json").exists())

    def test_actual_synxflow_serialized_preflight_passes_without_running_solver(self):
        if importlib.util.find_spec("synxflow") is None:
            self.skipTest("real serializer requires the existing pinned SynxFlow environment")
        protocol, protocol_hash = rain.load_protocol(rain.DEFAULT_PROTOCOL)
        helper = rain._load_reference_helper()
        context = rain.validate_workspace(protocol, helper)
        baseline_rain = rain.BASELINE_ROOT / "native/input/field/precipitation_source_all.dat"
        for rate in (48, 120):
            spec = rain.case_specs(protocol, (rate,))[0]
            with tempfile.TemporaryDirectory() as temp:
                case_dir = Path(temp) / spec["name"]
                case_dir.mkdir()
                shutil.copyfile(rain.BASELINE_ROOT / "DEM.asc", case_dir / "DEM.asc")
                helper._make_model_inputs(spec, case_dir)
                audit, hashes, comparison = rain.validate_native_input_audit(
                    spec, case_dir, helper, context["baseline_hashes"],
                    context["baseline_z_sha256"], baseline_rain_path=baseline_rain,
                )
                self.assertEqual(audit["z_field_sha256"], context["baseline_z_sha256"])
                self.assertEqual(comparison["changed_native_input_paths"], [
                    "field/precipitation_source_all.dat"
                ])
                self.assertEqual(hashes["field/z.dat"], context["baseline_z_sha256"])
                self.assertEqual(protocol_hash, rain.EXPECTED_PROTOCOL_SHA256)
                output_dir = case_dir / "native/output"
                self.assertFalse(any(output_dir.glob("h_*.asc")))
                self.assertFalse((output_dir / "timestep_log.txt").exists())

    def test_timeout_negative_depth_and_missing_exports_fail_health(self):
        self.assertEqual(
            rain.essential_process_failure(None, True, True, True, True, False)[0], "timeout"
        )
        self.assertEqual(
            rain.essential_process_failure(0, False, True, False, True, True)[0], "postrun_audit_missing"
        )
        self.assertEqual(rain.essential_process_failure(0, False, True, True, True, False)[0],
                         "success_marker_missing")
        helper = FakeHelper()
        helper.time_grid = lambda spec: [0.0, 60.0]
        spec = {
            "rows": 2, "cols": 2, "dx_m": 30.0, "duration_s": 60,
            "output_interval_s": 60, "rain_rate_m_per_s": 48 / 3_600_000.0,
        }
        dry = np.zeros((2, 2))
        wet = np.full((2, 2), spec["rain_rate_m_per_s"] * 60.0)
        fields0 = {"h": dry.copy(), "hUx": dry.copy(), "hUy": dry.copy()}
        fields60 = {"h": wet.copy(), "hUx": dry.copy(), "hUy": dry.copy()}
        good = rain.evaluate_export_health(spec, {0.0: fields0, 60.0: fields60}, helper)
        self.assertTrue(good["gate_passed"])
        negative = {key: value.copy() for key, value in fields60.items()}
        negative["h"][0, 0] = -0.001
        bad = rain.evaluate_export_health(spec, {0.0: fields0, 60.0: negative}, helper)
        self.assertFalse(bad["gate_passed"])
        missing = rain.evaluate_export_health(spec, {0.0: fields0}, helper)
        self.assertFalse(missing["gate_passed"])
        self.assertNotEqual(missing["actual_saved_export_count"], missing["expected_saved_export_count"])
        for field in ("h", "hUx", "hUy"):
            invalid = {key: value.copy() for key, value in fields60.items()}
            invalid[field][0, 0] = np.nan
            self.assertFalse(rain.evaluate_export_health(spec, {0.0: fields0, 60.0: invalid}, helper)["gate_passed"])
        dry_q = {key: value.copy() for key, value in fields60.items()}
        dry_q["h"][0, 0] = 0.0
        dry_q["hUx"][0, 0] = 1.0
        self.assertFalse(rain.evaluate_export_health(spec, {0.0: fields0, 60.0: dry_q}, helper)["gate_passed"])

    def test_phase_summary_keeps_nonpromotion_and_case_result_discovery(self):
        spec = rain.case_specs(self.protocol, (48,))[0]
        result = {
            "output_health": {"gate_passed": True, "actual_saved_export_count": 121,
                              "exact_dry_motionless_time_zero": True,
                              "storage_upper_bound_passed_at_all_exports": True},
            "metrics": {
                "global_observations": [{"time_s": 7200, "saved_water_volume_m3": 12.0}],
                "corridor_900s_support_criterion_met": False,
                "pond_15min_criteria_met_by_frozen_depression": {"label-20": False},
            },
        }
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            case_dir = out / "cases" / spec["name"]
            record = rain._summary_case(48, spec, case_dir, result, "healthy")
            summary = rain._write_phase_summary(out, rain.EXPECTED_PROTOCOL_SHA256,
                                                [record], "complete")
            saved = rain.read_json(out / "phase-summary.json")
        self.assertEqual(saved["cases"][0]["case_result_path"], record["case_result_path"])
        self.assertFalse(summary["cases"][0]["corridor_900s_support_criterion_met"])
        self.assertTrue(summary["cases"][0]["pond_classification_is_diagnostic_only"])
        self.assertEqual(summary["cases"][0]["status"], "healthy")


if __name__ == "__main__":
    unittest.main()
