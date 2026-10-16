import json
from pathlib import Path
import tempfile
import unittest

import analyze_live_perf_terrain_v1 as analysis


class Analysis(unittest.TestCase):
    def test_spread_retains_repeats_and_missing_evidence(self):
        self.assertEqual(analysis.spread([None]), {"n": 0})
        self.assertEqual(analysis.spread([2, 4, None]), {"n": 2, "median": 3, "min": 2, "max": 4})

    def test_resource_trend_skips_startup(self):
        row = {"measurement_wall_s": 600, "samples": [
            {"wall_s": 0, "viewer_rss_mib": 1}, {"wall_s": 100, "viewer_rss_mib": 100},
            {"wall_s": 500, "viewer_rss_mib": 101}]}
        trend = analysis.resource_trend(row)
        self.assertEqual(trend["viewer_rss_mib"]["median_change"], 1)
        self.assertIsNone(trend["worker_rss_mib"]["median_change"])

    def test_analysis_does_not_rewrite_trial_receipts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            trial = root / "coverage/example"
            trial.mkdir(parents=True)
            raw = json.dumps({"case": "example", "mode": "live", "status": "timeout",
                              "identity": {"spec": {"rows": 4, "cols": 5}}, "errors": []}).encode()
            (trial / "result.json").write_bytes(raw)
            result = analysis.analyze(root)
            self.assertEqual(result["trials"][0]["status"], "timeout")
            self.assertEqual(result["trials"][0]["triangles_per_display_layer_derived"], 96)
            self.assertEqual((trial / "result.json").read_bytes(), raw)
            self.assertEqual(result["phases"][0]["measures"]["pace_x"], {"n": 0})


if __name__ == "__main__":
    unittest.main()
