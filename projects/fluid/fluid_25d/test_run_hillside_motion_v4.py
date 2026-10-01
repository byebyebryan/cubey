"""Pure protocol and validation tests for the bounded V4 evidence runner."""

from decimal import Decimal
from fractions import Fraction
import io
import json
from pathlib import Path
import tempfile
import unittest
from contextlib import ExitStack, redirect_stderr, redirect_stdout
from unittest.mock import patch

import run_hillside_motion_v4 as motion
import run_hillside_sustained_flow_v2 as v2


def hydraulic_frame(frame: int) -> dict[tuple[str, str], Decimal]:
    return {
        (motion.HYDRAULIC_IDENTITY, "hash_hi_u32"): Decimal(1000 + frame),
        (motion.HYDRAULIC_IDENTITY, "hash_lo_u32"): Decimal(2000 + frame),
        (motion.WATER, "total_water_volume_m3"): Decimal(300 + frame),
        ("fluid_25d.solver.timing", "step_elapsed_ms"): Decimal(1),
    }


def marker_profiles(baseline: dict[int, dict[tuple[str, str], Decimal]]):
    current = {frame: dict(values) for frame, values in baseline.items()}
    for frame, values in current.items():
        values[("fluid_25d.motion_markers", "active_count")] = Decimal(frame + 2)
        values[("fluid_25d.motion_markers", "farthest_from_source_m")] = Decimal(frame + 10)
        values[("fluid_25d.motion_markers", "mean_from_source_m")] = Decimal(frame + 5)
        values[("fluid_25d.motion_markers", "maximum_age_s")] = Decimal(frame + 2)
        values[("fluid_25d.motion_markers", "hash_hi_u32")] = Decimal(99 + frame)
        values[("fluid_25d.motion_markers", "hash_lo_u32")] = Decimal(199 + frame)
    return current


def budget_frames(frame_count: int = 2) -> dict[int, dict[tuple[str, str], Decimal]]:
    result = {}
    for frame in range(frame_count):
        source = Decimal(v2.Q_M3_PER_S * v2.DT * (frame + 1))
        result[frame] = {
            (motion.SOLVER, "finite_volume_status_flags"): Decimal(0),
            (motion.WATER, "cumulative_source_volume_m3"): source,
            (motion.WATER, "cumulative_sink_volume_m3"): Decimal(0),
            (motion.WATER, "cumulative_boundary_outflow_volume_m3"): Decimal(0),
            (motion.WATER, "total_water_volume_m3"): source,
            (motion.WATER, "conservation_residual_m3"): Decimal(0),
        }
    return result


