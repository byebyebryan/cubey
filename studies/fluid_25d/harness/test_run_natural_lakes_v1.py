"""CPU-only dry-start schedule and unbiased collection reporting checks."""

import unittest

import numpy as np
import run_natural_lakes_v1 as study


class NaturalLakeTests(unittest.TestCase):
    def test_dry_start_has_no_selected_lake(self):
        s = study.spec()
        self.assertEqual(s["initial_condition"]["initial_volume_m3"], 0)
        self.assertEqual(s["initial_condition"]["seeded_basins"], [])
        self.assertEqual(s["boundary"], "fall")

    def test_storm_taper_and_recession(self):
        s = study.spec()
        study.recession.validate_history(s["rainfall_history"], study.END, study.RATE)
        for time in (study.TAPER_END, study.END):
            rate, depth, phase = study.recession.history_value(
                s["rainfall_history"], time
            )
            self.assertEqual(rate, 0)
            self.assertEqual(phase, "Off")
            self.assertAlmostEqual(depth, 0.961)

    def test_fields_report_multiple_basins_not_only_twenty(self):
        h = np.zeros((8, 8))
        h[1:3, 1:3] = 2
        h[5:7, 5:7] = 4
        labels = np.zeros_like(h, dtype=int)
        labels[1:3, 1:3] = 7
        labels[5:7, 5:7] = 90
        result = study.field_metrics(
            np.zeros_like(h), h, np.zeros_like(h), np.zeros_like(h), labels
        )
        self.assertEqual(
            [b["label"] for b in result["analytical_depression_observations"]], [90, 7]
        )
        self.assertEqual(len(result["observed_depth_connected_patches"]), 2)
        self.assertEqual(result["slow_water_volume_fraction"], 1)
        self.assertAlmostEqual(result["stored_m3"], 21600)

    def test_dry_fields_are_valid_but_dry_momentum_is_not(self):
        zero = np.zeros((4, 4))
        result = study.field_metrics(zero, zero, zero, zero, zero.astype(int))
        self.assertEqual(result["stored_m3"], 0)
        self.assertEqual(result["analytical_depression_observations"], [])
        with self.assertRaises(ValueError):
            study.field_metrics(zero, zero, np.ones_like(zero), zero, zero.astype(int))

    def test_moving_collection_not_called_calm(self):
        h = np.ones((4, 4))
        result = study.field_metrics(
            np.arange(16).reshape(4, 4),
            h,
            2 * h,
            np.zeros_like(h),
            np.ones_like(h, dtype=int),
        )
        self.assertEqual(result["slow_water_volume_fraction"], 0)
        b = result["analytical_depression_observations"][0]
        self.assertEqual(b["water_weighted_speed_m_per_s"], 2)
        self.assertGreater(b["surface_p90_minus_p10_m"], 1)

    def test_saved_cadence_covers_report_knots(self):
        times = list(range(0, study.END + 1, study.CADENCE))
        self.assertEqual(len(times), 145)
        self.assertTrue(all(t in times for t in study.TIMES))

    def test_every_saved_field_health_check_rejects_bad_values(self):
        zero = np.zeros((4, 4))
        for value in (float("nan"), float("inf")):
            bad = zero.copy()
            bad[1, 1] = value
            with self.assertRaises(ValueError):
                study.validate_fields(zero, bad, zero)
        bad = zero.copy()
        bad[1, 1] = -0.01
        with self.assertRaises(ValueError):
            study.validate_fields(bad, zero, zero)


if __name__ == "__main__":
    unittest.main()
