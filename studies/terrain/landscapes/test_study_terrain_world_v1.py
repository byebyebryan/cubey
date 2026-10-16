"""Portable preparation guards; no optional producer imports or GPU work."""
import unittest
from pathlib import Path
import study_terrain_world_v1 as study


class StudyTests(unittest.TestCase):
    def test_owned_fresh_leaf(self):
        expected = study.ROOT / "outputs/terrain/world-sites-unit"
        self.assertEqual(study.owned(expected), expected)
        for p in (study.ROOT, study.ROOT / "outputs/terrain", Path("/tmp/world-sites-unit")):
            with self.assertRaises(ValueError): study.owned(p)

    def test_model_world_mapping_round_trip(self):
        recipe = {"coordinate_mapping": {"world_origin_xz_m": [-30705,-30705],
                  "model_origin_ij": [30720,-28672], "native_spacing_m": 30}}
        for p in ((0,0), (11520,19200), (-100000,100000)):
            native = study.coverage.native_point(recipe, p)
            result = study.world_point(recipe, *native)
            for a, b in zip(p, result): self.assertAlmostEqual(a,b,places=8)

    def test_bound_and_headless_contract(self):
        self.assertEqual(study.LIMIT_SECONDS, 900)
        self.assertTrue(study.REFERENCE.is_relative_to(study.ROOT / "cache"))
        self.assertTrue(study.ENV.is_relative_to(study.ROOT / "cache"))


if __name__ == "__main__": unittest.main()
