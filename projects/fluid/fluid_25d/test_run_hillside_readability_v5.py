"""Pure protocol and evidence-validation tests for the V5 hillside runner."""

from decimal import Decimal
import tempfile
import threading
from pathlib import Path
import unittest
from unittest.mock import patch

import run_hillside_readability_v5 as v5
import run_hillside_sustained_flow_v2 as v2


def baseline_frame(frame: int) -> dict[tuple[str, str], Decimal]:
    return {
        ("fluid_25d.hydraulic_identity", "hash_hi_u32"): Decimal(1000 + frame),
        ("fluid_25d.hydraulic_identity", "hash_lo_u32"): Decimal(2000 + frame),
        ("fluid_25d.water", "total_water_volume_m3"): Decimal(300 + frame),
        ("fluid_25d.solver.timing", "step_elapsed_ms"): Decimal(1),
    }


def current_frame(frame: int, *, response: bool = False) -> dict[tuple[str, str], Decimal]:
    values = baseline_frame(frame)
    rate = v5._rate_at(Decimal(frame * v2.DT), response)
    scheduled = sum((v5._rate_at(Decimal(i * v2.DT), response) * v2.DT
                     for i in range(frame + 1)), Decimal(0))
    values.update({
        (v5.MARKER_CATEGORY, "active_count"): Decimal(1),
        (v5.MARKER_CATEGORY, "farthest_from_source_m"): Decimal(10),
        (v5.MARKER_CATEGORY, "mean_from_source_m"): Decimal(5),
        (v5.MARKER_CATEGORY, "maximum_age_s"): Decimal(frame * v2.DT),
        (v5.MARKER_CATEGORY, "hash_hi_u32"): Decimal(55 + frame),
        (v5.MARKER_CATEGORY, "hash_lo_u32"): Decimal(66 + frame),
        (v5.SUPPLY_CATEGORY, "step_start_seconds"): Decimal(frame * v2.DT),
        (v5.SUPPLY_CATEGORY, "source_m3_per_s"): rate,
        (v5.SUPPLY_CATEGORY, "scheduled_volume_m3"): scheduled,
        (v5.OBSERVATION_CATEGORY, "travel_cell_x"): Decimal(138),
        (v5.OBSERVATION_CATEGORY, "travel_cell_z"): Decimal(234),
        (v5.OBSERVATION_CATEGORY, "collection_cell_x"): Decimal(58),
        (v5.OBSERVATION_CATEGORY, "collection_cell_z"): Decimal(251),
        (v5.OBSERVATION_CATEGORY, "collection_depth_m"): Decimal("0.25"),
    })
    return values


def supply_profile(frame_count: int, *, response: bool) -> dict[int, dict[tuple[str, str], Decimal]]:
    return {frame: {
        (v5.SUPPLY_CATEGORY, "step_start_seconds"): Decimal(frame * v2.DT),
        (v5.SUPPLY_CATEGORY, "source_m3_per_s"): v5._rate_at(Decimal(frame * v2.DT), response),
        (v5.SUPPLY_CATEGORY, "scheduled_volume_m3"): sum(
            (v5._rate_at(Decimal(i * v2.DT), response) * v2.DT
             for i in range(frame + 1)), Decimal(0)),
    } for frame in range(frame_count)}


def water_budget_profile(frame_count: int, *, response: bool) -> dict[int, dict[tuple[str, str], Decimal]]:
    values = {}
    scheduled = Decimal(0)
    for frame in range(frame_count):
        scheduled += v5._rate_at(Decimal(frame * v2.DT), response) * v2.DT
        values[frame] = {
            ("fluid_25d.solver", "finite_volume_status_flags"): Decimal(0),
            ("fluid_25d.water", "cumulative_source_volume_m3"): scheduled,
            ("fluid_25d.water", "cumulative_sink_volume_m3"): Decimal(0),
            ("fluid_25d.water", "cumulative_boundary_outflow_volume_m3"): Decimal(0),
            ("fluid_25d.water", "total_water_volume_m3"): scheduled,
            ("fluid_25d.water", "conservation_residual_m3"): Decimal(0),
        }
    return values


