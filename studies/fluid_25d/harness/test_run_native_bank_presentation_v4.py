"""Plans and fail-closed provenance helpers for the quiet presentation pass."""
from pathlib import Path
import tempfile
import unittest

import run_native_bank_presentation_v4 as study


class QuietBankPlanTests(unittest.TestCase):
    def test_small_pinned_matrix(self):
        self.assertEqual(study.MODES, (("triangular",1),("bilinear",1),("bspline",2),("bspline",4)))
        self.assertEqual(len(study.still_plan()),3)
        self.assertTrue(all((a["width"],a["height"])==(1920,1080) for a in study.still_plan()))
        self.assertEqual([a["case"] for a in study.still_plan()],["rain-on","rain-off","rain-off"])

    def test_subdivision_flag_only_bspline(self):
        self.assertNotIn("--fluid25d-native-surface-subdivision",study.extra("triangular",1,"auto"))
        self.assertIn("--fluid25d-native-surface-subdivision",study.extra("bspline",2,"off"))
        with self.assertRaises(ValueError):
            study.extra("triangular",4,"off")
        with self.assertRaises(ValueError):
            study.extra("triangular",1,"invalid")

    def test_short_clips_advance_native_fields(self):
        clips = study.video_plan()
        self.assertEqual(len(clips),2)
        self.assertTrue(all(len({r["saved_field_time_s"] for r in a["timeline"]})>1 for a in clips))
        self.assertTrue(all(a["frame_count"]/a["fps"]<10 for a in clips))

    def test_media_hash_fails_closed(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            path = root/"test.png"
            path.write_bytes(b"old")
            row={"path":"test.png","sha256":study.reference.sha256_file(path)}
            study.validate_media(root,[row])
            path.write_bytes(b"changed")
            with self.assertRaises(ValueError):
                study.validate_media(root,[row])


if __name__ == "__main__":
    unittest.main()
