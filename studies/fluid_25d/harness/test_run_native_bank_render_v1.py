from pathlib import Path
import tempfile
import unittest

import run_native_bank_render_v1 as bank


class BankReviewPlanTests(unittest.TestCase):
    def test_small_review_is_three_matched_stories(self):
        plan = bank.review_plan()
        self.assertEqual(len(plan), 3)
        self.assertEqual([asset["label"] for asset, _ in plan],
                         ["junction", "recession", "continued-rain"])
        self.assertEqual([markers for _, markers in plan], [True, False, False])
        self.assertEqual(plan[1][0]["timeline"], plan[2][0]["timeline"])
        self.assertEqual(plan[1][0]["requested_start_time_s"], 7260)
        self.assertEqual(plan[1][0]["requested_end_time_s"], 14400)
        self.assertEqual(plan[1][0]["case"], "rain-off")
        self.assertEqual(plan[2][0]["case"], "rain-on")

    def test_junction_keeps_saved_field_clock_explicit(self):
        asset, markers = bank.review_plan()[0]
        self.assertTrue(markers)
        self.assertEqual(asset["render_interval_s"], 5)
        self.assertEqual(asset["saved_field_interval_s"], 60)
        self.assertFalse(asset["physical_field_interpolation"])
        self.assertEqual(asset["timeline"][1]["requested_time_s"], 4805)
        self.assertEqual(asset["timeline"][1]["saved_field_time_s"], 4800)

    def test_existing_evidence_is_not_replaced(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "phase"
            bank.reference.reserve_directory(path)
            with self.assertRaises(FileExistsError):
                bank.reference.reserve_directory(path)
            bank.reference.write_json_exclusive(path / "receipt.json", {"original": True})
            with self.assertRaises(FileExistsError):
                bank.reference.write_json_exclusive(path / "receipt.json", {})


if __name__ == "__main__":
    unittest.main()