def response_profile(frame_count: int) -> tuple[
    dict[int, dict[tuple[str, str], Decimal]],
    dict[int, dict[tuple[str, str], Decimal]],
]:
    baseline, current = {}, {}
    scheduled = Decimal(0)
    for frame in range(frame_count):
        baseline[frame] = baseline_frame(frame)
        values = dict(baseline[frame])
        rate = v5._rate_at(Decimal(frame * v2.DT), True)
        scheduled += rate * v2.DT
        values.update({
            (v5.MARKER_CATEGORY, "active_count"): Decimal(1),
            (v5.MARKER_CATEGORY, "farthest_from_source_m"): Decimal(10),
            (v5.MARKER_CATEGORY, "mean_from_source_m"): Decimal(5),
            (v5.MARKER_CATEGORY, "maximum_age_s"): Decimal(frame * v2.DT),
            (v5.MARKER_CATEGORY, "hash_hi_u32"): Decimal(55 + frame),
            (v5.MARKER_CATEGORY, "hash_lo_u32"): Decimal(66 + frame),
            (v5.SUPPLY_CATEGORY, "step_start_seconds"): Decimal(frame * v2.DT),
            (v5.SUPPLY_CATEGORY, "source_m3_per_s"): rate,
            (v5.SUPPLY_CATEGORY, "scheduled_volume_m3"): scheduled,
            (v5.OBSERVATION_CATEGORY, "travel_cell_x"): Decimal(138),
            (v5.OBSERVATION_CATEGORY, "travel_cell_z"): Decimal(234),
            (v5.OBSERVATION_CATEGORY, "collection_cell_x"): Decimal(58),
            (v5.OBSERVATION_CATEGORY, "collection_cell_z"): Decimal(251),
            (v5.OBSERVATION_CATEGORY, "collection_depth_m"): Decimal("0.25"),
        })
        current[frame] = values
    return baseline, current


