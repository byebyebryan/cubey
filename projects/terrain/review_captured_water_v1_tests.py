#!/usr/bin/env python3
"""Pure report checks; no renderer, browser or solver launch."""
from copy import deepcopy
from pathlib import Path
import tempfile
import unittest

import review_captured_water_v1 as review


class ReportTests(unittest.TestCase):
    def test_nearest_rank_percentile(self):
        self.assertEqual(review.percentile(list(range(100))), 94)
        self.assertEqual(review.percentile([3]), 3)
        with self.assertRaises(ValueError):
            review.percentile([])

    def test_acceptance_keeps_original_gate_failure(self):
        summary = {"performance_budget_pass": False, "performance": [
            {"camera": "lake", "repeat": i, "water_gpu_p95_ms": 3.78,
             "incremental_p95_ms": 2.034 if i == 5 else 1.5,
             "cpu_service_p95_ms": .32} for i in range(6)]}
        before = deepcopy(summary)
        accepted = review.accepted_water_cost(summary)
        self.assertEqual(summary, before)
        self.assertEqual(len(accepted["misses"]), 1)
        self.assertFalse(accepted["default_promoted"])
        summary["performance"][0]["water_gpu_p95_ms"] = 9
        with self.assertRaises(ValueError):
            review.accepted_water_cost(summary)
        with self.assertRaises(ValueError):
            review.accepted_water_cost({"performance_budget_pass": True})

    def test_report_does_not_claim_unrecorded_acceptance(self):
        with tempfile.TemporaryDirectory(prefix="cubey-water-report-") as directory:
            out = Path(directory)
            review.write_report(out, {})
            text = (out / "index.html").read_text()
            self.assertIn("V1 acceptance is not recorded", text)
            self.assertIn("stream-frozen.mp4", text)
            self.assertIn("clouds", text.lower())
            self.assertNotIn("Accepted as opt-in V1", text)
            review.write_report(out, {"acceptance": {
                "status": "user-accepted-v1-with-measured-limitation"}})
            self.assertIn("Accepted as opt-in V1", (out / "index.html").read_text())


if __name__ == "__main__":
    unittest.main()