class FullProfileComparisonTests(unittest.TestCase):
    def test_exact_common_metrics_allow_only_marker_additions_and_ignore_timings(self):
        baseline = {frame: hydraulic_frame(frame) for frame in range(3)}
        current = marker_profiles(baseline)
        for frame, values in current.items():
            values[("fluid_25d.motion_markers", "gpu_time_ms")]=Decimal(40 + frame)
            values[("fluid_25d.solver.timing", "step_elapsed_ms")]=Decimal(99 + frame)

        report = motion.compare_full_common_profiles(baseline, current, frame_count=3)

        self.assertTrue(report["passed"])
        self.assertEqual(report["frames_compared"], 3)
        self.assertEqual(report["current_only_metric_category_allowed"],
                         "fluid_25d.motion_markers")
        self.assertEqual(report["metric_values_compared"], 3 * 3)
        self.assertIn(["fluid_25d.motion_markers", "active_count"],
                      report["current_only_metric_keys"])

    def test_changed_common_metric_or_hash_word_fails_exact_comparison(self):
        baseline = {frame: hydraulic_frame(frame) for frame in range(2)}
        current = marker_profiles(baseline)
        current[1][(motion.WATER, "total_water_volume_m3")] += Decimal("0.0000001")
        with self.assertRaisesRegex(ValueError, "numerical profile metrics differ"):
            motion.compare_full_common_profiles(baseline, current, frame_count=2)

        current = marker_profiles(baseline)
        current[0][(motion.HYDRAULIC_IDENTITY, "hash_hi_u32")] += Decimal(1)
        with self.assertRaisesRegex(ValueError, "numerical profile metrics differ"):
            motion.compare_full_common_profiles(baseline, current, frame_count=2)

    def test_profile_comparison_rejects_missing_frames_or_metrics(self):
        baseline = {frame: hydraulic_frame(frame) for frame in range(2)}
        with self.assertRaisesRegex(ValueError, "every requested fixed-step frame"):
            motion.compare_full_common_profiles(baseline, {0: hydraulic_frame(0)}, frame_count=2)
        current = marker_profiles(baseline)
        current[1].pop((motion.WATER, "total_water_volume_m3"))
        with self.assertRaisesRegex(ValueError, "omits baseline numerical metrics"):
            motion.compare_full_common_profiles(baseline, current, frame_count=2)

    def test_profile_comparison_rejects_non_marker_current_only_numerics(self):
        baseline = {0: hydraulic_frame(0)}
        current = marker_profiles(baseline)
        current[0][("fluid_25d.unexpected", "new_metric")]=Decimal(1)
        with self.assertRaisesRegex(ValueError, "non-marker numerical metrics"):
            motion.compare_full_common_profiles(baseline, current, frame_count=1)

    def test_marker_profile_must_record_numerics_on_every_frame(self):
        baseline = {frame: hydraulic_frame(frame) for frame in range(2)}
        current = marker_profiles(baseline)
        for name in motion.MARKER_METRICS:
            current[1].pop((motion.MARKER_METRIC_CATEGORY, name))
        with self.assertRaisesRegex(ValueError, "missing required state metrics at frame 1"):
            motion.compare_full_common_profiles(baseline, current, frame_count=2)


class WaterBudgetTests(unittest.TestCase):
    def test_fixed_source_and_independent_residual_ledger_pass(self):
        report = motion.validate_water_budget_profile(budget_frames(), frame_count=2)
        self.assertTrue(report["passed"])
        self.assertEqual(report["explicit_sink_m3"], 0)
        self.assertEqual(report["nonzero_solver_flag_frames"], [])

    def test_water_budget_rejects_flags_source_drift_and_ledger_error(self):
        frames = budget_frames()
        frames[1][(motion.SOLVER, "finite_volume_status_flags")] = Decimal(1)
        with self.assertRaisesRegex(ValueError, "nonzero finite-volume status flags"):
            motion.validate_water_budget_profile(frames, frame_count=2)

        frames = budget_frames()
        frames[0][(motion.WATER, "cumulative_source_volume_m3")] += Decimal(1)
        with self.assertRaisesRegex(ValueError, "frozen 100 m3/s schedule"):
            motion.validate_water_budget_profile(frames, frame_count=2)

        frames = budget_frames()
        frames[0][(motion.WATER, "total_water_volume_m3")] += Decimal(1)
        frames[0][(motion.WATER, "conservation_residual_m3")] = Decimal(1)
        with self.assertRaisesRegex(ValueError, "exceeds the existing 3e-4 tolerance"):
            motion.validate_water_budget_profile(frames, frame_count=2)