class ProfileComparisonTests(unittest.TestCase):
    def test_all_retained_common_numerics_match_and_only_named_categories_are_excluded(self):
        baseline = {frame: baseline_frame(frame) for frame in range(3)}
        current = {frame: current_frame(frame) for frame in range(3)}
        current[1][("fluid_25d.solver.timing", "step_elapsed_ms")] = Decimal(99)

        report = v5.compare_common_profiles(baseline, current, frame_count=3)

        self.assertTrue(report["passed"])
        self.assertEqual(report["metric_values_compared"], 9)
        self.assertEqual(report["excluded_categories"], sorted(v5.ALLOWED_OBSERVATION_CATEGORIES))
        self.assertIn([v5.SUPPLY_CATEGORY, "source_m3_per_s"], report["current_only_observation_keys"])

    def test_filtering_observation_categories_never_hides_a_hydraulic_change(self):
        baseline = {frame: baseline_frame(frame) for frame in range(2)}
        current = {frame: current_frame(frame) for frame in range(2)}
        current[1][("fluid_25d.water", "total_water_volume_m3")] += Decimal("0.000001")

        with self.assertRaisesRegex(ValueError, "numerical profile metrics differ"):
            v5.compare_common_profiles(baseline, current, frame_count=2)

    def test_rejects_unapproved_metric_categories_missing_metrics_and_malformed_identity(self):
        baseline = {0: baseline_frame(0)}
        current = {0: current_frame(0)}
        current[0][("fluid_25d.future", "unexpected")]=Decimal(1)
        with self.assertRaisesRegex(ValueError, "unapproved numerical metrics"):
            v5.compare_common_profiles(baseline, current, frame_count=1)

        current = {0: current_frame(0)}
        current[0].pop((v5.SUPPLY_CATEGORY, "scheduled_volume_m3"))
        with self.assertRaisesRegex(ValueError, "lacks required supply/observation metrics"):
            v5.compare_common_profiles(baseline, current, frame_count=1)

        current = {0: current_frame(0)}
        current[0][("fluid_25d.hydraulic_identity", "hash_hi_u32")] = Decimal("4294967296")
        with self.assertRaisesRegex(ValueError, "non-uint32 state hash"):
            v5.compare_common_profiles(baseline, current, frame_count=1)

        with self.assertRaisesRegex(ValueError, "every requested fixed-step frame"):
            v5.compare_common_profiles({0: baseline_frame(0), 1: baseline_frame(1)},
                                      {0: current_frame(0)}, frame_count=2)

        with self.assertRaisesRegex(ValueError, "may exclude only the named"):
            v5.compare_common_profiles(baseline, {0: current_frame(0)}, frame_count=1,
                                       allowed_categories={"fluid_25d.water"})

    def test_observation_metrics_are_checked_as_bounded_study_context(self):
        frames = {0: current_frame(0), 1: current_frame(1)}
        result = v5.validate_observation_profile(frames, frame_count=2)
        self.assertEqual(result["final_observation"]["travel_cell_x"], "138")
        frames[1][(v5.OBSERVATION_CATEGORY, "collection_cell_x")] = Decimal(512)
        with self.assertRaisesRegex(ValueError, "outside the 512x512 domain"):
            v5.validate_observation_profile(frames, frame_count=2)

    def test_final_observation_requires_measured_material_collection(self):
        frames = {0: current_frame(0), 1: current_frame(1)}
        frames[0][(v5.OBSERVATION_CATEGORY, "collection_depth_m")] = Decimal(0)
        self.assertTrue(v5.validate_observation_profile(frames, frame_count=2)["passed"])
        for x, z, depth in ((213, 213, "1"), (58, 251, "0"), (58, 251, "0.005")):
            frames[1].update({
                (v5.OBSERVATION_CATEGORY, "collection_cell_x"): Decimal(x),
                (v5.OBSERVATION_CATEGORY, "collection_cell_z"): Decimal(z),
                (v5.OBSERVATION_CATEGORY, "collection_depth_m"): Decimal(depth),
            })
            with self.assertRaisesRegex(ValueError, "no material collection beyond 2 km"):
                v5.validate_observation_profile(frames, frame_count=2)

    def test_response_compares_exactly_through_60_minutes_then_allows_measured_change(self):
        baseline, response = response_profile(1801)
        response[1800][("fluid_25d.water", "total_water_volume_m3")] += Decimal(100)
        response[1800][("fluid_25d.hydraulic_identity", "hash_hi_u32")] += Decimal(1)

        report = v5.compare_response_prefix(baseline, response, frame_count=1801)

        self.assertEqual(report["frames_compared"], 1800)
        self.assertEqual(report["comparison_window_physical_seconds"], [0, 3600])
        response[1799][("fluid_25d.water", "total_water_volume_m3")] += Decimal(1)
        with self.assertRaisesRegex(ValueError, "numerical profile metrics differ"):
            v5.compare_response_prefix(baseline, response, frame_count=1801)

        response[1799][("fluid_25d.water", "total_water_volume_m3")] -= Decimal(1)
        response[1800][("fluid_25d.unexpected", "future_metric")] = Decimal(1)
        with self.assertRaisesRegex(ValueError, "unapproved numerical metrics"):
            v5.compare_response_prefix(baseline, response, frame_count=1801)


