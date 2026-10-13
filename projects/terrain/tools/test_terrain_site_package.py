import json
from pathlib import Path
import tempfile
import unittest

from terrain_site_package import load_site
from terrain_world_coverage import site_plan, world_recipe


class SitePackage(unittest.TestCase):
    def fixture(self, root):
        source = {"schema": "cubey.terrain.heightfield.v1", "seed": 0,
                  "source": {"generator": "terrain-diffusion", "settings": {}},
                  "height": {"scale": 1, "offset_m": 0},
                  "grid": {"width": 32, "height": 32, "sample_spacing_m": 30,
                           "sample_origin_x_m": -465, "sample_origin_z_m": -465,
                           "axis_mapping": {"world_x": "model_j", "world_z": "model_i"}},
                  "provenance": {"model_native_origin": {"i": 0, "j": 0}}}
        world = world_recipe(source, 2000)
        source["provenance"]["world_id"] = world["world_id"]
        site = site_plan(world, "test", (0, 0), spans=(60, 90, 120, 150),
                         camera_margin=10, sampling_margin=30)
        site.update(heightfield="heightfield.json", surface_fields="surface-fields.json")
        for name, doc in (("site.json", site), ("world-recipe.json", world),
                          ("heightfield.json", source), ("surface-fields.json", {"grid": source["grid"]})):
            (root / name).write_text(json.dumps(doc))
        return site, source, world

    def test_accepts_bound_package(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            self.assertEqual(load_site(root / "site.json")["heightfield"], root / "heightfield.json")

    def test_rejects_admission_coordinates_coverage_and_unsafe_references(self):
        mutations = [lambda s, f, w: s.update(admitted=False),
                     lambda s, f, w: s.update(heightfield="../heightfield.json"),
                     lambda s, f, w: s.update(center_xz_m=[30, 0]),
                     lambda s, f, w: f["grid"].update(sample_origin_x_m=-400),
                     lambda s, f, w: f["grid"].update(width=3),
                     lambda s, f, w: w.update(seed=1),
                     lambda s, f, w: f["height"].update(scale=2)]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for mutate in mutations:
                site, source, world = self.fixture(root)
                mutate(site, source, world)
                for name, doc in (("site.json", site), ("heightfield.json", source), ("world-recipe.json", world)):
                    (root / name).write_text(json.dumps(doc))
                with self.assertRaises(ValueError):
                    load_site(root / "site.json")


if __name__ == "__main__":
    unittest.main()
