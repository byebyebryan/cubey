import unittest

import numpy as np

from check_synxflow_source_correspondence_v1 import compare_field


class CorrespondenceTests(unittest.TestCase):
    def test_controls_precision_and_local_failure(self):
        expected = np.array([0.0, 0.01, 2.0])
        self.assertTrue(compare_field(expected, expected, mountain=False, depth=True)["exact"])
        self.assertTrue(compare_field(expected + 1e-6, expected, mountain=False, depth=True)["passed"])
        self.assertFalse(compare_field(expected + 3e-6, expected, mountain=False, depth=True)["passed"])

    def test_mountain_gate_tests_l1_and_local_max(self):
        expected = np.ones((100,))
        self.assertTrue(compare_field(expected * 1.0005, expected, mountain=True, depth=True)["passed"])
        self.assertFalse(compare_field(expected * 1.002, expected, mountain=True, depth=True)["passed"])
        observed = expected.copy()
        observed[0] += 0.02
        self.assertFalse(compare_field(observed, expected, mountain=True, depth=True)["passed"])
        self.assertTrue(compare_field(expected * 1.002, expected, mountain=True, depth=False)["passed"])

    def test_nonfinite_and_mismatched_shapes_rejected(self):
        for observed in (np.array([np.nan]), np.array([np.inf]), np.ones(2)):
            with self.assertRaises(ValueError):
                compare_field(observed, np.ones(1), mountain=False, depth=True)
        with self.assertRaises(ValueError):
            compare_field(np.ones(1), np.array([np.nan]), mountain=False, depth=True)


if __name__ == "__main__":
    unittest.main()
