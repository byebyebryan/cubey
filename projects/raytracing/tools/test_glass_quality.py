# /// script
# dependencies = ["numpy==2.4.6", "pillow==12.0.0"]
# ///
"""Opt-in asset-independent checks for Q0 statistics and native frame joins."""

import csv
import json
import tempfile
import unittest
from pathlib import Path

import numpy as np
from analyze_glass_quality import analyze_native, dilate, erode, errors


class QualityStatisticsTests(unittest.TestCase):
    def test_identical_references_keep_a_known_signal_error(self):
        reference = np.zeros((3, 4, 3))
        result = errors(
            reference + 2, reference, reference, np.ones((3, 4), dtype=bool)
        )
        self.assertEqual(result["rmse"], 2)
        self.assertEqual(result["candidate_noise_estimate"], 2)
        self.assertEqual(result["reference_mean_noise_estimate"], 0)

    def test_reference_mean_floor_uses_one_quarter_pair_mse(self):
        reference = np.zeros((3, 4, 3))
        result = errors(
            reference, reference + 1, reference - 1, np.ones((3, 4), dtype=bool)
        )
        self.assertEqual(result["reference_pair_rmse"], 2)
        self.assertEqual(result["reference_mean_noise_estimate"], 1)
        self.assertEqual(result["candidate_noise_estimate"], 0)

    def test_regions_reject_empty_or_nonfinite_inputs(self):
        reference = np.zeros((3, 4, 3))
        with self.assertRaises(AssertionError):
            errors(reference, reference, reference, np.zeros((3, 4), dtype=bool))
        reference[0, 0, 0] = np.nan
        with self.assertRaises(AssertionError):
            errors(reference, reference, reference, np.ones((3, 4), dtype=bool))

    def test_morphology_does_not_wrap_or_invent_border_pixels(self):
        mask = np.zeros((7, 7), dtype=bool)
        mask[3, 0] = True
        grown = dilate(mask, 1)
        self.assertEqual(grown.sum(), 4)
        self.assertFalse(grown[:, -1].any())
        self.assertFalse(grown[0].any())
        self.assertEqual(erode(np.ones((7, 7), dtype=bool), 1).sum(), 25)


class NativeJoinTests(unittest.TestCase):
    def write_fixture(self, root):
        frames = [
            {
                "frame": i,
                "host_frame": i,
                "clock_frame": i + 1,
                "epoch": 1,
                "update_steady_ns": (i + 1) * 1000000,
                "record_begin_steady_ns": (i + 1) * 1000000 + 10,
                "record_end_steady_ns": (i + 1) * 1000000 + 20,
                "cpu_record_ms": 0.1,
                "host_delta_ms": 2,
                "samples_after": i + 1,
                "yaw": 0.45,
                "pitch": 0.35,
                "distance": 1.75,
            }
            for i in range(3)
        ]
        journal = [
            {
                "frame": i,
                "epoch": 1,
                "new_samples": 1,
                "samples_before": i,
                "samples_after": i + 1,
            }
            for i in range(3)
        ]
        metrics = {
            "native_frames": frames,
            "sampling_frames": journal,
            "dropped_gpu_samples": 0,
            "dropped_native_frames": 0,
            "gpu_samples": [
                dict(
                    frame=i,
                    pass_name="unused",
                    **{"pass": "raytracing.trace", "milliseconds": i + 1},
                )
                for i in range(3)
            ],
            "optical_camera_rejected_updates": 0,
            "history_epochs": 1,
            "as_builds": 3,
            "resident_project_requested_bytes": 10,
            "peak_simultaneous_project_requested_bytes": 10,
        }
        (root / "metrics.json").write_text(json.dumps(metrics))
        (root / "events.json").write_text(
            json.dumps(
                {
                    "recording": False,
                    "validation": False,
                    "width": 1920,
                    "height": 1080,
                    "phases": [
                        {"name": "settle-return", "begin_ns": 0, "end_ns": 4000000}
                    ],
                }
            )
        )
        with (root / "host.passes.csv").open("w") as stream:
            writer = csv.DictWriter(
                stream, fieldnames=["frame_index", "kind", "label", "duration_ms"]
            )
            writer.writeheader()
            for i in range(3):
                for label in ["host.record_frame", "host.present"]:
                    writer.writerow(
                        {
                            "frame_index": i,
                            "kind": "cpu",
                            "label": label,
                            "duration_ms": i + 1,
                        }
                    )
        return metrics

    def test_join_keeps_host_and_gpu_identity_and_phase_counts(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            self.write_fixture(root)
            result = analyze_native(root)["phases"]["settle-return"]
            self.assertEqual(result["groups"]["reset"]["frames"], 1)
            self.assertEqual(result["groups"]["accumulating"]["frames"], 2)
            self.assertEqual(
                result["groups"]["accumulating"]["gpu_trace_ms"]["p50"], 2.5
            )
            self.assertIsNone(result["settle_recorded_ms"]["512"])

    def test_join_rejects_shifted_host_frames(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            metrics = self.write_fixture(root)
            for frame in metrics["native_frames"]:
                frame["host_frame"] += 1
            (root / "metrics.json").write_text(json.dumps(metrics))
            with self.assertRaises(AssertionError):
                analyze_native(root)

    def test_join_rejects_a_divergent_sampling_receipt(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            metrics = self.write_fixture(root)
            metrics["sampling_frames"][1]["samples_after"] = 99
            (root / "metrics.json").write_text(json.dumps(metrics))
            with self.assertRaises(AssertionError):
                analyze_native(root)


if __name__ == "__main__":
    unittest.main()
