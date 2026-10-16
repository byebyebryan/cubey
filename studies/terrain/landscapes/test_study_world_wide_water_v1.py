import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import study_world_wide_water_v1 as study


class Contracts(unittest.TestCase):
    def test_grids_and_legacy_recipe_are_independent(self):
        self.assertEqual(study.CANDIDATES["wide-64m"], (2048, 64))
        self.assertEqual(study.bake.CANDIDATES, {"pilot-64m":(957,64), "wide-32m":(1913,32)})
        for name in study.CANDIDATES:
            g = study.bake.geometry(name, study.CANDIDATES, (100, 200))
            self.assertEqual(g["center_xz_m"], [100, 200])
            self.assertLessEqual(g["cells"], 4194304)

    def test_output_confinement(self):
        with self.assertRaises(ValueError):
            study.owned_root(Path("/tmp/wider-water-world-test"))
        self.assertEqual(study.owned_root(study.ROOT / "outputs/terrain/wider-water-world-test").parent,
                         study.ROOT / "outputs/terrain")

    def test_binding_pins_site_source_and_capture(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            site = root / "site.json"
            site.write_text(json.dumps({"site_id":"test", "world_id":"a"*64}))
            (root / "world-recipe.json").write_text("pinned world recipe")
            (root / "water-capture.json").write_text("trusted exported fixture")
            p = {"terrain_site":{"manifest_path":str(site)}, "source_sha256":"b"*64}
            with patch.object(study.bake, "check_frozen", return_value=p):
                binding = study.binding(root, "test", root)
            self.assertEqual(binding["heightfield_manifest_sha256"], "b"*64)
            self.assertEqual(binding["site_manifest_sha256"], study.bake.digest(site))
            self.assertEqual(binding["capture"]["sha256"], study.bake.digest(root / "water-capture.json"))

    def test_publication_reuses_exact_copy_and_rejects_changed_copy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            capture = root / "out/test/captures/wide-64m"
            capture.mkdir(parents=True)
            for name in ("water-capture.json", "water-binding.json"):
                (capture / name).write_text(name)
            with patch.object(study, "ROOT", root), patch.object(study.bake, "check_frozen",
                    return_value={"terrain_site":{"manifest_path":"pinned-site"}}):
                study.publish(root / "out", "test", "wide-64m")
                study.publish(root / "out", "test", "wide-64m")
                p = json.loads((root / "out/test/published-wide-64m.json").read_bytes())
                Path(p["water_binding"]).write_text("changed")
                with self.assertRaises(ValueError):
                    study.publish(root / "out", "test", "wide-64m")


if __name__ == "__main__":
    unittest.main()