class SupplyAndBudgetTests(unittest.TestCase):
    def test_response_schedule_uses_the_recorded_step_start_on_boundaries(self):
        frames = supply_profile(2701, response=True)
        report = v5.validate_supply_profile(frames, response=True, frame_count=2701)
        self.assertEqual(report["rates_m3_per_s"], ["100", "150", "50"])
        self.assertEqual(frames[1799][(v5.SUPPLY_CATEGORY, "source_m3_per_s")], Decimal(100))
        self.assertEqual(frames[1800][(v5.SUPPLY_CATEGORY, "source_m3_per_s")], Decimal(150))
        self.assertEqual(frames[2700][(v5.SUPPLY_CATEGORY, "source_m3_per_s")], Decimal(50))

    def test_schedule_rejects_wrong_rate_wrong_clock_and_wrong_integral(self):
        frames = supply_profile(1802, response=True)
        frames[1800][(v5.SUPPLY_CATEGORY, "source_m3_per_s")] = Decimal(100)
        with self.assertRaisesRegex(ValueError, "recorded source rate differs"):
            v5.validate_supply_profile(frames, response=True, frame_count=1802)

        frames = supply_profile(2, response=False)
        frames[1][(v5.SUPPLY_CATEGORY, "step_start_seconds")] += Decimal(2)
        with self.assertRaisesRegex(ValueError, "step-start time is invalid"):
            v5.validate_supply_profile(frames, response=False, frame_count=2)

        frames = supply_profile(2, response=False)
        frames[1][(v5.SUPPLY_CATEGORY, "scheduled_volume_m3")] += Decimal(1)
        with self.assertRaisesRegex(ValueError, "scheduled volume differs"):
            v5.validate_supply_profile(frames, response=False, frame_count=2)

    def test_response_water_budget_checks_every_frame_and_ends_at_720000(self):
        frames = water_budget_profile(v5.FRAME_COUNT, response=True)
        report = v5.validate_water_budget_profile(frames, response=True)
        self.assertEqual(report["scheduled_source_integral_m3"], "720000")
        frames[1800][("fluid_25d.solver", "finite_volume_status_flags")] = Decimal(1)
        with self.assertRaisesRegex(ValueError, "nonzero finite-volume status flags"):
            v5.validate_water_budget_profile(frames, response=True)

        frames = water_budget_profile(3, response=False)
        frames[2][("fluid_25d.water", "cumulative_source_volume_m3")] += Decimal(2)
        with self.assertRaisesRegex(ValueError, "differs from recorded supply schedule"):
            v5.validate_water_budget_profile(frames, response=False, frame_count=3)


