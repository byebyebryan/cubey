"""Optional NumPy/SciPy tests; no native runtime import or GPU execution."""
import unittest
import tempfile
from pathlib import Path
import numpy as np

from survey_terrain_sites_v1 import water_metrics, bounds_for, prepare_presets
from terrain_water_sites import DEFAULT_CATALOGUE


class Diagnostics(unittest.TestCase):
    def test_metrics_separate_slow_deep_pool_and_fast_stream(self):
        fields = np.zeros((3, 4, 4), dtype="<f4")
        fields[0, :2, :2] = 2
        fields[0, 3, :] = .2
        fields[1, 3, :] = .4
        result = water_metrics(fields.tobytes(), (4, 4), 16)
        self.assertAlmostEqual(result["water_volume_m3"], 8.8*256, places=3)
        self.assertEqual(result["largest_slow_deep_patch_km2"], 4*256/1e6)
        self.assertEqual(result["flowing_km2"], 4*256/1e6)
        self.assertEqual(result["wet_speed_p95_m_per_s"], 2)

    def test_dry_and_invalid_states(self):
        fields = np.zeros((3, 3, 3), dtype="<f4")
        self.assertEqual(water_metrics(fields.tobytes(), (3, 3), 32)["water_volume_m3"], 0)
        for value in (-1, float("nan")):
            fields[0, 0, 0] = value
            with self.assertRaises(ValueError):
                water_metrics(fields.tobytes(), (3, 3), 32)
        fields[:] = 0
        fields[1, 0, 0] = 1
        with self.assertRaises(ValueError):
            water_metrics(fields.tobytes(), (3, 3), 32)

    def test_source_and_climate_intersection(self):
        a = {"grid": {"sample_origin_x_m": -100, "sample_origin_z_m": -100,
                      "width": 11, "height": 11, "sample_spacing_m": 20}}
        b = {"grid": {"sample_origin_x_m": -80, "sample_origin_z_m": -80,
                      "width": 9, "height": 9, "sample_spacing_m": 20}}
        self.assertEqual(bounds_for(a, b), (-80, -80, 80, 80))

    def test_preset_preparation_is_gated_before_any_case_write(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaisesRegex(ValueError, "finish default-grid confirmation"):
                prepare_presets(root, DEFAULT_CATALOGUE, None)
            self.assertEqual(list(root.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
