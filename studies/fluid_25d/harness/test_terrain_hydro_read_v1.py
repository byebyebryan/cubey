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
"""Exact synthetic controls for the offline Terrain Diffusion reader."""

from __future__ import annotations

import unittest

import numpy as np

import terrain_hydro_read_v1 as hydro


class HydroReadControls(unittest.TestCase):
    def test_invalid_input_is_rejected(self) -> None:
        with self.assertRaises(ValueError):
            hydro.make_grid(np.ones((2, 4)))
        bad = np.ones((4, 4))
        bad[1, 1] = np.nan
        with self.assertRaises(ValueError):
            hydro.make_grid(bad)

    def test_open_slope_drains_to_boundary_and_conserves_unit_runoff(self) -> None:
        z, x = np.mgrid[:16, :20]
        elevation = 100.0 - 2.0 * x + 0.2 * (z - 8) ** 2
        result = hydro.read_hydrology(elevation, spacing_m=30.0)
        self.assertEqual(result["drainage"]["d8_interior_terminal_count"], 0)
        self.assertAlmostEqual(result["drainage"]["d8_terminal_area_balance_relative"], 0.0, places=12)
        self.assertEqual(result["basins"]["component_count"], 0)
        self.assertGreaterEqual(result["drainage"]["d8_max_contributing_area_km2"], 0.1)

    def test_closed_bowl_has_exact_spill_storage_not_permanent_lake(self) -> None:
        elevation = np.full((7, 7), 10.0)
        elevation[0, 3] = 4.0
        elevation[1, 3] = 5.0
        elevation[2, 3] = 5.0
        elevation[3, 3] = 0.0
        result = hydro.read_hydrology(elevation, spacing_m=30.0)
        self.assertAlmostEqual(result["fill_delta_m"][3, 3], 5.0, places=6)
        self.assertAlmostEqual(result["basins"]["fill_volume_m3"], 5.0 * 900.0, places=5)
        self.assertEqual(result["basins"]["component_count"], 1)
        self.assertFalse(result["basins"]["top_components"][0]["touches_map_edge"])
        self.assertEqual(result["drainage"]["d8_interior_terminal_count"], 0)
        self.assertAlmostEqual(result["drainage"]["d8_terminal_area_balance_relative"], 0.0, places=12)

    def test_terminal_mask_includes_cross_edge_directions(self) -> None:
        directions = np.ones((3, 3), dtype=np.int16)
        terminals, exits = hydro.terminal_cells(directions)
        self.assertTrue(np.all(terminals[:, -1]))
        self.assertTrue(np.all(exits[:, -1]))
        self.assertFalse(terminals[1, 1])


if __name__ == "__main__":
    unittest.main()
