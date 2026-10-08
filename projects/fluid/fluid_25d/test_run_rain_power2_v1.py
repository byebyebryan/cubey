"""CPU-only checks for power-of-two rainfall schedules and mixed save cadences."""

import tempfile
import unittest
from pathlib import Path

import numpy as np
import run_rain_power2_v1 as study


class Power2RainTests(unittest.TestCase):
    def test_exact_equal_total_with_fractional_tapers(self):
        cases = [study.case_spec(rate) for rate in study.RATES]
        study.previous.assert_equal_total(cases)
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

    def test_native_rain_text_preserves_fractional_knots_exactly(self):
        for rate in (512, 1024):
            history = study.case_spec(rate)["rainfall_history"]
            lines = study.encode_rain_history(history).splitlines()
            self.assertEqual(lines[0], "1")
            self.assertEqual(
                [[float(v) for v in line.split()] for line in lines[1:]], history
            )
            rounded = float(f"{history[2][0]:.2f}")
            self.assertNotEqual(rounded, history[2][0])

    def test_exact_endpoints_have_regular_saved_states(self):
        for rate, storm, end, frames in (
            (512, 6750, 21150, 95),
            (1024, 3375, 17775, 80),
        ):
            s = study.case_spec(rate)
            self.assertEqual(s["storm_end_s"], storm)
            self.assertEqual(s["duration_s"], end)
            times = list(range(0, end + 1, s["output_interval_s"]))
            self.assertEqual(len(times), frames)
            self.assertTrue(all(t in times for t in (study.COMPARE_TIME, storm, end)))
            self.assertEqual(
                study.recession.history_value(
                    s["rainfall_history"], study.COMPARE_TIME
                )[2],
                "On",
            )

    def test_reference_is_not_relabelled_or_resampled(self):
        self.assertEqual(study.case_spec(480), study.previous.case_spec(480))
        self.assertEqual(study.case_spec(480)["output_interval_s"], 300)

    def test_dry_initial_and_other_settings_unchanged(self):
        base = study.natural.spec()
        for rate in (512, 1024):
            s = study.case_spec(rate)
            for key in (
                "rows",
                "cols",
                "dx_m",
                "boundary",
                "manning_n",
                "crop_xzwh",
                "initial_condition",
            ):
                self.assertEqual(s[key], base[key])

    def test_mixed_cadence_video_holds_reference_without_inventing_fields(self):
        timeline = study.presentation.timeline(0, 5, study.CADENCE, 300)
        self.assertEqual(
            [r["requested_time_s"] for r in timeline], [0, 225, 450, 675, 900]
        )
        self.assertEqual(
            [r["saved_field_time_s"] for r in timeline], [0, 0, 300, 600, 900]
        )
        s = study.case_spec(1024)
        timeline = study.presentation.timeline(
            0, 80, study.CADENCE, s["output_interval_s"]
        )
        self.assertEqual(timeline[-1]["saved_field_time_s"], s["duration_s"])

    def test_unknown_rate_and_non_dry_start_rejected(self):
        with self.assertRaises(ValueError):
            study.case_spec(2048)
        zero = np.zeros((4, 4))
        with self.assertRaisesRegex(ValueError, "not completely dry"):
            study.summarize(
                [(0, zero + 1, zero, zero)],
                study.case_spec(512),
                zero,
                np.full((4, 4), 20),
            )

    def test_missing_states_rejected(self):
        zero = np.zeros((4, 4))
        with self.assertRaisesRegex(ValueError, "missing or unexpected"):
            study.summarize(
                [(0, zero, zero, zero)], study.case_spec(512), zero, np.full((4, 4), 20)
            )

    def test_recapture_rejects_each_existing_artifact_without_writing(self):
        for name in ("media", "capture-manifest.json", "index.html"):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                out = Path(temp)
                study.require_fresh_presentation_outputs(out)
                artifact = out / name
                if name == "media":
                    artifact.mkdir()
                else:
                    artifact.write_text("retained evidence")
                with self.assertRaisesRegex(FileExistsError, name):
                    study.require_fresh_presentation_outputs(out)
                self.assertEqual(list(out.iterdir()), [artifact])
                if artifact.is_file():
                    self.assertEqual(artifact.read_text(), "retained evidence")

    def test_recapture_rejects_dangling_artifact_symlink(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            (out / "index.html").symlink_to(out / "missing.html")
            with self.assertRaisesRegex(FileExistsError, "index.html"):
                study.require_fresh_presentation_outputs(out)


if __name__ == "__main__":
    unittest.main()
