"""CPU-only evidence-helper tests; no subprocess or GPU launch."""
import unittest
from pathlib import Path
import tempfile

import validate_terrain_demo_v1 as study


class EvidenceHelpers(unittest.TestCase):
    def test_missing_short_run_profiles_do_not_prevent_cleanup_receipts(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "profile.csv"
            self.assertEqual(study.csv_rows(path), [])
            path.write_text("frame_index,value\n1,2\n")
            self.assertEqual(study.csv_rows(path), [{"frame_index":"1", "value":"2"}])

    def test_bounded_percentiles_ignore_nonfinite(self):
        self.assertIsNone(study.percentiles([]))
        result = study.percentiles([1, 2, 3, float("nan"), float("inf")])
        self.assertEqual(result["count"], 3)
        self.assertEqual(result["p50"], 2)
        self.assertAlmostEqual(result["p95"], 2.9)
        self.assertEqual(result["max"], 3)

    def test_control_trials_are_not_throughput_measurements(self):
        self.assertTrue(study.uninterrupted_run([{"kind": "resume", "after_s": 1}]))
        for actions in (None, [], [{"kind": "reset"}],
                        [{"kind": "resume"}, {"kind": "pause"}]):
            self.assertFalse(study.uninterrupted_run(actions))


if __name__ == "__main__":
    unittest.main()
