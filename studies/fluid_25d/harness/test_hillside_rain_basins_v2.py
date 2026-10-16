import unittest

import numpy as np

import hillside_rain_basins_v2 as study


class RainBasinsV2Tests(unittest.TestCase):
    def test_bowl_is_analytical_and_bed_unchanged(self):
        bed = np.full((7, 7), 3, dtype=np.float32)
        bed[2:5, 2:5] = 1
        original = bed.copy()
        result, labels = study.read_crop(bed, 2)
        np.testing.assert_array_equal(bed, original)
        self.assertEqual(result["basins"]["component_count"], 1)
        self.assertEqual(result["basins"]["fill_volume_m3"], 72)
        self.assertEqual(np.count_nonzero(labels), 9)
        self.assertIn("not observed water", result["meaning"])
        stages = result["ranked_observation_hypotheses"][0]["analytical_partial_stages"]
        self.assertEqual(stages[0]["floor_connected_cells"], 9)
        self.assertAlmostEqual(stages[0]["storage_to_stage_m3"], 3.6)

    def test_open_incline_has_no_depression(self):
        bed = np.tile(np.arange(7, 0, -1, dtype=np.float32), (7, 1))
        result, labels = study.read_crop(bed)
        self.assertEqual(result["basins"]["component_count"], 0)
        self.assertFalse(labels.any())

    def test_observed_volume_not_cell_fraction(self):
        bed = np.zeros((3, 3))
        depth = np.full((3, 3), .001)
        depth[1, 1] = 2
        velocity = np.zeros((3, 3, 2))
        velocity[1, 1, 0] = 1
        result = study.observe_water(bed, depth, velocity, np.ones((3, 3), dtype=bool), .01, 1)
        self.assertAlmostEqual(result["moving_volume_m3"], 2)
        self.assertAlmostEqual(result["slow_volume_m3"], .008)
        self.assertEqual(result["largest_connected_footprint_cells"], 1)
        self.assertAlmostEqual(result["net_storage_beyond_own_rain_m3"], 1.918)

    def test_surface_variation_prevents_pond_inference(self):
        bed = np.tile(np.arange(3, dtype=float), (3, 1))
        result = study.observe_water(bed, np.ones((3, 3)), np.zeros((3, 3, 2)),
                                     np.ones((3, 3), dtype=bool), 0, 1)
        self.assertEqual(result["largest_connected_footprint_cells"], 9)
        self.assertEqual(result["largest_connected_surface_range_m"], [1, 3])
        self.assertIn("do not establish", result["limits"])

    def test_empty_footprint(self):
        result = study.observe_water(np.zeros((3, 3)), np.zeros((3, 3)),
                                     np.zeros((3, 3, 2)), np.ones((3, 3), dtype=bool), 0)
        self.assertEqual(result["largest_connected_footprint_cells"], 0)
        self.assertIsNone(result["largest_connected_surface_range_m"])

    def test_invalid_inputs(self):
        for bed in (np.zeros((2, 2)), np.full((3, 3), np.nan)):
            with self.assertRaises(ValueError):
                study.read_crop(bed)
        with self.assertRaises(ValueError):
            study.read_crop(np.zeros((3, 3)), 0)
        with self.assertRaises(ValueError):
            study.observe_water(np.zeros((3, 3)), -np.ones((3, 3)),
                                np.zeros((3, 3, 2)), np.ones((3, 3), dtype=bool), 0)


if __name__ == "__main__":
    unittest.main()