class CaptureProtocolTests(unittest.TestCase):
    def test_source_marker_on_off_commands_are_matched_except_for_overlay_flag(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "source.mp4"
            on = motion.capture_arguments("source", "-0.72", True, output, frame_count=60)
            off = motion.capture_arguments("source", "-0.72", False, output, frame_count=60)
        self.assertIn("--fluid25d-motion-markers", on)
        self.assertNotIn("--fluid25d-motion-markers", off)
        on.remove("--fluid25d-motion-markers")
        self.assertEqual(on, off)
        self.assertEqual(off[off.index("--fluid25d-dye-pulse-start-seconds") + 1], "3600")
        self.assertEqual(off[off.index("--fluid25d-dye-pulse-duration-seconds") + 1], "120")
        self.assertEqual(off[off.index("--frames") + 1], "60")

    def test_source_topdown_and_branch_are_explicit_and_invalid_combinations_fail(self):
        self.assertIn(("source-topdown-markers", "source", "-1.55", True, "Source topdown"),
                      motion.VIDEO_CASES)
        with tempfile.TemporaryDirectory() as temp:
            topdown = motion.capture_arguments("source", "-1.55", True,
                                               Path(temp) / "topdown.mp4")
            branch = motion.capture_arguments("branch", "-0.72", True,
                                              Path(temp) / "branch.mp4")
        self.assertEqual(topdown[topdown.index("--fluid25d-hillside-camera") + 1], "source")
        self.assertEqual(topdown[topdown.index("--fluid25d-natural-flow-home-pitch-radians") + 1],
                         "-1.55")
        self.assertEqual(branch[branch.index("--fluid25d-hillside-camera") + 1], "branch")
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaises(ValueError):
                motion.capture_arguments("branch", "-1.55", True, Path(temp) / "bad.mp4")
            with self.assertRaises(ValueError):
                motion.capture_arguments("overview", "-0.72", True, Path(temp) / "bad.mp4")

    def test_excerpts_keep_physical_to_video_time_mapping_and_smoke_is_not_full_gate(self):
        self.assertEqual(motion.PLAYBACK_ACCELERATION, 60)
        self.assertEqual([
            (label, start / motion.PLAYBACK_ACCELERATION,
             end / motion.PLAYBACK_ACCELERATION,
             (end - start) // motion.PLAYBACK_ACCELERATION * motion.FPS)
            for label, start, end in motion.EXCERPT_SPECS
        ], [
            ("initial-00-15min", 0, 15, 450),
            ("mature-30-45min", 30, 45, 450),
            ("pulse-55-85min", 55, 85, 900),
        ])
        video_filter = motion._video_filter("Source topdown", physical_horizon=7200,
                                            markers=True)
        self.assertIn("n*2+2", video_filter)
        self.assertIn(r"Water source\: 100", video_filter)
        self.assertIn(r"%{eif\:n*2+2\:d}", video_filter)
        self.assertIn("0.01%", video_filter)
        self.assertIn("expansion=none", video_filter)
        excerpt_filter = (
            "trim=start_frame=1649:end_frame=2549,setpts=PTS-STARTPTS," +
            motion._drawtext_clause("Excerpt: pulse | physical window 55-85 min", 660))
        self.assertIn(r"Excerpt\: pulse", excerpt_filter)
        smoke_filter = motion._video_filter("Source", physical_horizon=7200,
                                            markers=False, smoke=True)
        self.assertIn("SMOKE QA ONLY", smoke_filter)
        self.assertIn("Motion markers OFF", smoke_filter)
        self.assertEqual(motion.SMOKE_FRAME_COUNT, 60)

    def test_phase_directory_is_exclusive_but_existing_root_is_allowed(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "evidence"
            phase = motion.create_phase_directory(output, "profiles")
            self.assertTrue(phase.is_dir())
            with self.assertRaises(FileExistsError):
                motion.create_phase_directory(output, "profiles")

            v3_root = Path(temp) / "retained-v3"
            v3_root.mkdir()
            with patch.object(motion, "V3_ROOT", v3_root):
                with self.assertRaisesRegex(ValueError, "must not be inside"):
                    motion.create_phase_directory(v3_root / "v4", "captures")

    def test_matched_overlay_validation_ignores_only_distinct_output_paths(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            cases = []
            for label, markers in (("source-markers", True), ("source-markers-off", False)):
                output = folder / f"{label}.mp4"
                command = ["rtk", "proxy", "/fake/fluid", *motion.capture_arguments(
                    "source", "-0.72", markers, output, frame_count=60)]
                cases.append({"label": label, "command": command,
                              "raw_video_path": str(output.resolve()),
                              "before": {"app_sha256": "same"},
                              "after": {"app_sha256": "same"}})
            motion._validate_matched_overlay_control(cases)
            cases[1]["command"][cases[1]["command"].index("--fluid25d-fixed-delta-seconds") + 1] = "3"
            with self.assertRaisesRegex(ValueError, "differ by more than"):
                motion._validate_matched_overlay_control(cases)

    def test_smoke_phase_keeps_same_512_identity_at_start_and_end(self):
        identity = {"app_sha256": "stable"}

        def fake_capture(app, case_dir, label, camera, pitch, markers, *, frame_count):
            output = case_dir / f"{label}.mp4"
            command = ["rtk", "proxy", str(app), *motion.capture_arguments(
                camera, pitch, markers, output, frame_count=frame_count)]
            return {
                "label": label,
                "before": identity,
                "after": identity,
                "command": command,
                "raw_video_path": str(output.resolve()),
                "raw_video_probe": {"validated": True},
                "labelled_video": {"probe": {"validated": True}},
            }

        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "evidence"
            with patch.object(motion, "_execution_identity", return_value=identity) as pinned, \
                    patch.object(motion, "_capture_one", side_effect=fake_capture) as capture:
                report = motion.smoke_phase(Path("/fake/fluid"), out)
        self.assertTrue(report["phase_checks_passed"])
        self.assertFalse(report["full_horizon_gate_satisfied"])
        self.assertEqual([call.args[1] for call in pinned.call_args_list], [512, 512])
        self.assertNotEqual(capture.call_args_list[0].args[1], capture.call_args_list[1].args[1])

    def test_video_probe_requires_exact_frame_count_dimensions_and_cadence(self):
        metadata = {
            "streams": [{"codec_name": "h264", "width": 1280, "height": 720,
                         "r_frame_rate": "30/1", "avg_frame_rate": "1800/59", "duration": "1.966667",
                         "nb_read_frames": "60"}],
            "format": {"duration": "1.966667"},
            "frames": [{"pts_time": f"{index / 30:.6f}"} for index in range(60)],
        }
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            video = folder / "sample.mp4"
            video.write_bytes(b"synthetic media placeholder")
            with patch.object(motion.subprocess, "run") as run:
                run.return_value.returncode = 0
                run.return_value.stdout = json.dumps(metadata)
                run.return_value.stderr = ""
                valid = motion.probe_video(video, folder / "probe.log", 60)
            self.assertTrue(valid["validated"])
            self.assertEqual(valid["frame_count"], 60)
            self.assertEqual(valid["fps"], 30)
            self.assertGreater(valid["container_average_fps"], 30)

        metadata["streams"][0]["nb_read_frames"] = "59"
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            video = folder / "sample.mp4"
            video.write_bytes(b"synthetic media placeholder")
            with patch.object(motion.subprocess, "run") as run:
                run.return_value.returncode = 0
                run.return_value.stdout = json.dumps(metadata)
                run.return_value.stderr = ""
                with self.assertRaisesRegex(ValueError, "invalid dimensions, cadence, duration, or frame count"):
                    motion.probe_video(video, folder / "probe.log", 60)

        metadata["streams"][0]["nb_read_frames"] = "60"
        metadata["frames"][20]["pts_time"] = "0.900000"
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            video = folder / "sample.mp4"
            video.write_bytes(b"synthetic media placeholder")
            with patch.object(motion.subprocess, "run") as run:
                run.return_value.returncode = 0
                run.return_value.stdout = json.dumps(metadata)
                run.return_value.stderr = ""
                with self.assertRaisesRegex(ValueError, "fixed-cadence frame timestamps"):
                    motion.probe_video(video, folder / "probe.log", 60)

    def test_source_only_is_scoped_to_capture_phase(self):
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            motion.main(["--phase", "review", "--source-only"])


class MarkerFilteredDyeValidationTests(unittest.TestCase):
    def test_dye_gate_filter_removes_only_motion_marker_category(self):
        metrics = {
            0: {
                (motion.WATER, "total_water_volume_m3"): Decimal(12),
                (motion.TRACER, "total_tracer_amount_m3"): Decimal(2),
                (motion.MARKER_METRIC_CATEGORY, "active_count"): Decimal(4),
                (motion.MARKER_METRIC_CATEGORY, "gpu_update_ms"): Decimal(0.5),
                ("fluid_25d.unexpected", "future_metric"): Decimal(33),
            },
            1: {
                (motion.WATER, "conservation_residual_m3"): Decimal("0.001"),
                (motion.MARKER_METRIC_CATEGORY, "hash_hi_u32"): Decimal(7),
            },
        }
        reference = {0: {("reference", "unchanged"): Decimal(1)}}
        expected = {
            frame: {key: value for key, value in values.items()
                    if key[0] != motion.MARKER_METRIC_CATEGORY}
            for frame, values in metrics.items()
        }

        with patch.object(motion.v3, "validate_dye_profile", return_value={"passed": True}) as gate:
            result = motion.validate_dye_without_marker_metrics(metrics, reference)

        self.assertEqual(result, {"passed": True})
        self.assertEqual(gate.call_args.args, (expected, reference))
        self.assertIn(("fluid_25d.unexpected", "future_metric"), gate.call_args.args[0][0])
        self.assertIn((motion.TRACER, "total_tracer_amount_m3"), gate.call_args.args[0][0])
        self.assertNotIn((motion.MARKER_METRIC_CATEGORY, "hash_hi_u32"), gate.call_args.args[0][1])
        self.assertNotIn((motion.MARKER_METRIC_CATEGORY, "gpu_update_ms"), gate.call_args.args[0][0])
        self.assertIn((motion.MARKER_METRIC_CATEGORY, "gpu_update_ms"), metrics[0])


class ProfileRevalidationTests(unittest.TestCase):
    def _workspace(self, root: Path) -> tuple[Path, Path, dict[int, dict[str, str]]]:
        out = root / "evidence"
        phase = out / "profiles"
        phase.mkdir(parents=True)
        app = root / "fluid_25d"
        identity_by_domain = {
            256: {"app_sha256": "app", "shader_map_sha256": "shader", "recipe": "256"},
            512: {"app_sha256": "app", "shader_map_sha256": "shader", "recipe": "512"},
        }
        motion.write_report(phase / "profiles.json", {
            "schema": "cubey.fluid25d.hillside_motion_v4.profiles",
            "phase": "profiles",
            "phase_checks_passed": False,
            "started_before_app_identity": identity_by_domain[512],
            "cases": [],
        })
        for label, domain, dye in motion.PROFILE_CASES:
            case_dir = phase / label
            case_dir.mkdir()
            prefix = case_dir / "profile"
            output = case_dir / "final.png"
            log = case_dir / "child.log"
            artifacts = motion._profile_artifact_paths(case_dir, prefix, output, log)
            artifact_paths = {}
            artifact_hashes = {}
            for name, path in artifacts.items():
                path.write_bytes(f"{label}:{name}".encode())
                artifact_paths[name] = str(path.resolve())
                artifact_hashes[name] = motion.sha256_file(path)
            receipt = {
                "schema": "cubey.fluid25d.hillside_motion_v4.execution",
                "label": label,
                "domain": domain,
                "frames": motion.FRAME_COUNT,
                "interval": 1,
                "dye": dye,
                "motion_markers": True,
                "command": ["rtk", "proxy", str(app.resolve()),
                            *motion.profile_arguments(domain, dye, prefix, output)],
                "exit_code": 0,
                "before": identity_by_domain[domain],
                "after": identity_by_domain[domain],
                "artifact_paths": artifact_paths,
                "artifact_hashes": artifact_hashes,
            }
            motion.write_report(case_dir / "execution.json", receipt)
        return out, app, identity_by_domain

    def _patch_revalidation(self, stack: ExitStack,
                            identity_by_domain: dict[int, dict[str, str]]) -> None:
        stack.enter_context(patch.object(
            motion, "_execution_identity", side_effect=lambda app, domain: identity_by_domain[domain]))
        stack.enter_context(patch.object(motion, "load_v3_baselines", return_value={"review": {}}))
        stack.enter_context(patch.object(motion, "_validate_profile_case", side_effect=lambda label, receipt, baselines: {
            "label": label,
            "domain": receipt["domain"],
            "dye": receipt["dye"],
            "motion_markers": True,
            "profile_metrics_path": receipt["artifact_paths"]["profile.metrics.csv"],
            "profile_metrics_sha256": receipt["artifact_hashes"]["profile.metrics.csv"],
            "v3_full_profile_comparison": {"passed": True},
        }))
        stack.enter_context(patch.object(motion, "_solver_bridge", return_value={"passed": True}))
        stack.enter_context(patch.object(motion.v2, "pinned_inputs", return_value={}))

    def _mutate_first_receipt(self, out: Path, mutate) -> None:
        label = motion.PROFILE_CASES[0][0]
        path = out / "profiles" / label / "execution.json"
        receipt = motion.read_report(path)
        mutate(receipt, out / "profiles" / label)
        motion.write_report(path, receipt)

    def _assert_revalidation_refused(self, mutate, pattern: str) -> None:
        with tempfile.TemporaryDirectory() as temp:
            out, app, identities = self._workspace(Path(temp))
            self._mutate_first_receipt(out, mutate)
            with ExitStack() as stack:
                self._patch_revalidation(stack, identities)
                with self.assertRaisesRegex((ValueError, FileNotFoundError), pattern):
                    motion.revalidate_profiles_phase(app, out)
            self.assertFalse((out / "profiles" / "revalidated-profiles.json").exists())

    def test_revalidation_accepts_only_complete_unchanged_successful_receipts(self):
        with tempfile.TemporaryDirectory() as temp:
            out, app, identities = self._workspace(Path(temp))
            original_path = out / "profiles" / "profiles.json"
            original_sha = motion.sha256_file(original_path)
            with ExitStack() as stack:
                self._patch_revalidation(stack, identities)
                report = motion.revalidate_profiles_phase(app, out)

            revalidated_path = out / "profiles" / "revalidated-profiles.json"
            self.assertTrue(report["phase_checks_passed"])
            self.assertEqual(len(report["cases"]), len(motion.PROFILE_CASES))
            self.assertEqual(report["retained_rejected_phase"], {
                "path": str(original_path.resolve()), "sha256": original_sha})
            self.assertEqual(motion.sha256_file(original_path), original_sha)
            self.assertFalse(motion.read_report(original_path)["phase_checks_passed"])
            self.assertTrue(revalidated_path.is_file())

    def test_revalidation_refuses_incomplete_artifact_map(self):
        def mutate(receipt, _case_dir):
            receipt["artifact_hashes"].pop("profile.trace.json")
        self._assert_revalidation_refused(mutate, "incomplete, changed, or unsuccessful")

    def test_revalidation_refuses_nonzero_or_changed_app_receipts(self):
        self._assert_revalidation_refused(
            lambda receipt, _case: receipt.update(exit_code=17),
            "incomplete, changed, or unsuccessful")
        self._assert_revalidation_refused(
            lambda receipt, _case: receipt.update(before={"app_sha256": "changed"}),
            "incomplete, changed, or unsuccessful")

    def test_revalidation_refuses_artifact_bytes_changed_after_receipt(self):
        def mutate(receipt, case_dir):
            (case_dir / "final.png").write_bytes(b"changed after the receipt")
        self._assert_revalidation_refused(mutate, "artifact changed")

    def test_revalidation_never_overwrites_existing_recovery_report(self):
        with tempfile.TemporaryDirectory() as temp:
            out, app, identities = self._workspace(Path(temp))
            report_path = out / "profiles" / "revalidated-profiles.json"
            report_path.write_text("retained recovery report\n")
            with ExitStack() as stack:
                self._patch_revalidation(stack, identities)
                with self.assertRaises(FileExistsError):
                    motion.revalidate_profiles_phase(app, out)
            self.assertEqual(report_path.read_text(), "retained recovery report\n")

    def test_revalidate_profiles_flag_is_accepted_only_for_profiles_phase(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "evidence"
            app = Path(temp) / "fluid_25d"
            result = {"phase_checks_passed": True, "report_path": "unused", "report_sha256": "sha"}
            with patch.object(motion, "revalidate_profiles_phase", return_value=result) as revalidate, \
                    redirect_stdout(io.StringIO()):
                self.assertEqual(motion.main(["--phase", "profiles", "--revalidate-profiles",
                                              "--app", str(app), "--out", str(out)]), 0)
            revalidate.assert_called_once_with(app.resolve(), out.resolve())
        for phase in ("smoke", "captures", "review"):
            with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                motion.main(["--phase", phase, "--revalidate-profiles"])


if __name__ == "__main__":
    unittest.main()
