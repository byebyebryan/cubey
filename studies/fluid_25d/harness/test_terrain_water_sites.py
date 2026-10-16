import copy
import json
from pathlib import Path
import tempfile
import unittest

from terrain_water_sites import DEFAULT_CATALOGUE, WORLD_CATALOGUE, load_sites


class SiteCatalogue(unittest.TestCase):
    def test_world_collection_is_separate_and_bounded(self):
        sites = load_sites(WORLD_CATALOGUE)
        self.assertEqual(list(sites), ["mountain", "mountain-interior", "valley-interior"])
        self.assertEqual(len({s["world_id"] for s in sites.values()}), 1)
        self.assertEqual(len(load_sites()), 6)

    def test_world_recipe_rejects_missing_binding_and_escaping_paths(self):
        original = json.loads(WORLD_CATALOGUE.read_text())
        mutations = [lambda d: d["sites"][0].pop("terrain_site"),
                     lambda d: d["sites"][0].update(terrain_site="/outside/site.json"),
                     lambda d: d["sites"][0].update(world_id="bad"),
                     lambda d: d["sites"][0].update(manifest="cache/terrain/sources/v1/a/heightfield.json")]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "catalogue.json"
            for mutate in mutations:
                doc = copy.deepcopy(original)
                mutate(doc)
                path.write_text(json.dumps(doc))
                with self.assertRaises(ValueError):
                    load_sites(path)

    def test_original_recipes_and_default_are_preserved(self):
        sites = load_sites()
        self.assertEqual(list(sites)[:3], ["mountain", "alpine", "rolling"])
        self.assertEqual(sites["mountain"]["center_xz_m"], [11520, 19200])
        self.assertEqual(sites["rolling"]["center_xz_m"], [0, 0])

    def test_rejects_unsafe_or_ambiguous_catalogues(self):
        original = json.loads(DEFAULT_CATALOGUE.read_text())
        mutations = [
            lambda d: d["sites"].append(copy.deepcopy(d["sites"][0])),
            lambda d: d["sites"][0].update(id="../outside"),
            lambda d: d["sites"][0].update(manifest="/outside/heightfield.json"),
            lambda d: d["sites"][0].update(manifest="cache/terrain/sources/v1/../heightfield.json"),
            lambda d: d["sites"][0].update(elevation_sha256="bad"),
            lambda d: d["sites"][0].update(center_xz_m=[True, 0]),
            lambda d: d["sites"][0].update(center_xz_m=[float("nan"), 0]),
            lambda d: d.update(default_site="missing"),
            lambda d: d.update(sites=d["sites"] * 3),
            lambda d: d["sites"][0].update(forcing=1024),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "catalogue.json"
            for mutate in mutations:
                doc = copy.deepcopy(original)
                mutate(doc)
                path.write_text(json.dumps(doc))
                with self.assertRaises(ValueError):
                    load_sites(path)
            path.write_text('{"schema":"a","schema":"b"}')
            with self.assertRaises(ValueError):
                load_sites(path)

    def test_custom_catalogue_drives_missing_case_admission(self):
        from unittest.mock import patch
        import run_terrain_rain_demo as demo
        site = dict(load_sites()["mountain"], id="another-crop", label="Another crop")
        with patch.object(demo, "case_identity", side_effect=ValueError("missing")):
            entries = demo.catalogue(Path("/study"), {site["id"]: site})
        self.assertEqual(len(entries), 5)
        self.assertTrue(all(e["site"] == "another-crop" and not e["available"] for e in entries))

    def test_admission_rejects_same_source_wrong_crop(self):
        from unittest.mock import patch
        import run_terrain_rain_demo as demo
        recipe = load_sites()["rolling"]
        source_grid = {"sample_origin_x_m": -30705, "sample_origin_z_m": -30705,
                       "width": 2048, "height": 2048, "sample_spacing_m": 30}
        binding = {"manifest_path": str(demo.runtime.ROOT / recipe["manifest"]),
                   "elevation_sha256": recipe["elevation_sha256"], "source_grid": source_grid,
                   "target_grid": {"width": 512, "height": 512, "spacing_m": 32,
                                   "center_xz_m": [12000, 0], "sample_origin_xz_m": [3824, -8176]}}
        spec = {"site": "rolling", "configuration": "patch-512-32m", "cols": 512,
                "rows": 512, "dx_m": 32, "terrain_source_identity": binding}
        with patch.object(demo, "case_identity", return_value={"spec": spec}), \
                patch.object(Path, "read_bytes", return_value=json.dumps({"grid": source_grid}).encode()):
            entries = demo.catalogue(Path("/study"), {"rolling": recipe})
        self.assertFalse(entries[0]["available"])
        self.assertIn("world coordinates", entries[0]["error"])


if __name__ == "__main__":
    unittest.main()
