"""Report gates must not confuse a completed worker with a healthy live viewer."""
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


if __name__ == "__main__":
    unittest.main()
