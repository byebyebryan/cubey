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
"""Focused synthetic controls for the native-flow site survey."""

from __future__ import annotations

import unittest

import numpy as np

import native_flow_site_survey_v1 as survey


class NativeFlowSiteSurveyControls(unittest.TestCase):
    def test_broad_downhill_channel_ranks_as_preferred(self) -> None:
        z, x = np.mgrid[:96, :128]
        elevation = (150.0 - 0.36 * x + 0.28 * np.abs(z - 48)).astype(np.float32)
        context = survey.full_map_context(elevation)
        candidate = {
            "full_map_cell_xz": [16, 48],
            "locator_contributing_area_km2": 1.0,
            "elevation_sha256": "synthetic-broad-channel",
        }
        evidence = survey.evaluate_candidate(elevation, context, candidate)
        self.assertEqual(evidence["status"], "preferred")
        self.assertGreater(evidence["route"]["raw_net_fall_m"], 0.0)
        self.assertGreaterEqual(
            evidence["route"]["raw_cross_section_width_summary_m"]["route_bed_plus_1.0m"]["p10"],
            90.0,
        )

    def test_large_closed_basin_is_reported_as_a_fill_warning(self) -> None:
        elevation = np.full((51, 51), 20.0, dtype=np.float32)
        elevation[10:41, 10:41] = 2.0
        elevation[15:36, 15:36] = 0.0
        context = survey.full_map_context(elevation)
        source = [(25, 25)]
        touched = survey.unique_touched_components(context["components"], source, source)
        components = touched["source_components"]
        self.assertEqual(len(components), 1)
        self.assertGreater(components[0]["equilibrium_fill_proxy_m3"], 1_000_000.0)
        self.assertFalse(survey.depression_preference(components[0]["max_fill_depth_m"])["meets_preference"])
        self.assertEqual(
            touched["source_route_union_equilibrium_fill_proxy_m3_deduplicated"],
            components[0]["equilibrium_fill_proxy_m3"],
        )

    def test_diagonal_only_direction_is_rejected(self) -> None:
        directions = np.zeros((5, 5), dtype=np.int16)
        directions[2, 2] = 2
        with self.assertRaisesRegex(ValueError, "non-cardinal"):
            survey.trace_cardinal(directions, (2, 2), max_steps=10)

    def test_crop_through_basin_lowering_is_detected(self) -> None:
        raw = np.full((12, 12), 5.0, dtype=np.float32)
        full_filled = np.full_like(raw, 5.0)
        full_filled[5, 5] = 9.0
        crop_filled = np.full((6, 6), 5.0, dtype=np.float32)
        crop_filled[2, 2] = 5.0
        reviewed_reach = survey.compare_full_and_crop_fill(
            raw, full_filled, crop_filled, (3, 3, 6, 6), [(4, 5)]
        )
        source_to_edge = survey.compare_full_and_crop_fill(
            raw, full_filled, crop_filled, (3, 3, 6, 6), [(4, 5), (5, 5)]
        )
        self.assertFalse(reviewed_reach["crop_gained_lower_outlet_on_profile"])
        self.assertTrue(source_to_edge["crop_gained_lower_outlet_on_profile"])
        self.assertEqual(source_to_edge["maximum_crop_lowering_m"], 4.0)
        self.assertEqual(source_to_edge["profiles"][1]["raw_m"], 5.0)

    def test_evaluator_checks_crop_terminal_beyond_reviewed_reach(self) -> None:
        z, x = np.mgrid[:96, :128]
        elevation = (150.0 - 0.36 * x + 0.28 * np.abs(z - 48)).astype(np.float32)
        context = survey.full_map_context(elevation)
        candidate = {
            "full_map_cell_xz": [16, 48],
            "locator_contributing_area_km2": 1.0,
            "elevation_sha256": "synthetic-broad-channel-terminal-control",
        }
        locator = survey.trace_cardinal(context["directions"], (16, 48), max_steps=51)
        locator_segment = survey.target_segment([tuple(point) for point in locator["path_full_map_cell_xz"]])
        bbox = survey.choose_crop_bbox(locator_segment, elevation.shape, (16, 48))
        x0, z0, width, height = bbox
        crop = elevation[z0 : z0 + height, x0 : x0 + width]
        crop_filled, crop_directions = survey.priority_flood_d4(crop)
        crop_exit = survey.trace_cardinal(
            crop_directions,
            (16 - x0, 48 - z0),
            max_steps=crop.size,
            origin_full_map_xz=(x0, z0),
        )
        terminal_x, terminal_z = crop_exit["path_full_map_cell_xz"][-1]
        self.assertGreater(crop_exit["length_m"], survey.TARGET_REACH_M)
        context["filled_m"][terminal_z, terminal_x] = np.float32(
            crop_filled[terminal_z - z0, terminal_x - x0] + 3.0
        )
        with self.assertRaisesRegex(survey.RejectCandidate, "source-to-edge route profile"):
            survey.evaluate_candidate(elevation, context, candidate)

    def test_unconnected_lower_neighbor_does_not_inflate_width(self) -> None:
        elevation = np.full((64, 64), 10.0, dtype=np.float32)
        for x in range(20, 45):
            elevation[32, x] = 5.0
            elevation[31, x] = 5.0
            elevation[33, x] = 5.0
        elevation[25, 32] = 0.0
        path = [(x, 32) for x in range(20, 45)]
        section = survey.cross_section_width(elevation, path, 10, 1.0)
        self.assertEqual(section["width_m"], 90.0)
        self.assertEqual(section["offset_cells_from_route"], [-1, 1])

    def test_width_at_sample_limit_is_marked_as_censored(self) -> None:
        elevation = np.full((64, 64), 5.0, dtype=np.float32)
        path = [(x, 32) for x in range(20, 45)]
        section = survey.cross_section_width(elevation, path, 10, 1.0)
        self.assertEqual(section["width_m"], 630.0)
        self.assertTrue(section["censored_by_sample_window"])
        self.assertFalse(section["left_bank_stop_observed"])
        self.assertFalse(section["right_bank_stop_observed"])

    def test_fingerprint_ignores_runtime_and_output_paths(self) -> None:
        first = {
            "runtime_s": {"survey": 2.0},
            "candidate": {"contact_sheet": "outputs/a.png", "score": 0.75},
        }
        second = {
            "runtime_s": {"survey": 95.0},
            "candidate": {"contact_sheet": "outputs/b.png", "score": 0.75},
        }
        self.assertEqual(
            survey.deterministic_result_fingerprint(first),
            survey.deterministic_result_fingerprint(second),
        )

    def test_candidate_sampling_is_deterministic_and_spatially_bounded(self) -> None:
        height, width = 512, 512
        area = np.ones((height, width), dtype=np.float64)
        directions = np.ones((height, width), dtype=np.int16)
        first, pool1 = survey.sample_network_centers(area, directions)
        second, pool2 = survey.sample_network_centers(area, directions)
        self.assertEqual(first, second)
        self.assertEqual(pool1, pool2)
        self.assertLessEqual(len(first), 20)
        self.assertGreater(len(first), 1)
        self.assertEqual(len({tuple(item["full_map_cell_xz"]) for item in first}), len(first))

    def test_shared_source_route_depression_is_counted_once(self) -> None:
        labels = np.zeros((5, 5), dtype=np.int32)
        labels[2, 2:4] = 1
        data = {
            "labels": labels,
            "sizes": np.asarray([23, 2]),
            "volumes_m3": np.asarray([0.0, 18_000.0]),
            "max_depth_m": np.asarray([0.0, 3.0]),
            "min_spill_m": np.asarray([0.0, 10.0]),
            "max_spill_m": np.asarray([np.inf, 10.0]),
        }
        source = [(2, 2), (3, 2), (1, 2)]
        route = [(2, 2), (3, 2)]
        result = survey.unique_touched_components(data, source, route)
        self.assertEqual(result["source_component_count"], 1)
        self.assertEqual(result["route_component_count"], 1)
        self.assertEqual(result["source_route_union_component_count"], 1)
        self.assertEqual(result["source_route_union_equilibrium_fill_proxy_m3_deduplicated"], 18_000.0)

    def test_cycle_free_cardinal_trace_rejects_a_loop(self) -> None:
        directions = np.zeros((4, 4), dtype=np.int16)
        directions[1, 1] = 1
        directions[1, 2] = 4
        directions[2, 2] = 16
        directions[2, 1] = 64
        with self.assertRaisesRegex(RuntimeError, "cycle"):
            survey.trace_cardinal(directions, (1, 1), max_steps=10)


if __name__ == "__main__":
    unittest.main()
