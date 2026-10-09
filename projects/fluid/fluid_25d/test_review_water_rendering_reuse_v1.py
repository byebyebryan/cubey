import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import review_water_rendering_reuse_v1 as study


class ReviewContracts(unittest.TestCase):
    def test_actual_saved_cadence_replaces_legacy_helper_default(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "recording.json"
            path.write_text(json.dumps({"frames": [{"time_s": t} for t in (0, 225, 450)]}))
            with patch.object(study, "RECORDINGS", {"fixture": path}):
                request = study.asset("fixture", "collection", 225, 120, 30, 1)
                self.assertEqual(request["saved_field_interval_s"], 225)
                self.assertEqual({r["saved_field_time_s"] for r in request["timeline"]}, {225})
                self.assertEqual(request["requested_end_time_s"], 344)
                path.write_text(json.dumps({"frames": [{"time_s": t} for t in (0, 225, 451)]}))
                with self.assertRaises(ValueError):
                    study.cadence("fixture")

    def test_tuning_receipt_detects_a_payload_change(self):
        self.assertIsNone(study.tuning_identity([]))
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "tuning.json"
            path.write_text('{"water_ripple_strength":0.025}')
            args = ["--fluid25d-scenic-tuning", str(path)]
            before = study.tuning_identity(args)
            self.assertEqual(before["document"]["water_ripple_strength"], 0.025)
            path.write_text('{"water_ripple_strength":0.1}')
            self.assertNotEqual(before["sha256"], study.tuning_identity(args)["sha256"])


if __name__ == "__main__":
    unittest.main()
