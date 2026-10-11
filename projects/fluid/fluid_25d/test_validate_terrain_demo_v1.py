"""CPU-only evidence-helper tests; no subprocess or GPU launch."""
import unittest

import validate_terrain_demo_v1 as study


class EvidenceHelpers(unittest.TestCase):
    def test_bounded_percentiles_ignore_nonfinite(self):
        self.assertIsNone(study.percentiles([]))
        result = study.percentiles([1, 2, 3, float("nan"), float("inf")])
        self.assertEqual(result["count"], 3)
        self.assertEqual(result["p50"], 2)
        self.assertAlmostEqual(result["p95"], 2.9)
        self.assertEqual(result["max"], 3)


if __name__ == "__main__":
    unittest.main()
