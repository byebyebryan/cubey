import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import study_world_water_v1 as study


class StudyContracts(unittest.TestCase):
    def test_changed_frozen_inputs_or_runtime_reject(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            study.write(root / "frozen.json", {"protected_inputs":{"source":"a"}, "runtime":{"app":"b"}})
            with patch.object(study, "protected_inputs", return_value={"source":"a"}), \
                    patch.object(study, "runtime", return_value={"app":"b"}):
                self.assertEqual(study.verify(root)["runtime"], {"app":"b"})
            with patch.object(study, "protected_inputs", return_value={"source":"changed"}), \
                    patch.object(study, "runtime", return_value={"app":"b"}):
                with self.assertRaises(ValueError):
                    study.verify(root)

    def test_lifecycle_requires_all_fifteen_admitted_cases(self):
        with patch.object(study.terrain, "catalogue", return_value=[]):
            with self.assertRaises(ValueError):
                study.lifecycle(Path("/not-created"))

    def test_publish_reuses_exact_bundle_but_never_overwrites_changed_copy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            out = root / "outputs"
            source = out / "visual/one/capture"
            source.mkdir(parents=True)
            (source / "water-capture.json").write_bytes(b"trusted exporter fixture")
            with patch.object(study, "ROOT", root), \
                    patch.object(study, "load_sites", return_value={"one":{"manifest":"source.json"}}):
                study.publish_captures(out)
                study.publish_captures(out)
                asset = json.loads((out / "consumer-assets.json").read_bytes())["assets"][0]
                Path(asset["manifest"]).write_bytes(b"changed")
                with self.assertRaises(ValueError):
                    study.publish_captures(out)


if __name__ == "__main__":
    unittest.main()
