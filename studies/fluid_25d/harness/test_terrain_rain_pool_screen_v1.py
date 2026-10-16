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
"""Focused synthetic controls for the bounded early-stage rain-pool screen."""

from __future__ import annotations

import json
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

import numpy as np

import terrain_rain_pool_screen_v1 as screen


class TerrainRainPoolScreenV1Tests(unittest.TestCase):
    @staticmethod
    def _filled_bowl() -> tuple[np.ndarray, np.ndarray]:
        raw = np.full((17, 19), 1.0, dtype=np.float32)
        raw[6:11, 7:12] = 0.04
        raw[8, 9] = 0.0
        filled = raw.copy()
        filled[6:11, 7:12] = 0.2
        return raw, filled

    def test_stage_footprint_storage_and_optimistic_local_budget(self) -> None:
        raw, filled = self._filled_bowl()
        result = screen.scan_stage_arrays(raw, filled)
        self.assertEqual(result["component_count"], 1)
        self.assertEqual(result["stage_counts"]["0.05m"]["footprints_ge_25"], 1)
        self.assertEqual(result["stage_counts"]["0.10m"]["footprints_ge_25"], 1)
        by_stage = {candidate["stage_depth_m"]: candidate for candidate in result["candidates"]}

        five = by_stage[0.05]
        self.assertEqual(five["stage_footprint_cells"], 25)
        self.assertAlmostEqual(five["stage_storage_proxy_m3"], 261.0, places=4)
        self.assertAlmostEqual(five["floor_to_spill_proxy_depth_m"], 0.2, places=6)
        self.assertAlmostEqual(five["direct_footprint_rain_budget_m3"], 225.0, places=4)
        self.assertEqual(five["three_cell_dilated_source_cells"], 121)
        self.assertAlmostEqual(five["optimistic_rain_budget_m3"], 1089.0, places=5)
        self.assertTrue(five["optimistic_budget_pass"])

        ten = by_stage[0.10]
        self.assertEqual(ten["stage_footprint_cells"], 25)
        self.assertAlmostEqual(ten["stage_storage_proxy_m3"], 1386.0, places=3)
        self.assertFalse(ten["optimistic_budget_pass"])

    def test_crop_fit_requires_source_and_crop_perimeter_margin(self) -> None:
        fit = screen.crop_for_bbox((400, 300, 419, 319), 2048, 2048)
        self.assertTrue(fit["fits"])
        self.assertGreaterEqual(fit["minimum_footprint_to_crop_edge_cells"], 4)
        self.assertLessEqual(fit["x"], 400 - 4)
        self.assertLessEqual(fit["z"], 300 - 4)

        near_source_edge = screen.crop_for_bbox((0, 300, 19, 319), 2048, 2048)
        self.assertFalse(near_source_edge["fits"])
        self.assertEqual(near_source_edge["reason"], "source_edge_prevents_requested_margin")

        too_wide = screen.crop_for_bbox((300, 300, 549, 319), 2048, 2048)
        self.assertFalse(too_wide["fits"])
        self.assertEqual(too_wide["reason"], "footprint_plus_margin_exceeds_crop")

        edge_label = np.zeros((5, 5), dtype=np.int32)
        edge_label[0, 0] = 1
        clipped_dilation_cells = screen.dilation_area_cells(
            edge_label,
            1,
            (slice(0, 1), slice(0, 1)),
            iterations=3,
        )
        self.assertEqual(clipped_dilation_cells, 16)

    def test_inventory_digest_is_order_independent_and_identity_sensitive(self) -> None:
        first = {
            "elevation_sha256": "a" * 64,
            "manifest_sha256": "1" * 64,
            "variant": "lowland",
            "seed": 12345,
            "shape_zx": [2048, 2048],
            "spacing_m": 30.0,
        }
        second = {
            "elevation_sha256": "b" * 64,
            "manifest_sha256": "2" * 64,
            "variant": "hills",
            "seed": 7,
            "shape_zx": [2048, 2048],
            "spacing_m": 30.0,
        }
        forward = screen.stable_inventory_sha256([first, second])
        reverse = screen.stable_inventory_sha256([second, first])
        self.assertEqual(forward, reverse)
        self.assertEqual(len(forward), 64)

        changed = {**second, "manifest_sha256": "3" * 64}
        self.assertNotEqual(forward, screen.stable_inventory_sha256([first, changed]))
        with self.assertRaises(ValueError):
            screen.stable_inventory_sha256([{**first, "elevation_sha256": "not-a-hash"}])

    def test_prior_hydro_outputs_are_optional_for_cache_reproduction(self) -> None:
        with TemporaryDirectory() as directory:
            missing = Path(directory) / "not-generated-yet" / "summary.json"
            cases, summary_hash, status = screen.load_corpus_cases(missing)
            malformed = Path(directory) / "malformed-summary.json"
            malformed.write_text("[]\n")
            malformed_cases, malformed_hash, malformed_status = screen.load_corpus_cases(malformed)
            bad_hash_summary = Path(directory) / "bad-hash-summary.json"
            bad_hash_summary.write_text(
                json.dumps(
                    {
                        "case_count": 12,
                        "cases": [{"elevation_sha256": []} for _ in range(12)],
                    }
                )
            )
            bad_hash_cases, _bad_hash_sha, bad_hash_status = screen.load_corpus_cases(bad_hash_summary)

            result_array = Path(directory) / "result-array.json"
            result_array.write_text("[]\n")
            result_summary = Path(directory) / "result-array-summary.json"
            result_summary.write_text(
                json.dumps(
                    {
                        "case_count": 12,
                        "cases": [
                            {"elevation_sha256": digest, "result": str(result_array)}
                            for digest in sorted(screen.EXPECTED_UNIQUE)
                        ],
                    }
                )
            )
            result_cases, _result_sha, result_status = screen.load_corpus_cases(result_summary)
        self.assertEqual(cases, {})
        self.assertIsNone(summary_hash)
        self.assertEqual(status, "not_present_optional")
        self.assertEqual(malformed_cases, {})
        self.assertEqual(len(malformed_hash), 64)
        self.assertEqual(malformed_status, "invalid_root_optional")
        self.assertEqual(bad_hash_cases, {})
        self.assertEqual(bad_hash_status, "invalid_hash_optional")
        self.assertEqual(set(result_cases), screen.EXPECTED_UNIQUE)
        self.assertEqual(result_status, "summary_hashes_match_result_files_partial_optional")

    def test_repeated_stage_screen_is_deterministic(self) -> None:
        raw, filled = self._filled_bowl()
        first = screen.scan_stage_arrays(raw, filled)
        second = screen.scan_stage_arrays(raw, filled)
        self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main()
