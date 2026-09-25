from __future__ import annotations

import copy
import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import run_terrain_site_water_v1 as runner


class TerrainSiteWaterRunnerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.recipe = json.loads(runner.RECIPE_PATH.read_text())

    def test_checked_in_recipe_and_real_manifests_validate(self) -> None:
        recipe, cases = runner.load_frozen_recipe(runner.RECIPE_PATH, runner.ROOT)

        self.assertEqual([case["rank"] for case, _path in cases], [1, 2, 3])
        self.assertEqual([case["crop"]["width"] for case, _path in cases], [512, 512, 512])
        self.assertEqual(recipe["solver_grid"], {"width": 512, "height": 256, "cell_size_m": 30})

    def test_recipe_rejects_solver_subwindow_and_tier_tuning(self) -> None:
        smaller_grid = copy.deepcopy(self.recipe)
        smaller_grid["solver_grid"]["width"] = 256
        with self.assertRaisesRegex(runner.StudyError, "full frozen native crop"):
            runner.validate_recipe_structure(smaller_grid)

        changed_tier = copy.deepcopy(self.recipe)
        changed_tier["protocol"]["tiers"][1]["rainfall_mm_per_hour"] = 72
        with self.assertRaisesRegex(runner.StudyError, "protocol, tier matrix"):
            runner.validate_recipe_structure(changed_tier)

    def test_recipe_rejects_rank_or_human_review_role_drift(self) -> None:
        changed_case = copy.deepcopy(self.recipe)
        changed_case["cases"][1]["crop"]["z"] = 64
        with self.assertRaisesRegex(runner.StudyError, "frozen corrected scan ranks"):
            runner.validate_recipe_structure(changed_case)

        changed_role = copy.deepcopy(self.recipe)
        changed_role["cases"][0]["review_role"] = "river"
        with self.assertRaisesRegex(runner.StudyError, "frozen corrected scan ranks"):
            runner.validate_recipe_structure(changed_role)

    def test_capture_schedule_is_truthful_f1_then_every_15_through_f600(self) -> None:
        self.assertEqual(runner.capture_frames(self.recipe), [1, *range(15, 601, 15)])
        self.assertEqual(len(runner.capture_frames(self.recipe)), 41)

    def test_provenance_records_runner_source_hash_and_size(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            result = SimpleNamespace(returncode=0, stdout="test", stderr="")
            with mock.patch.object(runner.subprocess, "run", return_value=result):
                runner.write_provenance(
                    Path(temporary),
                    {"app": {}, "tools": {}},
                    0.0,
                    False,
                    self.recipe,
                )

            provenance = json.loads((Path(temporary) / "provenance.json").read_text())
            runner_path = Path(runner.__file__).resolve()
            identity = provenance["runner"]
            self.assertEqual(identity["path"], runner_path.relative_to(runner.ROOT).as_posix())
            self.assertEqual(identity["sha256"], runner.sha256_file(runner_path))
            self.assertEqual(identity["size_bytes"], runner_path.stat().st_size)

    def test_app_command_uses_full_grid_and_fixed_neutral_protocol(self) -> None:
        case = self.recipe["cases"][1]
        tier = self.recipe["protocol"]["tiers"][0]
        args = runner.common_app_args(
            Path("/app/fluid"),
            case,
            Path("/terrain/heightfield.json"),
            tier,
            600,
            1280,
            720,
        )
        self.assertIn("512", args[args.index("--grid-width") + 1])
        self.assertIn("256", args[args.index("--grid-height") + 1])
        self.assertEqual(args[args.index("--fluid25d-terrain-crop-x") + 1], "512")
        self.assertEqual(args[args.index("--fluid25d-terrain-crop-z") + 1], "832")
        self.assertEqual(args[args.index("--fluid25d-rainfall-rate-mm-per-hour") + 1], "12")
        self.assertEqual(args[args.index("--fluid25d-source-active-duration-seconds") + 1], "600")
        self.assertIn("finite-volume", args)
        self.assertIn("8", args[args.index("--fluid25d-substeps") + 1])

    def test_capture_acceptance_requires_exact_selected_still_set(self) -> None:
        frames = runner.capture_frames(self.recipe)
        with tempfile.TemporaryDirectory() as temporary:
            output_root = Path(temporary)
            case_root = output_root / "neutral" / "case-a"
            selected = case_root / "captures" / "selected"
            composite = case_root / "captures" / "composite"
            depth = case_root / "captures" / "water-depth"
            selected.mkdir(parents=True)
            composite.mkdir(parents=True)
            depth.mkdir(parents=True)
            for sequence_index, frame in enumerate(frames, start=1):
                composite_path = composite / f"frame-{sequence_index:04d}.png"
                depth_path = depth / f"frame-{sequence_index:04d}.png"
                composite_path.write_bytes(b"png")
                depth_path.write_bytes(b"png")
                if sequence_index == 1:
                    sequence_rows = []
                sequence_rows.append(
                    {
                        "sequence_index": sequence_index,
                        "capture_frame": frame,
                        "simulation_seconds": frame * 2,
                        "composite_path": composite_path.relative_to(output_root).as_posix(),
                        "water_depth_path": depth_path.relative_to(output_root).as_posix(),
                    }
                )
            with (case_root / "sequence.csv").open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=sequence_rows[0].keys())
                writer.writeheader()
                writer.writerows(sequence_rows)
            selected_names = {"f0600-composite-profile.png"}
            selected_names.update(
                f"f{frame:04d}-{view}.png"
                for frame in self.recipe["protocol"]["selected_frames"]
                for view in ("wet-dry", "flow")
            )
            for name in selected_names:
                (selected / name).write_bytes(b"png")

            result = runner.check_capture_set(
                case_root,
                frames,
                self.recipe["protocol"]["selected_frames"],
                full=True,
            )
            self.assertEqual(result, {"sequence_frames": 41, "selected_stills": 13, "complete": True})

            (selected / "f0001-flow.png").unlink()
            with self.assertRaisesRegex(runner.StudyError, "selected capture set mismatch"):
                runner.check_capture_set(case_root, frames, self.recipe["protocol"]["selected_frames"], full=True)


if __name__ == "__main__":
    unittest.main()
