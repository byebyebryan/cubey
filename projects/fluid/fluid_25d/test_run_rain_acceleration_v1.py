"""CPU-only checks for frozen equal-total rainfall comparisons."""

import unittest

import numpy as np
import run_rain_acceleration_v1 as study


class RainAccelerationTests(unittest.TestCase):
    def test_exact_equal_rain_total_including_taper(self):
        cases = [study.case_spec(r) for r in study.RATES]
        study.assert_equal_total(cases)
        for s in cases:
            history = s["rainfall_history"]
            study.recession.validate_history(
                history, s["duration_s"], s["rain_rate_m_per_s"]
            )
            self.assertAlmostEqual(
                study.recession.history_value(history, s["storm_end_s"])[1], 0.96
            )
            self.assertAlmostEqual(
                study.recession.history_value(history, s["duration_s"])[1], 0.961
            )
            self.assertEqual(s["duration_s"] - s["storm_end_s"], 14400)

    def test_same_cadence_and_dry_unmodified_initial_state(self):
        for rate, frames in zip(study.RATES, (145, 97, 73)):
            s = study.case_spec(rate)
            self.assertEqual(s["output_interval_s"], study.CADENCE)
            self.assertEqual(s["duration_s"] // study.CADENCE + 1, frames)
            self.assertEqual(s["initial_condition"]["initial_volume_m3"], 0)
            self.assertEqual(s["initial_condition"]["seeded_basins"], [])
            self.assertFalse(s["initial_condition"]["terrain_modified"])
            self.assertEqual(s["boundary"], "fall")
            self.assertEqual(s["manning_n"], 0.05)

    def test_baseline_schedule_is_identical_to_existing_run(self):
        s = study.case_spec(120)
        for key in (
            "rainfall_history",
            "duration_s",
            "output_interval_s",
            "initial_condition",
        ):
            self.assertEqual(s[key], study.natural.spec()[key])

    def test_onset_brackets_not_interpolated_exact_time(self):
        rows = [
            {"time_s": t, "area": a}
            for t, a in ((0, 0), (300, 0.49), (600, 0.51), (900, 0.48))
        ]
        self.assertEqual(
            study.first_crossing(rows, "area", 0.5),
            {"after_s": 300, "at_or_before_s": 600},
        )
        self.assertIsNone(study.first_crossing(rows, "area", 1))

    def test_unplanned_rate_rejected(self):
        with self.assertRaises(ValueError):
            study.case_spec(960)

    def test_mismatched_total_rejected(self):
        case = study.case_spec(240)
        case["rainfall_history"][1][0] += 300
        with self.assertRaises(ValueError):
            study.assert_equal_total([case])

    def test_missing_saved_states_and_non_dry_start_rejected(self):
        zero = np.zeros((4, 4))
        labels = np.full((4, 4), 20)
        with self.assertRaisesRegex(ValueError, "missing or unexpected"):
            study.summarize([(0, zero, zero, zero)], study.case_spec(240), zero, labels)
        with self.assertRaisesRegex(ValueError, "not completely dry"):
            study.summarize(
                [(0, zero + 1, zero, zero)], study.case_spec(240), zero, labels
            )


if __name__ == "__main__":
    unittest.main()
