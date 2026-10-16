"""Host tests for raster classification/fixtures; GPU tests are separate."""
import json
from pathlib import Path
import tempfile
import unittest

import run_native_shoreline_raster_v1 as raster


class ShorelineRasterTests(unittest.TestCase):
    def test_components_four_neighbors_and_singletons_reported(self):
        self.assertEqual(raster.component_sizes(bytes([1,0,0,1]), 2, 2), [1,1])
        self.assertEqual(raster.component_sizes(bytes([1,1,0,1]), 2, 2), [3])
        self.assertEqual(raster.component_sizes(bytes(4), 2, 2), [])
        with self.assertRaises(ValueError):
            raster.component_sizes(bytes(3), 2, 2)

    def test_film_gap_is_numerically_wet_but_below_visible_band(self):
        bed, h = raster.fields("positive-film-gap")
        self.assertTrue(all(v == 0 for v in bed))
        self.assertTrue(all(v > 0.000001 for v in h))
        self.assertEqual(h[10*raster.COLS+11], 0.001)
        self.assertEqual(h[10*raster.COLS+10], 0.1)

    def test_recession_pointwise_decreases(self):
        early = raster.fields("recession-early")[1]
        late = raster.fields("recession-late")[1]
        self.assertTrue(any(early))
        self.assertTrue(all(b <= a for a,b in zip(early,late,strict=True)))

    def test_partial_dry_lake_analytic_stage(self):
        bed, h = raster.fields("partial-dry-lake")
        self.assertTrue(any(v == 0 for v in h))
        self.assertTrue(any(v > 0 for v in h))
        self.assertTrue(all(abs(b+d-.36)<1e-12 for b,d in zip(bed,h,strict=True) if d > 0))

    def test_converter_fixture_has_real_hashes_and_synthetic_identity(self):
        with tempfile.TemporaryDirectory() as temporary:
            manifest = raster.fixture(Path(temporary), "positive-film-gap")
            document = json.loads(manifest.read_text())
            self.assertEqual(document["grid"]["width"], raster.COLS)
            self.assertEqual(document["grid"]["height"], raster.ROWS)
            self.assertIn("synthetic analytic", document["provenance"]["terrain_source_identity"]["transform"])
            self.assertEqual([f["time_s"] for f in document["frames"]], [0,1,2])
            self.assertEqual(document["frames"][1]["max_depth_m"], document["frames"][2]["max_depth_m"])


if __name__ == "__main__":
    unittest.main()
