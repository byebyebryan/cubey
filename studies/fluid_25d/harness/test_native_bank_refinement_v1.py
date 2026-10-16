"""Host helpers and optional isolated-environment mask controls."""
from pathlib import Path
import unittest

import run_native_bank_refinement_v1 as study

try:
    import numpy as np
    from shapely.geometry import MultiPolygon, Polygon, box
    import PIL
    import scipy
    HAVE_BAKE = True
except ImportError:
    HAVE_BAKE = False


class Routing(unittest.TestCase):
    def test_reference_has_no_sidecar(self):
        self.assertNotIn("--fluid25d-native-display-coverage", study.mode_args(Path("out"), "rain-on", "reference"))

    def test_bspline_stays_independent(self):
        args = study.mode_args(Path("out"), "rain-on", "bspline-2x")
        self.assertNotIn("--fluid25d-native-display-coverage", args)
        self.assertEqual(args[-2:], ["--fluid25d-native-surface-subdivision", "2"])

    def test_case_specific_sidecar(self):
        args = study.mode_args(Path("out"), "rain-off", "moderate", "bake-final")
        self.assertEqual(args[-1], "out/bake-final/rain-off-moderate/coverage.json")


@unittest.skipUnless(HAVE_BAKE, "optional isolated bake libraries unavailable")
class RasterControls(unittest.TestCase):
    def test_levels_and_quadrature_bound(self):
        levels = study.display_levels()
        self.assertTrue(np.all(np.diff(levels) > 0))
        h = np.linspace(.002, .05, 1001)
        t = (h-.002)/.048
        expected = t*t*(3-2*t)
        reconstructed = (h[:,None] > levels[None,:]).mean(axis=1)
        self.assertLessEqual(np.max(np.abs(reconstructed-expected)), 1/24 + 1e-12)

    def test_wet_island_survives_component_order(self):
        outer = Polygon([(1,1),(11,1),(11,11),(1,11)], [[(3,3),(9,3),(9,9),(3,9)]])
        island = box(5,5,7,7)
        a = study.raster_shape(MultiPolygon([island, outer]), 13, 13)
        b = study.raster_shape(MultiPolygon([outer, island]), 13, 13)
        np.testing.assert_array_equal(a, b)
        self.assertEqual(a[6*4,6*4], 1)
        self.assertEqual(a[4*4,4*4], 0)
        self.assertEqual(a[2*4,2*4], 1)

    def test_empty_full_and_node_aligned_domain(self):
        empty = study.raster_shape(Polygon(), 8, 5)
        full = study.raster_shape(box(0,0,7,4), 8, 5)
        self.assertEqual(empty.shape, (17,29))
        np.testing.assert_array_equal(empty, np.zeros((17,29)))
        np.testing.assert_array_equal(full, np.ones((17,29)))


if __name__ == "__main__":
    unittest.main()
