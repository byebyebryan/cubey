"""No GPU/solver tests for story receipt and capture contracts."""
import unittest
from copy import deepcopy
from pathlib import Path

import review_rain_response_v1 as review


class Receipts(unittest.TestCase):
    def test_checkpoint_requires_a_nearby_real_saved_state(self):
        frames = [{"time_s": 0}, {"time_s": 1803}, {"time_s": 21603}]
        self.assertEqual(review.closest(frames, 1800)["time_s"], 1803)
        with self.assertRaises(ValueError):
            review.closest(frames, 7200)

    def test_video_requests_an_advancing_timeline_not_final_state_motion(self):
        command = review.command(Path("/case"), Path("/recording.json"), "collection",
                                 Path("/clip.mp4"), video=True)
        self.assertEqual(command[command.index("--fluid25d-recording-time-seconds")+1], "0")
        self.assertEqual(command[command.index("--fluid25d-recording-frame-interval-seconds")+1], "60")
        self.assertIn("--headless", command)
        self.assertNotIn("--headless-render-only", command)
        self.assertNotIn("--fluid25d-external-session", command)

    def test_completion_requires_continued_running_and_manual_override(self):
        base = {"session_id": "same", "solver_bed_sha256": "bed"}
        events = [dict(base, event="guide-applied") for _ in range(9)]
        events.append(dict(base, event="guide-complete-running", physical_s=21601, rain_mm_per_hour=0))
        full = {"events": events, "samples": [{"lifecycle": "running", "physical_s": 21660}]}
        manual = {"events": [{"event": name} for name in
                             ("guide-started", "guide-cancelled", "guide-started", "guide-cancelled")]}
        self.assertEqual(review.guide_receipts(full, manual)["physical_s"], 21601)
        bad = deepcopy(full)
        bad["samples"][0]["lifecycle"] = "paused"
        with self.assertRaises(ValueError):
            review.guide_receipts(bad, manual)
        bad = deepcopy(manual)
        bad["events"].append({"event": "guide-camera"})
        with self.assertRaises(ValueError):
            review.guide_receipts(full, bad)
        bad = deepcopy(full)
        bad["events"][0]["session_id"] = "reset"
        with self.assertRaises(ValueError):
            review.guide_receipts(bad, manual)


if __name__ == "__main__":
    unittest.main()
