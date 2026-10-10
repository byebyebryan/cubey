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
