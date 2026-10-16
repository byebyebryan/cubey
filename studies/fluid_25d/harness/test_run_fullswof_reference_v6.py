from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import run_fullswof_reference_v6 as runner


class NativeSeriesParserTests(unittest.TestCase):
    def write_series(self, path: Path, rows: list[str]) -> None:
        path.write_text("# time x y h u v\n" + "\n".join(rows) + "\n", encoding="ascii")

    def test_parses_ordered_complete_frames(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "series.dat"
            self.write_series(
                path,
                [
                    "0 15 15 0.2 1 0",
                    "0 15 45 0.3 2 0",
                    "0.1 15 15 0.21 1 0",
                    "0.1 15 45 0.31 2 0",
                ],
            )
            frames = runner.parse_specific_points(path, 2, ((15.0, 15.0), (15.0, 45.0)))
            self.assertEqual(tuple(sorted(frames)), (0.0, 0.1))
            self.assertEqual(frames[0.1][(15.0, 45.0)], (0.31, 2.0, 0.0))

    def test_rejects_coordinate_reordering(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "series.dat"
            self.write_series(path, ["0 15 45 .3 0 0", "0 15 15 .2 0 0"])
            with self.assertRaisesRegex(runner.ReferenceError, "coordinate order"):
                runner.parse_specific_points(path, 2, ((15.0, 15.0), (15.0, 45.0)))

    def test_rejects_nonincreasing_frame_times(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "series.dat"
            self.write_series(
                path,
                [
                    "0 15 15 .2 0 0", "0 15 45 .2 0 0",
                    ".1 15 15 .2 0 0", ".1 15 45 .2 0 0",
                    ".05 15 15 .2 0 0", ".05 15 45 .2 0 0",
                ],
            )
            with self.assertRaisesRegex(runner.ReferenceError, "strictly increasing"):
                runner.parse_specific_points(path, 2)

    def test_rejects_nonfinite_and_incomplete_frames(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "series.dat"
            self.write_series(path, ["0 15 15 nan 0 0", "0 15 45 .2 0 0"])
            with self.assertRaisesRegex(runner.ReferenceError, "non-finite"):
                runner.parse_specific_points(path, 2)
            self.write_series(path, ["0 15 15 .2 0 0", ".1 15 15 .2 0 0", ".1 15 45 .2 0 0"])
            with self.assertRaisesRegex(runner.ReferenceError, "ended before all expected cells"):
                runner.parse_specific_points(path, 2)


class SafetyAndPrecisionTests(unittest.TestCase):
    def test_output_leaf_refuses_traversal_absolute_and_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            self.assertEqual(runner._fresh_output_path(root, "fresh/leaf"), root / "fresh/leaf")
            with self.assertRaisesRegex(runner.ReferenceError, "escapes"):
                runner._fresh_output_path(root, "../escape")
            with self.assertRaisesRegex(runner.ReferenceError, "relative"):
                runner._fresh_output_path(root, str(root / "absolute"))
            occupied = root / "occupied"
            occupied.mkdir()
            with self.assertRaisesRegex(runner.ReferenceError, "overwrite"):
                runner._fresh_output_path(root, "occupied")

    def test_rain_ledger_uses_strict_uncertainty_interval(self) -> None:
        budget = 1.0e-6
        self.assertEqual(runner._ledger_classification(0.4e-6, 0.5e-6, budget)[0], "definite_pass_with_text_rounding_bound")
        self.assertEqual(runner._ledger_classification(1.2e-6, 0.3e-6, budget)[0], "inconclusive_rounding_interval_overlaps_budget")
        self.assertEqual(runner._ledger_classification(1.6e-6, 0.3e-6, budget)[0], "definite_failure_beyond_text_rounding_bound")
        self.assertLess(runner._ledger_classification(0.4e-6, 0.5e-6, budget)[1], 0.0)

    def test_rain_depth_rounding_bound_is_local_to_magnitude(self) -> None:
        self.assertTrue(runner._depth_within_precision(0.0020000000001, 0.002))
        # This difference fits the 0.002 m text envelope, but not the
        # substantially tighter envelope for a 0.0001 m frame.
        self.assertFalse(runner._depth_within_precision(0.0001000000001, 0.0001))


class LakeGateTests(unittest.TestCase):
    def make_frames(self, spec: runner.CaseSpec) -> dict[float, dict[tuple[float, float], tuple[float, float, float]]]:
        frame = {
            (x, y): (spec.reference_depths[i][j], 0.0, 0.0)
            for i, j in spec.measured_cells
            for x, y in [((i + 0.5) * spec.dx, (j + 0.5) * spec.dx)]
        }
        return {
            step * spec.timestep: dict(frame)
            for step in range(round(spec.horizon / spec.timestep) + 1)
        }

    def test_lake_uses_absolute_depth_and_velocity_limits(self) -> None:
        spec = runner._lake_case(3.0)
        frames = self.make_frames(spec)
        result = runner._measure_lake(spec, frames)
        self.assertTrue(result["depth_pass"])
        self.assertTrue(result["velocity_pass"])
        self.assertEqual(result["observations"]["all_saved_states"]["max_absolute_depth_drift_m"], 0.0)

        last_time = max(frames)
        first_cell = spec.measured_cells[0]
        x = (first_cell[0] + 0.5) * spec.dx
        y = (first_cell[1] + 0.5) * spec.dx
        frames[last_time][(x, y)] = (spec.reference_depths[first_cell[0]][first_cell[1]] + 1.1e-5, 0.0, 0.0)
        failed = runner._measure_lake(spec, frames)
        self.assertFalse(failed["depth_pass"])
        self.assertTrue(failed["velocity_pass"])

    def test_lake_requires_every_step_and_reports_rewetting(self) -> None:
        spec = runner._lake_case(0.5)
        frames = self.make_frames(spec)
        result = runner._measure_lake(spec, frames)
        summary = result["observations"]["all_saved_states"]
        self.assertGreater(summary["initially_dry_cell_count"], 0)
        self.assertEqual(max(summary["dry_rewet_count_by_time"].values()), 0)
        frames.pop(spec.timestep)
        missing_step = runner._measure_lake(spec, frames)
        self.assertFalse(missing_step["saved_state_coverage"]["every_fixed_step_saved"])
        self.assertFalse(missing_step["depth_pass"])


class BindingAndErrorRetentionTests(unittest.TestCase):
    @staticmethod
    def make_binding_fixture(root: Path) -> None:
        repo = root.resolve()
        setup = repo / runner.STUDY_REL / "reference-setup"
        source_root = setup / "source" / "FullSWOF_2D-1.10.00"
        build_root = setup / "build" / "FullSWOF_2D-1.10.00"
        source_root.mkdir(parents=True)
        build_root.mkdir(parents=True)
        archive = setup / "download" / "FullSWOF_2D-1.10.00.zip"
        archive.parent.mkdir(parents=True)
        archive.write_bytes(b"synthetic-source-archive")
        source_file = source_root / "source.cpp"
        source_file.write_text("unmodified source fixture\n", encoding="ascii")
        build_file = build_root / "build-source.cpp"
        build_file.write_text("same archived source fixture\n", encoding="ascii")
        source_manifest = setup / "source-tree.sha256"
        source_manifest.write_text(
            f"{runner._sha256(source_file)}  {source_file.relative_to(repo).as_posix()}\n",
            encoding="ascii",
        )
        build_manifest = setup / "build-copy-tree.sha256"
        build_manifest.write_text(f"{runner._sha256(build_file)}  ./build-source.cpp\n", encoding="ascii")
        bin_dir = build_root / "bin"
        bin_dir.mkdir()
        release = bin_dir / "FullSWOF_2D"
        observer = bin_dir / "FullSWOF_2D_observer"
        release.write_bytes(b"release binary fixture")
        observer.write_bytes(b"observer binary fixture")
        lock = {
            "archive_path": archive.relative_to(repo).as_posix(),
            "archive_sha256": runner._sha256(archive),
            "source_tree_manifest_path": source_manifest.relative_to(repo).as_posix(),
            "source_tree_manifest_sha256": runner._sha256(source_manifest),
            "build_copy_manifest_path": build_manifest.relative_to(repo).as_posix(),
            "build_copy_manifest_sha256": runner._sha256(build_manifest),
            "source_tree_path": source_root.relative_to(repo).as_posix(),
            "build_tree_path": build_root.relative_to(repo).as_posix(),
            "native_cli": {"path": release.relative_to(repo).as_posix(), "sha256": runner._sha256(release)},
            "observer_cli": {"path": observer.relative_to(repo).as_posix(), "sha256": runner._sha256(observer)},
        }
        lock_path = setup / "native-lock.json"
        lock_path.write_text(json.dumps(lock), encoding="utf-8")

    @staticmethod
    def binding_stub(root: Path) -> dict[str, object]:
        return {
            "source_root": root / "source",
            "build_root": root / "build",
            "binary": root / "FullSWOF_2D_observer",
            "release_binary": root / "FullSWOF_2D",
            "archive_sha256": "archive-fixture",
            "source_manifest_sha256": "source-manifest-fixture",
            "binary_sha256": "observer-fixture",
            "release_binary_sha256": "release-fixture",
            "source_file_count": 1,
            "build_file_count": 1,
        }

    def test_synthetic_archive_source_and_binary_binding_verifies_and_rejects_change(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            self.make_binding_fixture(root)
            binding = runner.verify_binding(root)
            self.assertEqual(binding["source_file_count"], 1)
            self.assertEqual(binding["build_file_count"], 1)
            self.assertEqual(runner._sha256(binding["binary"]), binding["binary_sha256"])
            binding["binary"].write_bytes(b"changed binary fixture")
            with self.assertRaisesRegex(runner.ReferenceError, "observer binary SHA-256 mismatch"):
                runner.verify_binding(root)

    def call_case_with_fixture(self, temporary: str, native_result: object, frames: dict | None = None):
        spec = runner._curved_case(30.0, 0.05)
        root = Path(temporary)
        binding = self.binding_stub(root)

        def fake_write_inputs(_spec, case_dir: Path, _source_root: Path) -> dict[str, str]:
            inputs = case_dir / "Inputs"
            inputs.mkdir()
            (case_dir / "Outputs").mkdir()
            sentinel = inputs / "fixture.txt"
            sentinel.write_text("fixture input\n", encoding="ascii")
            return {sentinel.name: runner._sha256(sentinel)}

        native_is_exception = isinstance(native_result, BaseException)
        with patch.object(runner, "verify_binding", return_value=binding), patch.object(
            runner, "_write_inputs", side_effect=fake_write_inputs
        ), patch.object(
            runner.subprocess,
            "run",
            side_effect=native_result if native_is_exception else None,
            return_value=None if native_is_exception else native_result,
        ):
            if frames is None:
                parse_patch = patch.object(runner, "parse_specific_points")
            else:
                parse_patch = patch.object(runner, "parse_specific_points", return_value=frames)
            with parse_patch:
                return runner.run_case(spec, output_relative="failure-leaf", study_root_override=root)

    def test_timeout_and_nonzero_native_raw_streams_are_retained(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            timed_out = subprocess.TimeoutExpired(["fake-native"], 60, output=b"partial out", stderr=b"partial err")
            case_dir, report, result = self.call_case_with_fixture(temporary, timed_out)
            self.assertEqual(result, 1)
            self.assertIn("exceeded case timeout", report["native_error"])
            self.assertEqual((case_dir / "native.stdout.bin").read_bytes(), b"partial out")
            self.assertEqual((case_dir / "native.stderr.bin").read_bytes(), b"partial err")

        with tempfile.TemporaryDirectory() as temporary:
            completed = subprocess.CompletedProcess(["fake-native"], 7, b"nonzero out", b"native diagnostic")
            case_dir, report, result = self.call_case_with_fixture(temporary, completed)
            self.assertEqual(result, 1)
            self.assertEqual(report["native_exit_code"], 7)
            self.assertEqual((case_dir / "native.stdout.bin").read_bytes(), b"nonzero out")
            self.assertEqual((case_dir / "native.stderr.bin").read_bytes(), b"native diagnostic")

    def test_measurement_error_and_raw_streams_are_retained(self) -> None:
        frames = {0.0: {}}
        completed = subprocess.CompletedProcess(["fake-native"], 0, b"native out", b"native err")
        with tempfile.TemporaryDirectory() as temporary:
            case_dir, report, result = self.call_case_with_fixture(temporary, completed, frames)
            self.assertEqual(result, 1)
            self.assertIn("requested observation 2.0s is absent", report["parse_error"])
            self.assertTrue((case_dir / "report.json").is_file())
            self.assertEqual((case_dir / "native.stdout.bin").read_bytes(), b"native out")
            self.assertEqual((case_dir / "native.stderr.bin").read_bytes(), b"native err")
            saved_report = json.loads((case_dir / "report.json").read_text(encoding="utf-8"))
            self.assertEqual(saved_report["full_accuracy_gate_status"], "native_run_or_parse_failed")
            self.assertEqual(saved_report["raw_stderr_sha256"], runner._sha256(case_dir / "native.stderr.bin"))


if __name__ == "__main__":
    unittest.main()
