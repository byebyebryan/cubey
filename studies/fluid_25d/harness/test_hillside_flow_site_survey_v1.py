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
"""Synthetic controls for the bounded hillside-flow site survey."""

from __future__ import annotations

import unittest

import numpy as np

import hillside_flow_site_survey_v1 as survey
import native_flow_site_survey_v1 as shared


def sloping_native_terrain() -> np.ndarray:
    z, x = np.mgrid[:320, :320]
    return (1_000.0 - 2.0 * x + 0.04 * np.abs(z - 160)).astype(np.float32)


class HillsideFlowSiteSurveyControls(unittest.TestCase):
    def test_domain_selection_is_deterministic_diverse_and_bounded(self) -> None:
        z, x = np.mgrid[:1_024, :1_024]
        elevation = (0.4 * x + 0.2 * z + 180.0 * np.sin(x / 93.0) * np.cos(z / 81.0)).astype(np.float32)
        domains = survey.describe_domains(elevation)
        first = survey.select_diverse_domains(domains, elevation.shape)
        second = survey.select_diverse_domains(domains, elevation.shape)
        self.assertEqual(first, second)
        self.assertLessEqual(len(first), 8)
        boxes = [tuple(item["bbox_full_map_cell_xzwh"]) for item in first]
        self.assertEqual(len(boxes), len(set(boxes)))
        self.assertTrue(all(box[2:] == (256, 256) for box in boxes))

    def test_transformed_crop_hash_uses_native_float32_geometry(self) -> None:
        values = np.arange(35, dtype=np.float32).reshape((5, 7))
        self.assertEqual(shared.crop_sha256(values), shared.crop_sha256(np.asfortranarray(values)))
        self.assertEqual(shared.validate_native_elevation(values).dtype, np.float32)
        with self.assertRaisesRegex(ValueError, "30 m native spacing"):
            shared.validate_native_elevation(values, spacing_m=29.9)

    def test_domain_does_not_need_to_exit_or_reach_an_edge(self) -> None:
        raw = sloping_native_terrain()
        context = shared.full_map_context(raw)
        domain = {
            "bbox_full_map_cell_xzwh": [32, 32, 256, 256],
            "domain_relief_m": 500.0,
            "robust_relief_p95_minus_p05_m": 430.0,
            "slope_p90_fraction": 0.07,
            "transformed_crop_sha256_f32le_c": "synthetic",
        }
        evidence = survey.evaluate_domain(raw, context, domain)
        self.assertEqual(evidence["domain"]["native_size_m"], [7_680.0, 7_680.0])
        self.assertFalse(evidence["route_context"]["full_map_path_reaches_domain_edge"])
        self.assertTrue(evidence["depressions"]["crop_edge_context"]["no_exit_required"])
        self.assertTrue(evidence["preferences"]["domain_edge_outlet"]["required"] is False)
        self.assertGreaterEqual(min(evidence["source"]["interior_margin_cells_west_east_north_south"]), 32)

    def test_uneven_five_cell_footprint_is_reported_without_flatness_gate(self) -> None:
        raw = sloping_native_terrain()
        context = shared.full_map_context(raw)
        domain = {
            "bbox_full_map_cell_xzwh": [32, 32, 256, 256],
            "domain_relief_m": 500.0,
            "robust_relief_p95_minus_p05_m": 430.0,
            "slope_p90_fraction": 0.07,
            "transformed_crop_sha256_f32le_c": "synthetic",
        }
        evidence = survey.evaluate_domain(raw, context, domain)
        self.assertGreater(evidence["source"]["raw_spread_m"], 2.0)
        self.assertFalse(evidence["preferences"]["source_footprint_flatness"]["is_a_gate"])

    def test_full_map_and_crop_fill_are_separate_warning_evidence(self) -> None:
        raw = sloping_native_terrain()
        context = shared.full_map_context(raw)
        synthetic_full_fill = np.asarray(context["filled_m"], dtype=np.float32).copy() + np.float32(4.0)
        context["filled_m"] = synthetic_full_fill
        context["components"] = shared.fill_components(raw, synthetic_full_fill)
        domain = {
            "bbox_full_map_cell_xzwh": [32, 32, 256, 256],
            "domain_relief_m": 500.0,
            "robust_relief_p95_minus_p05_m": 430.0,
            "slope_p90_fraction": 0.07,
            "transformed_crop_sha256_f32le_c": "synthetic",
        }
        evidence = survey.evaluate_domain(raw, context, domain)
        depressions = evidence["depressions"]
        self.assertEqual(depressions["full_map_sampled_fill_depths"]["max_fill_depth_m"], 4.0)
        self.assertEqual(depressions["crop_sampled_fill_depths"]["max_fill_depth_m"], 0.0)
        self.assertGreater(depressions["crop_fill_lowering_warning"]["maximum_crop_lowering_m"], 0.0)
        self.assertTrue(depressions["crop_fill_lowering_warning"]["warning_only"])
        self.assertEqual(evidence["candidate_id"].count("src"), 1)


if __name__ == "__main__":
    unittest.main()
