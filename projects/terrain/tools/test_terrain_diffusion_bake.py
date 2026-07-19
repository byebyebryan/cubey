#!/usr/bin/env python3

from __future__ import annotations

import sys
from pathlib import Path
import tempfile
import unittest

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import terrain_diffusion_bake as bake


class TerrainDiffusionBakeTests(unittest.TestCase):
    def test_selects_highest_relief_qualified_region(self) -> None:
        size = bake.CANDIDATE_OFFSETS[-1] + bake.COARSE_WINDOW_CELLS - bake.CANDIDATE_OFFSETS[0]
        field = np.full((size, size), 100.0, dtype=np.float32)
        base = bake.CANDIDATE_OFFSETS[0]
        target_i = 8
        target_j = -16
        i0 = target_i - base
        j0 = target_j - base
        field[i0 : i0 + 8, j0 : j0 + 8] = np.linspace(1.0, 4000.0, 64).reshape(8, 8)

        winner, candidates = bake.select_mountain_region(field)

        self.assertEqual((winner.coarse_i, winner.coarse_j), (target_i, target_j))
        self.assertEqual(len(candidates), 49)
        self.assertGreaterEqual(winner.land_fraction, bake.LAND_THRESHOLD)

    def test_selection_falls_back_to_land_coverage(self) -> None:
        size = bake.CANDIDATE_OFFSETS[-1] + bake.COARSE_WINDOW_CELLS - bake.CANDIDATE_OFFSETS[0]
        field = np.full((size, size), -100.0, dtype=np.float32)
        base = bake.CANDIDATE_OFFSETS[0]
        i0 = 0 - base
        j0 = 0 - base
        block = field[i0 : i0 + 8, j0 : j0 + 8]
        block.flat[:40] = np.linspace(10.0, 1000.0, 40)

        winner, _ = bake.select_mountain_region(field)

        self.assertEqual((winner.coarse_i, winner.coarse_j), (0, 0))
        self.assertLess(winner.land_fraction, bake.LAND_THRESHOLD)

    def test_calibration_is_global_affine_and_unclipped(self) -> None:
        first = np.arange(100, dtype=np.float32).reshape(10, 10)
        second = np.arange(100, 200, dtype=np.float32).reshape(10, 10)

        calibration = bake.comparison_calibration([first, second])
        transformed = bake.apply_calibration(first, calibration)

        self.assertAlmostEqual(
            calibration["height_scale"],
            bake.COMPARISON_RELIEF_M
            / (calibration["aggregate_p95_raw_m"] - calibration["aggregate_p05_raw_m"]),
        )
        self.assertLess(float(transformed.min()), 0.0)

    def test_overlap_comparison_uses_global_bounds(self) -> None:
        first_values = np.arange(36, dtype=np.float32).reshape(6, 6)
        second_values = first_values[:, 4:6].copy()
        first = bake.QueriedTile("first", (0, 0, 6, 6), first_values, None, 0.0)
        second = bake.QueriedTile("second", (0, 4, 6, 6), second_values, None, 0.0)

        count, maximum = bake.overlapping_max_abs(first, second)

        self.assertEqual(count, 12)
        self.assertEqual(maximum, 0.0)

    def test_raw_writer_is_little_endian_and_hashed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "field.f32"
            record = bake.write_f32(path, np.array([[1.0, 2.0]], dtype=np.float32))

            self.assertEqual(record["byte_count"], 8)
            self.assertEqual(record["sha256"], bake.sha256_file(path))
            self.assertEqual(np.fromfile(path, dtype="<f4").tolist(), [1.0, 2.0])


if __name__ == "__main__":
    unittest.main()
