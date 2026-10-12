"""Portable contract checks; no optional solver import or execution."""
import unittest
from pathlib import Path
import study_wider_water_v1 as study


class ContractTests(unittest.TestCase):
    def test_common_extent_and_nested_samples(self):
        a, b = study.geometry("pilot-64m"), study.geometry("wide-32m")
        self.assertEqual(a["drawn_span_m"], 61184)
        self.assertEqual(a["drawn_span_m"], b["drawn_span_m"])
        self.assertEqual(a["sample_origin_xz_m"], b["sample_origin_xz_m"])
        self.assertEqual((a["width"]-1)*2+1, b["width"])

    def test_source_climate_and_cell_bounds(self):
        for name in study.CANDIDATES:
            g = study.geometry(name)
            self.assertLess(g["cells"], 4194304)
            self.assertLess(g["drawn_span_m"], 255*240)
            self.assertGreater((2047*30-g["drawn_span_m"])/2, g["spacing_m"]/2)
            self.assertEqual(g["spacing_m"] & (g["spacing_m"]-1), 0)

    def test_milestones(self):
        self.assertEqual(sorted(set(study.MILESTONES)), list(study.MILESTONES))
        self.assertEqual(study.MILESTONES[-1], 72000)

    def test_bounded_outputs(self):
        self.assertEqual(study.owned_root(study.ROOT / "outputs/terrain/wider-water-unit"),
                         study.ROOT / "outputs/terrain/wider-water-unit")
        for p in (study.ROOT, study.ROOT / "outputs/terrain", Path("/tmp/wider-water-unit")):
            with self.assertRaises(ValueError):
                study.owned_root(p)


if __name__ == "__main__":
    unittest.main()
