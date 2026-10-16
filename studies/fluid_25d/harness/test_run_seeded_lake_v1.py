"""CPU-only initial lake geometry checks; run in the native study environment."""

import unittest

import numpy as np
import run_seeded_lake_v1 as lake


class LakeTests(unittest.TestCase):
    def setUp(self):
        self.bed = np.full((7, 7), 10.0)
        self.bed[2:5, 2:5] = np.array([[4, 3, 4], [3, 0, 3], [4, 3, 4]])
        self.mask = np.zeros_like(self.bed, dtype=bool)
        self.mask[3, 3] = True

    def test_horizontal_surface_not_uniform_depth(self):
        depth, receipt = lake.lake_depth(self.bed, self.mask, 10, 0.5)
        wet = depth > 0
        np.testing.assert_allclose(self.bed[wet] + depth[wet], 5.0)
        self.assertEqual(int(wet.sum()), 9)
        self.assertEqual(depth[3, 3], 5)
        self.assertEqual(depth[2, 2], 1)
        self.assertFalse(receipt["terrain_modified"])

    def test_mask_only_selects_floor_and_does_not_clip_water(self):
        depth, _ = lake.lake_depth(self.bed, self.mask, 10, 0.5)
        self.assertEqual(int((depth > 0).sum()), 9)
        self.assertEqual(int(self.mask.sum()), 1)

    def test_unconnected_low_patch_is_not_seeded(self):
        self.bed[1, 1] = -2
        depth, _ = lake.lake_depth(self.bed, self.mask, 10, 0.5)
        self.assertEqual(depth[1, 1], 0)

    def test_open_path_to_perimeter_rejects(self):
        self.bed[:3, 3] = 1
        with self.assertRaisesRegex(ValueError, "perimeter"):
            lake.lake_depth(self.bed, self.mask, 10, 0.5)

    def test_below_stage_disconnected_by_diagonal_uses_d4(self):
        self.bed[1, 1] = 1
        depth, _ = lake.lake_depth(self.bed, self.mask, 10, 0.5)
        self.assertEqual(depth[1, 1], 0)

    def test_invalid_inputs_reject(self):
        for fraction in (0, 1, -1, float("nan"), float("inf")):
            with self.assertRaises(ValueError):
                lake.lake_depth(self.bed, self.mask, 10, fraction)
        for spill in (0, -1, float("nan")):
            with self.assertRaises(ValueError):
                lake.lake_depth(self.bed, self.mask, spill, 0.5)
        with self.assertRaises(ValueError):
            lake.lake_depth(self.bed, np.zeros_like(self.mask), 10, 0.5)
        with self.assertRaises(ValueError):
            lake.lake_depth(self.bed, np.ones((3, 3)), 10, 0.5)


if __name__ == "__main__":
    unittest.main()
