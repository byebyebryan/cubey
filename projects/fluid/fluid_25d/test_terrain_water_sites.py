import copy
import json
from pathlib import Path
import tempfile
import unittest

from terrain_water_sites import DEFAULT_CATALOGUE, load_sites


class SiteCatalogue(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
