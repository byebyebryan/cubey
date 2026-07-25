#!/usr/bin/env python3

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import terrain_diffusion_bake as bake
import terrain_diffusion_source_presets as presets


class TerrainDiffusionSourcePresetTests(unittest.TestCase):
    catalog_path = (
        Path(__file__).resolve().parent.parent / "terrain_source_presets.json"
    )

    def test_catalog_has_one_default_and_four_optional_presets(self) -> None:
        catalog = presets.load_source_preset_catalog(bake, self.catalog_path)

        self.assertEqual(catalog.default_preset, "mountain-backdrop-1")
        self.assertEqual(
            [recipe.id for recipe in catalog.recipes],
            [
                "mountain-backdrop-1",
                "alpine-range-1",
                "mountain-valley-1",
                "rolling-hills-1",
                "rolling-lowland-1",
            ],
        )
        self.assertEqual(
            [recipe.tier for recipe in catalog.recipes].count("default"), 1
        )
        self.assertEqual(
            [recipe.tier for recipe in catalog.recipes].count("optional"), 4
        )

    def test_optional_recipe_builds_a_direct_semantic_selection(self) -> None:
        catalog = presets.load_source_preset_catalog(bake, self.catalog_path)
        recipe = catalog.recipe("mountain-valley-1")

        selection = presets._landscape_selection(bake, recipe)

        self.assertEqual(selection.variant.name, "mountain-valley-1")
        self.assertEqual(selection.variant.label, "Mountain valley 1")
        self.assertEqual(
            (
                selection.candidate.seed,
                selection.candidate.coarse_i,
                selection.candidate.coarse_j,
            ),
            (0, 104, -104),
        )

    def test_catalog_rejects_a_changed_producer_pin(self) -> None:
        document = json.loads(self.catalog_path.read_text())
        document["producer"]["model_revision"] = "0" * 40
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "presets.json"
            path.write_text(json.dumps(document))

            with self.assertRaisesRegex(RuntimeError, "pinned source"):
                presets.load_source_preset_catalog(bake, path)

    def test_catalog_rejects_duplicate_ids(self) -> None:
        document = json.loads(self.catalog_path.read_text())
        document["presets"].append(document["presets"][-1])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "presets.json"
            path.write_text(json.dumps(document))

            with self.assertRaisesRegex(RuntimeError, "duplicated"):
                presets.load_source_preset_catalog(bake, path)

    def test_missing_optional_bundle_does_not_validate(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "alpine-range-1"

            self.assertFalse(
                presets.validate_source_preset_asset(
                    bake,
                    self.catalog_path,
                    "alpine-range-1",
                    output,
                )
            )
            self.assertEqual(
                presets.generation_marker_path(output),
                Path(directory) / "alpine-range-1.generating",
            )


if __name__ == "__main__":
    unittest.main()
