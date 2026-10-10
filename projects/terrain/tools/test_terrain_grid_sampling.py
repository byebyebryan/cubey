import unittest
import numpy as np

from terrain_grid_sampling import grid_geometry, sample_grid


class SamplingTests(unittest.TestCase):
    def test_planar_world_sampling_and_orientation(self):
        raw = np.arange(5, dtype=np.float32)[None, :] + 10 * np.arange(4, dtype=np.float32)[:, None]
        source = {"grid": {"sample_spacing_m": 8, "sample_origin_x_m": -16,
                            "sample_origin_z_m": -8}, "height": {"offset_m": 2, "scale": .5}}
        g = grid_geometry(5, 3, 4, (0, 0), (-16, -8, 16, 16))
        result = sample_grid(source, raw, g)
        x = np.arange(5) * 4 - 8
        z = np.arange(3) * 4 - 4
        expected = ((x[None, :] + 16) / 8 + 10 * (z[:, None] + 8) / 8 + 2) * .5
        np.testing.assert_allclose(result, expected)

    def test_nested_footprints_shift_larger_at_edge(self):
        small = grid_geometry(1024, 1024, 16, (11520, 19200), (-30600, -30600, 30600, 30600))
        big = grid_geometry(2048, 2048, 16, (11520, 19200), (-30600, -30600, 30600, 30600))
        for axis in range(2):
            self.assertLessEqual(big["sample_origin_xz_m"][axis], small["sample_origin_xz_m"][axis])
            self.assertGreaterEqual(big["sample_origin_xz_m"][axis] + 2047 * 16,
                                    small["sample_origin_xz_m"][axis] + 1023 * 16)

    def test_rejects_out_of_coverage_and_invalid_geometry(self):
        for size, spacing in ((2048, 32), (4096, 8), (512, float("nan")), (512, 0)):
            with self.assertRaises(ValueError):
                grid_geometry(size, size, spacing, (0, 0), (-30000, -30000, 30000, 30000))


if __name__ == "__main__":
    unittest.main()
