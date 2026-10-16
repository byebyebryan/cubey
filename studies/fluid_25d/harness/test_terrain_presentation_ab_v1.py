from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from projects.fluid.fluid_25d import terrain_presentation_ab_v1 as study


class TerrainPresentationABTests(unittest.TestCase):
    def test_linear_percentile_matches_interpolated_rank(self) -> None:
        self.assertEqual(study.linear_percentile([0.0, 10.0, 20.0, 30.0], 50.0), 15.0)
        self.assertEqual(study.linear_percentile([0.0, 10.0, 20.0, 30.0], 0.0), 0.0)

    def test_frozen_palette_bounds_are_rank_two_crop_p05_p95(self) -> None:
        self.assertEqual(study.PALETTE_LOW_QUANTILE, 5.0)
        self.assertEqual(study.PALETTE_HIGH_QUANTILE, 95.0)
        self.assertAlmostEqual(study.PALETTE_LOW_M, 874.896182, places=6)
        self.assertAlmostEqual(study.PALETTE_HIGH_M, 1798.506873, places=6)

    def test_float32_terrain_height_transform_matches_loader_arithmetic(self) -> None:
        offset = study._float32(39.367266654968255)
        scale = study._float32(0.8484453174300328)
        self.assertEqual(offset, 39.36726760864258)
        self.assertEqual(scale, 0.8484452962875366)
        transformed = study._float32(study._float32(1000.0 + offset) * scale)
        self.assertEqual(transformed, 881.8463134765625)

    def test_render_variants_are_orthogonal(self) -> None:
        self.assertEqual(study.VARIANTS["baseline"], {})
        self.assertEqual(
            set(study.VARIANTS["palette-only"]),
            {
                "--fluid25d-terrain-palette-low-m",
                "--fluid25d-terrain-palette-high-m",
            },
        )
        self.assertEqual(set(study.VARIANTS["scale-only"]), {"--fluid25d-render-height-scale"})
        self.assertEqual(
            set(study.VARIANTS["camera-only"]),
            {"--fluid25d-home-camera-distance-m"},
        )
        self.assertEqual(
            set(study.VARIANTS["combined"]),
            set(study.VARIANTS["palette-only"])
            | set(study.VARIANTS["scale-only"])
            | set(study.VARIANTS["camera-only"]),
        )

    def test_profile_comparison_accepts_equal_and_rejects_changed_physics_rows(self) -> None:
        baseline = {
            300: {
                "fluid_25d.solver.finite_volume_status_flags": "0.000000",
                "fluid_25d.water.total_water_volume_m3": "123.000000",
            },
            600: {
                "fluid_25d.solver.finite_volume_status_flags": "0.000000",
                "fluid_25d.water.total_water_volume_m3": "100.000000",
            },
        }
        study.compare_checkpoint_profiles(baseline, {frame: dict(row) for frame, row in baseline.items()})
        changed = {frame: dict(row) for frame, row in baseline.items()}
        changed[600]["fluid_25d.water.total_water_volume_m3"] = "100.000001"
        with self.assertRaises(study.PresentationStudyError):
            study.compare_checkpoint_profiles(baseline, changed)
        self.assertEqual(
            study.canonical_profile_csv_sha256(299, baseline[300]),
            study.canonical_profile_csv_sha256(299, dict(reversed(list(baseline[300].items())))),
        )

    def test_profile_reader_requires_zero_status_and_selects_exact_frame(self) -> None:
        with tempfile.TemporaryDirectory(prefix="fluid25d-presentation-test-") as directory:
            metrics = Path(directory) / "metrics.csv"
            metrics.write_text(
                "frame_index,category,name,value\n"
                "299,fluid_25d.solver,finite_volume_status_flags,0.000000\n"
                "299,fluid_25d.water,total_water_volume_m3,10.000000\n"
                "299,fluid_25d.water,maximum_depth_m,0.250000\n"
                "299,fluid_25d.water,cumulative_source_volume_m3,20.000000\n"
                "299,fluid_25d.water,cumulative_boundary_outflow_volume_m3,10.000000\n"
                "300,fluid_25d.water,total_water_volume_m3,999.000000\n"
            )
            selected = study.physics_metric_rows(metrics, 299)
            self.assertEqual(selected["fluid_25d.water.total_water_volume_m3"], "10.000000")
            self.assertEqual(len(selected), 5)
            with self.assertRaises(study.PresentationStudyError):
                study.physics_metric_rows(metrics, 300)
            metrics.write_text(
                "frame_index,category,name,value\n"
                "299,fluid_25d.solver,finite_volume_status_flags,0.000000\n"
                "299,fluid_25d.solver,finite_volume_status_flags,0.000000\n"
            )
            with self.assertRaisesRegex(study.PresentationStudyError, "duplicate profile metric"):
                study.physics_metric_rows(metrics, 299)


if __name__ == "__main__":
    unittest.main()
