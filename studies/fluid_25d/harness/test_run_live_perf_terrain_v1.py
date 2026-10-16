"""NumPy helper tests in optional environment; no CUDA/GUI execution."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest

import numpy as np
import run_live_perf_terrain_v1 as study


class Helpers(unittest.TestCase):
    def test_binary_recording_uses_actual_applied_rain_without_ascii_provenance(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case, session = root / "case", root / "session"
            case.mkdir(); session.mkdir()
            spec = {"rows": 2, "cols": 2, "dx_m": 4, "rain_rate_m_per_s": 512 / 3600000}
            (case / "case-spec.json").write_text(json.dumps(spec))
            (case / "case-protocol.json").write_text("{}")
            (session / "metadata.json").write_text("{}")
            bed = np.ones(4, dtype="<f4").tobytes()
            (case / "source-bed.f32").write_bytes(bed)
            (session / "bed.f32").write_bytes(bed)
            states = []
            for physical in (0, 20):
                planes = np.zeros((3, 4), dtype="<f4")
                planes[0] = physical * .01
                state = {"frame": {"physical_time_s": physical}, "rain_m_per_s": 1024 / 3600000}
                raw = json.dumps(state)
                receipt = {"physical_time_s": physical, "state_sha256": hashlib.sha256(raw.encode()).hexdigest(),
                           "packet_sha256": "a" * 64, "_state_raw": raw}
                states.append((state, planes.tobytes(), receipt))
            manifest = study.save_recording(case, session, *states, root / "capture")
            document = json.loads(manifest.read_bytes())
            self.assertAlmostEqual(document["protocol"]["rain_rate_mm_per_h"], 1024)
            self.assertNotIn("raw_asc_sha256", document["provenance"])
            self.assertEqual([r["time_s"] for r in document["frames"]], [0, 20])
            self.assertEqual(json.loads((case / "case-spec.json").read_bytes()), spec)

    def test_packet_identity_and_true_time(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            packet = struct.pack("<8sQQd", b"CBWTRV1\0", 1, 7, 12.5) + bytes(48)
            state = {"frame": {"reset_generation": 1, "sequence": 7, "physical_time_s": 12.5},
                     "slot": 1, "payload_sha256": hashlib.sha256(packet).hexdigest()}
            raw = json.dumps(state).encode()
            (root / "state.json").write_bytes(raw)
            (root / "slot-1.bin").write_bytes(packet)
            observed, planes, receipt = study.snapshot(root)
            self.assertEqual(observed, state)
            self.assertEqual(len(planes), 48)
            self.assertEqual(receipt["physical_time_s"], 12.5)
            self.assertEqual(receipt["state_sha256"], hashlib.sha256(raw).hexdigest())

    def test_stats_and_percentiles(self):
        planes = np.array([[1., 0], [2., 0], [0., 0]], dtype="<f4").tobytes()
        self.assertEqual(study.field_stats(planes, 4)["water_volume_m3"], 16)
        self.assertEqual(study.field_stats(planes, 4)["max_speed_m_per_s"], 2)
        bad = np.array([[0.], [1.], [0.]], dtype="<f4").tobytes()
        with self.assertRaises(ValueError):
            study.field_stats(bad, 4)
        self.assertEqual(study.percentiles([1, 4, 2, 3])["p95"], 4)


if __name__ == "__main__":
    unittest.main()