class CommandAndArtifactTests(unittest.TestCase):
    def test_excerpt_binds_exact_source_trim_clock_and_frame_range(self):
        source, target = Path("raw.mp4"), Path("labelled.mp4")
        spec = v5._labelled_excerpt_spec(source, target, "travel-response", True)
        self.assertEqual(spec["expected_frames"], 1951)
        self.assertEqual(spec["source_frame_range_half_open"], [1649, 3600])
        self.assertTrue(spec["filter"].startswith("trim=start_frame=1649:end_frame=3600,"))
        self.assertIn("n+1649", spec["filter"])
        v5._validate_excerpt_encoding(spec, source, target, "travel-response", True)
        changed = dict(spec, source_frame_range_half_open=[0, 1951])
        with self.assertRaisesRegex(ValueError, "input, trim, clock"):
            v5._validate_excerpt_encoding(changed, source, target, "travel-response", True)
        changed = dict(spec, command=list(spec["command"]))
        changed["command"][changed["command"].index("-i") + 1] = "wrong-source.mp4"
        with self.assertRaisesRegex(ValueError, "input, trim, clock"):
            v5._validate_excerpt_encoding(changed, source, target, "travel-response", True)
        changed = dict(spec, filter=spec["filter"].replace("start_frame=1649", "start_frame=0"))
        with self.assertRaisesRegex(ValueError, "input, trim, clock"):
            v5._validate_excerpt_encoding(changed, source, target, "travel-response", True)

    def test_png_requires_decodable_pixels_not_only_a_matching_header(self):
        from PIL import Image
        with tempfile.TemporaryDirectory() as temp:
            image = Path(temp) / "capture.png"
            Image.new("RGB", (v5.EXPECTED_WIDTH, v5.EXPECTED_HEIGHT)).save(image)
            v5._validate_png(image)
            content = image.read_bytes()
            image.write_bytes(content[:24])
            with self.assertRaisesRegex(ValueError, "not a decoded"):
                v5._validate_png(image)
            image.write_bytes(content[:-16])
            with self.assertRaisesRegex(ValueError, "not a decoded"):
                v5._validate_png(image)

    def test_probe_sidecar_is_bound_to_its_case_path_and_complete_record(self):
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "case.raw.ffprobe.log"
            log.write_text("probe")
            payload = {"validated": True, "exit_code": 0,
                       "log_path": str(log.resolve()), "log_sha256": v5.sha256_file(log)}
            receipt = log.with_suffix(".json")
            v5.write_report(receipt, payload)
            record = dict(payload, receipt_path=str(receipt.resolve()),
                          receipt_sha256=v5.sha256_file(receipt))
            v5._validate_probe_binding(record, log, "raw")
            changed = dict(record, expected_frames=3600)
            with self.assertRaisesRegex(ValueError, "differs from its recorded result"):
                v5._validate_probe_binding(changed, log, "raw")
            other = Path(temp) / "wrong.ffprobe.json"
            v5.write_report(other, payload)
            changed = dict(record, receipt_path=str(other.resolve()),
                           receipt_sha256=v5.sha256_file(other))
            with self.assertRaisesRegex(ValueError, "receipt is misplaced"):
                v5._validate_probe_binding(changed, log, "raw")

    def test_parallel_profile_jobs_are_all_pinned_and_reported_in_protocol_order(self):
        barrier = threading.Barrier(3)
        identity = {"app_sha256": "pinned"}

        def child(app, case_dir, label, dye, response):
            case_dir.mkdir()
            receipt = {"before": identity, "after": identity, "artifact_hashes": {}}
            v5.write_report(case_dir / "execution.json", receipt)
            barrier.wait(timeout=5)
            return receipt

        with tempfile.TemporaryDirectory() as temp, \
                patch.object(v5, "_execution_identity", return_value=identity), \
                patch.object(v5, "_load_retained_baselines", return_value={"solver_bridge": {}}), \
                patch.object(v5, "_run_profile_case", side_effect=child), \
                patch.object(v5, "_validate_profile_case",
                             side_effect=lambda label, receipt, baseline: {"label": label}):
            report = v5.profiles_phase(Path(temp) / "app", Path(temp) / "out", profile_workers=3)
        self.assertTrue(report["phase_checks_passed"])
        self.assertEqual(report["profile_workers"], 3)
        self.assertEqual([case["label"] for case in report["cases"]],
                         [case[0] for case in v5.PROFILE_CASES])

    def test_parallel_profile_failure_cannot_produce_a_passing_report(self):
        identity = {"app_sha256": "pinned"}

        def child(app, case_dir, label, dye, response):
            case_dir.mkdir()
            receipt = {"before": identity, "after": {"app_sha256": "changed"},
                       "artifact_hashes": {}}
            v5.write_report(case_dir / "execution.json", receipt)
            return receipt

        with tempfile.TemporaryDirectory() as temp, \
                patch.object(v5, "_execution_identity", return_value=identity), \
                patch.object(v5, "_load_retained_baselines", return_value={"solver_bridge": {}}), \
                patch.object(v5, "_run_profile_case", side_effect=child):
            out = Path(temp) / "out"
            with self.assertRaisesRegex(ValueError, "initial app/terrain/shader identity"):
                v5.profiles_phase(Path(temp) / "app", out, profile_workers=3)
            self.assertFalse(v5.read_report(out / "profiles/profiles.json")["phase_checks_passed"])
        with self.assertRaisesRegex(ValueError, "profile workers must"):
            v5.profiles_phase(Path("unused"), Path("unused"), profile_workers=2)

    def test_profile_commands_keep_dye_and_supply_response_separate(self):
        with tempfile.TemporaryDirectory() as temp:
            folder = Path(temp)
            response = v5.profile_arguments(False, True, folder / "profile", folder / "final.png")
            dye = v5.profile_arguments(True, False, folder / "profile-dye", folder / "dye.png")
        self.assertIn("--fluid25d-hillside-supply-response", response)
        self.assertNotIn("--fluid25d-dye-pulse-start-seconds", response)
        self.assertNotIn("--fluid25d-hillside-supply-response", dye)
        self.assertEqual(dye[dye.index("--fluid25d-dye-pulse-start-seconds") + 1], "3600")
        self.assertEqual(dye[dye.index("--fluid25d-dye-pulse-duration-seconds") + 1], "120")
        for command in (response, dye):
            self.assertIn("--fluid25d-motion-markers", command)
            self.assertEqual(command[command.index("--fluid25d-motion-marker-mode") + 1], "local")
            self.assertIn("--fluid25d-hillside-depth-cues", command)
            self.assertEqual(command[command.index("--frames") + 1], str(v5.FRAME_COUNT))

    def test_capture_matrix_uses_reviewed_named_views_and_response_has_no_dye(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / "travel.mp4"
            command = v5.capture_arguments("travel", "-0.72", True, True, output, capture="video")
        self.assertEqual(command[command.index("--fluid25d-hillside-camera") + 1], "travel")
        self.assertIn("--fluid25d-hillside-supply-response", command)
        self.assertNotIn("--fluid25d-dye-pulse-start-seconds", command)
        self.assertEqual(command[command.index("--fps") + 1], "30")
        self.assertEqual({case[1] for case in v5.VIDEO_CASES + v5.STILL_CASES},
                         {"source", "travel", "collection", "overview"})
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(ValueError, "overview uses the topdown"):
                v5.capture_arguments("overview", "-0.72", False, True,
                                     Path(temp) / "bad.png", capture="png")

    def test_artifact_receipt_refuses_missing_hashes_changed_files_and_path_escape(self):
        with tempfile.TemporaryDirectory() as temp:
            case_dir = Path(temp) / "case"
            case_dir.mkdir()
            artifact = case_dir / "profile.metrics.csv"
            artifact.write_text("evidence")
            expected = {artifact.name: artifact}
            receipt = {"artifact_paths": {artifact.name: str(artifact.resolve())},
                       "artifact_hashes": {artifact.name: v5.sha256_file(artifact)}}
            self.assertEqual(v5._validate_artifact_receipt(receipt, expected, case_dir=case_dir),
                             {artifact.name: v5.sha256_file(artifact)})
            receipt["artifact_hashes"] = {}
            with self.assertRaisesRegex(ValueError, "incomplete"):
                v5._validate_artifact_receipt(receipt, expected, case_dir=case_dir)
            receipt["artifact_hashes"] = {artifact.name: "bad"}
            with self.assertRaisesRegex(ValueError, "missing, misplaced, or changed"):
                v5._validate_artifact_receipt(receipt, expected, case_dir=case_dir)
            receipt["artifact_paths"][artifact.name] = str((case_dir.parent / "escape.csv").resolve())
            receipt["artifact_hashes"][artifact.name] = "bad"
            with self.assertRaisesRegex(ValueError, "missing, misplaced, or changed"):
                v5._validate_artifact_receipt(receipt, expected, case_dir=case_dir)

    def test_metrics_csv_rejects_duplicate_and_nonfinite_values(self):
        with tempfile.TemporaryDirectory() as temp:
            metrics = Path(temp) / "profile.csv"
            metrics.write_text("frame_index,category,name,value\n0,water,depth,1\n0,water,depth,2\n")
            with self.assertRaisesRegex(ValueError, "duplicate metric"):
                v2._read_metrics(metrics, decimal_values=True)
            metrics.write_text("frame_index,category,name,value\n0,water,depth,nan\n")
            with self.assertRaisesRegex(ValueError, "nonfinite metric"):
                v2._read_metrics(metrics, decimal_values=True)

    def test_phase_directory_is_exclusive_but_preserves_primary_observation_folder(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp) / "evidence"
            (out / "observation").mkdir(parents=True)
            phase = v5.create_phase_directory(out, "profiles")
            self.assertTrue(phase.is_dir())
            self.assertTrue((out / "observation").is_dir())
            with self.assertRaises(FileExistsError):
                v5.create_phase_directory(out, "profiles")


if __name__ == "__main__":
    unittest.main()
