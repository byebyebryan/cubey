import unittest

import run_native_bank_reconstruction_v2 as study
import analyze_native_bank_reconstruction_v2 as analysis


class ReconstructionPlanTests(unittest.TestCase):
    def test_depth_band_classifier_uses_encoded_capture_colors(self):
        for rgb in ((89,124,255),(0,231,231)):
            self.assertTrue(analysis.high_depth_band(*rgb))
        for rgb in ((137,137,137),(36,48,61),(255,231,0),(243,0,218),(95,88,70)):
            self.assertFalse(analysis.high_depth_band(*rgb))

    def test_modes_are_explicit_and_bounded(self):
        self.assertEqual([study.mode_name(*mode) for mode in study.MODES],
                         ["triangular", "bilinear", "bspline-1x", "bspline-2x", "bspline-4x"])
        self.assertNotIn("--fluid25d-native-surface-subdivision", study.mode_args("triangular", 1))
        self.assertEqual(study.mode_args("bspline", 4)[-2:],
                         ["--fluid25d-native-surface-subdivision", "4"])
        for mode in (("triangular",2),("bilinear",4),("bspline",3),("invalid",1)):
            with self.assertRaises(ValueError):
                study.mode_args(*mode)

    def test_small_review_has_motion_and_recession(self):
        motion, recession = study.study_plan()
        self.assertTrue(motion[1])
        self.assertFalse(recession[1])
        self.assertEqual(motion[0]["render_interval_s"], 5)
        self.assertEqual(motion[0]["saved_field_interval_s"], 60)
        self.assertFalse(motion[0]["physical_field_interpolation"])
        self.assertEqual(recession[0]["requested_start_time_s"], 7260)
        self.assertEqual(recession[0]["requested_end_time_s"], 14400)
        self.assertEqual(recession[0]["case"], "rain-off")


if __name__ == "__main__":
    unittest.main()
