"""CPU evidence contracts; no native worker or renderer launch."""
import unittest

try:
    import numpy as np
except ImportError:
    np = None

from run_rain_response_v1 import actions, metrics


class RainResponse(unittest.TestCase):
    def test_physical_schedule_does_not_depend_on_wall_pace(self):
        sequence = actions(7200, 21600)
        self.assertEqual(sequence[0], {"kind": "resume", "after_s": 1})
        self.assertEqual(sequence[1], {"kind": "set-rain", "at_physical_s": 7200, "value": 0})
        self.assertEqual(sequence[2], {"kind": "pause", "at_physical_s": 21600})
        self.assertEqual(actions(7200, 21600, True), [sequence[0], sequence[2]])
        for off, end in ((0, 20), (20, 20), (30, 20), (20, 72001), (float("nan"), 20)):
            with self.assertRaises(ValueError):
                actions(off, end)

    @unittest.skipUnless(np is not None, "metrics require the existing optional NumPy environment")
    def test_metrics_use_depth_and_momentum_without_visual_amplification(self):
        fields = np.zeros((3, 4, 4), dtype="<f4")
        fields[0] = .5
        fields[1] = 1
        result = metrics(fields.tobytes(), (4, 4), 16)
        self.assertEqual(result["water_volume_m3"], 2048)
        self.assertEqual(result["max_speed_m_per_s"], 2)
        self.assertEqual(result["wet_km2"], .004096)
        self.assertEqual(result["flowing_km2"], .004096)
        fields[0, 0, 0] = -1
        with self.assertRaises(ValueError):
            metrics(fields.tobytes(), (4, 4), 16)
        fields[0, 0, 0] = 0
        with self.assertRaises(ValueError):
            metrics(fields.tobytes(), (4, 4), 16)


if __name__ == "__main__":
    unittest.main()
