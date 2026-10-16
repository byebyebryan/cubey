"""Numerical tests use the existing optional scientific environment, not Torch."""
import unittest
import numpy as np
import study_terrain_atlas_v1 as atlas


class FilterTests(unittest.TestCase):
    def test_constant_and_affine(self):
        z, x = np.mgrid[:100, :120]
        values = (7+2*x+3*z).astype(np.float32)
        xs = np.array([411., 714., 1234.]); zs = np.array([378., 812.])
        actual = atlas.filtered(values, (15., -10.), xs, zs)
        expected = 7+2*(xs[None, :]-15)/30+3*(zs[:, None]+10)/30
        np.testing.assert_allclose(actual, expected, rtol=1e-7)
        np.testing.assert_allclose(atlas.filtered(np.ones_like(values), (15., -10.), xs, zs), 1)

    def test_native_phase_and_tile_invariance(self):
        rng = np.random.default_rng(0)
        values = rng.normal(size=(200, 200)).astype(np.float32)
        xs = np.array([1000., 1500.]); zs = np.array([1100., 1600.])
        a = atlas.filtered(values, (0., 0.), xs, zs)
        b = atlas.filtered(values[8:100, 8:100], (240., 240.), xs, zs)
        np.testing.assert_array_equal(a, b)

    def test_no_edge_clamping(self):
        with self.assertRaises(ValueError):
            atlas.filtered(np.ones((20, 20)), (0, 0), [0], [0])


if __name__ == "__main__": unittest.main()
