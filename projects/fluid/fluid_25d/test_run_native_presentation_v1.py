#!/usr/bin/env python3
"""CPU-only planning, phase-safety, and media/profile validation tests."""
from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

import run_native_presentation_v1 as presentation


class NativePresentationTests(unittest.TestCase):
    def test_historical_plan_matches_the_frozen_short_capture_leaf(self):
        plan = presentation.historical_capture_plan()
        self.assertEqual(len(plan), 30)
        self.assertEqual(sum(asset["kind"] == "still" for asset in plan), 18)
        self.assertEqual(sum(asset["kind"] == "diagnostic" for asset in plan), 6)
        videos = [asset for asset in plan if asset["kind"] == "video"]
        self.assertEqual(len(videos), 6)
        by_id = {asset["id"]: asset for asset in plan}
        self.assertIn("rain-off-overview-900s.png", presentation.historical_expected_names())
        self.assertIn("rain-on-diagnostic-wet-dry-8160s.png", presentation.historical_expected_names())
        self.assertEqual(by_id["mature-runoff-rain-off"]["requested_end_time_s"], 6300)
        self.assertEqual(by_id["recession-collection-rain-on"]["requested_end_time_s"], 8160)

    def test_current_plan_uses_extended_story_windows_and_three_cameras(self):
        original = presentation.current_capture_plan("original")
        stills = [asset for asset in original if asset["kind"] == "still"]
        diagnostics = [asset for asset in original if asset["kind"] == "diagnostic"]
        videos = [asset for asset in original if asset["kind"] == "video"]
        self.assertEqual(len(stills), 24)
        self.assertEqual(len(diagnostics), 6)
        self.assertEqual(len(videos), 6)
        self.assertEqual({asset["camera"] for asset in stills}, set(presentation.CAMERAS))
        by_id = {asset["id"]: asset for asset in videos}
        self.assertEqual(by_id["development-overview-rain-off"]["requested_end_time_s"], 6000)
        self.assertEqual(by_id["recession-collection-rain-off"]["requested_end_time_s"], 14400)
        self.assertEqual(by_id["mature-runoff-rain-off"]["requested_end_time_s"], 6300)

    def test_requested_render_clock_and_held_saved_state_are_planned_separately(self):
        rows = presentation.timeline(4800, 4, 5)
        self.assertEqual(
            rows,
            [
                {"frame_index": 0, "requested_time_s": 4800, "saved_field_time_s": 4800},
                {"frame_index": 1, "requested_time_s": 4805, "saved_field_time_s": 4800},
                {"frame_index": 2, "requested_time_s": 4810, "saved_field_time_s": 4800},
                {"frame_index": 3, "requested_time_s": 4815, "saved_field_time_s": 4800},
            ],
        )
        self.assertEqual(presentation.timeline(7260, 3, 60)[-1]["saved_field_time_s"], 7380)

    def test_profile_plan_is_bounded_and_uses_the_gpu_presentation_scope(self):
        plan = presentation.profile_plan("readable-markers")
        self.assertEqual(plan["capture_frames"], 120)
        self.assertEqual(plan["fps"], 30)
        self.assertEqual(plan["requested_interval_s"], 5)
        self.assertEqual(plan["profile_warmup_frames"], 12)
        self.assertEqual(plan["measured_gpu_scope_sample_count"], 108)
        self.assertEqual(plan["measured_gpu_scope"], "fluid_25d native presentation total")
        self.assertTrue(plan["motion_markers"])
        self.assertIn("not rendering performance", plan["frames_csv_delta_ms"])

    def test_probe_validation_checks_count_rate_dimensions_and_duration(self):
        valid = {
            "streams": [{
                "width": 960,
                "height": 540,
                "r_frame_rate": "8/1",
                "nb_frames": "16",
                "nb_read_frames": "16",
            }],
            "format": {"duration": "1.875"},
        }
        self.assertEqual(presentation.validate_probe(valid, 16, 8)["decoded_frames"], 16)
        for changed in (
            {**valid, "streams": [{**valid["streams"][0], "nb_frames": "15"}]},
            {**valid, "streams": [{**valid["streams"][0], "nb_read_frames": "15"}]},
            {**valid, "streams": [{**valid["streams"][0], "r_frame_rate": "30/1"}]},
            {**valid, "format": {"duration": "5.0"}},
            {**valid, "streams": [{**valid["streams"][0], "width": 1280}]},
        ):
            with self.subTest(changed=changed), self.assertRaises(ValueError):
                presentation.validate_probe(changed, 16, 8)

    def test_phase_directory_reservation_refuses_an_overwrite(self):
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "candidate-motion"
            presentation.reserve_directory(target)
            self.assertTrue(target.is_dir())
            with self.assertRaises(FileExistsError):
                presentation.reserve_directory(target)

    def test_input_and_runtime_mismatch_is_rejected(self):
        original = {
            "executable": "fluid_25d",
            "executable_sha256": "a" * 64,
            "compiled_shaders": {"tree_sha256": "b" * 64},
            "inputs": {"tree_sha256": "c" * 64},
            "source": {"head": "old"},
        }
        same = {**original, "source": {"head": "new"}}
        presentation.assert_same_runtime(original, same, "in test")
        changed = {**same, "inputs": {"tree_sha256": "d" * 64}}
        with self.assertRaises(ValueError):
            presentation.assert_same_runtime(original, changed, "in test")

    def test_recording_log_parser_accepts_fields_after_zero_dispatch_count(self):
        asset = presentation.video_asset("mature-runoff", "rain-off", "runoff", 4800, 4, 30, 5)
        captures = []
        for row, delta in zip(asset["timeline"], (0.0, 5.0, 5.0, 5.0)):
            captures.append(
                "fluid_25d_recording_capture: "
                f"output_frame={row['frame_index']} requested_s={row['requested_time_s']:.9f} "
                f"saved_s={row['saved_field_time_s']:.9f} camera=runoff hydraulic_dispatches=0 "
                f"visual_delta_s={delta:.9f} presentation=motion"
            )
        parsed = presentation.verify_capture_log(
            "fluid_25d_recording_upload: PASS\n" + "\n".join(captures) + "\n",
            require_upload_validation=True,
            expected_asset=asset,
            expected_presentation="motion",
        )
        self.assertEqual(parsed["hydraulic_dispatches"], [0, 0, 0, 0])
        self.assertEqual(parsed["visual_delta_s"], [0.0, 5.0, 5.0, 5.0])
        self.assertEqual(parsed["presentation_log_values"], ["motion"] * 4)
        with self.assertRaises(ValueError):
            presentation.verify_capture_log(
                "fluid_25d_recording_upload: PASS\n" + "\n".join(captures[:-1]) + "\n",
                True, asset, "motion",
            )
        duplicate = captures[:-1] + [captures[2], captures[3]]
        with self.assertRaises(ValueError):
            presentation.verify_capture_log("\n".join(duplicate), False, asset, "motion")
        nonzero_later = captures.copy()
        nonzero_later[-1] = nonzero_later[-1].replace("hydraulic_dispatches=0", "hydraulic_dispatches=1")
        with self.assertRaises(ValueError):
            presentation.verify_capture_log("\n".join(nonzero_later), False, asset, "motion")
        changed_clock = captures.copy()
        changed_clock[-1] = changed_clock[-1].replace("requested_s=4815.000000000", "requested_s=4816.000000000")
        with self.assertRaises(ValueError):
            presentation.verify_capture_log("\n".join(changed_clock), False, asset, "motion")
        with self.assertRaises(ValueError):
            presentation.verify_capture_log("hydraulic_dispatches=1 visual_delta_s=0.033", False)

    def test_original_visual_delta_only_advances_when_saved_state_changes(self):
        asset = presentation.video_asset("mature-runoff", "rain-off", "runoff", 4800, 14, 30, 5)
        deltas = [row["visual_delta_s"] for row in presentation.expected_capture_rows(asset, "original")]
        self.assertEqual(deltas[:12], [0.0] * 12)
        self.assertEqual(deltas[12:], [60.0, 0.0])

    def test_profile_summary_uses_gpu_scope_and_excludes_warmup(self):
        with tempfile.TemporaryDirectory() as temp:
            prefix = Path(temp) / "profile"
            prefix.with_name("profile.frames.csv").write_text(
                "frame_index,delta_ms,width,height\n0,33.333,960,540\n1,33.333,960,540\n2,33.333,960,540\n"
            )
            prefix.with_name("profile.passes.csv").write_text(
                "frame_index,kind,label,start_ms,duration_ms\n"
                "0,gpu,fluid_25d native presentation total,0,9.0\n"
                "1,gpu,fluid_25d native presentation total,0,1.0\n"
                "2,gpu,fluid_25d native presentation total,0,2.0\n"
                "2,gpu,other scope,0,100.0\n"
            )
            summary = presentation.profile_summary(prefix, warmup_frames=1, expected_samples=2)
            self.assertEqual(summary["measured_gpu_span_count"], 2)
            self.assertEqual(summary["gpu_span_ms"], [1.0, 2.0])
            self.assertEqual(summary["gpu_median_ms"], 1.5)
            self.assertIn("not rendering duration", summary["frames_csv_delta_interpretation"])

    def test_profile_summary_rejects_missing_gpu_timing_scope(self):
        with tempfile.TemporaryDirectory() as temp:
            prefix = Path(temp) / "profile"
            prefix.with_name("profile.frames.csv").write_text("frame_index,delta_ms\n0,33.3\n")
            prefix.with_name("profile.passes.csv").write_text(
                "frame_index,kind,label,start_ms,duration_ms\n0,gpu,other scope,0,1.0\n"
            )
            with self.assertRaises(ValueError):
                presentation.profile_summary(prefix, warmup_frames=0, expected_samples=1)


if __name__ == "__main__":
    unittest.main()
