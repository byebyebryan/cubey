"""CPU-only material receipts, parity and compact-review guards."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import review_scenic_material_v2 as review


class MaterialReviewTests(unittest.TestCase):
    def test_material_receipts_are_explicit(self):
        text = 'fluid_25d_scenic_material: profile=refined+tuning settings={"wet_roughness":0.68}'
        self.assertEqual(review.effective_material(text)["settings"], {"wet_roughness": .68})
        with self.assertRaises(ValueError):
            review.effective_material("no settings")
        with self.assertRaises(ValueError):
            review.material_args("unknown")
        self.assertEqual(review.material_args("v1"), ["--fluid25d-scenic-material", "v1"])

    def test_legacy_parity_excludes_only_scenic_shaders(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            (out/"baseline").mkdir()
            value = {"inputs": {"bed": "frozen"}, "runtime": {"compiled_shaders": {"files": {
                "fluid_25d_fv_update.comp.spv": "hydraulic", "fluid_25d_water.frag.spv": "readable",
                "fluid_25d_scenic_water.frag.spv": "v1"}}}}
            (out/"baseline/protocol.json").write_text(json.dumps(value))
            runtime = {"compiled_shaders": {"files": dict(value["runtime"]["compiled_shaders"]["files"])}}
            runtime["compiled_shaders"]["files"]["fluid_25d_scenic_water.frag.spv"] = "v2"
            with patch.object(review.ref, "frozen_input_identity", return_value=value["inputs"]):
                self.assertEqual(review.legacy_parity(out, runtime), 2)
                runtime["compiled_shaders"]["files"]["fluid_25d_fv_update.comp.spv"] = "changed"
                with self.assertRaises(ValueError):
                    review.legacy_parity(out, runtime)

    def test_gallery_has_two_comparisons_and_truthful_limits(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            review.gallery(out)
            page = (out/"index.html").read_text()
            self.assertEqual(page.count("<video "), 2)
            self.assertEqual(page.count("poster="), 2)
            self.assertIn("Readable remains the launcher default", page)
            self.assertIn("Actual bank geometry remains stepped", page)
            self.assertIn("normal detail is decorative", page)
            with self.assertRaises(FileExistsError):
                review.gallery(out)

    def test_verifier_covers_posters_and_source_input_identity(self):
        with tempfile.TemporaryDirectory() as temp:
            out = Path(temp)
            (out/"index.html").write_text("<video src='clip.mp4' poster='poster.png'>")
            (out/"clip.mp4").write_bytes(b"clip")
            (out/"poster.png").write_bytes(b"poster")
            seal = {"schema": "cubey.fluid25d.scenic-material-review.v2", "leaves": [],
                    "files": ["index.html", "clip.mp4", "poster.png"], "runtime": {},
                    "source_files": {"shader": "frozen"}, "inputs": {"bed": "frozen"}}
            seal["artifacts"] = review.previous.artifact_inventory(out, [], seal["files"])
            (out/"review-seal.json").write_text(json.dumps(seal))
            with patch.object(review.ref, "runtime_identity", return_value={}), \
                 patch.object(review.ref, "assert_same_runtime"), \
                 patch.object(review.previous, "source_files", return_value=seal["source_files"]), \
                 patch.object(review.ref, "frozen_input_identity", return_value=seal["inputs"]):
                self.assertEqual(review.verify(out)["status"], "PASS")
                with patch.object(review.previous, "source_files", return_value={}), self.assertRaises(ValueError):
                    review.verify(out)
                (out/"poster.png").write_bytes(b"changed")
                with self.assertRaises(ValueError):
                    review.verify(out)


if __name__ == "__main__":
    unittest.main()
