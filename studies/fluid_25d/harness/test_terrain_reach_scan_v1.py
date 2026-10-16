from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

import numpy as np

from projects.fluid.fluid_25d import terrain_reach_scan_v1 as scan


class TerrainReachScanV1Tests(unittest.TestCase):
    @staticmethod
    def _sinuous_incised_reach(amplitude_m: float = 35.0, along_slope: float = -0.009) -> np.ndarray:
        height, width = 256, 512
        z, x = np.mgrid[:height, :width]
        x_m = x.astype(np.float64) * 30.0
        z_m = z.astype(np.float64) * 30.0
        center_z = 128.0 + 25.0 * np.sin(x / 43.0) + 8.0 * np.sin(x / 15.0)
        lateral_distance_m = (z - center_z) * 30.0
        floor = amplitude_m * (1.0 - np.exp(-np.square(lateral_distance_m / 260.0)))
        regional = 600.0 + along_slope * x_m + 0.0002 * z_m
        return (regional + floor).astype(np.float32)

    def test_flat_field_is_rejected(self) -> None:
        metrics = scan.analyze_elevation_window(np.full((256, 512), 300.0, dtype=np.float32))
        self.assertFalse(metrics["eligible"])
        self.assertIn("regional_relief_below_flatness_floor", metrics["failure_reasons"])
        self.assertIn("connected_trunk_path_too_short", metrics["failure_reasons"])

    def test_isolated_pits_do_not_form_a_long_trunk(self) -> None:
        z, x = np.mgrid[:256, :512]
        elevation = 500.0 - 0.004 * x * 30.0 + 0.002 * z * 30.0
        for cx, cz in ((80, 70), (190, 180), (320, 80), (430, 170)):
            elevation -= 28.0 * np.exp(-((x - cx) ** 2 + (z - cz) ** 2) / (2.0 * 7.0**2))
        metrics = scan.analyze_elevation_window(elevation.astype(np.float32))
        self.assertFalse(metrics["eligible"])
        self.assertLess(metrics["longest_trunk_path_m"], scan.MIN_CONTEXT_TRUNK_LENGTH_M)
        self.assertIn("connected_trunk_path_too_short", metrics["failure_reasons"])

    def test_steep_valley_fails_low_relief_and_plausible_grade_gates(self) -> None:
        elevation = self._sinuous_incised_reach(amplitude_m=160.0, along_slope=-0.08)
        metrics = scan.analyze_elevation_window(elevation)
        self.assertFalse(metrics["eligible"])
        self.assertGreater(metrics["p05_p95_relief_m"], scan.MAX_CONTRAST_CONTEXT_RELIEF_M)
        self.assertIn("regional_relief_above_700m_contrast_ceiling", metrics["failure_reasons"])
        self.assertIn("end_to_end_fall_too_steep_for_reach", metrics["failure_reasons"])

    def test_valid_sinuous_incised_reach_has_long_path_repeated_banks_and_floor(self) -> None:
        metrics = scan.analyze_elevation_window(self._sinuous_incised_reach())
        self.assertTrue(metrics["eligible"], metrics["failure_reasons"])
        self.assertLessEqual(metrics["p05_p95_relief_m"], scan.MAX_PRIMARY_CONTEXT_RELIEF_M)
        self.assertGreaterEqual(metrics["longest_trunk_path_m"], scan.MIN_CONTEXT_TRUNK_LENGTH_M)
        self.assertGreaterEqual(metrics["tested_count"], scan.MIN_CROSS_SECTIONS)
        self.assertGreaterEqual(metrics["paired_bank_ratio"], scan.MIN_PAIRED_BANK_RATIO)
        self.assertGreaterEqual(metrics["multi_cell_floor_ratio"], scan.MIN_MULTI_CELL_FLOOR_RATIO)
        self.assertGreater(metrics["path_end_to_end_fall_m"], 0.0)
        self.assertGreaterEqual(metrics["route_directness_ratio"], scan.MIN_ROUTE_DIRECTNESS_RATIO)

    def test_sinuous_valley_with_downstream_sill_fails_full_path_adverse_rise_gate(self) -> None:
        elevation = self._sinuous_incised_reach()
        elevation[:, 400:] += 30.0
        metrics = scan.analyze_elevation_window(elevation)
        self.assertGreater(metrics["p05_p95_relief_m"], scan.MIN_CONTEXT_RELIEF_M)
        self.assertGreater(metrics["path_end_to_end_fall_m"], 0.0)
        self.assertGreater(metrics["path_max_downstream_adverse_rise_m"], 12.0)
        self.assertIn("path_adverse_rise_exceeds_12m_contrast_ceiling", metrics["failure_reasons"])

    def test_grade_between_one_and_four_percent_is_explicit_contrast_only(self) -> None:
        metrics = scan.analyze_elevation_window(self._sinuous_incised_reach())
        metrics["path_end_to_end_slope"] = 0.02
        metrics["path_end_to_end_fall_m"] = 0.02 * metrics["longest_trunk_path_m"]
        metrics["path_max_downstream_adverse_rise_m"] = 0.0
        metrics["path_downhill_step_fraction"] = 1.0
        metrics["failure_reasons"] = scan.reach_failure_reasons(
            metrics, minimum_path_length_m=scan.MIN_CONTEXT_TRUNK_LENGTH_M
        )
        metrics["eligible"] = not metrics["failure_reasons"]
        self.assertGreater(metrics["path_end_to_end_slope"], scan.MAX_PRIMARY_END_TO_END_SLOPE)
        self.assertLessEqual(metrics["path_end_to_end_slope"], scan.MAX_CONTRAST_END_TO_END_SLOPE)
        self.assertIn(
            "path_grade_above_primary_gentle_reach_band_contrast_only",
            metrics["failure_reasons"],
        )
        status, selected_reach, _score, failures = scan.classify_window(metrics, None)
        self.assertEqual((status, selected_reach), ("contrast_only", "none"))
        self.assertIn("path_grade_above_primary_gentle_reach_band_contrast_only", failures)

    def test_three_to_twelve_meter_adverse_rise_is_pool_contrast_only(self) -> None:
        metrics = scan.analyze_elevation_window(self._sinuous_incised_reach())
        metrics["path_max_downstream_adverse_rise_m"] = 5.0
        metrics["failure_reasons"] = scan.reach_failure_reasons(
            metrics, minimum_path_length_m=scan.MIN_CONTEXT_TRUNK_LENGTH_M
        )
        metrics["eligible"] = not metrics["failure_reasons"]
        status, selected_reach, _score, failures = scan.classify_window(metrics, None)
        self.assertEqual((status, selected_reach), ("contrast_only", "none"))
        self.assertIn(
            "path_adverse_rise_above_gentle_through_flow_band_contrast_only", failures
        )

    def test_long_loop_in_a_basin_fails_endpoint_separation_gate(self) -> None:
        # A large closed meander can have plenty of skeleton length but does
        # not span the crop from an upstream end toward a distinct outlet.
        angles = np.linspace(0.0, 2.0 * np.pi, 512, endpoint=False)
        x = np.rint(256 + 95 * np.cos(angles)).astype(np.int32)
        z = np.rint(128 + 16 * np.sin(angles)).astype(np.int32)
        points = np.column_stack((z, x))
        keep = np.concatenate(([True], np.any(np.diff(points, axis=0) != 0, axis=1)))
        loop = points[keep]
        loop = np.vstack((loop, loop[0]))
        length_m = float(scan._path_distances(loop)[-1])
        cross = scan.measure_cross_sections(
            np.full((256, 512), 250.0, dtype=np.float32), loop
        )
        self.assertGreater(length_m, scan.MIN_CONTEXT_TRUNK_LENGTH_M)
        self.assertEqual(cross["endpoint_separation_m"], 0.0)
        self.assertEqual(cross["route_directness_ratio"], 0.0)
        metrics = {
            "p05_p95_relief_m": 250.0,
            "longest_trunk_path_m": length_m,
            "tested_count": scan.MIN_CROSS_SECTIONS,
            "median_left_bank_relief_m": 20.0,
            "median_right_bank_relief_m": 20.0,
            "paired_bank_ratio": 1.0,
            "multi_cell_floor_ratio": 1.0,
            "end_to_end_fall_m": 100.0,
            "end_to_end_slope": 0.001,
            "downhill_step_fraction": 1.0,
            "path_end_to_end_fall_m": 100.0,
            "path_end_to_end_slope": 0.001,
            "path_max_downstream_adverse_rise_m": 0.0,
            "path_downhill_step_fraction": 1.0,
            **cross,
        }
        failures = scan.reach_failure_reasons(
            metrics, minimum_path_length_m=scan.MIN_CONTEXT_TRUNK_LENGTH_M
        )
        self.assertIn("trunk_endpoints_too_close_for_crop_dimensions", failures)
        self.assertIn("trunk_route_directness_below_source_outlet_minimum", failures)

    def test_near_miss_filter_uses_identity_for_array_bearing_results(self) -> None:
        ineligible = scan.WindowResult(
            asset=SimpleNamespace(elevation_sha256="a" * 64),
            x=0,
            z=0,
            context={"_path": np.array([[0, 0], [1, 1]])},
            nested=None,
            selected_reach="none",
            status="ineligible",
            score=0.0,
            failures=["test"],
        )
        eligible = scan.WindowResult(
            asset=SimpleNamespace(elevation_sha256="b" * 64),
            x=1,
            z=1,
            context={"_path": np.array([[1, 1], [2, 2]])},
            nested=None,
            selected_reach="context",
            status="eligible_context",
            score=0.5,
            failures=[],
        )
        self.assertEqual(scan._near_miss_results([ineligible, eligible], [eligible]), [ineligible])

    def test_visual_shortlist_prefers_source_diversity_and_rejects_overlapping_reaches(self) -> None:
        def result(digest: str, x: int, score: float) -> scan.WindowResult:
            return scan.WindowResult(
                asset=SimpleNamespace(elevation_sha256=digest),
                x=x,
                z=0,
                context={},
                nested={"x_in_context": 0, "z_in_context": 0},
                selected_reach="nested",
                status="eligible_nested_reach",
                score=score,
                failures=[],
            )

        same_source = "a" * 64
        other_source = "b" * 64
        first = result(same_source, 0, 0.9)
        exact_duplicate = result(same_source, 0, 0.85)
        overlap = result(same_source, 32, 0.8)
        disjoint = result(same_source, 400, 0.7)
        diverse = result(other_source, 0, 0.6)
        shortlist = scan._diverse_shortlist(
            [first, exact_duplicate, overlap, disjoint, diverse], max_candidates=3
        )
        self.assertEqual(shortlist[:2], [first, diverse])
        self.assertNotIn(exact_duplicate, shortlist)
        self.assertNotIn(overlap, shortlist)
        self.assertIn(disjoint, shortlist)

    def test_selected_trace_has_global_endpoints_station_table_and_three_profiles(self) -> None:
        source_elevation = np.arange(512 * 512, dtype=np.float32).reshape(512, 512) * 0.01
        station_rows = []
        for index, distance in enumerate((300.0, 600.0, 900.0, 1200.0)):
            station_rows.append(
                {
                    "path_distance_m": distance,
                    "x_native_local": 110 + index,
                    "z_native_local": 64,
                    "normal_x": 0.0,
                    "normal_z": 1.0,
                    "floor_elevation_m": 100.0 - index,
                    "floor_width_m": 90.0,
                    "left_bank_relief_m": 12.0,
                    "right_bank_relief_m": 13.0,
                    "paired_banks": True,
                    "multi_cell_floor": True,
                }
            )
        context = {
            "_path": np.array([[64, 80], [64, 90], [64, 100]], dtype=np.int32),
            "_cross_sections": station_rows,
            "longest_trunk_path_m": 1500.0,
            "endpoint_separation_m": 600.0,
            "route_directness_ratio": 0.4,
        }
        item = scan.WindowResult(
            asset=SimpleNamespace(elevation_sha256="c" * 64),
            x=20,
            z=30,
            context=context,
            nested=None,
            selected_reach="context",
            status="eligible_context",
            score=0.5,
            failures=[],
        )
        trace = scan._selected_reach_trace(item, source_elevation)
        self.assertIsNotNone(trace)
        assert trace is not None
        self.assertEqual(trace["endpoint_a"], {"x_native": 100, "z_native": 94, "elevation_m": float(source_elevation[94, 100])})
        self.assertEqual(trace["endpoint_b"]["x_native"], 120)
        self.assertEqual(len(trace["station_measurements"]), 4)
        self.assertEqual(len(trace["representative_transverse_profiles"]), 3)
        self.assertEqual(len(trace["representative_transverse_profiles"][0]["elevation_m"]), 31)

    def test_nested_choice_and_candidate_rank_prioritize_eligibility_over_score(self) -> None:
        passing = {"eligible": True, "score": 0.1}
        failing = {"eligible": False, "score": 0.9}
        self.assertGreater(scan._nested_preference(passing), scan._nested_preference(failing))
        ineligible = scan.WindowResult(
            asset=SimpleNamespace(elevation_sha256="a" * 64),
            x=0,
            z=0,
            context={"paired_bank_ratio": 1.0, "longest_trunk_path_m": 9000.0},
            nested=None,
            selected_reach="none",
            status="ineligible",
            score=0.9,
            failures=["regional_relief_above_700m_contrast_ceiling"],
        )
        eligible = scan.WindowResult(
            asset=SimpleNamespace(elevation_sha256="b" * 64),
            x=0,
            z=0,
            context={"paired_bank_ratio": 0.7, "longest_trunk_path_m": 5000.0},
            nested=None,
            selected_reach="context",
            status="eligible_context",
            score=0.1,
            failures=[],
        )
        self.assertLess(scan._candidate_rank_key(eligible), scan._candidate_rank_key(ineligible))

    def test_native_origins_match_pinned_scanner_contract(self) -> None:
        self.assertEqual(
            scan.candidate_origins(2048, 2048),
            scan.PINNED.candidate_origins(
                2048,
                2048,
                crop_width=512,
                crop_height=256,
                stride_x=128,
                stride_z=64,
            ),
        )

    def test_nested_relief_floor_is_applied_at_50m(self) -> None:
        metrics = {
            "p05_p95_relief_m": 60.0,
            "longest_trunk_path_m": 4000.0,
            "endpoint_separation_fraction_of_diagonal": 0.5,
            "path_span_fraction_of_longer_crop_axis": 0.5,
            "tested_count": 10,
            "median_left_bank_relief_m": 20.0,
            "median_right_bank_relief_m": 20.0,
            "paired_bank_ratio": 0.9,
            "multi_cell_floor_ratio": 0.9,
            "end_to_end_fall_m": 20.0,
            "end_to_end_slope": 0.005,
            "downhill_step_fraction": 0.8,
            "path_end_to_end_fall_m": 20.0,
            "path_end_to_end_slope": 0.005,
            "path_max_downstream_adverse_rise_m": 0.0,
            "path_downhill_step_fraction": 0.8,
            "route_directness_ratio": 0.8,
        }
        nested_failures = scan.reach_failure_reasons(
            metrics,
            minimum_path_length_m=scan.MIN_NESTED_TRUNK_LENGTH_M,
            minimum_relief_m=scan.MIN_NESTED_RELIEF_M,
        )
        context_failures = scan.reach_failure_reasons(
            metrics,
            minimum_path_length_m=scan.MIN_CONTEXT_TRUNK_LENGTH_M,
        )
        self.assertNotIn("regional_relief_below_flatness_floor", nested_failures)
        self.assertIn("regional_relief_below_flatness_floor", context_failures)

    @staticmethod
    def _write_manifest(directory: Path, variant: str, payload: bytes, model_revision: str | None = None) -> Path:
        directory.mkdir(parents=True)
        elevation_path = directory / "elevation.f32"
        elevation_path.write_bytes(payload)
        digest = hashlib.sha256(payload).hexdigest()
        document = {
            "schema": "cubey.terrain.heightfield.v1",
            "seed": 0,
            "source": {
                "generator": "terrain-diffusion",
                "id": "terrain-diffusion-30m",
                "native_resolution_m": 30.0,
                "code_revision": scan.PINNED.PINNED_CODE_REVISION,
                "model_id": scan.PINNED.PINNED_MODEL_ID,
                "model_revision": model_revision or scan.PINNED.PINNED_MODEL_REVISION,
            },
            "grid": {
                "width": 4,
                "height": 4,
                "sample_spacing_m": 30.0,
                "axis_mapping": {"world_x": "model_j", "world_z": "model_i"},
            },
            "height": {"offset_m": 0.0, "scale": 1.0, "relief_scale_m": 1.0},
            "files": {
                "elevation": {
                    "path": "elevation.f32",
                    "dtype": "float32-le",
                    "layout": "row-major-zx",
                    "shape": [4, 4],
                    "unit": "m",
                    "byte_count": 64,
                    "sha256": digest,
                }
            },
            "provenance": {"landscape_variant": variant},
        }
        manifest = directory / "heightfield.json"
        manifest.write_text(json.dumps(document))
        return manifest

    def test_pinned_producer_hash_deduplication_and_exact_inventory_gate(self) -> None:
        payload = np.arange(16, dtype="<f4").tobytes()
        with tempfile.TemporaryDirectory(prefix="terrain-reach-pins-") as temporary:
            root = Path(temporary)
            first = self._write_manifest(root / "a", "first", payload)
            alias = self._write_manifest(root / "b", "alias", payload)
            records = [
                scan.PINNED.load_manifest_record(first, expected_shape=(4, 4)),
                scan.PINNED.load_manifest_record(alias, expected_shape=(4, 4)),
            ]
            assets = scan.PINNED.deduplicate_assets(records)
            self.assertEqual(len(assets), 1)
            self.assertEqual(len(assets[0].aliases), 2)
            self.assertEqual(assets[0].elevation_sha256, hashlib.sha256(payload).hexdigest())
            with self.assertRaisesRegex(scan.ReachScanError, "differs from the frozen 12-payload"):
                scan.validate_pinned_inventory(assets)

            wrong_pin = self._write_manifest(
                root / "wrong", "wrong", payload, model_revision="not-the-pinned-model"
            )
            with self.assertRaisesRegex(ValueError, "pinned source"):
                scan.PINNED.load_manifest_record(wrong_pin, expected_shape=(4, 4))

    def test_live_cached_inventory_matches_frozen_twelve_payload_pin(self) -> None:
        assets, identity = scan.load_pinned_assets(scan.ROOT / scan.SOURCE_ROOT_RELATIVE)
        self.assertEqual(identity["manifest_count"], 16)
        self.assertEqual(identity["unique_payload_count"], 12)
        self.assertEqual(identity["deduplicated_alias_count"], 4)
        self.assertEqual({asset.elevation_sha256 for asset in assets}, scan.EXPECTED_UNIQUE_PAYLOADS)

    def test_existing_output_is_rejected_before_any_input_scan(self) -> None:
        with tempfile.TemporaryDirectory(prefix="terrain-reach-output-") as temporary:
            root = Path(temporary)
            output = root / "occupied"
            output.mkdir()
            marker = output / "keep.txt"
            marker.write_text("user data\n")
            with self.assertRaisesRegex(FileExistsError, "refusing to overwrite"):
                scan.run_scan(root / "does-not-exist", output)
            self.assertEqual(marker.read_text(), "user data\n")


if __name__ == "__main__":
    unittest.main()
