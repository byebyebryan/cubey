"""Optional study-environment tests; no heavy dependency added to Cubey."""
import unittest
try:
    import numpy as np
    import run_shoreline_boundary_study_v1 as study
except ModuleNotFoundError:
    np = study = None


@unittest.skipIf(study is None, "Optional boundary study dependencies require the isolated study environment")
class BoundaryTests(unittest.TestCase):
    def test_scalar_contour_retains_fractional_crossings(self):
        h = np.zeros((4,4))
        h[1:3,1:3] = .1
        curves = study.extract(h,.026,study.METHODS[0])
        self.assertEqual(len(curves),1)
        self.assertTrue(any(abs(c-.26)<1e-12 for c in curves[0].ravel()))

    def test_reference_fixtures_have_expected_topology(self):
        for name,h in study.fixtures().items():
            for method in study.METHODS:
                with self.subTest(name=name,method=method):
                    if name == "diagonal-separated" and method == study.METHODS[1]:
                        with self.assertRaisesRegex(ValueError,"branching contour"):
                            study.extract(h,.026,method)
                        continue
                    curves = study.extract(h,.026,method)
                    shape = study.filled(curves,h,.026)
                    expected = (2,0) if name in ("positive-film-gap","dry-gap","diagonal-separated") else (1,1) if name=="island-ring" else (1,0)
                    self.assertEqual(study.topology(shape),expected)
                    self.assertTrue(study.linework_simple(curves))

    def test_constraint_is_total_not_per_iteration(self):
        curves = [np.array([[1.,1.],[3.,1.],[3.,3.],[1.,3.],[1.,1.]])]
        variant = {"iterations":100,"distance_cells":.1}
        result = study.constrained(curves,variant,.25)
        self.assertLessEqual(np.max(np.linalg.norm(result[0]-curves[0],axis=1)),.100000001)

    def test_open_contour_endpoints_are_anchored(self):
        curves = [np.array([[0.,0.],[1.,1.],[2.,0.]])]
        result = study.constrained(curves,{"iterations":10,"distance_cells":.25},.25)
        np.testing.assert_array_equal(result[0][[0,-1]],curves[0][[0,-1]])

    def test_no_water_and_full_water(self):
        for value,area in ((0,0),(.1,9)):
            h = np.full((4,4),value,dtype=float)
            for method in study.METHODS:
                curves = study.extract(h,.026,method)
                self.assertAlmostEqual(study.filled(curves,h,.026).area,area)

    def test_self_intersection_detected(self):
        self.assertFalse(study.linework_simple([np.array([[0,0],[1,1],[0,1],[1,0],[0,0]])]))

    def test_moved_face_keeps_original_region_label(self):
        h = np.zeros((5,5))
        h[2,2] = .1
        raw = study.extract(h,.09,study.METHODS[0])
        moved = [c+np.array([.6,0]) for c in raw]
        shape = study.filled(moved,h,.09,raw)
        self.assertEqual(study.topology(shape),(1,0))
        self.assertAlmostEqual(shape.area,study.filled(raw,h,.09).area)
        self.assertTrue(study.filled(moved,h,.09).is_empty)

    def test_moved_ring_preserves_hole_label(self):
        h = study.fixtures()["island-ring"]
        raw = study.extract(h,.026,study.METHODS[0])
        curves = study.constrained(raw,{"iterations":10,"distance_cells":.1},.25)
        self.assertEqual(study.topology(study.filled(curves,h,.026,raw)),(1,1))


if __name__ == "__main__":
    unittest.main()
