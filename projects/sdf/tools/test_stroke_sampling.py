import math
import unittest
from stroke_sampling import segment, validate


class SamplingTests(unittest.TestCase):
    def poses(self, count=61):
        return [dict(t=i/(count-1), p=[i/(count-1), 0, 0], r=.02) for i in range(count)]

    def test_straight_sweep_and_width_ramp(self):
        poses = self.poses()
        for p in poses:
            p['r'] += p['t']*.01
        self.assertEqual(segment(poses, .0001), [(0, 60)])
        self.assertLess(validate(poses, [(0, 60)], .0001)['max_combined_error'], 1e-12)

    def test_width_bulge_survives_straight_centerline(self):
        poses = self.poses()
        for p in poses:
            p['r'] += .01*math.sin(math.pi*p['t'])
        spans = segment(poses, .0001)
        self.assertIn(30, {i for span in spans for i in span})
        self.assertGreater(len(spans), 1)
        validate(poses, spans, .0001)

    def test_explicit_corner_and_stationary_pressure(self):
        poses = self.poses()
        poses[30]['corner'] = True
        self.assertEqual(segment(poses, .0001), [(0, 30), (30, 60)])
        for p in poses:
            p['p'] = [0, 0, 0]
            p['r'] = .01+.02*p['t']
        validate(poses, segment(poses, .0001), .0001)

    def test_frozen_prefix_never_changes(self):
        poses = self.poses()
        for p in poses:
            p['p'][1] = .03*math.sin(p['t']*8)
        frozen = []
        for count in range(2, len(poses)+1):
            spans = segment(poses[:count], .0001)
            self.assertEqual(spans[:len(frozen)], frozen)
            frozen = spans[:-1]  # final span is the replaceable provisional tip
            validate(poses[:count], spans, .0001)

    def test_invalid_input_is_rejected(self):
        for key, value in [('t', 0), ('r', -1), ('r', math.nan)]:
            poses = self.poses(2); poses[1][key] = value
            with self.assertRaises(ValueError):
                segment(poses, .0001)


if __name__ == '__main__':
    unittest.main()
