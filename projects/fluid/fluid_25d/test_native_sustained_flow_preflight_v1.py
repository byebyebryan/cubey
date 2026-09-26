#!/usr/bin/env -S uv run --python 3.12
# /// script
# requires-python = "==3.12.*"
# dependencies = [
#   "numpy==1.26.4",
#   "pillow==12.3.0",
#   "pysheds==0.4",
#   "pyflwdir==0.5.12",
#   "pyproj==3.8.0",
#   "rasterio==1.4.3",
#   "scikit-image==0.26.0",
#   "scipy==1.15.3",
# ]
# ///
"""Bounded synthetic controls for native sustained-flow preflight helpers."""

from __future__ import annotations

import unittest

import numpy as np

import native_sustained_flow_preflight_v1 as preflight


class NativeSustainedFlowPreflightControls(unittest.TestCase):
    def setUp(self) -> None:
        self.bowl = np.full((5, 5), 3.0, dtype=np.float32)
        self.bowl[1:4, 1:4] = np.asarray(
            [[2, 1, 2], [1, 0, 1], [2, 1, 2]], dtype=np.float32
        )

    def test_diagonal_only_route_has_distinct_four_and_eight_neighbor_saddles(self) -> None:
        elevation = np.full((7, 7), 9.0, dtype=np.float32)
        for index in range(1, 7):
            elevation[index, index] = 0.0
        four = preflight.minimax_to_edges(elevation, (1, 1), connectivity=4)
        eight = preflight.minimax_to_edges(elevation, (1, 1), connectivity=8)
        self.assertEqual(four["lowest_open_saddle_m"], 9.0)
        self.assertEqual(eight["lowest_open_saddle_m"], 0.0)

    def test_known_bowl_local_fill_has_exact_storage(self) -> None:
        result = preflight.local_depression_audit(self.bowl, (2, 2), [(2, 2)], connectivity=4)
        self.assertEqual(result["source_component_count"], 1)
        self.assertEqual(result["source_component_fill_proxy_m3"], 13_500.0)

    def test_multi_source_cells_deduplicate_shared_depression(self) -> None:
        one = preflight.local_depression_audit(self.bowl, (2, 2), [(2, 2)], connectivity=4)
        several = preflight.local_depression_audit(
            self.bowl, [(2, 2), (2, 3), (3, 2)], [(2, 2)], connectivity=4
        )
        self.assertEqual(several["source_component_count"], 1)
        self.assertEqual(several["source_component_fill_proxy_m3"], one["source_component_fill_proxy_m3"])

    def test_nine_cell_asymmetric_bowl_depths_remain_z_major(self) -> None:
        elevation = np.full((5, 5), 10.0, dtype=np.float32)
        elevation[1:4, 1:4] = np.asarray(
            [[1, 2, 3], [4, 5, 6], [7, 8, 9]], dtype=np.float32
        )
        filled = np.full_like(elevation, 10.0)
        directions = np.zeros_like(elevation, dtype=np.int16)
        source_patch = [(x, z) for z in range(1, 4) for x in range(1, 4)]
        result = preflight.local_depression_audit(
            elevation,
            source_patch,
            [],
            connectivity=4,
            filled_surface=filled,
            directions=directions,
        )
        self.assertEqual(
            result["source_cell_fill_depths_m_z_major"],
            [[9.0, 8.0, 7.0], [6.0, 5.0, 4.0], [3.0, 2.0, 1.0]],
        )
        self.assertEqual(result["source_component_count"], 1)

    def test_open_slope_has_zero_local_fill_and_descent_exits_crop(self) -> None:
        elevation = np.tile(np.asarray([5, 4, 3, 2, 1], dtype=np.float32), (5, 1))
        result = preflight.local_depression_audit(elevation, (2, 2), [(2, 2)], connectivity=4)
        self.assertEqual(result["source_component_fill_proxy_m3"], 0.0)

        descent = preflight.raw_cardinal_descent(elevation, (2, 2), crop_origin_world_xz=(352, 877))
        self.assertEqual(descent["termination"], "crop-edge")
        self.assertFalse(descent["ended_at_raw_four_neighbor_pit_or_flat"])
        self.assertEqual(descent["path_crop_local_xz"], [[2, 2], [3, 2], [4, 2]])
        self.assertEqual(descent["path_world_xz"], [[354, 879], [355, 879], [356, 879]])

    def test_direction_trace_is_cardinal_cycle_free_and_origin_aware(self) -> None:
        directions = np.zeros((3, 4), dtype=np.int16)
        directions[1, 1] = 1  # east
        directions[1, 2] = 1
        result = preflight.trace_direction_map(directions, (1, 1), (352, 877))
        self.assertEqual(result["path_crop_local_xz"], [[1, 1], [2, 1], [3, 1]])
        self.assertEqual(result["path_world_xz"], [[353, 878], [354, 878], [355, 878]])
        self.assertTrue(result["cardinal_only"])
        self.assertFalse(result["contains_cycle"])

    def test_direction_trace_rejects_diagonal_and_cycle(self) -> None:
        diagonal = np.zeros((3, 3), dtype=np.int16)
        diagonal[1, 1] = 2
        with self.assertRaises(ValueError):
            preflight.trace_direction_map(diagonal, (1, 1))

        cycle = np.zeros((3, 3), dtype=np.int16)
        cycle[1, 1] = 1  # east
        cycle[1, 2] = 16  # west
        with self.assertRaises(RuntimeError):
            preflight.trace_direction_map(cycle, (1, 1))

    def test_crop_hash_pins_little_endian_row_major_float32(self) -> None:
        crop = np.asarray([[1.0, 2.0], [3.0, 4.0]], dtype=np.float32)
        expected = preflight._sha256_bytes(np.asarray(crop, dtype="<f4").tobytes(order="C"))
        self.assertEqual(preflight.crop_sha256(crop), expected)
        with self.assertRaises(ValueError):
            preflight.crop_sha256(np.empty((0, 2), dtype=np.float32))
        with self.assertRaises(ValueError):
            preflight.crop_sha256(np.asarray([[1.0, np.nan]], dtype=np.float32))

    def test_public_raw_helpers_reject_nonfinite_and_invalid_connectivity(self) -> None:
        bad = np.ones((3, 3), dtype=np.float32)
        bad[1, 1] = np.nan
        with self.assertRaises(ValueError):
            preflight.minimax_to_edges(bad, (1, 1), connectivity=4)
        with self.assertRaises(ValueError):
            preflight.local_depression_audit(self.bowl, (2, 2), [(2, 2)], connectivity=6)
        with self.assertRaises(ValueError):
            preflight.minimax_to_edges(self.bowl, (5, 5), connectivity=4)


if __name__ == "__main__":
    unittest.main()
