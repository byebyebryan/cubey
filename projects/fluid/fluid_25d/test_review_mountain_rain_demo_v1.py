"""CPU-only compact media plan/caption checks; no captures or solver."""
import unittest

import review_mountain_rain_demo_v1 as review


class MountainReviewTests(unittest.TestCase):
    def test_compact_plan_has_complete_story_and_matched_pair(self):
        rows = review.plan()
        self.assertEqual(len(rows), 10)
        self.assertEqual(sum(r["kind"] != "video" for r in rows), 6)
        story = rows[0]
        self.assertEqual(story["timeline"][0]["requested_time_s"], 0)
        self.assertEqual(story["timeline"][-1]["requested_time_s"], 14400)
        self.assertAlmostEqual(story["frame_count"] / story["fps"], 96.1)
        off, on = rows[2:4]
        self.assertEqual(off["timeline"], on["timeline"])
        self.assertEqual(off["camera"], on["camera"])
        self.assertEqual(off["timeline"][-1]["saved_field_time_s"], 14400)
        for row in rows[:4]:
            self.assertLessEqual(row["timeline"][-1]["requested_time_s"], 14400)
            self.assertLessEqual(row["fps"] * row["render_interval_s"], 300)

    def test_rain_caption_changes_at_saved_state_not_initial_clip_state(self):
        off = review.caption_filters(review.plan()[0])
        self.assertIn("Rain at saved field ON", off)
        self.assertIn("Rain at saved field OFF", off)
        self.assertIn("lt(floor((0+n*15)/60)*60,7260)", off)
        self.assertIn("gte(floor((0+n*15)/60)*60,7260)", off)
        self.assertIn("150x viewing", off)
        on = review.caption_filters(review.plan()[3])
        self.assertNotIn("Rain at saved field OFF", on)

    def test_raw_diagnostics_are_labelled_separately(self):
        diagnostic = review.caption_filters(review.plan()[-1])
        self.assertIn("RAW speed", diagnostic)
        self.assertNotIn("dots = approximate", diagnostic)


if __name__ == "__main__":
    unittest.main()
