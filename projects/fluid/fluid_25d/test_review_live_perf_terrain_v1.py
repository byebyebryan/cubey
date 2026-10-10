"""Report gates must not confuse a completed worker with a healthy live viewer."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

import review_live_perf_terrain_v1 as review


class Review(unittest.TestCase):
    def test_completed_trial_is_not_frame_budget_acceptance(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            row = {"mode": "live", "physical_wall_ratio": 60, "measurement_final_physical_s": 7200,
                   "cleanup": [{"active_group_remaining": [], "escalated": False}],
                   "completion": {"inputs_unchanged": True, "reference_inputs_unchanged": True},
                   "profile": {"frame_ms": {"p95": 50}, "metrics": {
                       "fluid_25d.external.publication_freshness_s": {"p95": .2},
                       "fluid_25d.external.advancing_state_age_s": {"p95": .2},
                       "fluid_25d.external.physical_time_s": {"max": 7170}}}}
            gates = review.gates(row, root)
            self.assertTrue(gates["pace_at_least_54x"])
            self.assertFalse(gates["frame_p95_at_most_16_7ms"])
            self.assertTrue(gates["display_near_worker_endpoint"])
            (root / "viewer.log").write_text("VIEWER ERROR: lost producer")
            row["profile"]["metrics"]["fluid_25d.external.physical_time_s"]["max"] = 0
            gates = review.gates(row, root)
            self.assertTrue(gates["viewer_reported_failure"])
            self.assertFalse(gates["display_near_worker_endpoint"])

    def test_progress_plot_distinguishes_scope_and_escapes_labels(self):
        rows = [{"case": "mountain-patch-1024-16m", "mode": "worker-only",
                 "samples": [{"wall_s": 120, "physical_s": 7200}]}]
        svg = review.progress_plot(rows, "mountain")
        self.assertIn("stroke-dasharray", svg)
        self.assertIn("7200 physical s", svg)
        self.assertEqual(review.progress_plot(rows, "alpine"), "")

    def test_missing_evidence_is_not_an_acceptance_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            row = {"mode": "live"}
            result = review.gates(row, Path(temporary))
            self.assertIsNone(result["clean_owned_shutdown"])
            self.assertIsNone(result["warm_ack_p95_at_most_250ms"])
            row["warm_ack_ms"] = {"p95": 251}
            self.assertFalse(review.gates(row, Path(temporary))["warm_ack_p95_at_most_250ms"])
            row["warm_ack_ms"]["p95"] = 249
            self.assertTrue(review.gates(row, Path(temporary))["warm_ack_p95_at_most_250ms"])

    def test_matrix_keeps_missing_and_incomplete_trials_visible(self):
        rows = [{"case": "mountain-patch-2048-8m", "mode": "live", "status": "timeout",
                 "physical_wall_ratio": 8.3, "measurement_final_physical_s": 5064}]
        matrix = review.coverage_matrix(rows)
        self.assertIn("timeout", matrix)
        self.assertIn("5064", matrix)
        self.assertIn("pending", matrix)
        self.assertIn("32.768 km", matrix)
        self.assertEqual(matrix.count("<tr>"), 16)

    def test_report_curation_preserves_trial_receipts(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            trial = root / "coverage/mountain-patch-1024-16m-live-0"
            trial.mkdir(parents=True)
            row = {"case": "mountain-patch-1024-16m", "mode": "live", "status": "completed",
                   "errors": [], "captures": [{"path": "final.png", "observed_physical_s": 7200}]}
            raw = json.dumps(row).encode()
            (trial / "result.json").write_bytes(raw)
            with contextlib.redirect_stdout(io.StringIO()):
                review.render(root)
            page = (root / "index.html").read_text()
            self.assertIn("Start here: the three 1k terrain options", page)
            self.assertIn("<details><summary>coverage", page)
            self.assertIn("All other live captures · 0", page)
            self.assertIn("No physical-monitor FPS claim", page)
            self.assertEqual((trial / "result.json").read_bytes(), raw)


if __name__ == "__main__":
    unittest.main()
