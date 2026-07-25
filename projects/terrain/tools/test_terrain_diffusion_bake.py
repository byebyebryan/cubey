#!/usr/bin/env python3

from __future__ import annotations

import dataclasses
import hashlib
import random
import sys
import tempfile
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import terrain_diffusion_bake as bake
from export_heightfield_manifest import HEIGHTFIELD_SCHEMA, heightfield_manifest


class TerrainDiffusionBakeTests(unittest.TestCase):
    @staticmethod
    def climate_scan_fixture() -> np.ndarray:
        size = bake.CLIMATE_SCAN_END - bake.CLIMATE_SCAN_BEGIN
        coarse = np.zeros((6, size, size), dtype=np.float32)
        elevation = np.sqrt(np.linspace(100.0, 3_000.0, 64, dtype=np.float32)).reshape(
            8, 8
        )
        coarse[0] = np.tile(elevation, (size // 8, size // 8))
        coarse[2] = 30.0
        coarse[3] = 800.0
        coarse[4] = 100.0
        coarse[5] = 50.0
        climates = {
            "hot-dry": (22.0, 150.0),
            "hot-wet": (22.0, 2_000.0),
            "cool-wet": (10.0, 800.0),
            "cold-dry": (-8.0, 50.0),
            "cold-wet": (-5.0, 300.0),
        }
        for name, (temperature, precipitation) in climates.items():
            coarse_i, coarse_j = bake.CLIMATE_EXPECTED_ORIGINS[name]
            i0 = coarse_i - bake.CLIMATE_SCAN_BEGIN
            j0 = coarse_j - bake.CLIMATE_SCAN_BEGIN
            coarse[2, i0 : i0 + 8, j0 : j0 + 8] = temperature
            coarse[4, i0 : i0 + 8, j0 : j0 + 8] = precipitation
        return coarse

    @staticmethod
    def landscape_candidate(
        seed: int,
        coarse_i: int,
        coarse_j: int,
        relief_m: float,
        temperature_c: float,
        precipitation_mm: float,
        land_fraction: float = 1.0,
    ) -> bake.LandscapeCandidate:
        return bake.LandscapeCandidate(
            seed=seed,
            coarse_i=coarse_i,
            coarse_j=coarse_j,
            land_fraction=land_fraction,
            land_p25_m=100.0,
            land_p50_m=100.0 + 0.4 * relief_m,
            land_p90_m=100.0 + relief_m,
            relief_m=relief_m,
            temperature_median_c=temperature_c,
            temperature_stddev_median_c=7.0,
            precipitation_median_mm=precipitation_mm,
            precipitation_cv_median=0.4,
            distance_squared=coarse_i * coarse_i + coarse_j * coarse_j,
        )

    def test_process_rng_seeding_handles_zero_deterministically(self) -> None:
        class FakeCuda:
            @staticmethod
            def is_available() -> bool:
                return False

        class FakeTorch:
            cuda = FakeCuda()

            @staticmethod
            def manual_seed(seed: int) -> None:
                FakeTorch.seed = seed

        bake._seed_process_rngs(FakeTorch, 0)
        first = (random.randint(0, 2**30), float(np.random.random()), FakeTorch.seed)
        bake._seed_process_rngs(FakeTorch, 0)
        second = (random.randint(0, 2**30), float(np.random.random()), FakeTorch.seed)

        self.assertEqual(first, second)

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

    def test_seam_comparison_ignores_patch_edge_context(self) -> None:
        size = 2 * bake.TILE_CONTEXT_HALO + bake.CORE_TILE_SIZE
        first_values = np.zeros((size, size), dtype=np.float32)
        second_values = np.zeros((size, size), dtype=np.float32)
        first_values[:, -1] = 10.0
        second_values[:, 0] = 20.0
        first = bake.QueriedTile("west", (0, 0, size, size), first_values, None, 0.0)
        second = bake.QueriedTile(
            "east",
            (0, bake.CORE_TILE_SIZE, size, bake.CORE_TILE_SIZE + size),
            second_values,
            None,
            0.0,
        )

        count, maximum = bake.seam_max_abs(first, second)

        self.assertEqual(count, size * 2 * bake.SEAM_VALIDATION_HALF_WIDTH)
        self.assertEqual(maximum, 0.0)

    def test_product_stitch_does_not_require_climate(self) -> None:
        original = (bake.FIELD_SIZE, bake.CORE_TILE_SIZE, bake.TILE_CONTEXT_HALO)
        bake.FIELD_SIZE = 4
        bake.CORE_TILE_SIZE = 2
        bake.TILE_CONTEXT_HALO = 1
        try:
            tiles = [
                bake.QueriedTile(
                    str(index),
                    (0, 0, 4, 4),
                    np.full((4, 4), float(index + 1), dtype=np.float32),
                    None,
                    0.0,
                )
                for index in range(4)
            ]
            elevation, climate = bake._stitch_tiles(tiles, with_climate=False)
        finally:
            bake.FIELD_SIZE, bake.CORE_TILE_SIZE, bake.TILE_CONTEXT_HALO = original

        self.assertIsNone(climate)
        self.assertEqual(
            elevation.tolist(),
            [
                [1.0, 1.0, 2.0, 2.0],
                [1.0, 1.0, 2.0, 2.0],
                [3.0, 3.0, 4.0, 4.0],
                [3.0, 3.0, 4.0, 4.0],
            ],
        )

    def test_raw_writer_is_little_endian_and_hashed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "field.f32"
            record = bake.write_f32(path, np.array([[1.0, 2.0]], dtype=np.float32))

            self.assertEqual(record["byte_count"], 8)
            self.assertEqual(record["sha256"], bake.sha256_file(path))
            self.assertEqual(np.fromfile(path, dtype="<f4").tolist(), [1.0, 2.0])

    def test_sha256_verification_rejects_mismatched_payloads(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "payload.bin"
            path.write_bytes(b"pinned payload")
            expected = hashlib.sha256(path.read_bytes()).hexdigest()

            self.assertEqual(bake.verify_sha256_file(path, expected, "fixture"), expected)
            with self.assertRaisesRegex(RuntimeError, "fixture SHA-256 mismatch"):
                bake.verify_sha256_file(path, "0" * 64, "fixture")

    def test_data_cache_rejects_bad_worldclim_archive_before_extraction(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            reference = root / "reference"
            (reference / "data" / "global").mkdir(parents=True)
            (reference / "data" / "global" / "etopo_10m.tif").write_bytes(b"etopo")
            cache = root / "cache"
            cache.mkdir()
            (cache / "wc2.1_10m_bio.zip").write_bytes(b"not the pinned archive")

            with self.assertRaisesRegex(RuntimeError, "WorldClim archive SHA-256 mismatch"):
                bake._prepare_data_cache(reference, cache)

    def test_climate_channels_are_normalized_to_physical_units(self) -> None:
        native = np.array([12.0, 850.0, 640.0, 45.0], dtype=np.float32).reshape(4, 1, 1)

        normalized = bake.normalize_climate_channels(native)

        self.assertEqual(normalized[:3, 0, 0].tolist(), [12.0, 8.5, 640.0])
        self.assertAlmostEqual(float(normalized[3, 0, 0]), 0.45)
        self.assertEqual(native[:, 0, 0].tolist(), [12.0, 850.0, 640.0, 45.0])

    def test_climate_calibration_selects_five_distinct_regimes(self) -> None:
        selections, candidates = bake.select_climate_calibration_regions(
            self.climate_scan_fixture(), require_expected=True
        )

        self.assertEqual(
            {
                selection.regime.name: (
                    selection.candidate.coarse_i,
                    selection.candidate.coarse_j,
                )
                for selection in selections
            },
            bake.CLIMATE_EXPECTED_ORIGINS,
        )
        self.assertEqual(len(selections), len(bake.CLIMATE_REGIMES))
        self.assertEqual(len(candidates), (264 // 8) ** 2)
        self.assertEqual(
            len(
                {
                    (selection.candidate.coarse_i, selection.candidate.coarse_j)
                    for selection in selections
                }
            ),
            len(selections),
        )
        for selection in selections:
            self.assertTrue(
                selection.regime.accepts(
                    selection.candidate.temperature_median_c,
                    selection.candidate.precipitation_median_mm,
                )
            )

    def test_climate_calibration_rejects_insufficient_relief(self) -> None:
        coarse = self.climate_scan_fixture()
        coarse[0] = np.sqrt(100.0)

        with self.assertRaisesRegex(RuntimeError, "canonical hot-dry control"):
            bake.select_climate_calibration_regions(coarse)

    def test_landscape_variations_select_four_distinct_recipes(self) -> None:
        candidates = [
            self.landscape_candidate(0, -8, -8, 2_700.0, -4.0, 300.0),
            self.landscape_candidate(9012, -8, 8, 1_600.0, 10.0, 800.0),
            self.landscape_candidate(12345, 8, -8, 1_300.0, 20.0, 150.0),
            self.landscape_candidate(0, 8, 8, 600.0, 16.0, 1_000.0),
        ]

        selections = bake.select_landscape_variations(candidates)

        self.assertEqual(
            [selection.variant.name for selection in selections],
            [variant.name for variant in bake.LANDSCAPE_VARIANTS],
        )
        self.assertEqual(
            len(
                {
                    (
                        selection.candidate.seed,
                        selection.candidate.coarse_i,
                        selection.candidate.coarse_j,
                    )
                    for selection in selections
                }
            ),
            len(selections),
        )

    def test_landscape_lowland_does_not_inherit_mountain_relief_gate(self) -> None:
        candidate = self.landscape_candidate(0, 0, 0, 600.0, 16.0, 1_000.0)

        self.assertTrue(bake.LANDSCAPE_VARIANTS[-1].accepts(candidate))
        self.assertLess(candidate.relief_m, bake.CLIMATE_MIN_RELIEF_M)

    def test_landscape_selection_rejects_missing_recipe_without_fallback(self) -> None:
        candidates = [
            self.landscape_candidate(0, 0, 0, 1_300.0, 20.0, 150.0),
        ]

        with self.assertRaisesRegex(RuntimeError, "landscape variant alpine-range"):
            bake.select_landscape_variations(candidates)

    def test_landscape_selection_uses_frozen_seed_order_for_ties(self) -> None:
        candidates = [
            self.landscape_candidate(9012, -8, -8, 2_700.0, -4.0, 300.0),
            self.landscape_candidate(0, 8, 8, 2_700.0, -4.0, 300.0),
            self.landscape_candidate(9012, -8, 8, 1_600.0, 10.0, 800.0),
            self.landscape_candidate(12345, 8, -8, 1_300.0, 20.0, 150.0),
            self.landscape_candidate(12345, 16, 8, 600.0, 16.0, 1_000.0),
        ]

        selections = bake.select_landscape_variations(candidates)

        self.assertEqual(selections[0].candidate.seed, 0)

    def test_landscape_selection_validation_freezes_world_origins(self) -> None:
        selections = [
            bake.LandscapeSelection(
                variant,
                self.landscape_candidate(
                    *bake.LANDSCAPE_EXPECTED_SELECTIONS[variant.name],
                    variant.target_relief_m,
                    variant.target_temperature_c,
                    variant.target_precipitation_mm,
                ),
                0.0,
            )
            for variant in bake.LANDSCAPE_VARIANTS
        ]

        bake.validate_landscape_selections(selections)
        changed = list(selections)
        changed[0] = dataclasses.replace(
            changed[0],
            candidate=dataclasses.replace(changed[0].candidate, coarse_i=8),
        )
        with self.assertRaisesRegex(RuntimeError, "selections changed"):
            bake.validate_landscape_selections(changed)

    def test_area_average_field_uses_disjoint_source_blocks(self) -> None:
        source = np.arange(4 * 4, dtype=np.float32).reshape(1, 4, 4)

        averaged = bake.area_average_field(source, 2)

        self.assertEqual(averaged.shape, (1, 2, 2))
        self.assertEqual(averaged[0].tolist(), [[2.5, 4.5], [10.5, 12.5]])

    def test_surface_manifest_binds_canonical_heightfield_and_units(self) -> None:
        manifest = bake.surface_study_manifest(
            {
                "path": "climate.f32",
                "dtype": "float32-le",
                "byte_count": 16,
                "sha256": "a" * 64,
            },
            {"model_native_origin": {"i": -2048, "j": -6144}},
            3.5,
        )

        self.assertEqual(manifest["schema"], bake.SURFACE_STUDY_SCHEMA)
        self.assertEqual(
            manifest["heightfield"]["elevation_sha256"], bake.DEFAULT_ASSET_ELEVATION_SHA256
        )
        self.assertEqual(manifest["grid"]["width"], bake.SURFACE_STUDY_SIZE)
        self.assertEqual(manifest["grid"]["sample_spacing_m"], 240.0)
        self.assertEqual(
            [channel["unit"] for channel in manifest["files"]["climate"]["channels"]],
            ["deg_c", "deg_c", "mm_per_year", "fraction"],
        )

    def test_calibration_manifests_bind_matching_region_fields(self) -> None:
        selection = bake.ClimateRegimeSelection(
            bake.CLIMATE_REGIMES[1],
            bake.ClimateRegionCandidate(
                -56, 24, 1.0, 100.0, 1_500.0, 1_400.0, 22.0, 8.0, 2_000.0, 0.5, 3_712
            ),
            0.0,
        )
        generated = {
            "selection": selection,
            "model_native_origin": {"i": -14_336, "j": 6_144},
            "total_seconds": 3.5,
        }
        source = {"id": "test", "generator": "terrain-diffusion"}
        elevation_file = {
            "path": "elevation.f32",
            "dtype": "float32-le",
            "byte_count": 16,
            "sha256": "e" * 64,
        }
        climate_file = {
            "path": "climate.f32",
            "dtype": "float32-le",
            "byte_count": 16,
            "sha256": "c" * 64,
        }

        heightfield = bake.climate_calibration_heightfield_manifest(
            elevation_file, generated, source
        )
        surface = bake.climate_calibration_surface_manifest(
            climate_file, elevation_file["sha256"], generated, source
        )

        self.assertEqual(heightfield["provenance"]["regime"], "hot-wet")
        self.assertEqual(
            surface["heightfield"]["elevation_sha256"],
            heightfield["files"]["elevation"]["sha256"],
        )
        self.assertEqual(surface["grid"]["sample_spacing_m"], 240.0)
        self.assertEqual(surface["provenance"]["regime"], "hot-wet")

    def test_landscape_manifests_preserve_variant_seed_and_binding(self) -> None:
        candidate = self.landscape_candidate(
            9012, -8, 8, 1_600.0, 10.0, 800.0
        )
        selection = bake.LandscapeSelection(
            bake.LANDSCAPE_VARIANTS[1], candidate, 0.0
        )
        generated = {
            "seed": 9012,
            "selection": selection,
            "model_native_origin": {"i": -2_048, "j": 2_048},
            "total_seconds": 3.5,
        }
        source = {"id": "test", "generator": "terrain-diffusion"}
        elevation_file = {
            "path": "elevation.f32",
            "dtype": "float32-le",
            "byte_count": 16,
            "sha256": "e" * 64,
        }
        climate_file = {
            "path": "climate.f32",
            "dtype": "float32-le",
            "byte_count": 16,
            "sha256": "c" * 64,
        }

        heightfield = bake.landscape_variation_heightfield_manifest(
            elevation_file, generated, source
        )
        surface = bake.landscape_variation_surface_manifest(
            climate_file, elevation_file["sha256"], generated, source
        )

        self.assertEqual(heightfield["seed"], 9012)
        self.assertEqual(surface["seed"], 9012)
        self.assertEqual(
            heightfield["provenance"]["landscape_variant"],
            "temperate-mountain-valley",
        )
        self.assertEqual(
            surface["heightfield"]["elevation_sha256"],
            heightfield["files"]["elevation"]["sha256"],
        )

    def test_float_hash_matches_written_payload(self) -> None:
        values = np.array([[1.0, 2.0]], dtype=np.float32)
        with tempfile.TemporaryDirectory() as temporary:
            record = bake.write_f32(Path(temporary) / "field.f32", values)

        self.assertEqual(bake.sha256_f32(values), record["sha256"])

    def test_runtime_heightfield_manifest_drops_climate_dependency(self) -> None:
        study = {
            "schema": "cubey.terrain.raster-study.v1",
            "source": {"id": "test-source", "generator": "test"},
            "seed": 9012,
            "grid": {
                "width": 4,
                "height": 4,
                "sample_spacing_m": 30.0,
                "sample_origin_x_m": -45.0,
                "sample_origin_z_m": -45.0,
            },
            "comparison": {
                "height_offset_m": -100.0,
                "height_scale": 2.0,
                "target_relief_m": 3500.0,
            },
            "files": {
                "elevation": {"path": "elevation.f32"},
                "climate": {"path": "climate.f32"},
            },
        }

        runtime = heightfield_manifest(study)

        self.assertEqual(runtime["schema"], HEIGHTFIELD_SCHEMA)
        self.assertEqual(runtime["source"], study["source"])
        self.assertEqual(runtime["height"]["offset_m"], -100.0)
        self.assertEqual(runtime["files"], {"elevation": {"path": "elevation.f32"}})

    def test_environment_package_inventory_does_not_require_pip(self) -> None:
        packages = bake._installed_packages()

        self.assertTrue(packages)
        self.assertTrue(all("==" in package for package in packages))
        self.assertEqual(packages, sorted(set(packages), key=str.casefold))

    def test_morphology_previews_match_the_common_report_palette(self) -> None:
        heights = bake.height_preview_rgb(np.array([0.0, bake.COMPARISON_RELIEF_M]))
        slopes = bake.slope_preview_rgb(np.array([0.0, 0.75, 1.5]))

        self.assertEqual(heights.tolist(), [[25, 31, 36], [242, 240, 235]])
        self.assertEqual(slopes.tolist(), [[31, 87, 121], [224, 198, 70], [170, 45, 45]])


if __name__ == "__main__":
    unittest.main()
