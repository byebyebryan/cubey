"""Exact local predicates for the captured thin-fold failures; no GPU or mesh download.

These face pairs establish why an origin crossed an exit. They neither emulate
hardware traversal nor certify a replacement offset/error policy.
"""

import json
import unittest
from fractions import Fraction
from pathlib import Path


def vector(values):
    return tuple(Fraction(value) for value in values)


def subtract(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def intersection(vertices, origin, direction):
    """Exact rational ray/triangle result, inclusive edges, no epsilon or tie order."""
    a, b, c = map(vector, vertices)
    origin, direction = vector(origin), vector(direction)
    e1, e2 = subtract(b, a), subtract(c, a)
    h = cross(direction, e2)
    determinant = dot(e1, h)
    if determinant == 0:
        return None
    s = subtract(origin, a)
    q = cross(s, e1)
    u, v = dot(s, h) / determinant, dot(direction, q) / determinant
    if u < 0 or v < 0 or u + v > 1:
        return None
    return dot(e2, q) / determinant


class SpawnGeometryTests(unittest.TestCase):
    def test_recorded_offsets_cross_the_exit_faces(self):
        fixture = json.loads(
            (Path(__file__).parent / "fixtures/ray-spawn-thin-folds.json").read_text()
        )
        for case in fixture["cases"]:
            with self.subTest(case=case["name"]):
                vertices, direction = case["exit_vertices"], case["direction"]
                before = intersection(vertices, case["hit_point"], direction)
                after = intersection(vertices, case["spawn_origin"], direction)
                half = intersection(vertices, case["half_offset_origin"], direction)
                self.assertIsNotNone(before)
                self.assertIsNotNone(after)
                self.assertIsNotNone(half)
                self.assertGreater(before, 0)
                self.assertLess(after, 0)
                self.assertGreater(half, 0)
                # The emitted ray clears its source face; this is a different,
                # opposing face being skipped, not a source self-intersection.
                source = intersection(
                    case["entry_vertices"], case["spawn_origin"], direction
                )
                self.assertTrue(source is None or source < 0)

    def test_shadow_spawn_segment_crosses_an_exit(self):
        case = json.loads(
            (Path(__file__).parent / "fixtures/ray-spawn-shadow-fold.json").read_text()
        )
        point, spawned = vector(case["source_point"]), vector(case["spawn_origin"])
        displacement = subtract(spawned, point)
        crossing = intersection(case["crossed_vertices"], point, displacement)
        behind = intersection(case["crossed_vertices"], spawned, displacement)
        self.assertGreater(crossing, 0)
        self.assertLess(crossing, 1)
        self.assertLess(behind, 0)

    def test_shared_edge_has_no_arbitrary_triangle_winner(self):
        a = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        b = [(1, 1, 0), (0, 1, 0), (1, 0, 0)]
        self.assertEqual(intersection(a, (0.5, 0.5, 1), (0, 0, -1)), 1)
        self.assertEqual(intersection(b, (0.5, 0.5, 1), (0, 0, -1)), 1)

    def test_grazing_and_parallel_are_distinguished_without_epsilon(self):
        triangle = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        tiny = 2.0**-100
        self.assertEqual(intersection(triangle, (0.25, 0.25, tiny), (0, 0, -tiny)), 1)
        self.assertIsNone(intersection(triangle, (0.25, 0.25, tiny), (1, 0, 0)))


if __name__ == "__main__":
    unittest.main()
