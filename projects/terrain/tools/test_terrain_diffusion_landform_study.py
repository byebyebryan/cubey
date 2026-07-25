#!/usr/bin/env python3

from __future__ import annotations

import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))

import terrain_diffusion_bake as bake
import terrain_diffusion_landform_study as study


class TerrainDiffusionLandformStudyTests(unittest.TestCase):
    @staticmethod
    def candidate(
        seed: int,
        coarse_i: int,
        coarse_j: int,
        relief_m: float,
        temperature_c: float = 22.0,
        precipitation_mm: float = 120.0,
        land_fraction: float = 1.0,
        upper_mass: float = 0.45,
    ) -> bake.LandscapeCandidate:
        p25 = 100.0
        p90 = p25 + relief_m
        p50 = p25 + relief_m * upper_mass
        return bake.LandscapeCandidate(
            seed=seed,
            coarse_i=coarse_i,
            coarse_j=coarse_j,
            land_fraction=land_fraction,
            land_p25_m=p25,
            land_p50_m=p50,
            land_p90_m=p90,
            relief_m=relief_m,
            temperature_median_c=temperature_c,
            temperature_stddev_median_c=8.0,
            precipitation_median_mm=precipitation_mm,
            precipitation_cv_median=0.5,
            distance_squared=coarse_i * coarse_i + coarse_j * coarse_j,
        )

    @staticmethod
    def probed(
        candidate: bake.LandscapeCandidate,
        score: float,
    ) -> study.ProbedCandidate:
        metrics = study.ProbeMetrics(
            relief_p05_p95_m=1_500.0,
            hypsometric_median=0.6,
            mean_slope=0.2,
            p95_slope=0.8,
            plateau_fraction=0.3,
            valley_depth_mean_m=80.0,
            valley_depth_p95_m=400.0,
            valley_depth_p99_m=700.0,
            deep_valley_fraction=0.12,
            largest_deep_component_fraction=0.7,
            deep_component_count=2,
            canyon_score=score,
        )
        return study.ProbedCandidate(
            candidate,
            study.ProbeAnalysis(metrics, None, None, None),
            1.0,
        )

    def test_desert_selection_spans_available_relief(self) -> None:
        candidates = [
            self.candidate(0, 0, 0, 1_500.0, precipitation_mm=90.0),
            self.candidate(9012, 8, 0, 700.0, precipitation_mm=110.0),
            self.candidate(12345, 16, 0, 300.0, precipitation_mm=160.0),
            self.candidate(
                0,
                24,
                0,
                2_000.0,
                temperature_c=18.0,
                precipitation_mm=80.0,
            ),
        ]

        selected = study.select_desert_candidates(candidates)

        self.assertEqual(
            [candidate.relief_m for candidate in selected], [1_500.0, 700.0, 300.0]
        )

    def test_desert_selection_rejects_an_insufficient_pool(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "at least three"):
            study.select_desert_candidates([self.candidate(0, 0, 0, 500.0)])

    def test_canyon_shortlist_balances_seeds(self) -> None:
        candidates = [
            self.candidate(
                seed,
                index * 8,
                seed_index * 8,
                1_500.0 + index * 50.0,
                temperature_c=12.0,
                precipitation_mm=400.0,
            )
            for seed_index, seed in enumerate(bake.SEEDS)
            for index in range(6)
        ]

        selected = study.select_canyon_shortlist(candidates, set())

        self.assertEqual(len(selected), study.CANYON_SHORTLIST_SIZE)
        self.assertEqual(
            {
                seed: sum(candidate.seed == seed for candidate in selected)
                for seed in bake.SEEDS
            },
            {seed: 4 for seed in bake.SEEDS},
        )

    def test_probe_prefers_connected_plateau_incision_to_rolling_relief(self) -> None:
        axis = np.arange(study.PROBE_SIZE, dtype=np.float32)
        z, x = np.meshgrid(axis, axis, indexing="ij")
        center = study.PROBE_SIZE * 0.5 + np.sin(z / 70.0) * 45.0
        canyon = 650.0 * np.exp(-np.square((x - center) / 24.0))
        plateau = 1_400.0 + x * 0.04 + z * 0.02
        incised = plateau - canyon
        rolling = (
            900.0
            + 180.0 * np.sin(x / 45.0)
            + 160.0 * np.sin(z / 52.0)
            + 70.0 * np.sin((x + z) / 18.0)
        )

        canyon_analysis = study.analyze_probe(incised)
        rolling_analysis = study.analyze_probe(rolling)

        self.assertGreater(
            canyon_analysis.metrics.largest_deep_component_fraction,
            rolling_analysis.metrics.largest_deep_component_fraction,
        )
        self.assertGreater(
            canyon_analysis.metrics.canyon_score,
            rolling_analysis.metrics.canyon_score,
        )

    def test_canyon_selection_preserves_spatial_and_seed_diversity(self) -> None:
        probes = [
            self.probed(self.candidate(0, 0, 0, 1_500.0), 10.0),
            self.probed(self.candidate(0, 8, 8, 1_500.0), 9.0),
            self.probed(self.candidate(0, 32, 0, 1_500.0), 8.0),
            self.probed(self.candidate(9012, 0, 0, 1_500.0), 7.0),
            self.probed(self.candidate(9012, 32, 0, 1_500.0), 6.0),
            self.probed(self.candidate(12345, 0, 0, 1_500.0), 5.0),
        ]

        selected = study.select_canyon_candidates(probes)
        identities = {probe.identity() for probe in selected}

        self.assertEqual(len(selected), study.CANYON_SELECTION_SIZE)
        self.assertNotIn((0, 8, 8), identities)
        self.assertGreaterEqual(len({probe.candidate.seed for probe in selected}), 2)

    def test_real_study_selection_contract_is_frozen(self) -> None:
        selections = [
            study.StudySelection(
                name,
                name,
                kind,
                self.probed(
                    self.candidate(seed, coarse_i, coarse_j, 1_500.0),
                    1.0,
                ),
            )
            for name, (
                kind,
                seed,
                coarse_i,
                coarse_j,
            ) in study.EXPECTED_STUDY_SELECTIONS.items()
        ]

        study.validate_study_selections(selections)
        changed = list(selections)
        original = changed[0]
        candidate = original.probed.candidate
        changed[0] = study.StudySelection(
            original.id,
            original.label,
            original.kind,
            self.probed(
                self.candidate(
                    candidate.seed,
                    candidate.coarse_i + 8,
                    candidate.coarse_j,
                    candidate.relief_m,
                ),
                original.probed.analysis.metrics.canyon_score,
            ),
        )
        with self.assertRaisesRegex(RuntimeError, "selections changed"):
            study.validate_study_selections(changed)


if __name__ == "__main__":
    unittest.main()
